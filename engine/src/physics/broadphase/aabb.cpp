#include "physics/broadphase/aabb.h"
#include "physics/broadphase/grid.h"

#include <algorithm>
#include <oneapi/tbb/parallel_invoke.h>

namespace
{
bool proxySortLess(BoundingRadiusProjectionProxy *a, BoundingRadiusProjectionProxy *b) noexcept
{
    const float posA = a->getCachedProjectedPosition();
    const float posB = b->getCachedProjectedPosition();
    if (posA != posB)
    {
        return posA < posB;
    }
    if (a->getIsMaxima() != b->getIsMaxima())
    {
        return !a->getIsMaxima();
    }
    const Collider *colA = a->getCollider();
    const Collider *colB = b->getCollider();
    const uint32_t slotA = colA->getHandle().slot;
    const uint32_t slotB = colB->getHandle().slot;
    if (slotA != slotB)
    {
        return slotA < slotB;
    }
    return colA < colB;
}
} // namespace

AABB::AABB(Grid *owner)
    : bindingPool(), bindingMap(), intervalListX(this, true), intervalListY(this, false),
      pairsWithBothAxisOverlapping(), owner(owner)
{
}

AABB::~AABB()
{
    intervalListX.clear();
    intervalListY.clear();
    bindingMap.forEach([this](Collider *, ColliderBinding *binding) { bindingPool.destroy(binding); });
    bindingMap.clearAndRelease();
    bindingPool.clear();
}

void AABB::add(Collider *element)
{
    ColliderBinding *binding = bindingMap.emplaceIfAbsent(element, [&]() { return bindingPool.create(element); });
    if (binding == nullptr)
    {
        return;
    }
    intervalListX.add(&binding->xBinding);
    intervalListY.add(&binding->yBinding);
}

void AABB::addBatch(const std::vector<Collider *> &colliders)
{
    if (colliders.size() < 32)
    {
        for (Collider *collider : colliders)
        {
            add(collider);
        }
        return;
    }

    const bool wasEmpty = getIsEmpty();
    std::vector<ColliderBinding *> added;
    added.reserve(colliders.size());
    for (Collider *collider : colliders)
    {
        ColliderBinding *binding = bindingMap.emplaceIfAbsent(collider, [&]() { return bindingPool.create(collider); });
        if (binding != nullptr)
        {
            added.push_back(binding);
        }
    }
    if (added.empty())
    {
        return;
    }

    if (wasEmpty)
    {
        std::vector<BoundingRadiusProjectionProxy *> xProxies;
        std::vector<BoundingRadiusProjectionProxy *> yProxies;
        xProxies.reserve(added.size() * 2);
        yProxies.reserve(added.size() * 2);
        for (ColliderBinding *binding : added)
        {
            xProxies.push_back(&binding->xBinding.minProxy);
            xProxies.push_back(&binding->xBinding.maxProxy);
            yProxies.push_back(&binding->yBinding.minProxy);
            yProxies.push_back(&binding->yBinding.maxProxy);
        }

        auto buildX = [&]() {
            std::sort(xProxies.begin(), xProxies.end(), proxySortLess);
            intervalListX.addBulkEmpty(xProxies);
        };
        auto buildY = [&]() {
            std::sort(yProxies.begin(), yProxies.end(), proxySortLess);
            intervalListY.addBulkEmpty(yProxies);
        };

        if (added.size() >= 64)
        {
            tbb::parallel_invoke(buildX, buildY);
        }
        else
        {
            buildX();
            buildY();
        }
        return;
    }

    auto insertX = [&]() {
        for (ColliderBinding *binding : added)
        {
            intervalListX.add(&binding->xBinding);
        }
    };
    auto insertY = [&]() {
        for (ColliderBinding *binding : added)
        {
            intervalListY.add(&binding->yBinding);
        }
    };

    if (added.size() >= 64)
    {
        tbb::parallel_invoke(insertX, insertY);
    }
    else
    {
        insertX();
        insertY();
    }
}

void AABB::remove(Collider *element)
{
    ColliderBinding *binding = getBinding(element);
    if (binding == nullptr)
    {
        return;
    }

    remove(binding);
}

void AABB::remove(ColliderBinding *binding)
{
    if (binding == nullptr)
    {
        return;
    }

    intervalListX.remove(&binding->xBinding);
    intervalListY.remove(&binding->yBinding);
    bindingMap.erase(binding->collider);
    bindingPool.destroy(binding);
}

bool AABB::tryClearAllRemoved(const std::vector<Collider *> &colliders)
{
    if (bindingMap.size() < 16 || colliders.size() < bindingMap.size())
    {
        return false;
    }

    std::vector<ColliderBinding *> matched;
    matched.reserve(bindingMap.size());
    for (Collider *collider : colliders)
    {
        if (ColliderBinding *binding = bindingMap.find(collider); binding != nullptr && !binding->pendingRemoval)
        {
            binding->pendingRemoval = true;
            matched.push_back(binding);
        }
    }

    if (matched.size() != bindingMap.size())
    {
        for (ColliderBinding *binding : matched)
        {
            binding->pendingRemoval = false;
        }
        return false;
    }

    for (const AABBPair &pair : pairsWithBothAxisOverlapping)
    {
        owner->recordPairDelta(pair.a, pair.b, -1);
    }
    pairsWithBothAxisOverlapping.clear();
    intervalListX.resetToEmpty();
    intervalListY.resetToEmpty();
    bindingMap.clear();
    for (ColliderBinding *binding : matched)
    {
        bindingPool.destroy(binding);
    }
    return true;
}

void AABB::removeBatch(const std::vector<Collider *> &colliders)
{
    if (colliders.empty())
    {
        return;
    }
    if (tryClearAllRemoved(colliders))
    {
        return;
    }
    if (colliders.size() < 64)
    {
        for (Collider *collider : colliders)
        {
            remove(collider);
        }
        return;
    }

    std::vector<ColliderBinding *> toRemove;
    toRemove.reserve(colliders.size());
    for (Collider *collider : colliders)
    {
        if (ColliderBinding *binding = bindingMap.find(collider); binding != nullptr && !binding->pendingRemoval)
        {
            binding->pendingRemoval = true;
            toRemove.push_back(binding);
        }
    }
    if (toRemove.empty())
    {
        return;
    }

    tbb::parallel_invoke(
        [&]() {
            for (ColliderBinding *binding : toRemove)
            {
                intervalListX.remove(&binding->xBinding);
            }
        },
        [&]() {
            for (ColliderBinding *binding : toRemove)
            {
                intervalListY.remove(&binding->yBinding);
            }
        });

    for (ColliderBinding *binding : toRemove)
    {
        bindingMap.erase(binding->collider);
        bindingPool.destroy(binding);
    }
}

void AABB::repair(Collider *element)
{
    ColliderBinding *binding = getBinding(element);
    if (binding == nullptr)
    {
        return;
    }

    repair(binding);
}

void AABB::repairAxis(SegmentedIntervalList &list, BoundingRadiusProjectionAxisBinding &axisBinding)
{
    const float oldMin = axisBinding.minProxy.getCachedProjectedPosition();
    const float newMin = axisBinding.minProxy.getProjectedPosition();
    const float oldMax = axisBinding.maxProxy.getCachedProjectedPosition();
    const float newMax = axisBinding.maxProxy.getProjectedPosition();
    const bool minChanged = newMin != oldMin;
    const bool maxChanged = newMax != oldMax;
    if (!minChanged && !maxChanged)
    {
        return;
    }

    // Move the leading endpoint first so fast motion never inverts [min, max] mid-repair.
    if (newMax > oldMax)
    {
        if (maxChanged)
        {
            axisBinding.maxProxy.updateCachedProjectedPosition();
            list.repairProjection(&axisBinding.maxProxy);
        }
        if (minChanged)
        {
            axisBinding.minProxy.updateCachedProjectedPosition();
            list.repairProjection(&axisBinding.minProxy);
        }
    }
    else
    {
        if (minChanged)
        {
            axisBinding.minProxy.updateCachedProjectedPosition();
            list.repairProjection(&axisBinding.minProxy);
        }
        if (maxChanged)
        {
            axisBinding.maxProxy.updateCachedProjectedPosition();
            list.repairProjection(&axisBinding.maxProxy);
        }
    }
}

void AABB::repair(ColliderBinding *binding)
{
    repairAxis(intervalListX, binding->xBinding);
    repairAxis(intervalListY, binding->yBinding);
}

AABB::ColliderBinding *AABB::getBinding(Collider *element)
{
    return bindingMap.find(element);
}

SegmentedIntervalList *AABB::getIntervalListX()
{
    return &intervalListX;
}

SegmentedIntervalList *AABB::getIntervalListY()
{
    return &intervalListY;
}

void AABB::axisOverlapBegin(Collider *colliderA, Collider *colliderB)
{
    if (!colliderA->getGameObject()->getActive() || !colliderB->getGameObject()->getActive())
    {
        return;
    }
    if ((colliderA->getCollisionGroup() & colliderB->getCollisionMask()) == 0 ||
        (colliderB->getCollisionGroup() & colliderA->getCollisionMask()) == 0)
    {
        return;
    }

    if (!checkOverlapOnAxis(colliderA, colliderB))
    {
        return;
    }

    if (pairsWithBothAxisOverlapping.insert(AABBPair(colliderA, colliderB)))
    {
        owner->recordPairDelta(colliderA, colliderB, 1);
    }
}

void AABB::axisOverlapEnd(Collider *colliderA, Collider *colliderB)
{
    if (pairsWithBothAxisOverlapping.erase(AABBPair(colliderA, colliderB)) == 0)
    {
        return;
    }

    owner->recordPairDelta(colliderA, colliderB, -1);
}

void AABB::overlapBeginCheckpoint(Collider *colliderA, Collider *colliderB)
{
    axisOverlapBegin(colliderA, colliderB);
}

void AABB::overlapEndCheckpoint(Collider *colliderA, Collider *colliderB)
{
    axisOverlapEnd(colliderA, colliderB);
}

bool AABB::checkOverlapOnAxis(Collider *colliderA, Collider *colliderB) const
{
    auto *xA = colliderA->getXProjections();
    auto *xB = colliderB->getXProjections();
    if (xA->getMax()->getProjectedPosition() < xB->getMin()->getProjectedPosition() ||
        xA->getMin()->getProjectedPosition() > xB->getMax()->getProjectedPosition())
    {
        return false;
    }
    auto *yA = colliderA->getYProjections();
    auto *yB = colliderB->getYProjections();
    return yA->getMax()->getProjectedPosition() >= yB->getMin()->getProjectedPosition() &&
           yA->getMin()->getProjectedPosition() <= yB->getMax()->getProjectedPosition();
}

bool AABB::getIsEmpty() const
{
    // checking just one is enough because if one is empty, the other one has to be empty as well
    return intervalListX.getIsEmpty();
}
