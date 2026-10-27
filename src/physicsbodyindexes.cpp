#include "physicsmanager.h"

#include "physicsmembership.h"

void PhysicsManager::queueBodyParticipationChange(Rigidbody *body)
{
    if (body->participationQueueIndex != SIZE_MAX)
    {
        return;
    }

    body->participationQueueIndex = pendingBodyChanges.size();
    pendingBodyChanges.push_back(body);
}

void PhysicsManager::cancelBodyParticipationChange(Rigidbody *body)
{
    if (body->participationQueueIndex == SIZE_MAX)
    {
        return;
    }

    const auto index = body->participationQueueIndex;
    auto *moved = pendingBodyChanges.back();

    pendingBodyChanges[index] = moved;
    moved->participationQueueIndex = index;
    pendingBodyChanges.pop_back();
    body->participationQueueIndex = SIZE_MAX;
}

void PhysicsManager::reconcileBodyParticipation(Rigidbody *body)
{
    // Derive memberships from current physical state after queued changes coalesce.
    const bool live =
        body->getIsRegisteredInPhysicsManager() && !body->isRetired() && body->getGameObject()->getActive();

    const bool awake = live && body->getBodyType() == DYNAMIC && !body->getIsSleeping();
    membership(awakeDynamicBodies, body, &Rigidbody::awakeDynamicIndex, awake);
    membership(movingKinematicBodies, body, &Rigidbody::movingKinematicIndex,
               live && body->getBodyType() == KINEMATIC &&
                   (!body->getVelocity().isZero(0) || body->getAngularVelocity() != 0));

    const bool previouslyEligible = body->sleepCandidateIndex != SIZE_MAX;
    membership(sleepCandidates, body, &Rigidbody::sleepCandidateIndex,
               awake && (body->hasSupportContact() || !body->getGravity()));

    if (!previouslyEligible && body->sleepCandidateIndex != SIZE_MAX)
    {
        // Impact waking can admit this candidate after the integration reset.
        resetSleepIsland(body);
    }

    if (auto *collider = body->getGameObject()->getComponent<Collider>())
    {
        refreshSolverPairs(collider);
    }
}

Rigidbody *PhysicsManager::findSleepRoot(Rigidbody *body)
{
    auto *root = body;
    while (root->sleepParent != root)
    {
        root = root->sleepParent;
    }

    // Compress the traversed path for subsequent lookups in this step.
    while (body->sleepParent != body)
    {
        auto *next = body->sleepParent;
        body->sleepParent = root;
        body = next;
    }
    return root;
}

void PhysicsManager::mergeSleepIslands(Rigidbody *a, Rigidbody *b)
{
    if (a->sleepCandidateIndex == SIZE_MAX || b->sleepCandidateIndex == SIZE_MAX)
    {
        return;
    }

    a = findSleepRoot(a);
    b = findSleepRoot(b);

    if (a == b)
    {
        return;
    }

    if (a->sleepRank < b->sleepRank)
    {
        std::swap(a, b);
    }

    b->sleepParent = a;
    if (a->sleepRank == b->sleepRank)
    {
        ++a->sleepRank;
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
