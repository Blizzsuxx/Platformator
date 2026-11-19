#include "aabb.h"
#include "grid.h"

AABB::AABB(Grid *owner)
    : bindingBlocks(),
      freeBindings(),
      bindingMap(),
      intervalListX(this, true),
      intervalListY(this, false),
      pairsWithBothAxisOverlapping(),
      owner(owner)
{
}

AABB::~AABB()
{
    intervalListX.clear();
    intervalListY.clear();
    bindingMap.forEach([](Collider *, ColliderBinding *binding) {
        std::destroy_at(binding);
    });
    bindingMap.clear();
    freeBindings.clear();
    bindingBlocks.clear();
}

AABB::BindingSlot *AABB::allocateBindingSlot()
{
    if (!freeBindings.empty())
    {
        BindingSlot *slot = freeBindings.back();
        freeBindings.pop_back();
        return slot;
    }

    if (bindingBlocks.empty() || bindingBlocks.back().used == bindingBlocks.back().capacity)
    {
        const size_t prevCap = bindingBlocks.empty() ? 0 : bindingBlocks.back().capacity;
        const size_t nextCap = prevCap == 0 ? 4 : std::min<size_t>(prevCap * 2, 64);
        BindingBlock block;
        block.slots = std::unique_ptr<BindingSlot[]>(new BindingSlot[nextCap]);
        block.capacity = nextCap;
        block.used = 0;
        bindingBlocks.push_back(std::move(block));
    }

    BindingBlock &block = bindingBlocks.back();
    return &block.slots[block.used++];
}

void AABB::add(Collider *element)
{
    ColliderBinding *binding = bindingMap.emplaceIfAbsent(element, [&]() {
        BindingSlot *slot = allocateBindingSlot();
        return std::construct_at(reinterpret_cast<ColliderBinding *>(slot), element);
    });
    if (binding == nullptr)
    {
        return;
    }
    intervalListX.add(&binding->xBinding);
    intervalListY.add(&binding->yBinding);
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
    std::destroy_at(binding);
    freeBindings.push_back(reinterpret_cast<BindingSlot *>(binding));
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

void AABB::repair(ColliderBinding *binding)
{
    binding->xBinding.minProxy.updateCachedProjectedPosition();
    intervalListX.repairProjection(&binding->xBinding.minProxy);

    binding->xBinding.maxProxy.updateCachedProjectedPosition();
    intervalListX.repairProjection(&binding->xBinding.maxProxy);

    binding->yBinding.minProxy.updateCachedProjectedPosition();
    intervalListY.repairProjection(&binding->yBinding.minProxy);

    binding->yBinding.maxProxy.updateCachedProjectedPosition();
    intervalListY.repairProjection(&binding->yBinding.maxProxy);
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

    if (pairsWithBothAxisOverlapping.emplace(colliderA, colliderB).second)
    {
        owner->recordPairDelta(colliderA, colliderB, 1);
    }
}

void AABB::axisOverlapEnd(Collider *colliderA, Collider *colliderB)
{
    if (pairsWithBothAxisOverlapping.empty())
    {
        return;
    }

    AABBPair pair(colliderA, colliderB);
    auto iterator = pairsWithBothAxisOverlapping.find(pair);
    if (iterator == pairsWithBothAxisOverlapping.end())
    {
        return;
    }
    pairsWithBothAxisOverlapping.erase(iterator);

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