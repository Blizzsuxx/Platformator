#include "physics/physicsmanager.h"
#include "benchmark/workprobe.h"
#include "core/membership.h"
#include <cassert>

namespace
{
bool isLiveContact(const ColliderPair *pair) noexcept
{
    return pair->getCollision() != nullptr && !pair->getObjectA()->isRetired() && !pair->getObjectB()->isRetired() &&
           pair->getObjectA()->getGameObject()->getActive() && pair->getObjectB()->getGameObject()->getActive();
}
} // namespace

void PhysicsManager::queueSupportDelta(Rigidbody *body, int delta)
{
    if (!delta)
        return;
    if (body->supportDeltaIndex == SIZE_MAX)
    {
        body->supportDeltaIndex = supportDeltaBodies.size();
        supportDeltaBodies.push_back(body);
    }
    body->supportDelta += delta;
}

void PhysicsManager::reconcilePairSupport(ColliderPair *pair)
{
    auto *collision = pair->getCollision();
    auto *a = pair->getObjectA()->getAttachedRigidbody();
    auto *b = pair->getObjectB()->getAttachedRigidbody();
    const auto [supportsA, supportsB] = pairSupportFlags(pair, collision);
    reconcileSupportContribution(pair->contributedSupportBodyA, supportsA ? a : nullptr);
    reconcileSupportContribution(pair->contributedSupportBodyB, supportsB ? b : nullptr);
}

void PhysicsManager::reconcileSupportContribution(Rigidbody *&contributor, Rigidbody *desired)
{
    if (contributor == desired)
        return;
    // The old component survives until the queued credit transfer is flushed, even after replacement.
    if (contributor)
        queueSupportDelta(contributor, -1);
    if (desired)
        queueSupportDelta(desired, 1);
    contributor = desired;
}

void PhysicsManager::flushSupportDeltas()
{
    // Commit the net change once: losing and gaining support in this batch must not wake at an interim zero.
    for (auto *body : supportDeltaBodies)
    {
        const auto next = static_cast<int64_t>(body->supportContactCount) + body->supportDelta;
        assert(next >= 0);
        const bool lostLast = body->supportContactCount && next == 0;
        const bool changedSupport = (body->supportContactCount == 0) != (next == 0);
        body->supportContactCount = static_cast<size_t>(next);
        body->supportDelta = 0;
        body->supportDeltaIndex = SIZE_MAX;
        if (lostLast && !body->isRetired())
            body->wakeUp();
        if (changedSupport)
            queueBodyParticipationChange(body);
    }
    supportDeltaBodies.clear();
    reconcileChangedBodies();
}

void PhysicsManager::refreshContactParticipation(ColliderPair *pair)
{
    const bool live = isLiveContact(pair);
    membership(contactPairs, pair, &ColliderPair::contactIndex, live);
    refreshSolverParticipation(pair, live);
}

void PhysicsManager::refreshSolverParticipation(ColliderPair *pair, bool live)
{
    const auto response = live ? pair->getCollision()->getResolutionType() : Collision::NONE_COLLISIONS;
    auto *rbA = live ? pair->getObjectA()->getAttachedRigidbody() : nullptr;
    auto *rbB = live ? pair->getObjectB()->getAttachedRigidbody() : nullptr;
    const bool solve = live && (response == Collision::DYNAMIC || response == Collision::KINEMATIC) &&
                       ((rbA != nullptr && rbA->isMovingBody()) || (rbB != nullptr && rbB->isMovingBody()));

    updatePersistentIslandContact(pair, solve, response, rbA, rbB);
}

void PhysicsManager::refreshSolverPairs(Collider *collider)
{
    if (auto *pairs = grid.getTouchingPairs(collider))
    {
        for (ColliderPair *pair : *pairs)
        {
            PLATFORMATOR_WORK_ADD(solverRefreshPairs, 1);
            refreshSolverParticipation(pair, isLiveContact(pair));
        }
    }
}

void PhysicsManager::refreshResponse(Collider *collider)
{
    if (auto *pairs = grid.getTouchingPairs(collider))
    {
        for (ColliderPair *pair : *pairs)
        {
            if (auto *collision = pair->getCollision())
            {
                collision->updateSupportState(gravityVectorNormalized);
            }
            reconcilePairSupport(pair);
            refreshContactParticipation(pair);
        }
    }
}

void PhysicsManager::removePair(ColliderPair *pair)
{
    // This is the explicit end boundary for contacts lost with their final cell witness.
    if (pair->getCollision())
        addPendingPhysicsEvent(makePhysicsEvent(PhysicsEvent::COLLISION_EXIT, pair, nullptr));
    reconcileSupportContribution(pair->contributedSupportBodyA, nullptr);
    reconcileSupportContribution(pair->contributedSupportBodyB, nullptr);
    membership(contactPairs, pair, &ColliderPair::contactIndex, false);
    updatePersistentIslandContact(pair, false, Collision::NONE_COLLISIONS, nullptr, nullptr);
}
