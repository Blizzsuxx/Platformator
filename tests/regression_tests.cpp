#include <SDL3/SDL.h>

#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "boxcollider.h"
#include "circlecollider.h"
#include "constants.h"
#include "gamemanager.h"
#include "physicsmanager.h"
#include "rigidbody.h"

namespace
{
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

    void simulateFrames(GameManager &gameManager, int frameCount)
    {
        PhysicsManager *physicsManager = gameManager.getPhysicsManager();
        require(physicsManager != nullptr, "PhysicsManager must be available during regression tests.");

        for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            physicsManager->checkForCollisions();
            physicsManager->applyPhysics(kTimeStep);
            physicsManager->resolveCollisions(kTimeStep);
            physicsManager->applyMovement(kTimeStep);
            gameManager.deleteMarkedGameObjects();
        }
    }

    void destroyObjects(GameManager &gameManager, const std::vector<GameObject *> &objects)
    {
        for (GameObject *object : objects)
        {
            if (object != nullptr)
            {
                gameManager.destroyGameObject(object);
            }
        }

        gameManager.deleteMarkedGameObjects();
    }

    GameObject *createStaticBox(GameManager &gameManager, const std::string &name, float x, float y, float width, float height)
    {
        return gameManager
            .createGameObject()
            ->setName(name)
            ->addComponent<Rigidbody>(STATIC, false)
            ->addComponent<BoxCollider>(width, height)
            ->setPosition(Eigen::Vector2f(x, y));
    }

    GameObject *createDynamicCircle(GameManager &gameManager, const std::string &name, float x, float y, float radius)
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

    GameObject *createDynamicBox(GameManager &gameManager, const std::string &name, float x, float y, float width, float height)
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

    void testCircleCollisionStability()
    {
        GameManager &gameManager = GameManager::getInstance();

        std::vector<GameObject *> objects;
        GameObject *floor = createStaticBox(gameManager, "Regression Floor", 200.0f, 300.0f, 400.0f, 40.0f);
        GameObject *ball = createDynamicCircle(gameManager, "Regression Ball", 200.0f, 140.0f, 20.0f);
        objects.push_back(floor);
        objects.push_back(ball);

        simulateFrames(gameManager, 720);

        const float expectedRestY = 300.0f - 20.0f - 20.0f;
        const float actualRestY = ball->getPosition().y();

        require(std::isfinite(actualRestY), "Circle stability regression produced a non-finite position.");
        require(std::abs(actualRestY - expectedRestY) <= 6.0f,
                "Circle stability regression drifted too far from the expected resting height.");

        destroyObjects(gameManager, objects);
    }

    void testSleepingOnSupport()
    {
        GameManager &gameManager = GameManager::getInstance();

        std::vector<GameObject *> objects;
        GameObject *floor = createStaticBox(gameManager, "Sleep Floor", 320.0f, 340.0f, 420.0f, 40.0f);
        GameObject *box = createDynamicBox(gameManager, "Sleep Box", 320.0f, 180.0f, 40.0f, 40.0f);
        objects.push_back(floor);
        objects.push_back(box);

        simulateFrames(gameManager, 720);

        Rigidbody *rigidbody = box->getComponent<Rigidbody>();
        require(rigidbody != nullptr, "Sleeping regression lost the box rigidbody.");
        require(rigidbody->hasSupportContact(), "Sleeping regression ended without a support contact.");
        require(rigidbody->getIsSleeping(), "Supported dynamic body failed to enter sleep state.");

        destroyObjects(gameManager, objects);
    }

    void testBroadPhasePairTracking()
    {
        GameManager &gameManager = GameManager::getInstance();
        PhysicsManager *physicsManager = gameManager.getPhysicsManager();
        require(physicsManager != nullptr, "PhysicsManager must be available for pair tracking regression.");

        std::vector<GameObject *> objects;
        GameObject *boxA = createStaticBox(gameManager, "Pair A", 120.0f, 120.0f, 40.0f, 40.0f);
        GameObject *boxB = createStaticBox(gameManager, "Pair B", 320.0f, 120.0f, 40.0f, 40.0f);
        objects.push_back(boxA);
        objects.push_back(boxB);

        physicsManager->checkForCollisions();
        require(physicsManager->getGrid().getCandidatePairCount() == 0,
                "Broad-phase regression expected zero candidate pairs before overlap.");

        boxB->setPosition(Eigen::Vector2f(135.0f, 120.0f));
        physicsManager->checkForCollisions();
        require(physicsManager->getGrid().getCandidatePairCount() == 1,
                "Broad-phase regression expected one candidate pair after overlap.");

        boxB->setPosition(Eigen::Vector2f(360.0f, 120.0f));
        physicsManager->checkForCollisions();
        require(physicsManager->getGrid().getCandidatePairCount() == 0,
                "Broad-phase regression expected pair removal after separation.");

        destroyObjects(gameManager, objects);
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

    static const TestCase testCases[] = {
        {"circle_collision_stability", testCircleCollisionStability},
        {"sleeping_on_support", testSleepingOnSupport},
        {"broad_phase_pair_tracking", testBroadPhasePairTracking},
    };

    GameManager::getInstance();

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