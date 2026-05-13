#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "boxcollider.h"
#include "constants.h"
#include "platformator/runtime.h"
#include "rigidbody.h"
#include "scriptcomponent.h"

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

    bool containsGameObjectPointer(Runtime &gameManager, const GameObject *gameObject)
    {
        return gameObject != nullptr && gameManager.getObjectById(gameObject->getId()) == gameObject;
    }

    void configureHeadlessEnvironment()
    {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        setenv("SDL_AUDIODRIVER", "dummy", 1);
        setenv("SDL_RENDER_DRIVER", "software", 1);
    }

    class SceneScope
    {
    public:
        explicit SceneScope(Runtime &gameManager) : gameManager(gameManager), objects()
        {
        }

        ~SceneScope()
        {
            for (GameObject *object : objects)
            {
                if (object != nullptr && containsGameObjectPointer(gameManager, object))
                {
                    gameManager.destroyGameObject(object);
                }
            }

            gameManager.simulateFrame(kTimeStep);
        }

        void add(GameObject *object)
        {
            objects.push_back(object);
        }

    private:
        Runtime &gameManager;
        std::vector<GameObject *> objects;
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

    class TestBehavior : public Behavior
    {
    public:
        std::string getTypeName() const override
        {
            return "TestBehavior";
        }

        void serialize(nlohmann::json &) const override
        {
        }

        void deserialize(const nlohmann::json &) override
        {
        }

        void resolveReferences() override
        {
        }
    };

    class CreateThenDestroyBehavior : public TestBehavior
    {
    public:
        CreateThenDestroyBehavior() : ran(false)
        {
        }

        void update(double) override
        {
            if (ran)
            {
                return;
            }

            ran = true;

            Runtime &gameManager = getRuntime();
            GameObject *temporaryObject = gameManager
                                              .createGameObject()
                                              ->setName("Queued Add Victim")
                                              ->addComponent<BoxCollider>(20.0f, 20.0f);

            gameManager.destroyGameObject(temporaryObject);
        }

    private:
        bool ran;
    };

    class MoveThenDestroyBehavior : public TestBehavior
    {
    public:
        MoveThenDestroyBehavior() : ran(false)
        {
        }

        void update(double) override
        {
            if (ran)
            {
                return;
            }

            ran = true;

            GameObject *gameObject = getGameObject();
            gameObject->setPosition(gameObject->getPosition() + Eigen::Vector2f(15.0f, 0.0f));
            getRuntime().destroyGameObject(gameObject);
        }

    private:
        bool ran;
    };

    class DestroyOnUpdateBehavior : public TestBehavior
    {
    public:
        DestroyOnUpdateBehavior() : ran(false)
        {
        }

        void update(double) override
        {
            if (ran)
            {
                return;
            }

            ran = true;
            getRuntime().destroyGameObject(getGameObject());
        }

    private:
        bool ran;
    };

    class DestroyViaGameObjectBehavior : public TestBehavior
    {
    public:
        DestroyViaGameObjectBehavior() : ran(false)
        {
        }

        void update(double) override
        {
            if (ran)
            {
                return;
            }

            ran = true;
            getGameObject()->destroy();
        }

    private:
        bool ran;
    };

    class DestroyTargetsBehavior : public TestBehavior
    {
    public:
        explicit DestroyTargetsBehavior(const std::vector<GameObject *> &targets) : ran(false), targets(targets)
        {
        }

        void update(double) override
        {
            if (ran)
            {
                return;
            }

            ran = true;

            Runtime &gameManager = getRuntime();
            for (GameObject *target : targets)
            {
                if (containsGameObjectPointer(gameManager, target))
                {
                    gameManager.destroyGameObject(target);
                }
            }
        }

    private:
        bool ran;
        std::vector<GameObject *> targets;
    };

    class CollisionCounterBehavior : public TestBehavior
    {
    public:
        CollisionCounterBehavior() : enterCount(0), exitCount(0), stayCount(0)
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
        size_t exitCount;
        size_t stayCount;
    };

    void testQueuedAddDeletion()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *driver = gameManager.createGameObject()->setName("Queued Add Driver")->addComponent<ScriptComponent>();
        sceneScope.add(driver);

        ScriptComponent *scriptComponent = driver->getComponent<ScriptComponent>();
        require(scriptComponent != nullptr, "Queued add deletion regression failed to create the driver script component.");
        scriptComponent->addBehavior(new CreateThenDestroyBehavior());

        gameManager.simulateFrame(kTimeStep);
        require(gameManager.getGameObject("Queued Add Victim") == nullptr,
                "Queued add deletion regression expected the temporary object to be removed from the GameManager immediately.");

        gameManager.simulateFrame(kTimeStep);
    }

    void testQueuedSyncDeletion()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *victim = createStaticBox(gameManager, "Queued Sync Victim", 120.0f, 120.0f, 40.0f, 40.0f)->addComponent<ScriptComponent>();
        sceneScope.add(victim);

        ScriptComponent *scriptComponent = victim->getComponent<ScriptComponent>();
        require(scriptComponent != nullptr, "Queued sync deletion regression failed to create the victim script component.");
        scriptComponent->addBehavior(new MoveThenDestroyBehavior());

        gameManager.simulateFrame(kTimeStep);
        require(gameManager.getGameObject("Queued Sync Victim") == nullptr,
                "Queued sync deletion regression expected the moved object to be removed from the GameManager immediately.");

        gameManager.simulateFrame(kTimeStep);
    }

    void testLateFrameExitEventDeletion()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *survivor = createStaticBox(gameManager, "Exit Survivor", 200.0f, 120.0f, 40.0f, 40.0f)->addComponent<ScriptComponent>();
        GameObject *victim = createStaticBox(gameManager, "Exit Victim", 210.0f, 120.0f, 40.0f, 40.0f)->addComponent<ScriptComponent>();
        sceneScope.add(survivor);
        sceneScope.add(victim);

        ScriptComponent *survivorScriptComponent = survivor->getComponent<ScriptComponent>();
        ScriptComponent *victimScriptComponent = victim->getComponent<ScriptComponent>();
        require(survivorScriptComponent != nullptr && victimScriptComponent != nullptr,
                "Late-frame exit regression failed to create the script components.");

        auto *survivorCounter = new CollisionCounterBehavior();
        survivorScriptComponent->addBehavior(survivorCounter);
        victimScriptComponent->addBehavior(new DestroyOnUpdateBehavior());

        gameManager.simulateFrame(kTimeStep);
        require(survivorCounter->enterCount == 1,
                "Late-frame exit regression expected an initial collision enter event before the victim was destroyed.");
        require(survivorCounter->exitCount == 0,
                "Late-frame exit regression expected destroy-driven exit delivery to be deferred until the following frame.");
        require(gameManager.getGameObject("Exit Victim") == nullptr,
                "Late-frame exit regression expected the destroyed victim to be removed from the GameManager immediately.");

        gameManager.simulateFrame(kTimeStep);
        require(survivorCounter->exitCount == 1,
                "Late-frame exit regression unexpectedly dispatched a duplicate collision exit on the following frame.");
    }

    void testGameObjectDestroyShortcut()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *victim = gameManager.createGameObject()->setName("Destroy Shortcut Victim")->addComponent<ScriptComponent>();
        sceneScope.add(victim);

        ScriptComponent *scriptComponent = victim->getComponent<ScriptComponent>();
        require(scriptComponent != nullptr, "Destroy shortcut regression failed to create the victim script component.");
        scriptComponent->addBehavior(new DestroyViaGameObjectBehavior());

        gameManager.simulateFrame(kTimeStep);
        require(!containsGameObjectPointer(gameManager, victim),
                "Destroy shortcut regression expected GameObject::destroy() to remove the object from the GameManager live object list.");

        gameManager.simulateFrame(kTimeStep);
    }

    void testSharedAdjacencyDeletion()
    {
        Runtime &gameManager = Runtime::current();

        SceneScope sceneScope(gameManager);
        GameObject *anchor = createStaticBox(gameManager, "Adjacency Anchor", 200.0f, 120.0f, 180.0f, 80.0f)->addComponent<ScriptComponent>();
        sceneScope.add(anchor);

        ScriptComponent *anchorScriptComponent = anchor->getComponent<ScriptComponent>();
        require(anchorScriptComponent != nullptr,
                "Shared adjacency deletion regression failed to create the anchor script component.");

        auto *anchorCounter = new CollisionCounterBehavior();
        anchorScriptComponent->addBehavior(anchorCounter);

        std::vector<GameObject *> victims;
        for (size_t victimIndex = 0; victimIndex < 4; ++victimIndex)
        {
            float victimX = 140.0f + static_cast<float>(victimIndex) * 40.0f;
            GameObject *victim = createStaticBox(
                gameManager,
                "Adjacency Victim " + std::to_string(victimIndex),
                victimX,
                120.0f,
                20.0f,
                20.0f);
            sceneScope.add(victim);
            victims.push_back(victim);
        }

        GameObject *driver = gameManager.createGameObject()->setName("Adjacency Destroy Driver")->addComponent<ScriptComponent>();
        sceneScope.add(driver);

        ScriptComponent *driverScriptComponent = driver->getComponent<ScriptComponent>();
        require(driverScriptComponent != nullptr,
                "Shared adjacency deletion regression failed to create the destroy driver script component.");
        driverScriptComponent->addBehavior(new DestroyTargetsBehavior(victims));

        gameManager.simulateFrame(kTimeStep);
        require(anchorCounter->enterCount == victims.size(),
                "Shared adjacency deletion regression expected one enter event per overlapping victim before destruction.");
        for (GameObject *victim : victims)
        {
            require(!containsGameObjectPointer(gameManager, victim),
                    "Shared adjacency deletion regression expected every victim to be removed during late-frame deletion.");
        }

        gameManager.simulateFrame(kTimeStep);
        require(anchorCounter->exitCount == victims.size(),
                "Shared adjacency deletion regression expected one exit event per destroyed victim on the following frame.");
    }
} // namespace

int main()
{
    configureHeadlessEnvironment();
    platformator::Runtime runtime;

    try
    {
        testQueuedAddDeletion();
        testQueuedSyncDeletion();
        testLateFrameExitEventDeletion();
        testGameObjectDestroyShortcut();
        testSharedAdjacencyDeletion();
        std::cout << "[PASS] queued_deletion_safety_test\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "[FAIL] queued_deletion_safety_test: " << exception.what() << '\n';
        return 1;
    }
}