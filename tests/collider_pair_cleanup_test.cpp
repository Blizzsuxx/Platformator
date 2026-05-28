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
#include "physicsmanager.h"
#include "rigidbody.h"
#include "runtimeaccess.h"
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

    bool containsObjectId(Runtime &runtime, int objectId)
    {
        return runtime.getObjectById(objectId) != nullptr;
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

    void runDestroyedVictimCleanupScenario()
    {
        Runtime runtime;

        GameObject *survivor = createDynamicCircle(runtime, "Deletion Sync Survivor", 0.0f, 0.0f, 20.0f)->addComponent<ScriptComponent>();
        GameObject *victim = createStaticBox(runtime, "Deletion Sync Victim", 0.0f, 0.0f, 40.0f, 40.0f)->addComponent<ScriptComponent>();
        const int victimId = victim->getId();

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
        require(!containsObjectId(runtime, victimId),
            "Collider pair cleanup regression expected the victim to be removed from the GameManager immediately.");

        for (int frameIndex = 0; frameIndex < 8; ++frameIndex)
        {
            runtime.simulateFrame(kTimeStep);
        }

        require(survivorCounter->exitCount <= 1,
            "Collider pair cleanup regression observed duplicate collision exits after the victim was destroyed.");
    }

    void runNonPrimaryRemovalCoverageScenario()
    {
        Runtime runtime;

        GameObject *covering = createStaticBox(runtime, "Coverage Long", 20.0f, 100.0f, 8.0f, 180.0f);
        GameObject *victim = createStaticBox(runtime, "Coverage Victim", 20.0f, 100.0f, 8.0f, 8.0f)->addComponent<ScriptComponent>();
        const int coveringId = covering->getId();
        const int victimId = victim->getId();
        BoxCollider *coveringCollider = covering->getComponent<BoxCollider>();
        BoxCollider *victimCollider = victim->getComponent<BoxCollider>();
        ScriptComponent *victimScriptComponent = victim->getComponent<ScriptComponent>();
        require(coveringCollider != nullptr && victimCollider != nullptr && victimScriptComponent != nullptr,
            "Coverage removal scenario failed to create the required collider state.");

        for (int index = 0; index < 31; ++index)
        {
            const float x = 110.0f + static_cast<float>(index % 3) * 8.0f;
            const float y = 8.0f + static_cast<float>(index) * 5.5f;
            createStaticBox(runtime, "Coverage Filler " + std::to_string(index), x, y, 4.0f, 4.0f);
        }

        runtime.simulateFrame(kTimeStep);
        require(platformator_detail::RuntimeAccess::physicsManager()->getGrid().containsCellPair(coveringCollider, victimCollider),
            "Coverage removal scenario expected an initial same-cell full-overlap pair before deletion.");

        victimScriptComponent->addBehavior(new DestroyOnUpdateBehavior());
        runtime.simulateFrame(kTimeStep);

        require(!containsObjectId(runtime, victimId),
            "Coverage removal scenario expected the victim to be removed from the GameManager immediately.");
        require(containsObjectId(runtime, coveringId),
            "Coverage removal scenario unexpectedly removed the covering collider.");
        require(!platformator_detail::RuntimeAccess::physicsManager()->getGrid().containsCellPair(coveringCollider, victimCollider),
            "Coverage removal scenario left a stale same-cell pair witness for the removed victim.");
    }
}

int main()
{
    configureHeadlessEnvironment();

    try
    {
        runDestroyedVictimCleanupScenario();
        runNonPrimaryRemovalCoverageScenario();

        std::cout << "[PASS] collider_pair_cleanup_test\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "[FAIL] collider_pair_cleanup_test: " << exception.what() << '\n';
        return 1;
    }
}