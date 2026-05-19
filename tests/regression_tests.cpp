#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <json.hpp>

#include "animationclip.h"
#include "animator.h"
#include "boxcollider.h"
#include "camera.h"
#include "circlecollider.h"
#include "constants.h"
#include "grid.h"
#include "aabb.h"
#include "audio.h"
#include "localsortarray.h"
#include "pathmanager.h"
#include "physicsmanager.h"
#include "rigidbody.h"
#include "scene.h"
#include "scriptcomponent.h"
#include "platformator/runtime.h"
#include "sprite.h"
#include "texturewrapper.h"

namespace
{
    using platformator::Runtime;

    constexpr double kTimeStep = FRAME_TIME;

    void require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void configureHeadlessEnvironment()
    {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        setenv("SDL_AUDIODRIVER", "dummy", 1);
        setenv("SDL_RENDER_DRIVER", "software", 1);
    }

    void simulateFrames(Runtime &gameManager, int frameCount)
    {
        for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            gameManager.simulateFrame(kTimeStep);
        }
    }

    void drainPhysicsQueuesAndDeleteMarkedObjects(Runtime &gameManager)
    {
        gameManager.simulateFrame(kTimeStep);
    }

    void destroyObjects(Runtime &gameManager, const std::vector<GameObject *> &objects)
    {
        for (GameObject *object : objects)
        {
            if (object != nullptr)
            {
                gameManager.destroyGameObject(object);
            }
        }

        drainPhysicsQueuesAndDeleteMarkedObjects(gameManager);
    }

    class SceneScope
    {
    public:
        explicit SceneScope(Runtime &gameManager) : gameManager(gameManager), objects()
        {
        }

        ~SceneScope()
        {
            destroyObjects(gameManager, objects);
        }

        void add(GameObject *object)
        {
            objects.push_back(object);
        }

        void forget(GameObject *object)
        {
            objects.erase(std::remove(objects.begin(), objects.end(), object), objects.end());
        }

    private:
        Runtime &gameManager;
        std::vector<GameObject *> objects;
    };

    class WorkingDirectoryScope
    {
    public:
        explicit WorkingDirectoryScope(std::filesystem::path originalWorkingDirectory)
            : originalWorkingDirectory(std::move(originalWorkingDirectory))
        {
        }

        ~WorkingDirectoryScope()
        {
            std::filesystem::current_path(originalWorkingDirectory);
        }

    private:
        std::filesystem::path originalWorkingDirectory;
    };

    GameObject *createStaticBox(Runtime &gameManager, const std::string &name, float x, float y, float width, float height)
    {
        return gameManager
            .createGameObject()
            ->setName(name)
            ->addComponent<Rigidbody>(STATIC, false)
            ->addComponent<BoxCollider>(width, height)
            ->setPosition(Eigen::Vector2f(x, y));
    }

    GameObject *createDynamicCircle(Runtime &gameManager, const std::string &name, float x, float y, float radius)
    {
        GameObject *object = gameManager
                                 .createGameObject()
                                 ->setName(name)
                                 ->addComponent<Rigidbody>()
                                 ->addComponent<CircleCollider>(radius)
                                 ->setPosition(Eigen::Vector2f(x, y));

        object->getComponent<Rigidbody>()
            ->setMass(10.0f)
            ->setFriction(0.8f)
            ->setRestitution(0.0f);

        return object;
    }

    GameObject *createDynamicBox(Runtime &gameManager, const std::string &name, float x, float y, float width, float height)
    {
        GameObject *object = gameManager
                                 .createGameObject()
                                 ->setName(name)
                                 ->addComponent<Rigidbody>()
                                 ->addComponent<BoxCollider>(width, height)
                                 ->setPosition(Eigen::Vector2f(x, y));

        object->getComponent<Rigidbody>()
            ->setMass(8.0f)
            ->setFriction(0.9f)
            ->setRestitution(0.0f);

        return object;
    }

    std::vector<GameObject *> createDynamicBoxColumn(
        Runtime &gameManager,
        SceneScope &sceneScope,
        const std::string &namePrefix,
        float x,
        float bottomY,
        size_t boxCount,
        float width = 40.0f,
        float height = 40.0f,
        float verticalSpacing = 60.0f)
    {
        std::vector<GameObject *> boxes;
        boxes.reserve(boxCount);

        for (size_t index = 0; index < boxCount; ++index)
        {
            GameObject *box = createDynamicBox(
                gameManager,
                namePrefix + " " + std::to_string(index),
                x,
                bottomY - static_cast<float>(index) * verticalSpacing,
                width,
                height);
            sceneScope.add(box);
            boxes.push_back(box);
        }

        return boxes;
    }

    std::vector<Eigen::Vector2f> capturePositions(const std::vector<GameObject *> &objects)
    {
        std::vector<Eigen::Vector2f> positions;
        positions.reserve(objects.size());

        for (GameObject *object : objects)
        {
            positions.push_back(object->getPosition());
        }

        return positions;
    }

    void requireAllSleeping(const std::vector<GameObject *> &objects, const std::string &message)
    {
        for (GameObject *object : objects)
        {
            Rigidbody *rigidbody = object->getComponent<Rigidbody>();
            require(rigidbody != nullptr, message + " (missing rigidbody).");
            require(rigidbody->getIsSleeping(), message);
        }
    }

    void requirePositionsStable(const std::vector<GameObject *> &objects, const std::vector<Eigen::Vector2f> &positionsBefore, float tolerance, const std::string &message)
    {
        require(objects.size() == positionsBefore.size(), message + " (position snapshot mismatch).");

        const float toleranceSquared = tolerance * tolerance;
        for (size_t index = 0; index < objects.size(); ++index)
        {
            require((objects[index]->getPosition() - positionsBefore[index]).squaredNorm() <= toleranceSquared, message);
        }
    }

    GameObject *createKinematicBox(Runtime &gameManager, const std::string &name, float x, float y, float width, float height, bool gravity)
    {
        return gameManager
            .createGameObject()
            ->setName(name)
            ->addComponent<Rigidbody>(KINEMATIC, gravity)
            ->addComponent<BoxCollider>(width, height)
            ->setPosition(Eigen::Vector2f(x, y));
    }

    class CollisionEventCounterBehavior : public Behavior
    {
    public:
        CollisionEventCounterBehavior() : enterCount(0), stayCount(0), exitCount(0)
        {
        }

        std::string getTypeName() const override
        {
            return "CollisionEventCounterBehavior";
        }

        void serialize(nlohmann::json &j) const override
        {
            j = nlohmann::json{{"type", getTypeName()}};
        }

        void deserialize(const nlohmann::json &) override
        {
        }

        void resolveReferences() override
        {
        }

        void onCollisionEnter(const Collision *, Collider *, double) override
        {
            enterCount++;
        }

        void onCollisionExit(Collider *, double) override
        {
            exitCount++;
        }

        void onCollisionStay(const Collision *, Collider *, double) override
        {
            stayCount++;
        }

        size_t enterCount;
        size_t stayCount;
        size_t exitCount;
    };

    std::filesystem::path makeRuntimeAssetPath(const std::string &fileName)
    {
        return (std::filesystem::path(PathManager::getInstance().getAssetsRootAbsolutePath()) / "test_runtime" / fileName).lexically_normal();
    }

    std::string canonicalAssetPathForTest(const std::filesystem::path &assetPath)
    {
        return PathManager::getInstance().canonicalizeAssetPath(assetPath.generic_string());
    }

    std::filesystem::path createTemporaryBmpAsset(const std::string &fileName, Uint8 red, Uint8 green, Uint8 blue)
    {
        std::filesystem::path filePath = makeRuntimeAssetPath(fileName);

        std::error_code directoryError;
        std::filesystem::create_directories(filePath.parent_path(), directoryError);

        std::error_code errorCode;
        std::filesystem::remove(filePath, errorCode);

        SDL_Surface *surface = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
        require(surface != nullptr, "Animator regression failed to create a temporary SDL surface.");

        Uint32 color = SDL_MapSurfaceRGBA(surface, red, green, blue, 255);
        bool filled = SDL_FillSurfaceRect(surface, nullptr, color);
        bool saved = SDL_SaveBMP(surface, filePath.string().c_str());
        SDL_DestroySurface(surface);

        require(filled, "Animator regression failed to fill the temporary surface.");
        require(saved, "Animator regression failed to save the temporary BMP asset.");
        return filePath;
    }

    std::filesystem::path createTemporaryStripBmpAsset(const std::string &fileName)
    {
        std::filesystem::path filePath = makeRuntimeAssetPath(fileName);

        std::error_code directoryError;
        std::filesystem::create_directories(filePath.parent_path(), directoryError);

        std::error_code errorCode;
        std::filesystem::remove(filePath, errorCode);

        SDL_Surface *surface = SDL_CreateSurface(8, 4, SDL_PIXELFORMAT_RGBA32);
        require(surface != nullptr, "Advanced animator regression failed to create a temporary strip surface.");

        const Uint32 red = SDL_MapSurfaceRGBA(surface, 255, 0, 0, 255);
        const Uint32 green = SDL_MapSurfaceRGBA(surface, 0, 255, 0, 255);
        const SDL_Rect leftHalf{0, 0, 4, 4};
        const SDL_Rect rightHalf{4, 0, 4, 4};

        const bool filledLeft = SDL_FillSurfaceRect(surface, &leftHalf, red);
        const bool filledRight = SDL_FillSurfaceRect(surface, &rightHalf, green);
        const bool saved = SDL_SaveBMP(surface, filePath.string().c_str());
        SDL_DestroySurface(surface);

        require(filledLeft && filledRight, "Advanced animator regression failed to fill the temporary strip surface.");
        require(saved, "Advanced animator regression failed to save the temporary strip BMP asset.");
        return filePath;
    }

    void addColliderToLocalAabb(Grid &grid, AABB &aabb, Collider *collider)
    {
        require(collider != nullptr, "Local AABB regression received a null collider.");
        collider->prepareSync();
        aabb.add(collider);
        grid.flushDeferredPairDeltas();
    }

    void syncColliderInLocalAabb(Grid &grid, AABB &aabb, Collider *collider)
    {
        require(collider != nullptr, "Local AABB regression received a null collider for repair.");
        collider->prepareSync();
        aabb.repair(collider);
        grid.flushDeferredPairDeltas();
    }

    void removeColliderFromLocalAabb(Grid &grid, AABB &aabb, Collider *collider)
    {
        require(collider != nullptr, "Local AABB regression received a null collider for removal.");
        aabb.remove(collider);
        grid.flushDeferredPairDeltas();
    }

    void testCircleCollisionStability()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *floor = createStaticBox(gameManager, "Regression Floor", 200.0f, 300.0f, 400.0f, 40.0f);
        GameObject *ball = createDynamicCircle(gameManager, "Regression Ball", 200.0f, 140.0f, 20.0f);
        sceneScope.add(floor);
        sceneScope.add(ball);

        simulateFrames(gameManager, 720);

        const float expectedRestY = 300.0f - 20.0f - 20.0f;
        const float actualRestY = ball->getPosition().y();

        require(std::isfinite(actualRestY), "Circle stability regression produced a non-finite position.");
        require(std::abs(actualRestY - expectedRestY) <= 6.0f,
                "Circle stability regression drifted too far from the expected resting height.");
    }

    void testWarmStartFeatureReuse()
    {
        Collision collision;

        const ContactFeature featureA = makeContactFeature(EDGE1, NO_EDGE, EDGE2, EDGE3);
        const ContactFeature featureB = makeContactFeature(NO_EDGE, EDGE4, EDGE4, EDGE1);
        const ContactFeature featureC = makeContactFeature(EDGE2, NO_EDGE, EDGE3, EDGE4);

        ClipPoints initialContacts;
        initialContacts.add(Eigen::Vector2f(10.0f, 20.0f), featureA);
        initialContacts.add(Eigen::Vector2f(30.0f, 40.0f), featureB);
        collision.setContactPoints(initialContacts);

        ClipPointsWithData &cachedContacts = collision.getContactPoints();
        cachedContacts.points[0].accumulatedNormalImpulse = 1.25f;
        cachedContacts.points[0].accumulatedTangentImpulse = -0.5f;
        cachedContacts.points[0].accumulatedNormalImpulseBias = 0.2f;
        cachedContacts.points[1].accumulatedNormalImpulse = 2.5f;
        cachedContacts.points[1].accumulatedTangentImpulse = 0.75f;
        cachedContacts.points[1].accumulatedNormalImpulseBias = 0.4f;

        ClipPoints reorderedContacts;
        reorderedContacts.add(Eigen::Vector2f(31.0f, 41.0f), featureB);
        reorderedContacts.add(Eigen::Vector2f(11.0f, 21.0f), featureA);
        collision.setContactPoints(reorderedContacts);

        const ClipPointsWithData &remappedContacts = collision.getContactPoints();
        require(remappedContacts.count == 2,
                "Warm-start feature regression expected both contacts to survive remapping.");
        require(remappedContacts.points[0].point.feature.key == featureB.key &&
                    std::abs(remappedContacts.points[0].accumulatedNormalImpulse - 2.5f) <= 1e-6f &&
                    std::abs(remappedContacts.points[0].accumulatedTangentImpulse - 0.75f) <= 1e-6f &&
                    std::abs(remappedContacts.points[0].accumulatedNormalImpulseBias - 0.4f) <= 1e-6f,
                "Warm-start feature regression failed to remap the first contact by feature id.");
        require(remappedContacts.points[1].point.feature.key == featureA.key &&
                    std::abs(remappedContacts.points[1].accumulatedNormalImpulse - 1.25f) <= 1e-6f &&
                    std::abs(remappedContacts.points[1].accumulatedTangentImpulse + 0.5f) <= 1e-6f &&
                    std::abs(remappedContacts.points[1].accumulatedNormalImpulseBias - 0.2f) <= 1e-6f,
                "Warm-start feature regression failed to remap the second contact by feature id.");

        ClipPoints partiallyNewContacts;
        partiallyNewContacts.add(Eigen::Vector2f(32.0f, 42.0f), featureB);
        partiallyNewContacts.add(Eigen::Vector2f(50.0f, 60.0f), featureC);
        collision.setContactPoints(partiallyNewContacts);

        const ClipPointsWithData &updatedContacts = collision.getContactPoints();
        require(updatedContacts.points[0].point.feature.key == featureB.key &&
                    std::abs(updatedContacts.points[0].accumulatedNormalImpulse - 2.5f) <= 1e-6f,
                "Warm-start feature regression failed to preserve the cached impulse for a stable feature.");
        require(updatedContacts.points[1].point.feature.key == featureC.key &&
                    std::abs(updatedContacts.points[1].accumulatedNormalImpulse) <= 1e-6f &&
                    std::abs(updatedContacts.points[1].accumulatedTangentImpulse) <= 1e-6f &&
                    std::abs(updatedContacts.points[1].accumulatedNormalImpulseBias) <= 1e-6f,
                "Warm-start feature regression expected a new feature to start without cached impulses.");
    }

    void testClipSegmentFeaturePropagation()
    {
        ClipVertex input[2] = {
            ClipVertex(Eigen::Vector2f(-2.0f, 0.0f), makeContactFeature(NO_EDGE, NO_EDGE, EDGE2, EDGE1)),
            ClipVertex(Eigen::Vector2f(2.0f, 0.0f), makeContactFeature(NO_EDGE, NO_EDGE, EDGE1, EDGE4))};
        ClipVertex output[2];

        int outputCount = clipSegmentToLine(output, input, Eigen::Vector2f(1.0f, 0.0f), 0.0f, EDGE3);

        require(outputCount == 2,
                "Clip feature regression expected one retained endpoint and one clipped intersection.");
        require((output[0].point - input[1].point).squaredNorm() <= 1e-6f &&
                    output[0].feature.key == input[1].feature.key,
                "Clip feature regression failed to preserve the feature of the retained endpoint.");
        require(std::abs(output[1].point.x()) <= 1e-6f && std::abs(output[1].point.y()) <= 1e-6f,
                "Clip feature regression expected the clipped intersection to land on the clipping plane.");
        require(output[1].feature.e.inEdge1 == static_cast<uint8_t>(EDGE3) &&
                    output[1].feature.e.outEdge1 == static_cast<uint8_t>(NO_EDGE) &&
                    output[1].feature.e.inEdge2 == input[0].feature.e.inEdge2 &&
                    output[1].feature.e.outEdge2 == input[0].feature.e.outEdge2,
                "Clip feature regression failed to stamp the clipping edge onto the intersection feature.");
    }

    void testSleepingOnSupport()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *floor = createStaticBox(gameManager, "Sleep Floor", 320.0f, 340.0f, 420.0f, 40.0f);
        GameObject *box = createDynamicBox(gameManager, "Sleep Box", 320.0f, 180.0f, 40.0f, 40.0f);
        sceneScope.add(floor);
        sceneScope.add(box);

        simulateFrames(gameManager, 720);

        Rigidbody *rigidbody = box->getComponent<Rigidbody>();
        require(rigidbody != nullptr, "Sleeping regression lost the box rigidbody.");
        require(rigidbody->hasSupportContact(), "Sleeping regression ended without a support contact.");
        require(rigidbody->getIsSleeping(), "Supported dynamic body failed to enter sleep state.");
    }

    void testSleepingStackStaysAsleep()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *floor = createStaticBox(gameManager, "Sleep Stack Floor", 320.0f, 340.0f, 420.0f, 40.0f);
        GameObject *bottomBox = createDynamicBox(gameManager, "Sleep Stack Bottom", 320.0f, 180.0f, 40.0f, 40.0f);
        GameObject *middleBox = createDynamicBox(gameManager, "Sleep Stack Middle", 320.0f, 120.0f, 40.0f, 40.0f);
        GameObject *topBox = createDynamicBox(gameManager, "Sleep Stack Top", 320.0f, 60.0f, 40.0f, 40.0f);
        sceneScope.add(floor);
        sceneScope.add(bottomBox);
        sceneScope.add(middleBox);
        sceneScope.add(topBox);

        simulateFrames(gameManager, 720);

        Rigidbody *bottomBody = bottomBox->getComponent<Rigidbody>();
        Rigidbody *middleBody = middleBox->getComponent<Rigidbody>();
        Rigidbody *topBody = topBox->getComponent<Rigidbody>();
        require(bottomBody != nullptr && middleBody != nullptr && topBody != nullptr,
                "Sleeping stack regression lost one of the rigidbodies.");

        require(bottomBody->getIsSleeping() && middleBody->getIsSleeping() && topBody->getIsSleeping(),
                "Sleeping stack regression expected the settled stack to fall asleep.");

        const float bottomYBefore = bottomBox->getPosition().y();
        const float middleYBefore = middleBox->getPosition().y();
        const float topYBefore = topBox->getPosition().y();

        simulateFrames(gameManager, 240);

        require(bottomBody->getIsSleeping() && middleBody->getIsSleeping() && topBody->getIsSleeping(),
                "Sleeping stack regression expected the settled stack to remain asleep.");
        require(std::abs(bottomBox->getPosition().y() - bottomYBefore) <= 1.0f &&
                    std::abs(middleBox->getPosition().y() - middleYBefore) <= 1.0f &&
                    std::abs(topBox->getPosition().y() - topYBefore) <= 1.0f,
                "Sleeping stack regression expected the resting stack to remain stable after sleeping.");
    }

    void testSleepingTallStackStaysAsleep()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *floor = createStaticBox(gameManager, "Tall Sleep Stack Floor", 320.0f, 340.0f, 420.0f, 40.0f);
        sceneScope.add(floor);

        std::vector<GameObject *> boxes = createDynamicBoxColumn(gameManager, sceneScope, "Tall Sleep Stack Box", 320.0f, 180.0f, 6);

        simulateFrames(gameManager, 960);

        requireAllSleeping(boxes, "Tall sleeping stack regression expected every box in the settled stack to be asleep.");

        const std::vector<Eigen::Vector2f> positionsBefore = capturePositions(boxes);

        simulateFrames(gameManager, 480);

        requireAllSleeping(boxes, "Tall sleeping stack regression expected the settled stack to stay asleep over a long idle period.");
        requirePositionsStable(boxes, positionsBefore, 1.0f,
                               "Tall sleeping stack regression expected the resting stack to stay positionally stable over a long idle period.");
    }

    void testSleepingWidePileStaysAsleep()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *floor = createStaticBox(gameManager, "Wide Sleep Pile Floor", 320.0f, 340.0f, 520.0f, 40.0f);
        sceneScope.add(floor);

        std::vector<GameObject *> leftColumn = createDynamicBoxColumn(gameManager, sceneScope, "Wide Sleep Pile Left", 300.0f, 180.0f, 3);
        std::vector<GameObject *> rightColumn = createDynamicBoxColumn(gameManager, sceneScope, "Wide Sleep Pile Right", 340.0f, 180.0f, 3);

        std::vector<GameObject *> boxes;
        boxes.reserve(leftColumn.size() + rightColumn.size());
        boxes.insert(boxes.end(), leftColumn.begin(), leftColumn.end());
        boxes.insert(boxes.end(), rightColumn.begin(), rightColumn.end());

        simulateFrames(gameManager, 960);

        requireAllSleeping(boxes, "Wide sleeping pile regression expected the settled pile to fall asleep.");

        const std::vector<Eigen::Vector2f> positionsBefore = capturePositions(boxes);

        simulateFrames(gameManager, 360);

        requireAllSleeping(boxes, "Wide sleeping pile regression expected the settled pile to remain asleep.");
        requirePositionsStable(boxes, positionsBefore, 1.0f,
                               "Wide sleeping pile regression expected the resting pile to remain stable after sleeping.");
    }

    void testSleepingBodyWakesAfterSupportRemoved()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *floor = createStaticBox(gameManager, "Support Removal Floor", 320.0f, 340.0f, 420.0f, 40.0f);
        GameObject *box = createDynamicBox(gameManager, "Support Removal Box", 320.0f, 180.0f, 40.0f, 40.0f);
        sceneScope.add(floor);
        sceneScope.add(box);

        simulateFrames(gameManager, 720);

        Rigidbody *rigidbody = box->getComponent<Rigidbody>();
        require(rigidbody != nullptr, "Support removal sleeping regression lost the box rigidbody.");
        require(rigidbody->getIsSleeping(), "Support removal sleeping regression expected the body to be asleep before removing support.");
        require(rigidbody->hasSupportContact(), "Support removal sleeping regression expected the body to have support before removing the floor.");

        gameManager.destroyGameObject(floor);
        sceneScope.forget(floor);
        simulateFrames(gameManager, static_cast<int>(std::ceil(SUPPORT_LOSS_WAKE_DELAY / kTimeStep)) + 2);

        require(!rigidbody->getIsSleeping(), "Support removal sleeping regression expected the body to wake once support was gone long enough.");

        const float yBeforeFall = box->getPosition().y();
        simulateFrames(gameManager, 60);

        require(box->getPosition().y() > yBeforeFall + 5.0f,
                "Support removal sleeping regression expected the body to start falling after waking.");
    }

    void testBroadPhasePairTracking()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *boxA = createStaticBox(gameManager, "Pair A", 120.0f, 120.0f, 40.0f, 40.0f);
        GameObject *boxB = createStaticBox(gameManager, "Pair B", 320.0f, 120.0f, 40.0f, 40.0f);
        sceneScope.add(boxA);
        sceneScope.add(boxB);

        boxA->addComponent<ScriptComponent>();
        ScriptComponent *scriptComponent = boxA->getComponent<ScriptComponent>();
        require(scriptComponent != nullptr, "Broad-phase regression failed to create a script component for the first collider.");

        auto *collisionCounter = new CollisionEventCounterBehavior();
        scriptComponent->addBehavior(collisionCounter);

        gameManager.simulateFrame(kTimeStep);
        require(collisionCounter->enterCount == 0 && collisionCounter->stayCount == 0 && collisionCounter->exitCount == 0,
                "Broad-phase regression unexpectedly reported a collision before overlap.");

        boxB->setPosition(Eigen::Vector2f(135.0f, 120.0f));
        gameManager.simulateFrame(kTimeStep);
        require(collisionCounter->enterCount == 1,
                "Broad-phase regression expected a collision enter callback after overlap.");
        require(collisionCounter->exitCount == 0,
                "Broad-phase regression unexpectedly reported a collision exit while overlap persisted.");

        boxB->setPosition(Eigen::Vector2f(360.0f, 120.0f));
        gameManager.simulateFrame(kTimeStep);
        require(collisionCounter->exitCount == 1,
                "Broad-phase regression expected a collision exit callback after separation.");
    }

    void testCheckpointFastMotionCancellation()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *box = createStaticBox(gameManager, "Checkpoint Box", 120.0f, 120.0f, 40.0f, 40.0f);
        sceneScope.add(box);

        Collider *collider = box->getComponent<Collider>();
        require(collider != nullptr, "Checkpoint regression lost the collider.");

        LocalSortArray chunk(static_cast<SegmentedIntervalList *>(nullptr));

        chunk.removeCheckpoint(collider);
        require(!chunk.getCheckpoints()->contains(collider),
                "Checkpoint regression unexpectedly created a checkpoint during the pending-remove phase.");

        chunk.addCheckpoint(collider);
        require(!chunk.getCheckpoints()->contains(collider),
                "Checkpoint regression failed to cancel a pending fast-motion remove-then-add sequence.");

        chunk.addCheckpoint(collider);
        require(chunk.getCheckpoints()->contains(collider),
                "Checkpoint regression failed to add a real checkpoint after the fast-motion cancellation state was cleared.");

        chunk.removeCheckpoint(collider);
        require(!chunk.getCheckpoints()->contains(collider),
                "Checkpoint regression failed to remove an existing checkpoint cleanly.");
    }

    void testParallelBroadAndNarrowPhaseStress()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        constexpr int rowCount = 12;
        constexpr int columnCount = 12;
        constexpr int pairCount = rowCount * columnCount;
        constexpr float columnSpacing = 160.0f;
        constexpr float rowSpacing = 120.0f;
        constexpr float overlapOffsetX = 12.0f;
        constexpr float stayOffsetX = 6.0f;
        constexpr float separationOffsetX = 96.0f;

        std::vector<GameObject *> probes;
        probes.reserve(pairCount);

        std::vector<CollisionEventCounterBehavior *> counters;
        counters.reserve(pairCount);

        for (int row = 0; row < rowCount; ++row)
        {
            for (int column = 0; column < columnCount; ++column)
            {
                const float baseX = 120.0f + static_cast<float>(column) * columnSpacing;
                const float baseY = 120.0f + static_cast<float>(row) * rowSpacing;

                GameObject *anchor = createStaticBox(
                    gameManager,
                    "Parallel Anchor " + std::to_string(row) + "-" + std::to_string(column),
                    baseX,
                    baseY,
                    40.0f,
                    40.0f);
                GameObject *probe = createStaticBox(
                    gameManager,
                    "Parallel Probe " + std::to_string(row) + "-" + std::to_string(column),
                    baseX + overlapOffsetX,
                    baseY,
                    40.0f,
                    40.0f);

                sceneScope.add(anchor);
                sceneScope.add(probe);
                probes.push_back(probe);

                probe->addComponent<ScriptComponent>();
                ScriptComponent *scriptComponent = probe->getComponent<ScriptComponent>();
                require(scriptComponent != nullptr,
                        "Parallel broad/narrow regression failed to create a script component for a probe collider.");

                auto *collisionCounter = new CollisionEventCounterBehavior();
                scriptComponent->addBehavior(collisionCounter);
                counters.push_back(collisionCounter);
            }
        }

        gameManager.simulateFrame(kTimeStep);

        for (const CollisionEventCounterBehavior *counter : counters)
        {
            require(counter->enterCount == 1 && counter->stayCount == 0 && counter->exitCount == 0,
                    "Parallel broad/narrow regression expected exactly one enter event for each overlapping pair after the first frame.");
        }

        for (GameObject *probe : probes)
        {
            probe->setPosition(probe->getPosition() + Eigen::Vector2f(stayOffsetX, 0.0f));
        }

        gameManager.simulateFrame(kTimeStep);

        for (const CollisionEventCounterBehavior *counter : counters)
        {
            require(counter->enterCount == 1 && counter->stayCount == 1 && counter->exitCount == 0,
                    "Parallel broad/narrow regression expected exactly one stay event for each pair after an in-overlap sync.");
        }

        for (GameObject *probe : probes)
        {
            probe->setPosition(probe->getPosition() + Eigen::Vector2f(separationOffsetX, 0.0f));
        }

        gameManager.simulateFrame(kTimeStep);

        for (const CollisionEventCounterBehavior *counter : counters)
        {
            require(counter->enterCount == 1 && counter->stayCount == 1 && counter->exitCount == 1,
                    "Parallel broad/narrow regression expected exactly one exit event for each pair after separation.");
        }
    }

    void testSegmentedIntervalListFastMotionAcrossChunks()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        Grid localGrid;
        AABB localAabb(&localGrid);

        for (int index = 0; index < 17; ++index)
        {
            GameObject *filler = createStaticBox(
                gameManager,
                "Chunk Filler " + std::to_string(index),
                10.0f + static_cast<float>(index) * 20.0f,
                0.0f,
                2.0f,
                2.0f);
            sceneScope.add(filler);
            addColliderToLocalAabb(localGrid, localAabb, filler->getComponent<Collider>());
        }

        GameObject *bridge = createStaticBox(gameManager, "Fast Bridge", 110.0f, 100.0f, 80.0f, 20.0f);
        sceneScope.add(bridge);
        Collider *bridgeCollider = bridge->getComponent<Collider>();
        addColliderToLocalAabb(localGrid, localAabb, bridgeCollider);

        require(localGrid.getCandidatePairCount() == 0,
                "Fast-motion regression expected no broad-phase pairs before adding the probe collider.");

        bridge->setPosition(Eigen::Vector2f(170.0f, 100.0f));
        syncColliderInLocalAabb(localGrid, localAabb, bridgeCollider);

        GameObject *probe = createStaticBox(gameManager, "Checkpoint Probe", 145.0f, 100.0f, 2.0f, 20.0f);
        sceneScope.add(probe);
        Collider *probeCollider = probe->getComponent<Collider>();
        addColliderToLocalAabb(localGrid, localAabb, probeCollider);

        require(localGrid.getCandidatePairCount() == 1,
                "Fast-motion regression expected the probe to overlap the fast bridge through real chunk checkpoint handling.");

        bridge->setPosition(Eigen::Vector2f(250.0f, 100.0f));
        syncColliderInLocalAabb(localGrid, localAabb, bridgeCollider);

        require(localGrid.getCandidatePairCount() == 0,
                "Fast-motion regression expected the probe pair to be removed after the fast bridge moved away.");
    }

    void testSegmentedIntervalListMergesUnderfullChunks()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        Grid localGrid;
        AABB localAabb(&localGrid);
        SegmentedIntervalList *xIntervalList = localAabb.getIntervalListX();

        require(xIntervalList->getChunkCount() == 1,
                "Chunk-merge regression expected a fresh segmented interval list to start with one chunk.");

        std::vector<Collider *> fillerColliders;
        fillerColliders.reserve(static_cast<size_t>(MAX_CHUNK_SIZE / 2) + 1);

        for (int index = 0; index < static_cast<int>(MAX_CHUNK_SIZE / 2) + 1; ++index)
        {
            GameObject *filler = createStaticBox(
                gameManager,
                "Merge Filler " + std::to_string(index),
                10.0f + static_cast<float>(index) * 20.0f,
                0.0f,
                2.0f,
                2.0f);
            sceneScope.add(filler);

            Collider *fillerCollider = filler->getComponent<Collider>();
            fillerColliders.push_back(fillerCollider);
            addColliderToLocalAabb(localGrid, localAabb, fillerCollider);
        }

        require(xIntervalList->getChunkCount() > 1,
                "Chunk-merge regression expected enough colliders to split the segmented interval list into multiple chunks.");
        require(localGrid.getCandidatePairCount() == 0,
                "Chunk-merge regression expected the split fillers to remain non-overlapping.");

        for (size_t index = 0; index < 3; ++index)
        {
            removeColliderFromLocalAabb(localGrid, localAabb, fillerColliders[index]);
        }

        require(xIntervalList->getChunkCount() == 1,
                "Chunk-merge regression expected removing enough colliders to merge neighboring underfull chunks back into one chunk.");
        require(localGrid.getCandidatePairCount() == 0,
                "Chunk-merge regression expected no broad-phase pairs before the post-merge overlap check.");

        GameObject *bridge = createStaticBox(gameManager, "Merged Bridge", 145.0f, 100.0f, 80.0f, 20.0f);
        sceneScope.add(bridge);
        Collider *bridgeCollider = bridge->getComponent<Collider>();
        addColliderToLocalAabb(localGrid, localAabb, bridgeCollider);

        GameObject *probe = createStaticBox(gameManager, "Merged Probe", 145.0f, 100.0f, 2.0f, 20.0f);
        sceneScope.add(probe);
        Collider *probeCollider = probe->getComponent<Collider>();
        addColliderToLocalAabb(localGrid, localAabb, probeCollider);

        require(xIntervalList->getChunkCount() == 1,
                "Chunk-merge regression expected the merged list to stay within one chunk during the post-merge overlap scenario.");
        require(localGrid.getCandidatePairCount() == 1,
                "Chunk-merge regression expected the post-merge bridge and probe to produce one broad-phase pair.");

        bridge->setPosition(Eigen::Vector2f(250.0f, 100.0f));
        syncColliderInLocalAabb(localGrid, localAabb, bridgeCollider);

        require(xIntervalList->getChunkCount() == 1,
                "Chunk-merge regression expected the list to remain merged after the post-merge bridge moved away.");
        require(localGrid.getCandidatePairCount() == 0,
                "Chunk-merge regression expected the post-merge bridge pair to be removed after separation.");
    }

    std::filesystem::path findWorkspaceRelativePath(const std::filesystem::path &relativePath)
    {
        std::filesystem::path searchDirectory = std::filesystem::current_path();

        while (true)
        {
            std::filesystem::path candidate = searchDirectory / relativePath;
            if (std::filesystem::exists(candidate))
            {
                return candidate.lexically_normal();
            }

            std::filesystem::path parentDirectory = searchDirectory.parent_path();
            if (parentDirectory == searchDirectory)
            {
                break;
            }

            searchDirectory = parentDirectory;
        }

        throw std::runtime_error("Regression tests could not locate resource '" + relativePath.generic_string() + "'.");
    }

    std::filesystem::path findRegressionAsset(const std::string &fileName)
    {
        return (std::filesystem::path("assets") / fileName).lexically_normal();
    }

    std::filesystem::path findRegressionAudioAsset(const std::string &fileName)
    {
        return (std::filesystem::path("assets") / "audio" / fileName).lexically_normal();
    }

    std::string makeSceneRelativeResourcePath(const std::filesystem::path &resourcePath, const std::filesystem::path &scenePath)
    {
        std::error_code errorCode;
        std::filesystem::path relativePath = std::filesystem::relative(resourcePath, scenePath.parent_path(), errorCode);
        if (!errorCode && !relativePath.empty())
        {
            return relativePath.generic_string();
        }

        return resourcePath.generic_string();
    }

    nlohmann::json makeVectorJson(float x, float y)
    {
        return nlohmann::json{{"x", x}, {"y", y}};
    }

    nlohmann::json makeRectJson(float x, float y, float w, float h)
    {
        return nlohmann::json{{"x", x}, {"y", y}, {"w", w}, {"h", h}};
    }

    nlohmann::json makeGameObjectJson(int id, const std::string &name)
    {
        return nlohmann::json{{"id", id},
                              {"rotation", 0.0f},
                              {"active", true},
                              {"position", makeVectorJson(0.0f, 0.0f)},
                              {"scale", makeVectorJson(1.0f, 1.0f)},
                              {"name", name},
                              {"tag", ""},
                              {"children", nlohmann::json::array()},
                              {"components", nlohmann::json::array()}};
    }

    const nlohmann::json *findSavedObject(const nlohmann::json &objects, const std::string &name)
    {
        for (const nlohmann::json &objectJson : objects)
        {
            if (!objectJson.is_object())
            {
                continue;
            }

            const auto nameIt = objectJson.find("name");
            if (nameIt != objectJson.end() && nameIt->is_string() && nameIt->get<std::string>() == name)
            {
                return &objectJson;
            }
        }

        return nullptr;
    }

    const nlohmann::json *findSavedComponent(const nlohmann::json &components, ComponentType componentType)
    {
        for (const nlohmann::json &componentJson : components)
        {
            if (!componentJson.is_object())
            {
                continue;
            }

            const auto typeIt = componentJson.find("type");
            if (typeIt != componentJson.end() && typeIt->is_number_integer() && typeIt->get<int>() == static_cast<int>(componentType))
            {
                return &componentJson;
            }
        }

        return nullptr;
    }

    void writeJsonFile(const std::filesystem::path &path, const nlohmann::json &document, const std::string &errorMessage)
    {
        if (path.has_parent_path())
        {
            std::error_code directoryError;
            std::filesystem::create_directories(path.parent_path(), directoryError);
        }

        std::ofstream file(path);
        require(file.is_open(), errorMessage);
        file << document.dump(4);
    }

    void destroyNamedObject(Runtime &gameManager, const std::string &name)
    {
        GameObject *gameObject = gameManager.getGameObject(name);
        if (gameObject == nullptr)
        {
            return;
        }

        gameManager.destroyGameObject(gameObject);
    }

    void testSceneLoading()
    {
        Runtime &gameManager = Runtime::current();
        require(gameManager.getWindowHandle() != nullptr, "Scene loading regression requires an SDLWindow.");

        std::filesystem::path scenePath = std::filesystem::current_path() / "scene_loading_regression.scene";
        std::filesystem::path wallTexturePath = findRegressionAsset("wall.png");
        std::string sceneSpritePath = wallTexturePath.generic_string();

        auto cleanup = [&]()
        {
            destroyNamedObject(gameManager, "Loaded Ball");
            destroyNamedObject(gameManager, "Loaded Camera");

            drainPhysicsQueuesAndDeleteMarkedObjects(gameManager);

            std::error_code errorCode;
            std::filesystem::remove(scenePath, errorCode);
        };

        cleanup();

        try
        {
            nlohmann::json loadedBallJson = makeGameObjectJson(1100, "Loaded Ball");
            loadedBallJson["tag"] = "Player";
            loadedBallJson["position"] = makeVectorJson(123.0f, 234.0f);
            loadedBallJson["rotation"] = 0.5f;
            loadedBallJson["scale"] = makeVectorJson(2.0f, 1.5f);
            loadedBallJson["components"].push_back({{"id", 1101},
                                                    {"velocity", makeVectorJson(10.0f, -20.0f)},
                                                    {"force", makeVectorJson(1.0f, 2.0f)},
                                                    {"mass", 7.0f},
                                                    {"angularVelocity", 0.25f},
                                                    {"torque", 0.75f},
                                                    {"friction", 0.4f},
                                                    {"restitution", 0.6f},
                                                    {"bodyType", static_cast<int>(BodyType::DYNAMIC)},
                                                    {"type", static_cast<int>(ComponentType::RIGID_BODY)},
                                                    {"gravity", false}});
            loadedBallJson["components"].push_back({{"id", 1102},
                                                    {"radius", 12.0f},
                                                    {"offset", makeVectorJson(-3.0f, 4.0f)},
                                                    {"trigger", false},
                                                    {"collisionGroup", 3},
                                                    {"type", static_cast<int>(ComponentType::COLLIDER)},
                                                    {"colliderType", static_cast<int>(ColliderType::CircleCollider)},
                                                    {"collisionMask", 5}});
            loadedBallJson["components"].push_back({{"id", 1103},
                                                    {"textureFilePath", sceneSpritePath},
                                                    {"flip", static_cast<int>(SDL_FLIP_HORIZONTAL)},
                                                    {"width", 32.0f},
                                                    {"height", 48.0f},
                                                    {"sourceRectEnabled", false},
                                                    {"type", static_cast<int>(ComponentType::SPRITE)}});

            nlohmann::json loadedCameraJson = makeGameObjectJson(1200, "Loaded Camera");
            loadedCameraJson["components"].push_back({{"id", 1201},
                                                      {"width", 320.0f},
                                                      {"height", 240.0f},
                                                      {"type", static_cast<int>(ComponentType::CAMERA)}});
            loadedCameraJson["position"] = makeVectorJson(5.0f, 10.0f);

            writeJsonFile(scenePath, nlohmann::json::array({loadedBallJson, loadedCameraJson}),
                          "Scene loading regression failed to create the temporary scene file.");

            Scene scene(scenePath.string());
            gameManager.loadScene(scene);

            GameObject *loadedBall = gameManager.getGameObject("Loaded Ball");
            require(loadedBall != nullptr, "Scene loading regression failed to create the ball object.");
            require(loadedBall->getTag() == "Player", "Scene loading regression failed to load the object tag.");
            require(std::abs(loadedBall->getPosition().x() - 123.0f) <= 1e-5f && std::abs(loadedBall->getPosition().y() - 234.0f) <= 1e-5f,
                    "Scene loading regression failed to load the object position.");
            require(std::abs(loadedBall->getRotation() - 0.5f) <= 1e-5f,
                    "Scene loading regression failed to load the object rotation.");
            require(std::abs(loadedBall->getScale().x() - 2.0f) <= 1e-5f && std::abs(loadedBall->getScale().y() - 1.5f) <= 1e-5f,
                    "Scene loading regression failed to load the object scale.");

            Rigidbody *rigidbody = loadedBall->getComponent<Rigidbody>();
            require(rigidbody != nullptr, "Scene loading regression failed to load the rigidbody component.");
            require(rigidbody->getBodyType() == BodyType::DYNAMIC && !rigidbody->getGravity(),
                    "Scene loading regression failed to load rigidbody type or gravity.");
            require(std::abs(rigidbody->getMass() - 7.0f) <= 1e-5f,
                    "Scene loading regression failed to load rigidbody mass.");
            require(std::abs(rigidbody->getVelocity().x() - 10.0f) <= 1e-5f && std::abs(rigidbody->getVelocity().y() + 20.0f) <= 1e-5f,
                    "Scene loading regression failed to load rigidbody velocity.");
            require(std::abs(rigidbody->getForce().x() - 1.0f) <= 1e-5f && std::abs(rigidbody->getForce().y() - 2.0f) <= 1e-5f,
                    "Scene loading regression failed to load rigidbody force.");
            require(std::abs(rigidbody->getAngularVelocity() - 0.25f) <= 1e-5f && std::abs(rigidbody->getTorque() - 0.75f) <= 1e-5f,
                    "Scene loading regression failed to load angular rigidbody values.");
            require(std::abs(rigidbody->getFriction() - 0.4f) <= 1e-5f && std::abs(rigidbody->getRestitution() - 0.6f) <= 1e-5f,
                    "Scene loading regression failed to load rigidbody material properties.");

            CircleCollider *circleCollider = loadedBall->getComponent<CircleCollider>();
            require(circleCollider != nullptr, "Scene loading regression failed to load the circle collider.");
            require(std::abs(circleCollider->getRadius() - 24.0f) <= 1e-5f,
                    "Scene loading regression failed to load the circle collider radius.");
            require(circleCollider->getCollisionGroup() == 3 && circleCollider->getCollisionMask() == 5 && !circleCollider->getIsTrigger(),
                    "Scene loading regression failed to load the circle collider filter settings.");
            require(std::abs(circleCollider->getOffset().x() + 3.0f) <= 1e-5f && std::abs(circleCollider->getOffset().y() - 4.0f) <= 1e-5f,
                    "Scene loading regression failed to load the circle collider offset.");

            Sprite *sprite = loadedBall->getComponent<Sprite>();
            require(sprite != nullptr, "Scene loading regression failed to load the sprite component.");
            require(sprite->getTexture() != nullptr, "Scene loading regression failed to load the sprite texture.");
            require(sprite->getFlip() == SDL_FLIP_HORIZONTAL,
                    "Scene loading regression failed to load the sprite flip mode.");
            require(std::abs(sprite->getWidthWithoutScale() - 32.0f) <= 1e-5f && std::abs(sprite->getHeightWithoutScale() - 48.0f) <= 1e-5f,
                    "Scene loading regression failed to load the sprite size.");

            GameObject *loadedCameraObject = gameManager.getGameObject("Loaded Camera");
            require(loadedCameraObject != nullptr, "Scene loading regression failed to create the camera object.");

            Camera *camera = loadedCameraObject->getComponent<Camera>();
            require(camera != nullptr, "Scene loading regression failed to load the camera component.");
            require(gameManager.getMainCamera() == camera,
                    "Scene loading regression failed to register the loaded camera as the main camera.");
            const SDL_FRect &cameraRect = camera->getCamera();
            require(std::abs(cameraRect.x - 5.0f) <= 1e-5f && std::abs(cameraRect.y - 10.0f) <= 1e-5f &&
                        std::abs(cameraRect.w - 320.0f) <= 1e-5f && std::abs(cameraRect.h - 240.0f) <= 1e-5f,
                    "Scene loading regression failed to load the camera viewport.");

            cleanup();
        }
        catch (const std::exception &)
        {
            cleanup();
            throw;
        }
    }

    void testSceneSavingRoundTrip()
    {
        Runtime &gameManager = Runtime::current();
        require(gameManager.getWindowHandle() != nullptr, "Scene round-trip regression requires an SDLWindow.");

        std::filesystem::path scenePath = std::filesystem::current_path() / "scene_roundtrip_regression.scene";
        std::filesystem::path wallTexturePath = findRegressionAsset("wall.png");
        std::string expectedSavedSpritePath = wallTexturePath.generic_string();
        std::string wallTexturePathString = wallTexturePath.generic_string();

        auto cleanup = [&]()
        {
            destroyNamedObject(gameManager, "Roundtrip Ball");
            destroyNamedObject(gameManager, "Roundtrip Camera");

            drainPhysicsQueuesAndDeleteMarkedObjects(gameManager);

            std::error_code errorCode;
            std::filesystem::remove(scenePath, errorCode);
        };

        cleanup();

        try
        {
            GameObject *roundtripBall = gameManager
                                            .createGameObject()
                                            ->setName("Roundtrip Ball")
                                            ->setTag("Roundtrip")
                                            ->setPosition(Eigen::Vector2f(210.0f, 220.0f))
                                            ->setRotation(0.25f)
                                            ->setScale(Eigen::Vector2f(2.0f, 1.5f))
                                            ->addComponent<Rigidbody>(KINEMATIC, false)
                                            ->addComponent<CircleCollider>(12.0f)
                                            ->addComponent<Sprite>(wallTexturePathString.c_str(), SDL_FLIP_VERTICAL, 32.0f, 48.0f);

            Sprite *sourceSprite = roundtripBall->getComponent<Sprite>();
            require(sourceSprite != nullptr && sourceSprite->getTexture() != nullptr,
                    "Scene round-trip regression failed to create the source sprite texture.");

            Rigidbody *rigidbody = roundtripBall->getComponent<Rigidbody>();
            require(rigidbody != nullptr, "Scene round-trip regression failed to create the source rigidbody.");
            rigidbody->setMass(9.0f)
                ->setVelocity(Eigen::Vector2f(30.0f, -40.0f))
                ->setForce(Eigen::Vector2f(4.0f, 5.0f))
                ->setAngularVelocity(0.6f)
                ->setTorque(1.2f)
                ->setFriction(0.3f)
                ->setRestitution(0.7f);

            CircleCollider *circleCollider = roundtripBall->getComponent<CircleCollider>();
            require(circleCollider != nullptr, "Scene round-trip regression failed to create the source circle collider.");
            circleCollider->setOffset(Eigen::Vector2f(6.0f, -8.0f));
            circleCollider->setIsTrigger(true);
            circleCollider->setCollisionGroup(7);
            circleCollider->setCollisionMask(11);

            GameObject *roundtripCamera = gameManager
                                              .createGameObject()
                                              ->setName("Roundtrip Camera")
                                              ->addComponent<Camera>(3.0f, 4.0f, 300.0f, 200.0f);

            Scene scene(scenePath.string());
            gameManager.saveScene(scene);

            require(std::filesystem::exists(scenePath), "Scene round-trip regression failed to create the saved scene file.");

            std::ifstream savedSceneFile(scenePath);
            require(savedSceneFile.is_open(), "Scene round-trip regression failed to reopen the saved scene file.");
            const nlohmann::json savedScene = nlohmann::json::parse(savedSceneFile);
            require(savedScene.is_array(), "Scene round-trip regression failed to save the objects array.");
            const nlohmann::json *savedBall = findSavedObject(savedScene, "Roundtrip Ball");
            require(savedBall != nullptr, "Scene round-trip regression failed to save the ball object.");
            const nlohmann::json *savedSprite = findSavedComponent(savedBall->at("components"), ComponentType::SPRITE);
            require(savedSprite != nullptr && savedSprite->at("textureFilePath").get<std::string>() == expectedSavedSpritePath,
                    "Scene round-trip regression failed to save sprite paths relative to the scene file.");
            const nlohmann::json *savedCollider = findSavedComponent(savedBall->at("components"), ComponentType::COLLIDER);
            require(savedCollider != nullptr,
                    "Scene round-trip regression failed to save the collider component.");
            require(std::abs(savedCollider->at("offset").at("x").get<float>() - 6.0f) <= 1e-5f &&
                        std::abs(savedCollider->at("offset").at("y").get<float>() + 8.0f) <= 1e-5f,
                    "Scene round-trip regression failed to save the collider offset.");

            destroyNamedObject(gameManager, "Roundtrip Ball");
            destroyNamedObject(gameManager, "Roundtrip Camera");
            drainPhysicsQueuesAndDeleteMarkedObjects(gameManager);

            gameManager.loadScene(scene);

            GameObject *loadedBall = gameManager.getGameObject("Roundtrip Ball");
            require(loadedBall != nullptr, "Scene round-trip regression failed to recreate the saved ball.");
            require(loadedBall->getTag() == "Roundtrip", "Scene round-trip regression failed to preserve the object tag.");
            require(std::abs(loadedBall->getPosition().x() - 210.0f) <= 1e-5f && std::abs(loadedBall->getPosition().y() - 220.0f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve the object position.");
            require(std::abs(loadedBall->getRotation() - 0.25f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve the object rotation.");
            require(std::abs(loadedBall->getScale().x() - 2.0f) <= 1e-5f && std::abs(loadedBall->getScale().y() - 1.5f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve the object scale.");

            Rigidbody *loadedRigidbody = loadedBall->getComponent<Rigidbody>();
            require(loadedRigidbody != nullptr, "Scene round-trip regression failed to reload the rigidbody.");
            require(loadedRigidbody->getBodyType() == BodyType::KINEMATIC && !loadedRigidbody->getGravity(),
                    "Scene round-trip regression failed to preserve rigidbody bodyType or gravity.");
            require(std::abs(loadedRigidbody->getMass() - 9.0f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve rigidbody mass.");
            require(std::abs(loadedRigidbody->getVelocity().x() - 30.0f) <= 1e-5f && std::abs(loadedRigidbody->getVelocity().y() + 40.0f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve rigidbody velocity.");
            require(std::abs(loadedRigidbody->getForce().x() - 4.0f) <= 1e-5f && std::abs(loadedRigidbody->getForce().y() - 5.0f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve rigidbody force.");
            require(std::abs(loadedRigidbody->getAngularVelocity() - 0.6f) <= 1e-5f && std::abs(loadedRigidbody->getTorque() - 1.2f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve angular rigidbody values.");
            require(std::abs(loadedRigidbody->getFriction() - 0.3f) <= 1e-5f && std::abs(loadedRigidbody->getRestitution() - 0.7f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve rigidbody material properties.");

            CircleCollider *loadedCircleCollider = loadedBall->getComponent<CircleCollider>();
            require(loadedCircleCollider != nullptr, "Scene round-trip regression failed to reload the circle collider.");
            require(std::abs(loadedCircleCollider->getRadius() - 24.0f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve the circle collider radius.");
            require(loadedCircleCollider->getIsTrigger() && loadedCircleCollider->getCollisionGroup() == 7 && loadedCircleCollider->getCollisionMask() == 11,
                    "Scene round-trip regression failed to preserve the circle collider filter settings.");
            require(std::abs(loadedCircleCollider->getOffset().x() - 6.0f) <= 1e-5f &&
                        std::abs(loadedCircleCollider->getOffset().y() + 8.0f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve the collider offset.");

            Sprite *loadedSprite = loadedBall->getComponent<Sprite>();
            require(loadedSprite != nullptr, "Scene round-trip regression failed to reload the sprite.");
            require(loadedSprite->getTexture() != nullptr, "Scene round-trip regression failed to reload the sprite texture.");
            require(loadedSprite->getFlip() == SDL_FLIP_VERTICAL,
                    "Scene round-trip regression failed to preserve the sprite flip mode.");
            require(std::abs(loadedSprite->getWidthWithoutScale() - 32.0f) <= 1e-5f && std::abs(loadedSprite->getHeightWithoutScale() - 48.0f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve the sprite size.");

            GameObject *loadedCameraObject = gameManager.getGameObject("Roundtrip Camera");
            require(loadedCameraObject != nullptr, "Scene round-trip regression failed to recreate the saved camera.");
            Camera *loadedCamera = loadedCameraObject->getComponent<Camera>();
            require(loadedCamera != nullptr, "Scene round-trip regression failed to reload the camera component.");
            require(gameManager.getMainCamera() == loadedCamera,
                    "Scene round-trip regression failed to register the reloaded camera as the main camera.");
            const SDL_FRect &loadedCameraRect = loadedCamera->getCamera();
            require(std::abs(loadedCameraRect.x - 3.0f) <= 1e-5f && std::abs(loadedCameraRect.y - 4.0f) <= 1e-5f &&
                        std::abs(loadedCameraRect.w - 300.0f) <= 1e-5f && std::abs(loadedCameraRect.h - 200.0f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve the camera viewport.");

            cleanup();
        }
        catch (const std::exception &)
        {
            cleanup();
            throw;
        }
    }

    void testRestitutionBounce()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *floor = createStaticBox(gameManager, "Bounce Floor", 220.0f, 320.0f, 400.0f, 40.0f);
        GameObject *ball = createDynamicCircle(gameManager, "Bounce Ball", 220.0f, 120.0f, 20.0f);
        sceneScope.add(floor);
        sceneScope.add(ball);

        Rigidbody *rigidbody = ball->getComponent<Rigidbody>();
        require(rigidbody != nullptr, "Restitution regression lost the circle rigidbody.");
        rigidbody->setRestitution(0.8f);
        rigidbody->setFriction(0.0f);

        const float expectedContactHeight = 320.0f - 20.0f - 20.0f;
        bool observedBounce = false;

        for (int frameIndex = 0; frameIndex < 360; ++frameIndex)
        {
            gameManager.simulateFrame(kTimeStep);

            if (ball->getPosition().y() >= expectedContactHeight - 2.0f && rigidbody->getVelocity().y() < -5.0f)
            {
                observedBounce = true;
                break;
            }
        }

        require(observedBounce, "Restitution regression never produced an upward post-impact velocity.");
    }

    void testKinematicBodySemantics()
    {
        Runtime &gameManager = Runtime::current();

        {
            SceneScope sceneScope(gameManager);
            GameObject *kinematicMover = createKinematicBox(gameManager, "Kinematic Mover", 100.0f, 100.0f, 80.0f, 20.0f, true);
            GameObject *staticWall = createStaticBox(gameManager, "Kinematic Wall", 200.0f, 100.0f, 20.0f, 220.0f);
            sceneScope.add(kinematicMover);
            sceneScope.add(staticWall);

            Rigidbody *kinematicBody = kinematicMover->getComponent<Rigidbody>();
            require(kinematicBody != nullptr, "Kinematic regression lost the mover rigidbody.");

            kinematicBody->setVelocity(Eigen::Vector2f(120.0f, 0.0f));
            kinematicBody->setForce(Eigen::Vector2f(5000.0f, 5000.0f));

            simulateFrames(gameManager, 180);

            require(kinematicBody->getInverseMass() == 0.0f, "Kinematic bodies must have zero inverse mass in the solver.");
            require(kinematicBody->getInverseMomentOfInertia() == 0.0f, "Kinematic bodies must have zero inverse inertia in the solver.");
            require(kinematicMover->getPosition().x() <= 151.0f,
                    "Kinematic body passed through a static wall instead of being blocked.");
            require(std::abs(kinematicMover->getPosition().y() - 100.0f) <= 1.0f,
                    "Kinematic body should ignore gravity and force-based integration.");
            require(std::abs(kinematicBody->getVelocity().x()) <= 1.0f,
                    "Kinematic body should have its wall-normal velocity cancelled on contact.");
        }

        {
            SceneScope sceneScope(gameManager);
            GameObject *kinematicFloor = createKinematicBox(gameManager, "Kinematic Floor", 260.0f, 340.0f, 360.0f, 40.0f, true);
            GameObject *dynamicBox = createDynamicBox(gameManager, "Dynamic On Kinematic", 260.0f, 180.0f, 40.0f, 40.0f);
            sceneScope.add(kinematicFloor);
            sceneScope.add(dynamicBox);

            simulateFrames(gameManager, 720);

            Rigidbody *dynamicBody = dynamicBox->getComponent<Rigidbody>();
            require(dynamicBody != nullptr, "Kinematic floor regression lost the dynamic rigidbody.");
            require(std::abs(kinematicFloor->getPosition().y() - 340.0f) <= 0.5f,
                    "Kinematic floor should not be displaced by gravity or solver impulses.");
            require(dynamicBody->hasSupportContact(), "Dynamic body failed to register support from a kinematic floor.");
            require(std::abs(dynamicBox->getPosition().y() - 300.0f) <= 6.0f,
                    "Dynamic body failed to settle on the kinematic floor.");
        }

        {
            SceneScope sceneScope(gameManager);
            GameObject *staticFloor = createStaticBox(gameManager, "Static Floor For Kinematic", 260.0f, 340.0f, 360.0f, 40.0f);
            GameObject *kinematicBox = createKinematicBox(gameManager, "Kinematic Supported Box", 260.0f, 300.0f, 40.0f, 40.0f, false);
            sceneScope.add(staticFloor);
            sceneScope.add(kinematicBox);

            gameManager.simulateFrame(kTimeStep);

            Rigidbody *kinematicBody = kinematicBox->getComponent<Rigidbody>();
            require(kinematicBody != nullptr, "Kinematic support regression lost the rigidbody.");
            require(kinematicBody->hasSupportContact(),
                    "Kinematic body failed to register support contact while resting on a static floor.");
        }
    }

    void testAnimatorComponentPlayback()
    {
        Runtime &gameManager = Runtime::current();

        const std::filesystem::path frameAPath = createTemporaryBmpAsset("animator_frame_a_regression.bmp", 255, 0, 0);
        const std::filesystem::path frameBPath = createTemporaryBmpAsset("animator_frame_b_regression.bmp", 0, 255, 0);
        const std::string canonicalFrameAPath = canonicalAssetPathForTest(frameAPath);
        const std::string canonicalFrameBPath = canonicalAssetPathForTest(frameBPath);

        auto cleanupFiles = [&]()
        {
            std::error_code errorCode;
            std::filesystem::remove(frameAPath, errorCode);
            std::filesystem::remove(frameBPath, errorCode);
        };

        try
        {
            SceneScope sceneScope(gameManager);
            GameObject *animatedObject = gameManager
                                             .createGameObject()
                                             ->setName("Animated Sprite")
                                             ->addComponent<Sprite>(frameAPath.string().c_str(), SDL_FLIP_NONE, 8.0f, 8.0f)
                                             ->addComponent<Animator>();
            sceneScope.add(animatedObject);

            Animator *animator = animatedObject->getComponent<Animator>();
            Sprite *sprite = animatedObject->getComponent<Sprite>();
            require(animator != nullptr, "Animator regression failed to create the animator component.");
            require(sprite != nullptr, "Animator regression failed to create the sprite component.");

            TextureWrapper *frameATexture = gameManager.loadTexture(frameAPath.string());
            TextureWrapper *frameBTexture = gameManager.loadTexture(frameBPath.string());
            require(frameATexture != nullptr && frameBTexture != nullptr,
                    "Animator regression failed to load the test frame textures.");

            const std::vector<AnimationFrame> blinkFrames = {
                AnimationFrame(frameATexture, 0.0f),
                AnimationFrame(frameBTexture, 0.0f)};
            const AnimationClip blinkAnimationClip(blinkFrames, 10.0f, true, 8.0f, 8.0f, "blink");

            require(animator->play(&blinkAnimationClip), "Animator regression failed to start the test animation clip.");
            require(sprite->getTextureWrapper() != nullptr && sprite->getTextureWrapper()->getFilePath() == canonicalFrameAPath,
                    "Animator regression failed to apply the initial clip frame.");

            simulateFrames(gameManager, 4);
            require(sprite->getTextureWrapper() != nullptr && sprite->getTextureWrapper()->getFilePath() == canonicalFrameAPath,
                    "Animator regression failed to respect the animation clip playback speed.");

            simulateFrames(gameManager, 9);
            require(sprite->getTextureWrapper() != nullptr && sprite->getTextureWrapper()->getFilePath() == canonicalFrameBPath,
                    "Animator regression failed to advance to the second frame.");

            require(animator->play(&blinkAnimationClip), "Animator regression restarted instead of reusing the current animation clip.");
            require(animator->getCurrentFrameIndex() == 1,
                    "Animator regression reset the current frame when asked to play the already active animation clip.");

            simulateFrames(gameManager, 12);
            require(sprite->getTextureWrapper() != nullptr && sprite->getTextureWrapper()->getFilePath() == canonicalFrameAPath,
                    "Animator regression failed to loop back to the first frame.");

            cleanupFiles();
        }
        catch (const std::exception &)
        {
            cleanupFiles();
            throw;
        }
    }

    void testAnimatorAdvancedPlayback()
    {
        Runtime &gameManager = Runtime::current();

        const std::filesystem::path frameAPath = createTemporaryBmpAsset("animator_advanced_idle_regression.bmp", 255, 0, 0);
        const std::filesystem::path sheetPath = createTemporaryStripBmpAsset("animator_advanced_sheet_regression.bmp");
        const std::string canonicalFrameAPath = canonicalAssetPathForTest(frameAPath);
        const std::string canonicalSheetPath = canonicalAssetPathForTest(sheetPath);

        auto cleanupFiles = [&]()
        {
            std::error_code errorCode;
            std::filesystem::remove(frameAPath, errorCode);
            std::filesystem::remove(sheetPath, errorCode);
        };

        try
        {
            SceneScope sceneScope(gameManager);
            GameObject *animatedObject = gameManager
                                             .createGameObject()
                                             ->setName("Advanced Animated Sprite")
                                             ->addComponent<Sprite>(frameAPath.string().c_str(), SDL_FLIP_NONE, 4.0f, 4.0f)
                                             ->addComponent<Animator>();
            sceneScope.add(animatedObject);

            Animator *animator = animatedObject->getComponent<Animator>();
            Sprite *sprite = animatedObject->getComponent<Sprite>();
            require(animator != nullptr, "Advanced animator regression failed to create the animator component.");
            require(sprite != nullptr, "Advanced animator regression failed to create the sprite component.");

            TextureWrapper *idleTexture = gameManager.loadTexture(frameAPath.string());
            TextureWrapper *sheetTexture = gameManager.loadTexture(sheetPath.string());
            require(idleTexture != nullptr && sheetTexture != nullptr,
                    "Advanced animator regression failed to load the clip textures.");

            const std::vector<AnimationFrame> idleFrames = {AnimationFrame(idleTexture, 0.0f)};

            const std::vector<AnimationFrame> stripFrames = {
                AnimationFrame(sheetTexture, SDL_FRect{0.0f, 0.0f, 4.0f, 4.0f}, 0.05f),
                AnimationFrame(sheetTexture, SDL_FRect{4.0f, 0.0f, 4.0f, 4.0f}, 0.05f)};
            const AnimationClip stripAnimationClip(stripFrames, 20.0f, false, 4.0f, 4.0f, "strip");
            const AnimationClip idleAnimationClip(idleFrames, 1.0f, true, 4.0f, 4.0f, "idle");

            require(animator->play(&stripAnimationClip), "Advanced animator regression failed to start one-shot playback.");

            require(sprite->getTextureWrapper() != nullptr && sprite->getTextureWrapper()->getFilePath() == canonicalSheetPath,
                    "Advanced animator regression failed to apply the strip texture.");
            require(sprite->getSourceRect() != nullptr && std::abs(sprite->getSourceRect()->x - 0.0f) <= 1e-5f,
                    "Advanced animator regression failed to apply the first strip source rectangle.");

            simulateFrames(gameManager, 7);
            require(sprite->getTextureWrapper() != nullptr && sprite->getTextureWrapper()->getFilePath() == canonicalSheetPath,
                    "Advanced animator regression unexpectedly changed the strip texture mid-clip.");
            require(sprite->getSourceRect() != nullptr && std::abs(sprite->getSourceRect()->x - 4.0f) <= 1e-5f,
                    "Advanced animator regression failed to advance to the second strip frame.");

            simulateFrames(gameManager, 7);
            require(sprite->getTextureWrapper() != nullptr && sprite->getTextureWrapper()->getFilePath() == canonicalSheetPath,
                    "Advanced animator regression failed to keep the final one-shot frame active after playback completed.");
            require(sprite->getSourceRect() != nullptr && std::abs(sprite->getSourceRect()->x - 4.0f) <= 1e-5f,
                    "Advanced animator regression failed to stop on the final one-shot frame.");
            require(!animator->getIsPlaying(),
                    "Advanced animator regression failed to stop playback when a non-looping animation finished.");

            require(animator->play(&idleAnimationClip), "Advanced animator regression failed to switch to a replacement animation clip.");
            require(sprite->getTextureWrapper() != nullptr && sprite->getTextureWrapper()->getFilePath() == canonicalFrameAPath,
                    "Advanced animator regression failed to switch to the idle animation clip.");
            require(sprite->getSourceRect() == nullptr,
                    "Advanced animator regression failed to clear the sprite source rectangle when switching to a full-frame animation clip.");

            cleanupFiles();
        }
        catch (const std::exception &)
        {
            cleanupFiles();
            throw;
        }
    }

    void testAnimationClipFileRoundTrip()
    {
        Runtime &gameManager = Runtime::current();

        const std::filesystem::path frameAPath = createTemporaryBmpAsset("animationclip_roundtrip_idle_regression.bmp", 255, 0, 0);
        const std::filesystem::path sheetPath = createTemporaryStripBmpAsset("animationclip_roundtrip_sheet_regression.bmp");
        const std::filesystem::path clipPath = makeRuntimeAssetPath("animationclip_roundtrip_regression.animset");
        const std::string canonicalFrameAPath = canonicalAssetPathForTest(frameAPath);
        const std::string canonicalSheetPath = canonicalAssetPathForTest(sheetPath);

        auto cleanupFiles = [&]()
        {
            std::error_code errorCode;
            std::filesystem::remove(frameAPath, errorCode);
            std::filesystem::remove(sheetPath, errorCode);
            std::filesystem::remove(clipPath, errorCode);
        };

        try
        {
            TextureWrapper *idleTexture = gameManager.loadTexture(frameAPath.string());
            TextureWrapper *sheetTexture = gameManager.loadTexture(sheetPath.string());
            require(idleTexture != nullptr && sheetTexture != nullptr,
                    "Animation clip file round-trip regression failed to load the source textures.");

            const std::vector<AnimationFrame> frames = {
                AnimationFrame(idleTexture, 0.0f),
                AnimationFrame(sheetTexture, SDL_FRect{4.0f, 0.0f, 4.0f, 4.0f}, 0.05f)};
            const AnimationClip sourceClip(frames, 20.0f, false, 4.0f, 4.0f, "saved_clip", clipPath.string());

            writeJsonFile(clipPath, nlohmann::json(sourceClip),
                          "Animation clip file round-trip regression failed to create the clip file.");
            AnimationClip *loadedClipPointer = gameManager.loadAnimationClip(clipPath.string());
            require(loadedClipPointer != nullptr,
                    "Animation clip file round-trip regression failed to load the saved clip file.");
            const AnimationClip &loadedClip = *loadedClipPointer;

            require(loadedClip.getName() == "saved_clip",
                    "Animation clip file round-trip regression lost the clip name.");
            require(std::abs(loadedClip.getFramesPerSecond() - 20.0) <= 1e-6,
                    "Animation clip file round-trip regression lost the clip playback speed.");
            require(!loadedClip.getLoop(),
                    "Animation clip file round-trip regression lost the clip loop flag.");
            require(std::abs(loadedClip.getWidth() - 4.0f) <= 1e-5f && std::abs(loadedClip.getHeight() - 4.0f) <= 1e-5f,
                    "Animation clip file round-trip regression lost the clip size.");
            require(loadedClip.getFrames().size() == 2,
                    "Animation clip file round-trip regression lost clip frames during save/load.");

            const AnimationFrame &loadedIdleFrame = loadedClip.getFrames()[0];
            const AnimationFrame &loadedStripFrame = loadedClip.getFrames()[1];

            require(loadedIdleFrame.textureWrapper != nullptr && loadedIdleFrame.textureWrapper->getFilePath() == canonicalFrameAPath,
                    "Animation clip file round-trip regression lost the first frame texture path.");
            require(!loadedIdleFrame.hasSourceRect,
                    "Animation clip file round-trip regression added an unexpected source rectangle to the first frame.");
            require(loadedStripFrame.textureWrapper != nullptr && loadedStripFrame.textureWrapper->getFilePath() == canonicalSheetPath,
                    "Animation clip file round-trip regression lost the second frame texture path.");
            require(loadedStripFrame.hasSourceRect,
                    "Animation clip file round-trip regression lost the strip frame source rectangle.");
            require(std::abs(loadedStripFrame.duration - 0.05f) <= 1e-5f,
                    "Animation clip file round-trip regression lost the strip frame duration.");
            require(std::abs(loadedStripFrame.sourceRect.x - 4.0f) <= 1e-5f && std::abs(loadedStripFrame.sourceRect.y) <= 1e-5f &&
                        std::abs(loadedStripFrame.sourceRect.w - 4.0f) <= 1e-5f && std::abs(loadedStripFrame.sourceRect.h - 4.0f) <= 1e-5f,
                    "Animation clip file round-trip regression lost the strip frame source rectangle contents.");

            cleanupFiles();
        }
        catch (const std::exception &)
        {
            cleanupFiles();
            throw;
        }
    }

    void testAudioComponentControls()
    {
        Runtime &gameManager = Runtime::current();
        require(gameManager.getWindowHandle() != nullptr, "Audio regression requires an SDLWindow.");

        const std::filesystem::path audioPath = findRegressionAudioAsset("jump.wav");
        const std::string audioPathString = audioPath.string();

        SceneScope sceneScope(gameManager);
        GameObject *audioObject = gameManager
                                      .createGameObject()
                                      ->setName("Audio Object")
                                      ->addComponent<Audio>(audioPathString.c_str());
        sceneScope.add(audioObject);

        Audio *audio = audioObject->getComponent<Audio>();
        require(audio != nullptr, "Audio regression failed to create the audio component.");
        require(audio->getAudioWrapper() != nullptr, "Audio regression failed to load the test audio asset.");

        audio->setGain(0.5f);
        require(std::abs(audio->getGain() - 0.5f) <= 1e-5f,
                "Audio regression failed to apply the configured gain.");

        require(audio->play(), "Audio regression failed to start playback.");
        require(audio->isPlaying(), "Audio regression expected the track to be playing after play().");

        require(audio->pause(), "Audio regression failed to pause playback.");
        require(audio->isPaused(), "Audio regression expected the track to be paused after pause().");

        require(audio->resume(), "Audio regression failed to resume playback.");
        require(audio->isPlaying(), "Audio regression expected the track to be playing after resume().");

        require(audio->stop(), "Audio regression failed to stop playback.");
        require(!audio->isPlaying() && !audio->isPaused(),
                "Audio regression expected the track to be idle after stop().");
    }

    void testPathManagerCanonicalizationAndFallback()
    {
        PathManager &pathManager = PathManager::getInstance();

        std::string mixedCaseAbsolutePath = std::filesystem::absolute(std::filesystem::path("assets") / "ball.png").generic_string();
        std::replace(mixedCaseAbsolutePath.begin(), mixedCaseAbsolutePath.end(), '/', '\\');
        std::transform(mixedCaseAbsolutePath.begin(), mixedCaseAbsolutePath.end(), mixedCaseAbsolutePath.begin(), [](unsigned char character)
                       { return static_cast<char>(std::toupper(character)); });

        const ResolvedAssetPath sanitizedTexturePath = pathManager.resolveAssetPath(mixedCaseAbsolutePath, AssetPathType::Texture);
        require(!sanitizedTexturePath.usedFallback,
                "PathManager regression unexpectedly used a fallback for a real asset addressed by an absolute OS path.");
        require(sanitizedTexturePath.canonicalPath == "assets/ball.png",
                "PathManager regression failed to canonicalize an absolute texture path to assets/ball.png.");
        require(std::filesystem::is_regular_file(sanitizedTexturePath.absolutePath),
                "PathManager regression failed to resolve the canonical texture path to a real file on disk.");

        const ResolvedAssetPath missingTexturePath = pathManager.resolveAssetPath("ASSETS\\TEXTURES\\DOES_NOT_EXIST.PNG", AssetPathType::Texture);
        require(missingTexturePath.usedFallback,
                "PathManager regression failed to switch to the texture fallback for a missing asset.");
        require(missingTexturePath.canonicalPath == "assets/textures/missing.png",
                "PathManager regression returned the wrong texture fallback path.");
        require(std::filesystem::is_regular_file(missingTexturePath.absolutePath),
                "PathManager regression resolved the texture fallback to a missing file.");

        const ResolvedAssetPath missingModelPath = pathManager.resolveAssetPath("/tmp/not_in_assets/default_cube.obj", AssetPathType::Model);
        require(missingModelPath.usedFallback,
                "PathManager regression failed to reject a non-assets absolute model path.");
        require(missingModelPath.canonicalPath == "assets/models/default_cube.obj",
                "PathManager regression returned the wrong model fallback path.");
        require(std::filesystem::is_regular_file(missingModelPath.absolutePath),
                "PathManager regression resolved the model fallback to a missing file.");
    }

    void testPathManagerLookupDoesNotDependOnCurrentWorkingDirectory()
    {
        PathManager &pathManager = PathManager::getInstance();
        const std::filesystem::path temporaryWorkingDirectory = std::filesystem::temp_directory_path() / "platformator_pathmanager_cwd_regression";
        WorkingDirectoryScope workingDirectoryScope(std::filesystem::current_path());

        std::filesystem::create_directories(temporaryWorkingDirectory);
        std::filesystem::current_path(temporaryWorkingDirectory);

        const ResolvedAssetPath resolvedTexturePath = pathManager.resolveAssetPath("assets/ball.png", AssetPathType::Texture);

        require(!resolvedTexturePath.usedFallback,
                "PathManager regression unexpectedly used a fallback when resolving an asset after changing the working directory.");
        require(resolvedTexturePath.canonicalPath == "assets/ball.png",
                "PathManager regression changed the canonical asset path after the working directory changed.");
        require(std::filesystem::is_regular_file(resolvedTexturePath.absolutePath),
                "PathManager regression failed to find a real asset using executable-relative lookup.");
    }

    void testSceneLookupDoesNotDependOnCurrentWorkingDirectory()
    {
        const std::filesystem::path temporaryWorkingDirectory = std::filesystem::temp_directory_path() / "platformator_scene_cwd_regression";
        WorkingDirectoryScope workingDirectoryScope(std::filesystem::current_path());

        std::filesystem::create_directories(temporaryWorkingDirectory);
        std::filesystem::current_path(temporaryWorkingDirectory);

        Scene scene("assets/scenes/default.scene");
        std::vector<GameObject *> loadedObjects = scene.loadScene();
        require(!loadedObjects.empty(),
                "Scene path regression failed to load the default scene after changing the working directory.");
    }

    void testSceneLookupFallsBackToAncestorAssetsDirectoryWhenNearestAssetsTreeIsIncomplete()
    {
        const std::filesystem::path temporaryWorkingDirectory = std::filesystem::temp_directory_path() / "platformator_scene_benchmark_path_regression";
        WorkingDirectoryScope workingDirectoryScope(std::filesystem::current_path());

        std::filesystem::create_directories(temporaryWorkingDirectory);
        std::filesystem::current_path(temporaryWorkingDirectory);

        Scene scene("assets/scenes/benchmarks/broad_phase_churn.scene");
        std::vector<GameObject *> loadedObjects = scene.loadScene();
        require(!loadedObjects.empty(),
                "Scene path regression failed to load a benchmark scene when the nearest executable-relative assets tree was incomplete.");
    }

    void testRotatedBoxSupportEdgeSelection()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *boxObject = createStaticBox(gameManager, "Rotated Support Box", 240.0f, 180.0f, 80.0f, 40.0f);
        sceneScope.add(boxObject);

        boxObject->setRotation(0.6f);

        BoxCollider *boxCollider = boxObject->getComponent<BoxCollider>();
        require(boxCollider != nullptr, "Rotated support-edge regression lost the box collider.");

        boxCollider->prepareSync();

        const auto &vertices = boxCollider->getVertices();

        auto verifySupportEdge = [&](const Eigen::Vector2f &normal, const char *label)
        {
            Edge edge = boxCollider->getEdgeWithNormal(normal);

            float maxProjection = -std::numeric_limits<float>::infinity();
            for (const Eigen::Vector2f &vertex : vertices)
            {
                maxProjection = std::max(maxProjection, vertex.dot(normal));
            }

            constexpr float kProjectionTolerance = 1e-3f;
            require(std::abs(edge.v1.dot(normal) - maxProjection) <= kProjectionTolerance,
                    std::string("Rotated support-edge regression chose a non-support first vertex for ") + label + ".");
            require(std::abs(edge.v2.dot(normal) - maxProjection) <= kProjectionTolerance,
                    std::string("Rotated support-edge regression chose a non-support second vertex for ") + label + ".");
        };

        const std::vector<Eigen::Vector2f> normals = boxCollider->getNormals(boxCollider);
        require(normals.size() == 2, "Rotated support-edge regression expected two box normals.");

        verifySupportEdge(normals[0], "+x face normal");
        verifySupportEdge(-normals[0], "-x face normal");
        verifySupportEdge(normals[1], "+y face normal");
        verifySupportEdge(-normals[1], "-y face normal");
    }

    void testParentChildRelativeMovement()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *parent = gameManager.createGameObject()->setName("Parent");
        sceneScope.add(parent);

        GameObject *child = gameManager.createGameObject();
        sceneScope.add(child);
        child->setName("Child");
        child->addComponent<BoxCollider>(20.0f, 10.0f);

        parent->addChild(child);
        require(gameManager.getObjectById(child->getId()) == child,
                "Parent-child regression expected manager-created children to remain registered after parenting.");

        child->setPosition(Eigen::Vector2f(15.0f, -8.0f));
        parent->setPosition(Eigen::Vector2f(120.0f, 64.0f));

        require((child->getPosition() - Eigen::Vector2f(135.0f, 56.0f)).squaredNorm() <= 1e-6f,
                "Parent-child regression expected child world position to follow parent movement while preserving local offset.");

        BoxCollider *childCollider = child->getComponent<BoxCollider>();
        require(childCollider != nullptr, "Parent-child regression lost the child box collider.");
        childCollider->prepareSync();

        Eigen::Vector2f colliderCenter = Eigen::Vector2f::Zero();
        for (const Eigen::Vector2f &vertex : childCollider->getVertices())
        {
            colliderCenter += vertex;
        }
        colliderCenter /= 4.0f;

        require((colliderCenter - child->getPosition()).squaredNorm() <= 1e-6f,
                "Parent-child regression expected the child collider center to move with the child transform.");
    }

    void testColliderOffsetAffectsGeometry()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *boxObject = createStaticBox(gameManager, "Offset Box", 200.0f, 120.0f, 40.0f, 20.0f);
        sceneScope.add(boxObject);

        BoxCollider *boxCollider = boxObject->getComponent<BoxCollider>();
        require(boxCollider != nullptr, "Collider offset regression lost the box collider.");

        boxCollider->setOffset(Eigen::Vector2f(10.0f, -5.0f));
        boxCollider->prepareSync();

        Eigen::Vector2f boxCenter = Eigen::Vector2f::Zero();
        for (const Eigen::Vector2f &vertex : boxCollider->getVertices())
        {
            boxCenter += vertex;
        }
        boxCenter /= 4.0f;

        require((boxCenter - Eigen::Vector2f(210.0f, 115.0f)).squaredNorm() <= 1e-6f,
                "Collider offset regression expected an unrotated box collider to shift by its offset.");

        boxObject->setRotation(static_cast<float>(M_PI_2));
        boxCollider->prepareSync();

        boxCenter = Eigen::Vector2f::Zero();
        for (const Eigen::Vector2f &vertex : boxCollider->getVertices())
        {
            boxCenter += vertex;
        }
        boxCenter /= 4.0f;

        require((boxCenter - Eigen::Vector2f(205.0f, 130.0f)).squaredNorm() <= 1e-5f,
                "Collider offset regression expected the offset to rotate with the owning game object.");

        GameObject *circleObject = createDynamicCircle(gameManager, "Offset Circle", 80.0f, 90.0f, 12.0f);
        sceneScope.add(circleObject);

        CircleCollider *circleCollider = circleObject->getComponent<CircleCollider>();
        require(circleCollider != nullptr, "Collider offset regression lost the circle collider.");

        circleCollider->setOffset(Eigen::Vector2f(-6.0f, 4.0f));
        circleCollider->prepareSync();

        Eigen::Vector2f xProjection = circleCollider->projectOntoAxis(X_AXIS);
        Eigen::Vector2f yProjection = circleCollider->projectOntoAxis(Y_AXIS);

        require(std::abs((xProjection.x() + xProjection.y()) * 0.5f - 74.0f) <= 1e-6f,
                "Collider offset regression expected the circle collider X projection center to include its offset.");
        require(std::abs((yProjection.x() + yProjection.y()) * 0.5f - 94.0f) <= 1e-6f,
                "Collider offset regression expected the circle collider Y projection center to include its offset.");
    }

    struct TestCase
    {
        const char *name;
        void (*run)();
    };
} // namespace

int main()
{
    configureHeadlessEnvironment();
    platformator::Runtime runtime;

    static const TestCase testCases[] = {
        {"circle_collision_stability", testCircleCollisionStability},
        {"warm_start_feature_reuse", testWarmStartFeatureReuse},
        {"clip_segment_feature_propagation", testClipSegmentFeaturePropagation},
        {"sleeping_on_support", testSleepingOnSupport},
        {"sleeping_stack_stays_asleep", testSleepingStackStaysAsleep},
        {"sleeping_tall_stack_stays_asleep", testSleepingTallStackStaysAsleep},
        {"sleeping_wide_pile_stays_asleep", testSleepingWidePileStaysAsleep},
        {"sleeping_body_wakes_after_support_removed", testSleepingBodyWakesAfterSupportRemoved},
        {"broad_phase_pair_tracking", testBroadPhasePairTracking},
        {"checkpoint_fast_motion_cancellation", testCheckpointFastMotionCancellation},
        {"parallel_broad_and_narrow_phase_stress", testParallelBroadAndNarrowPhaseStress},
        {"segmented_interval_fast_motion_across_chunks", testSegmentedIntervalListFastMotionAcrossChunks},
        {"segmented_interval_merges_underfull_chunks", testSegmentedIntervalListMergesUnderfullChunks},
        {"scene_loading", testSceneLoading},
        {"scene_saving_round_trip", testSceneSavingRoundTrip},
        {"restitution_bounce", testRestitutionBounce},
        {"kinematic_body_semantics", testKinematicBodySemantics},
        {"animator_component_playback", testAnimatorComponentPlayback},
        {"animator_advanced_playback", testAnimatorAdvancedPlayback},
        {"animationclip_file_round_trip", testAnimationClipFileRoundTrip},
        {"audio_component_controls", testAudioComponentControls},
        {"path_manager_canonicalization_and_fallback", testPathManagerCanonicalizationAndFallback},
        {"path_manager_lookup_does_not_depend_on_current_working_directory", testPathManagerLookupDoesNotDependOnCurrentWorkingDirectory},
        {"scene_lookup_does_not_depend_on_current_working_directory", testSceneLookupDoesNotDependOnCurrentWorkingDirectory},
        {"scene_lookup_falls_back_to_ancestor_assets_directory_when_nearest_assets_tree_is_incomplete", testSceneLookupFallsBackToAncestorAssetsDirectoryWhenNearestAssetsTreeIsIncomplete},
        {"rotated_box_support_edge_selection", testRotatedBoxSupportEdgeSelection},
        {"parent_child_relative_movement", testParentChildRelativeMovement},
        {"collider_offset_affects_geometry", testColliderOffsetAffectsGeometry},
    };

    size_t failureCount = 0;
    for (const TestCase &testCase : testCases)
    {
        try
        {
            testCase.run();
            std::cout << "[PASS] " << testCase.name << '\n';
        }
        catch (const std::exception &exception)
        {
            ++failureCount;
            std::cerr << "[FAIL] " << testCase.name << ": " << exception.what() << '\n';
        }
    }

    if (failureCount != 0)
    {
        std::cerr << failureCount << " regression test(s) failed.\n";
        return 1;
    }

    std::cout << "All Platformator regression tests passed.\n";
    return 0;
}