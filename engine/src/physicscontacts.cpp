#include "physicsmanager.h"
#include "workprobe.h"
#include "physicsmembership.h"
#include <cassert>

namespace
{
    bool movingBody(const Rigidbody *body)
    {
        return body && body->getBodyType() != STATIC && !body->getIsSleeping();
    }
    bool isLiveContact(const ColliderPair *pair)
    {
        return pair->getCollision() && !pair->getObjectA()->isRetired() && !pair->getObjectB()->isRetired() &&
               pair->getObjectA()->getGameObject()->getActive() &&
               pair->getObjectB()->getGameObject()->getActive();
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

void PhysicsManager::reconcilePairSupport(const ColliderPair *pair)
{
    auto *collision = pair->getCollision();
    auto *a = pair->getObjectA()->getGameObject()->getComponent<Rigidbody>();
    auto *b = pair->getObjectB()->getGameObject()->getComponent<Rigidbody>();
    const bool referenceIsA = collision && collision->getReferenceObject() == pair->getObjectA();
    const bool supportsA = collision && (collision->supportState &
                                         (referenceIsA ? Collision::SUPPORTS_REFERENCE : Collision::SUPPORTS_INCIDENT));
    const bool supportsB = collision && (collision->supportState &
                                         (referenceIsA ? Collision::SUPPORTS_INCIDENT : Collision::SUPPORTS_REFERENCE));
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

void PhysicsManager::refreshContactParticipation(const ColliderPair *pair)
{
    const bool live = isLiveContact(pair);
    membership(contactPairs, pair, &ColliderPair::contactIndex, live);
    refreshSolverParticipation(pair, live);
}

void PhysicsManager::refreshSolverParticipation(const ColliderPair *pair, bool live)
{
    const auto response = live ? pair->getCollision()->getResolutionType() : Collision::NONE_COLLISIONS;
    const bool solve = live &&
                       (response == Collision::DYNAMIC || response == Collision::KINEMATIC) &&
                       (movingBody(pair->getObjectA()->getGameObject()->getComponent<Rigidbody>()) ||
                        movingBody(pair->getObjectB()->getGameObject()->getComponent<Rigidbody>()));

    membership(solverPairs, pair, &ColliderPair::solverIndex, solve);
}

void PhysicsManager::refreshSolverPairs(Collider *collider)
{
    if (const auto *pairs = grid.getTouchingPairs(collider))
    {
        for (auto *pair : *pairs)
        {
            PLATFORMATOR_WORK_ADD(solverRefreshPairs, 1);
            refreshSolverParticipation(pair, isLiveContact(pair));
        }
    }
}

void PhysicsManager::refreshResponse(Collider *collider)
{
    if (const auto *pairs = grid.getTouchingPairs(collider))
        for (auto *pair : *pairs)
        {
            if (auto *collision = pair->getCollision())
                collision->updateSupportState(gravityVectorNormalized);
            reconcilePairSupport(pair);
            refreshContactParticipation(pair);
        }
}

void PhysicsManager::removePair(const ColliderPair *pair)
{
    // This is the explicit end boundary for contacts lost with their final cell witness.
    if (pair->getCollision())
        addPendingPhysicsEvent(makePhysicsEvent(PhysicsEvent::COLLISION_EXIT, pair, nullptr));
    reconcileSupportContribution(pair->contributedSupportBodyA, nullptr);
    reconcileSupportContribution(pair->contributedSupportBodyB, nullptr);
    membership(contactPairs, pair, &ColliderPair::contactIndex, false);
    membership(solverPairs, pair, &ColliderPair::solverIndex, false);
}
