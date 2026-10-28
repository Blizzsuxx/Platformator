#include "aabb.h"

#include <cassert>
#include "grid.h"

AABBPair::AABBPair(ProjectionOwner a, ProjectionOwner b)
    : a(a.pointer), b(b.pointer)
{
    // An AABB index accepts one endpoint kind, so the owning context carries the tag.
    assert(a.kind == b.kind);
    if (std::less<void *>{}(this->b, this->a))
        std::swap(this->a, this->b);
}

AABB::AABB(Grid *owner)
    : AABB(PairPolicy{owner, ProjectionOwnerKind::Collider, nullptr, nullptr, true})
{
}

AABB::AABB(PairPolicy pairPolicy)
    : intervalListX(this, true), intervalListY(this, false), pairsWithBothAxisOverlapping(), colliderBindings(),
      pairPolicy(pairPolicy)
{
    pairsWithBothAxisOverlapping.reserve(8);
}

AABB::~AABB() = default;

void AABB::add(Collider *element)
{
    if (element != nullptr)
        add({element, ProjectionOwnerKind::Collider}, element->getXProjections(), element->getYProjections());
}

void AABB::add(ProjectionOwner endpoint, BoundingRadiusProjectionAxis *xAxis,
               BoundingRadiusProjectionAxis *yAxis)
{
    if (endpoint.pointer == nullptr || endpoint.kind != pairPolicy.endpointKind || xAxis == nullptr || yAxis == nullptr)
        return;
    auto [result, inserted] = colliderBindings.try_emplace(endpoint.pointer, endpoint, xAxis, yAxis);
    if (!inserted)
        return;
    ColliderBinding *binding = &result->second;
    intervalListX.add(&binding->xBinding);
    intervalListY.add(&binding->yBinding);
}

void AABB::remove(Collider *element)
{
    if (element != nullptr)
        remove({element, ProjectionOwnerKind::Collider});
}

void AABB::remove(ProjectionOwner endpoint)
{
    remove(getBinding(endpoint));
}

void AABB::remove(ColliderBinding *binding)
{
    if (binding == nullptr)
        return;

    intervalListX.remove(&binding->xBinding);
    intervalListY.remove(&binding->yBinding);
    colliderBindings.erase(binding->endpoint);
}

void AABB::repair(Collider *element)
{
    if (element != nullptr)
        repair(ProjectionOwner{element, ProjectionOwnerKind::Collider});
}

void AABB::repair(ProjectionOwner endpoint)
{
    repair(getBinding(endpoint));
}

void AABB::repair(ColliderBinding *binding)
{
    if (binding == nullptr)
        return;
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
    if (element == nullptr)
        return nullptr;
    return getBinding({element, ProjectionOwnerKind::Collider});
}

AABB::ColliderBinding *AABB::getBinding(ProjectionOwner endpoint)
{
    if (endpoint.kind != pairPolicy.endpointKind)
        return nullptr;
    auto iterator = colliderBindings.find(endpoint.pointer);
    return iterator == colliderBindings.end() ? nullptr : &iterator->second;
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
    axisOverlapBegin({colliderA, ProjectionOwnerKind::Collider}, {colliderB, ProjectionOwnerKind::Collider});
}

void AABB::axisOverlapBegin(ProjectionOwner endpointA, ProjectionOwner endpointB)
{
    if (endpointA.pointer == nullptr || endpointB.pointer == nullptr ||
        endpointA.kind != pairPolicy.endpointKind || endpointB.kind != pairPolicy.endpointKind ||
        !checkOverlapOnAxis(endpointA, endpointB))
        return;

    AABBPair pair(endpointA, endpointB);
    if (pairsWithBothAxisOverlapping.find(pair) != pairsWithBothAxisOverlapping.end())
        return;
    if (pairPolicy.directPhysics)
    {
        auto *colliderA = static_cast<Collider *>(endpointA.pointer);
        auto *colliderB = static_cast<Collider *>(endpointB.pointer);
        if (!static_cast<Grid *>(pairPolicy.context)->eligiblePair(colliderA, colliderB))
            return;
    }
    else if (pairPolicy.eligible && !pairPolicy.eligible(pairPolicy.context, endpointA, endpointB))
        return;

    pairsWithBothAxisOverlapping.insert(pair);
    if (pairPolicy.directPhysics)
        static_cast<Grid *>(pairPolicy.context)->recordPairDelta(static_cast<Collider *>(endpointA.pointer),
                                                                 static_cast<Collider *>(endpointB.pointer), 1);
    else if (pairPolicy.changed)
        pairPolicy.changed(pairPolicy.context, endpointA, endpointB, 1);
}

void AABB::axisOverlapEnd(Collider *colliderA, Collider *colliderB)
{
    axisOverlapEnd({colliderA, ProjectionOwnerKind::Collider}, {colliderB, ProjectionOwnerKind::Collider});
}

void AABB::axisOverlapEnd(ProjectionOwner endpointA, ProjectionOwner endpointB)
{
    if (endpointA.pointer == nullptr || endpointB.pointer == nullptr ||
        endpointA.kind != pairPolicy.endpointKind || endpointB.kind != pairPolicy.endpointKind)
        return;
    AABBPair pair(endpointA, endpointB);
    auto iterator = pairsWithBothAxisOverlapping.find(pair);
    if (iterator == pairsWithBothAxisOverlapping.end())
        return;
    pairsWithBothAxisOverlapping.erase(iterator);
    if (pairPolicy.directPhysics)
        static_cast<Grid *>(pairPolicy.context)->recordPairDelta(static_cast<Collider *>(endpointA.pointer),
                                                                 static_cast<Collider *>(endpointB.pointer), -1);
    else if (pairPolicy.changed)
        pairPolicy.changed(pairPolicy.context, endpointA, endpointB, -1);
}

void AABB::overlapBeginCheckpoint(Collider *colliderA, Collider *colliderB)
{
    axisOverlapBegin(colliderA, colliderB);
}

void AABB::overlapBeginCheckpoint(ProjectionOwner endpointA, ProjectionOwner endpointB)
{
    axisOverlapBegin(endpointA, endpointB);
}

void AABB::overlapEndCheckpoint(Collider *colliderA, Collider *colliderB)
{
    axisOverlapEnd(colliderA, colliderB);
}

void AABB::overlapEndCheckpoint(ProjectionOwner endpointA, ProjectionOwner endpointB)
{
    axisOverlapEnd(endpointA, endpointB);
}

bool AABB::checkOverlapOnAxis(ProjectionOwner endpointA, ProjectionOwner endpointB) const
{
    // Physics cells contain only colliders. Read their projections directly on the hot path;
    // the owner-kind checks in axisOverlapBegin precede these casts. Other spatial contexts
    // keep the generic binding lookup.
    if (pairPolicy.endpointKind == ProjectionOwnerKind::Collider)
    {
        auto *colliderA = static_cast<Collider *>(endpointA.pointer);
        auto *colliderB = static_cast<Collider *>(endpointB.pointer);
        const auto *aMinX = colliderA->getXProjections()->getMin();
        const auto *aMaxX = colliderA->getXProjections()->getMax();
        const auto *bMinX = colliderB->getXProjections()->getMin();
        const auto *bMaxX = colliderB->getXProjections()->getMax();
        const auto *aMinY = colliderA->getYProjections()->getMin();
        const auto *aMaxY = colliderA->getYProjections()->getMax();
        const auto *bMinY = colliderB->getYProjections()->getMin();
        const auto *bMaxY = colliderB->getYProjections()->getMax();
        return aMaxX->getProjectedPosition() >= bMinX->getProjectedPosition() &&
               aMinX->getProjectedPosition() <= bMaxX->getProjectedPosition() &&
               aMaxY->getProjectedPosition() >= bMinY->getProjectedPosition() &&
               aMinY->getProjectedPosition() <= bMaxY->getProjectedPosition();
    }

    const auto *bindingA = const_cast<AABB *>(this)->getBinding(endpointA);
    const auto *bindingB = const_cast<AABB *>(this)->getBinding(endpointB);
    if (bindingA == nullptr || bindingB == nullptr)
        return false;

    const auto &aMinX = *bindingA->xBinding.minProxy.getProjection();
    const auto &aMaxX = *bindingA->xBinding.maxProxy.getProjection();
    const auto &bMinX = *bindingB->xBinding.minProxy.getProjection();
    const auto &bMaxX = *bindingB->xBinding.maxProxy.getProjection();
    const auto &aMinY = *bindingA->yBinding.minProxy.getProjection();
    const auto &aMaxY = *bindingA->yBinding.maxProxy.getProjection();
    const auto &bMinY = *bindingB->yBinding.minProxy.getProjection();
    const auto &bMaxY = *bindingB->yBinding.maxProxy.getProjection();
    return aMaxX >= bMinX && aMinX <= bMaxX && aMaxY >= bMinY && aMinY <= bMaxY;
}

bool AABB::getIsEmpty() const
{
    return intervalListX.getIsEmpty();
}

size_t AABB::getOwnedCapacityBytes() const
{
    return (pairsWithBothAxisOverlapping.size() + colliderBindings.size()) * 2 * sizeof(void *) +
           intervalListX.getOwnedCapacityBytes() + intervalListY.getOwnedCapacityBytes() +
           pairsWithBothAxisOverlapping.bucket_count() * sizeof(void *) +
           pairsWithBothAxisOverlapping.size() * sizeof(AABBPair) +
           colliderBindings.bucket_count() * sizeof(void *) +
           colliderBindings.size() * sizeof(decltype(colliderBindings)::value_type);
}
