#include "physicsmanager.h"
#include "gamemanager.h"
#include "runtimeaccess.h"
#include "test_support.h"
#include "spatial_test_support.h"

#include <algorithm>
#include <iostream>
#include <platformator/boxcollider.h>
#include <platformator/rigidbody.h>
#include <platformator/scriptcomponent.h>

using namespace test_support;
using namespace spatial_test_support;

namespace
{
struct PairEvents : test_support::Behavior
{
    int enters = 0;
    int exits = 0;
    bool exitEndpointsReadable = true;

    void onCollisionEnter(const Collision *, Collider *, double) override
    {
        ++enters;
    }

    void onCollisionExit(Collider *other, double) override
    {
        ++exits;
        exitEndpointsReadable &= other != nullptr && other->getGameObject() != nullptr;
    }
};

void verifyPendingQueue(const PhysicsManager &physics)
{
    const auto &pending = *physics.getGrid().getPendingNarrowPhasePairs();
    std::vector<const ColliderPair *> unique(pending.begin(), pending.end());
    std::sort(unique.begin(), unique.end());
    require(std::adjacent_find(unique.begin(), unique.end()) == unique.end(),
            "Narrow-phase queue must contain each pair at most once");
    for (size_t i = 0; i < pending.size(); ++i)
    {
        require(pending[i] != nullptr && pending[i]->getIsQueuedForNarrowPhase() &&
                    pending[i]->getNarrowPhaseQueueIndex() == i,
                "Swap removal must repair every pending pair index");
    }
}

void retainedWitnessesAndSupport()
{
    RuntimeFixture fixture(6);
    auto &runtime = fixture.getRuntime();
    auto *physics = platformator_detail::RuntimeAccess::physicsManager();

    auto *surface = runtime.createGameObject()
                        ->addComponent<BoxCollider>(2048, 20)
                        ->addComponent<Rigidbody>(DYNAMIC, false)
                        ->addComponent<ScriptComponent>();
    auto *body = surface->getComponent<Rigidbody>();
    auto *events = new PairEvents;
    surface->getComponent<ScriptComponent>()->addBehavior(events);
    std::vector<GameObject *> floors;
    for (int i = 0; i < 128; ++i)
    {
        floors.push_back(runtime.createGameObject()
                             ->setPosition({-1016.f + i * 16.f, 19})
                             ->addComponent<BoxCollider>(15, 20)
                             ->addComponent<Rigidbody>(STATIC, false));
    }

    const auto freeze = [&](double) {
        surface->setPosition({0, 0})->setRotation(0);
        body->setVelocity({0, 0})->setAngularVelocity(0);
    };
    runtime.simulateFrameWithCustomCallback(dt, freeze);
    require(body->getSupportContactCount() == 128 && physics->getContactPairs().size() == 128,
            "Multi-cell witnesses reduce to one supported physics pair per floor");
    require(physics->getGrid().getCandidatePairCount() == 128,
            "Duplicate cell witnesses cannot manufacture duplicate canonical pairs");
    require(events->enters == 128, "Each canonical contact dispatches exactly one Enter pair event");

    auto *firstFloor = floors.front()->getComponent<BoxCollider>();
    firstFloor->setCollisionMask(0);
    runtime.simulateFrameWithCustomCallback(dt, freeze);
    require(body->getSupportContactCount() == 127 && physics->getContactPairs().size() == 127,
            "Losing the last witness removes one contact and one support contribution");
    require(events->exits == 1 && events->exitEndpointsReadable,
            "Pair removal preserves both event endpoints until the removal callback is delivered");

    firstFloor->setCollisionMask(1);
    firstFloor->setIsTrigger(true);
    runtime.simulateFrameWithCustomCallback(dt, freeze);
    require(physics->getGrid().getCandidatePairCount() == 128 && body->getSupportContactCount() == 127,
            "A retained overlap returns as a candidate while trigger response contributes no support");
    firstFloor->setIsTrigger(false);
    runtime.simulateFrameWithCustomCallback(dt, freeze);
    require(body->getSupportContactCount() == 128,
            "Response-only refresh restores support without changing candidate membership");

    for (auto *floor : floors)
        floor->getComponent<BoxCollider>()->setCollisionMask(0);
    runtime.simulateFrameWithCustomCallback(dt, freeze);
    require(body->getSupportContactCount() == 0 && physics->getContactPairs().empty() && events->exits == 129,
            "Removing every final witness clears all contacts and support without stale adjacency (support=" +
                std::to_string(body->getSupportContactCount()) + ", contacts=" +
                std::to_string(physics->getContactPairs().size()) + ", exits=" + std::to_string(events->exits) +
                ")");
    require(events->exitEndpointsReadable, "Final removal callbacks retain valid endpoint objects");
}

void queueRepairAndStableBatchRetirement()
{
    RuntimeFixture fixture(6);
    auto &runtime = fixture.getRuntime();
    auto *physics = platformator_detail::RuntimeAccess::physicsManager();
    auto &manager = platformator_detail::RuntimeAccess::gameManager();

    for (int i = 0; i < 131; ++i)
    {
        auto *a = runtime.createGameObject()->setPosition({float(i * 40), 0})->addComponent<BoxCollider>(4, 4);
        a->getComponent<BoxCollider>()->setIsTrigger(true);
        runtime.createGameObject()->setPosition({float(i * 40 + 1), 0})->addComponent<BoxCollider>(4, 4);
    }
    manager.commitMutations();
    require(physics->getGrid().getPendingNarrowPhasePairs()->size() == 131,
            "Fixture publishes a large stable batch of distinct pairs");
    verifyPendingQueue(*physics);

    auto &pending = *physics->getGrid().getPendingNarrowPhasePairs();
    for (size_t index : {size_t(0), pending.size() / 2, pending.size() - 1})
        pending[index]->getObjectB()->setCollisionMask(0);
    manager.commitMutations();
    require(pending.size() == 128, "First, middle and last removals cancel pending work exactly once");
    verifyPendingQueue(*physics);

    physics->checkForCollisions();
    physics->handlePendingPhysicsEvents(dt);
    require(pending.empty() && physics->getContactPairs().size() == 128,
            "Parallel narrow-phase consumption leaves no queue references and confirms survivors");
    for (const auto *pair : physics->getContactPairs())
        require(!pair->getIsQueuedForNarrowPhase() && pair->getNarrowPhaseQueueIndex() == SIZE_MAX,
                "Consumed pair records retain no queue index");

    const auto *retiredPair = physics->getContactPairs().front();
    auto *survivor = retiredPair->getObjectA()->getGameObject();
    auto *exitEvents = new PairEvents;
    survivor->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(exitEvents);
    runtime.simulateFrame(dt); // Start the observer before exercising retirement.
    auto *retiredEndpoint = retiredPair->getObjectB()->getGameObject();
    retiredEndpoint->destroy();
    manager.commitMutations();
    require(physics->getContactPairs().size() == 127 && pending.empty(),
            "Callback-time retirement after the stable worker batch removes all pair references");
    require(exitEvents->exits == 1 && exitEvents->exitEndpointsReadable,
            "Final-pair removal retains valid event endpoints through callback delivery");
    require(physics->getPendingPhysicsEvents().empty(), "Delivered removal callbacks leave no stale event queue");
}

void identityAndInclusiveOracle()
{
    const auto handle = [](uint32_t slot, uint64_t generation) {
        return platformator::ObjectHandle{slot, generation, 77};
    };
    const EndpointState physicsA{{1, handle(0, 1)}, {0, 0, 2, 2}, 1, UINT64_MAX, true};
    const EndpointState physicsB{{1, handle(1, 1)}, {2, 0, 3, 2}, 1, 1, true};
    const EndpointState visibilityA{{2, handle(0, 1)}, {0, 0, 2, 2}, 1, UINT64_MAX, true};
    const EndpointState visibilityB{{2, handle(1, 1)}, {2, 0, 3, 2}, 1, 1, true};
    auto pairs = exactOverlapOracle({physicsA, physicsB, visibilityA, visibilityB});
    require(pairs.size() == 2 && pairs[0].first.context != pairs[1].first.context,
            "Equal endpoint handles in physics and visibility contexts remain distinct pairs");
    require(physicsA.bounds.overlapsInclusive(physicsB.bounds), "Touching bounds overlap inclusively");
}
} // namespace

int main()
{
    try
    {
        identityAndInclusiveOracle();
        retainedWitnessesAndSupport();
        queueRepairAndStableBatchRetirement();
        std::cout << "Spatial pair-state regressions passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
