#include "physicsmanager.h"
#include "platformator/scriptcomponent.h"
#include <cassert>

namespace
{
bool canonicalLess(const ColliderPair *a, const ColliderPair *b)
{
    if (a->getObjectA()->getHandle() != b->getObjectA()->getHandle())
        return a->getObjectA()->getHandle() < b->getObjectA()->getHandle();
    return a->getObjectB()->getHandle() < b->getObjectB()->getHandle();
}
bool membership(std::vector<const ColliderPair *> &list, const ColliderPair *pair, size_t ColliderPair::*index,
                bool desired)
{
    auto &position = const_cast<ColliderPair *>(pair)->*index;
    if (desired == (position != SIZE_MAX))
        return false;
    if (desired)
    {
        position = list.size();
        list.push_back(pair);
    }
    else
    {
        const auto *moved = list.back();
        list[position] = moved;
        const_cast<ColliderPair *>(moved)->*index = position;
        list.pop_back();
        position = SIZE_MAX;
    }
    return true;
}
void order(std::vector<const ColliderPair *> &list, size_t ColliderPair::*index, bool &dirty)
{
    if (!dirty)
        return;
    std::sort(list.begin(), list.end(), canonicalLess);
    for (size_t i = 0; i < list.size(); ++i)
        const_cast<ColliderPair *>(list[i])->*index = i;
    dirty = false;
}
bool observed(Collider *collider)
{
    const auto *script = collider->getGameObject()->getComponent<ScriptComponent>();
    return collider->getIsCallbackEligible() && script && script->canDispatch() && !script->getBehaviors().empty();
}
bool movingBody(const Rigidbody *body)
{
    return body && body->getBodyType() != STATIC && !body->getIsSleeping();
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

void PhysicsManager::reducePairSupport(const ColliderPair *pair)
{
    auto *collision = pair->getCollision();
    auto *a = pair->getObjectA()->getGameObject()->getComponent<Rigidbody>();
    auto *b = pair->getObjectB()->getGameObject()->getComponent<Rigidbody>();
    const bool referenceIsA = collision && collision->getReferenceObject() == pair->getObjectA();
    const bool supportsA = collision && (collision->supportState &
                                         (referenceIsA ? Collision::SUPPORTS_REFERENCE : Collision::SUPPORTS_INCIDENT));
    const bool supportsB = collision && (collision->supportState &
                                         (referenceIsA ? Collision::SUPPORTS_INCIDENT : Collision::SUPPORTS_REFERENCE));
    const auto reduce = [&](Rigidbody *&oldBody, bool &oldSupport, Rigidbody *body, bool support) {
        if (oldBody == body && oldSupport == support)
            return;
        if (oldSupport)
            queueSupportDelta(oldBody, -1);
        if (support)
            queueSupportDelta(body, 1);
        oldBody = body;
        oldSupport = support;
    };
    reduce(pair->supportBodyA, pair->supportsA, a, supportsA);
    reduce(pair->supportBodyB, pair->supportsB, b, supportsB);
}

void PhysicsManager::flushSupportDeltas()
{
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
            queueBodyChange(body);
    }
    supportDeltaBodies.clear();
    drainBodyChanges();
}

void PhysicsManager::refreshPairLists(const ColliderPair *pair)
{
    auto *collision = pair->getCollision();
    const bool live = collision && !pair->getObjectA()->isRetired() && !pair->getObjectB()->isRetired() &&
                      pair->getObjectA()->getGameObject()->getActive() &&
                      pair->getObjectB()->getGameObject()->getActive();
    membership(contactPairs, pair, &ColliderPair::activeIndex, live);
    const bool solve = live &&
                       (collision->getResolutionType() == Collision::DYNAMIC ||
                        collision->getResolutionType() == Collision::KINEMATIC) &&
                       (movingBody(pair->getObjectA()->getGameObject()->getComponent<Rigidbody>()) ||
                        movingBody(pair->getObjectB()->getGameObject()->getComponent<Rigidbody>()));
    solverOrderDirty |= membership(solverPairs, pair, &ColliderPair::solverIndex, solve);
    stayOrderDirty |= membership(stayPairs, pair, &ColliderPair::stayIndex,
                                 live && (observed(pair->getObjectA()) || observed(pair->getObjectB())));
}

void PhysicsManager::refreshSubscriptions(Collider *collider)
{
    if (const auto *pairs = grid.getTouchingPairs(collider))
        for (auto *pair : *pairs)
            refreshPairLists(pair);
}

void PhysicsManager::refreshResponse(Collider *collider)
{
    if (const auto *pairs = grid.getTouchingPairs(collider))
        for (auto *pair : *pairs)
        {
            if (auto *collision = pair->getCollision())
                collision->updateSupportState(gravityVectorNormalized);
            reducePairSupport(pair);
            refreshPairLists(pair);
        }
}

void PhysicsManager::removePair(const ColliderPair *pair)
{
    if (pair->supportsA)
        queueSupportDelta(pair->supportBodyA, -1);
    if (pair->supportsB)
        queueSupportDelta(pair->supportBodyB, -1);
    pair->supportsA = pair->supportsB = false;
    membership(contactPairs, pair, &ColliderPair::activeIndex, false);
    solverOrderDirty |= membership(solverPairs, pair, &ColliderPair::solverIndex, false);
    stayOrderDirty |= membership(stayPairs, pair, &ColliderPair::stayIndex, false);
}

void PhysicsManager::canonicalizePairs()
{
    // Only solver and observed-event iteration require canonical order. The
    // contact collection is indexed membership, never a solver/dispatch input.
    order(stayPairs, &ColliderPair::stayIndex, stayOrderDirty);
    if (solverOrderDirty)
    {
        order(solverPairs, &ColliderPair::solverIndex, solverOrderDirty);
        activeCollisions.clear();
        activeCollisions.reserve(solverPairs.size());
        for (auto *pair : solverPairs)
            activeCollisions.push_back(pair->getCollision());
    }
}
