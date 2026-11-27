#include "physics/colliderpair.h"

ColliderPair::ColliderPair(Collider *a, Collider *b)
    : objectA(nullptr), objectB(nullptr), objectAStateVersion(UINT32_MAX), objectBStateVersion(UINT32_MAX),
      narrowPhaseQueueIndex(INVALID_INDEX), hasCollision(false), collisionStorage(), adjacencyIndexA(INVALID_INDEX),
      adjacencyIndexB(INVALID_INDEX), witnessCount(0)
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
