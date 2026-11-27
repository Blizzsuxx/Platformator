#include "physics/physicsmanager.h"

#include "benchmark/workprobe.h"
#include "core/membership.h"

void PhysicsManager::queueBodyParticipationChange(Rigidbody *body)
{
    if (body->participationQueueIndex != SIZE_MAX)
    {
        return;
    }

    body->participationQueueIndex = pendingBodyChanges.size();
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
        body->participationQueueIndex = SIZE_MAX;
        reconcileBodyParticipation(body);
    }
    pendingBodyChanges.clear();
}
