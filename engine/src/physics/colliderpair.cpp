#include "physics/colliderpair.h"

ColliderPair::ColliderPair(Collider *a, Collider *b)
    : objectA(nullptr), objectB(nullptr), collisionStorage(), objectAStateVersion(-1), objectBStateVersion(-1),
      narrowPhaseQueueIndex(SIZE_MAX), adjacencyIndexA(SIZE_MAX), adjacencyIndexB(SIZE_MAX), witnessCount(0),
      hasCollision(false)
{
    if (a->getHandle() < b->getHandle())
    {
        objectA = a;
        objectB = b;
    }
    else
    {
        objectA = b;
        objectB = a;
    }
}

ColliderPair::~ColliderPair() = default;

Collision *ColliderPair::getOrCreateCollisionNoEvent()
{
    if (!hasCollision)
    {
        collisionStorage = Collision(objectA, objectB);
        hasCollision = true;
    }

    updateCachedCollisionVersions();

    return &collisionStorage;
}

void ColliderPair::clearCollisionNoEvent() noexcept
{
    hasCollision = false;
    updateCachedCollisionVersions();
}
