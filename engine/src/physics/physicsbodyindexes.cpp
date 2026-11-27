#include "physics/physicsmanager.h"

#include "benchmark/workprobe.h"
#include "core/membership.h"
#include "core/vectorcapacity.h"

void PhysicsManager::reserveAttachmentWork(Rigidbody *old, Collider *collider)
{
    using platformator_detail::reserveForAppend;
    if (old && old->getIsRegisteredInPhysicsManager() && old->participationQueueIndex == Rigidbody::INVALID_INDEX)
        reserveForAppend(pendingBodyChanges, 1);
    if (collider && collider->getIsRegisteredInPhysicsManager() && collider->gridQueueIndex == Collider::INVALID_INDEX)
        reserveForAppend(grid.pendingColliderOperations, 1);
}

void PhysicsManager::queueBodyParticipationChange(Rigidbody *body)
{
    if (body->participationQueueIndex != Rigidbody::INVALID_INDEX)
    {
        return;
    }

    body->participationQueueIndex = static_cast<uint32_t>(pendingBodyChanges.size());
    pendingBodyChanges.push_back(body);
    PLATFORMATOR_WORK_ADD(bodyParticipationEnqueues, 1);
}

void PhysicsManager::cancelBodyParticipationChange(Rigidbody *body)
{
    membership(pendingBodyChanges, body, &Rigidbody::participationQueueIndex, false);
}

void PhysicsManager::reconcileBodyParticipation(Rigidbody *body)
{
    // Derive memberships from current physical state after queued changes coalesce.
    const bool live =
        body->getIsRegisteredInPhysicsManager() && !body->isRetired() && body->getGameObject()->getActive();

    const bool awake = live && body->isAwakeDynamic();
    membership(awakeDynamicBodies, body, &Rigidbody::awakeDynamicIndex, awake);
    membership(movingKinematicBodies, body, &Rigidbody::movingKinematicIndex, live && body->isMovingKinematic());
    membership(sleepCandidates, body, &Rigidbody::sleepCandidateIndex,
               awake && (body->hasSupportContact() || !body->getGravity()));

    if (auto *collider = body->getGameObject()->getComponent<Collider>())
    {
        refreshSolverPairs(collider);
    }

    if ((!live || body->getBodyType() != DYNAMIC) && body->solverIslandId != -1)
    {
        detachBodyFromPersistentIsland(body);
    }
}

void PhysicsManager::reconcileChangedBodies()
{
    // No integration or callback occurs here; participation and affected solver neighbors catch up.
    for (auto *body : pendingBodyChanges)
    {
        body->participationQueueIndex = Rigidbody::INVALID_INDEX;
        reconcileBodyParticipation(body);
    }
    pendingBodyChanges.clear();
}
