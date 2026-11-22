#include "physics/colliderpair.h"
#include "physics/broadphase/aabb.h"

ColliderPair::ColliderPair(Collider *a, Collider *b)
    : objectA(nullptr), objectB(nullptr), collision(nullptr), objectAStateVersion(-1), objectBStateVersion(-1),
      narrowPhaseQueueIndex(SIZE_MAX), adjacencyIndexA(SIZE_MAX),
      adjacencyIndexB(SIZE_MAX), witnessCount(0)
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
    collision = nullptr;
}

ColliderPair::~ColliderPair()
{
    delete collision;
}

Collision *ColliderPair::getOrCreateCollisionNoEvent() const
{
    if (collision == nullptr)
    {
        collision = new Collision(objectA, objectB);
    }

    updateCachedCollisionVersions();

    return collision;
}

void ColliderPair::clearCollisionNoEvent() const
{
    if (collision != nullptr)
    {
        delete collision;
        collision = nullptr;
    }

    updateCachedCollisionVersions();
}
