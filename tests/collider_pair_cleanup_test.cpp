#include <SDL3/SDL.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "boxcollider.h"
#include "circlecollider.h"
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

    void configureHeadlessEnvironment()
    {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        setenv("SDL_AUDIODRIVER", "dummy", 1);
        setenv("SDL_RENDER_DRIVER", "software", 1);
    }

    bool containsGameObjectPointer(Runtime &runtime, const GameObject *gameObject)
    {
        return gameObject != nullptr && runtime.getObjectById(gameObject->getId()) == gameObject;
    }

    class TestBehavior : public Behavior
    {
    public:
        std::string getTypeName() const override
        {
            return "ColliderPairCleanupTestBehavior";
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

    class DriftBehavior : public TestBehavior
    {
    public:
        explicit DriftBehavior(const Eigen::Vector2f &delta) : delta(delta)
        {
        }

        void update(double) override
        {
            GameObject *gameObject = getGameObject();
            gameObject->setPosition(gameObject->getPosition() + delta);
        }

    private:
        Eigen::Vector2f delta;
    };

    class CollisionCounterBehavior : public TestBehavior
    {
    public:
        CollisionCounterBehavior() : enterCount(0), exitCount(0)
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

        size_t enterCount;
        size_t exitCount;
    };

    GameObject *createStaticBox(Runtime &runtime, const std::string &name, float x, float y, float width, float height)
    {
        return runtime
            .createGameObject()
            ->setName(name)
            ->addComponent<Rigidbody>(STATIC, false)
            ->addComponent<BoxCollider>(width, height)
            ->setPosition(Eigen::Vector2f(x, y));
    }

    GameObject *createDynamicCircle(Runtime &runtime, const std::string &name, float x, float y, float radius)
    {
        return runtime
            .createGameObject()
            ->setName(name)
            ->addComponent<Rigidbody>(DYNAMIC, false)
            ->addComponent<CircleCollider>(radius)
            ->setPosition(Eigen::Vector2f(x, y));
    }
}

int main()
{
    configureHeadlessEnvironment();

    try
    {
        Runtime runtime;

        GameObject *survivor = createDynamicCircle(runtime, "Deletion Sync Survivor", 0.0f, 0.0f, 20.0f)->addComponent<ScriptComponent>();
        GameObject *victim = createStaticBox(runtime, "Deletion Sync Victim", 0.0f, 0.0f, 40.0f, 40.0f)->addComponent<ScriptComponent>();

        ScriptComponent *survivorScriptComponent = survivor->getComponent<ScriptComponent>();
        ScriptComponent *victimScriptComponent = victim->getComponent<ScriptComponent>();
        require(survivorScriptComponent != nullptr && victimScriptComponent != nullptr,
            "Collider pair cleanup regression failed to create the script components.");

        auto *survivorCounter = new CollisionCounterBehavior();
        survivorScriptComponent->addBehavior(survivorCounter);
        survivorScriptComponent->addBehavior(new DriftBehavior(Eigen::Vector2f(1.0f, 0.0f)));
        victimScriptComponent->addBehavior(new DestroyOnUpdateBehavior());

        runtime.simulateFrame(kTimeStep);
        require(survivorCounter->enterCount == 1,
            "Collider pair cleanup regression expected an initial collision enter before the victim was destroyed.");
        require(!containsGameObjectPointer(runtime, victim),
            "Collider pair cleanup regression expected the victim to be removed from the GameManager immediately.");

        for (int frameIndex = 0; frameIndex < 8; ++frameIndex)
        {
            runtime.simulateFrame(kTimeStep);
        }

        require(survivorCounter->exitCount <= 1,
            "Collider pair cleanup regression observed duplicate collision exits after the victim was destroyed.");

        std::cout << "[PASS] collider_pair_cleanup_test\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "[FAIL] collider_pair_cleanup_test: " << exception.what() << '\n';
        return 1;
    }
}