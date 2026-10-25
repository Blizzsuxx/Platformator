#include "physicsmanager.h"

namespace
{
void membership(std::vector<Rigidbody *> &list, Rigidbody *body, size_t Rigidbody::*index, bool desired)
{
    auto &position = body->*index;
    if (desired == (position != SIZE_MAX))
        return;
    if (desired)
    {
        position = list.size();
        list.push_back(body);
    }
    else
    {
        auto *moved = list.back();
        list[position] = moved;
        moved->*index = position;
        list.pop_back();
        position = SIZE_MAX;
    }
}
} // namespace

void PhysicsManager::queueBodyChange(Rigidbody *body)
{
    if (body->bodyQueueIndex != SIZE_MAX)
        return;
    body->bodyQueueIndex = changedBodies.size();
    changedBodies.push_back(body);
}

void PhysicsManager::cancelBodyChange(Rigidbody *body)
{
    if (body->bodyQueueIndex == SIZE_MAX)
        return;
    const auto index = body->bodyQueueIndex;
    auto *moved = changedBodies.back();
    changedBodies[index] = moved;
    moved->bodyQueueIndex = index;
    changedBodies.pop_back();
    body->bodyQueueIndex = SIZE_MAX;
}

void PhysicsManager::reconcileBody(Rigidbody *body)
{
    const bool live =
        body->getIsRegisteredInPhysicsManager() && !body->isRetired() && body->getGameObject()->getActive();
    const bool awake = live && body->getBodyType() == DYNAMIC && !body->getIsSleeping();
    membership(awakeBodies, body, &Rigidbody::awakeIndex, awake);
    membership(kinematicBodies, body, &Rigidbody::kinematicIndex,
               live && body->getBodyType() == KINEMATIC &&
                   (!body->getVelocity().isZero(0) || body->getAngularVelocity() != 0));
    const bool previouslyEligible = body->sleepIndex != SIZE_MAX;
    membership(sleepEligibleBodies, body, &Rigidbody::sleepIndex,
               awake && (body->hasSupportContact() || !body->getGravity()));
    if (!previouslyEligible && body->sleepIndex != SIZE_MAX)
    {
        body->sleepParent = body;
        body->sleepRank = 0;
        body->islandQuiet = true;
        body->islandTimer = body->getSleepTimer();
    }
    if (auto *collider = body->getGameObject()->getComponent<Collider>())
        refreshSolverPairs(collider);
}

Rigidbody *PhysicsManager::sleepRoot(Rigidbody *body)
{
    auto *root = body;
    while (root->sleepParent != root)
        root = root->sleepParent;
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
    if (a->sleepIndex == SIZE_MAX || b->sleepIndex == SIZE_MAX)
        return;
    a = sleepRoot(a);
    b = sleepRoot(b);
    if (a == b)
        return;
    if (a->sleepRank < b->sleepRank)
        std::swap(a, b);
    b->sleepParent = a;
    if (a->sleepRank == b->sleepRank)
        ++a->sleepRank;
}

void PhysicsManager::drainBodyChanges()
{
    for (auto *body : changedBodies)
    {
        body->bodyQueueIndex = SIZE_MAX;
        reconcileBody(body);
    }
    changedBodies.clear();
}
