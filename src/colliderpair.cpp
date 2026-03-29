#include "colliderpair.h"
#include "aabb.h"

ColliderPair::ColliderPair(Collider *a, Collider *b) : objectA(nullptr), objectB(nullptr), collision(nullptr), objectAStateVersion(-1), objectBStateVersion(-1), isQueuedForNarrowPhase(false), narrowPhaseQueueIndex(SIZE_MAX), adjacencyIndexA(SIZE_MAX), adjacencyIndexB(SIZE_MAX), witnessCountX(0), witnessCountY(0)
{
    if (a < b)
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
    clearCollision();
}

Collision *ColliderPair::getCollision() const
{
    return collision;
}

Collision *ColliderPair::getOrCreateCollision() const
{
    if (collision == nullptr)
    {
        collision = new Collision(objectA, objectB);
        triggerCollisionEnter();
    }
    else
    {
        triggerCollisionStay();
    }

    updateCachedCollisionVersions();

    return collision;
}

void ColliderPair::clearCollision() const
{
    if (collision != nullptr)
    {
        triggerCollisionExit();
        delete collision;
        collision = nullptr;
    }
    updateCachedCollisionVersions();
}

bool ColliderPair::getIsQueuedForNarrowPhase() const
{
    return isQueuedForNarrowPhase;
}

void ColliderPair::setIsQueuedForNarrowPhase(bool queued) const
{
    isQueuedForNarrowPhase = queued;
}

size_t ColliderPair::getNarrowPhaseQueueIndex() const
{
    return narrowPhaseQueueIndex;
}

void ColliderPair::setNarrowPhaseQueueIndex(size_t index) const
{
    narrowPhaseQueueIndex = index;
}

size_t ColliderPair::getAdjacencyIndexA() const
{
    return adjacencyIndexA;
}

void ColliderPair::setAdjacencyIndexA(size_t index) const
{
    adjacencyIndexA = index;
}

size_t ColliderPair::getAdjacencyIndexB() const
{
    return adjacencyIndexB;
}

void ColliderPair::setAdjacencyIndexB(size_t index) const
{
    adjacencyIndexB = index;
}

void ColliderPair::triggerCollisionEnter() const
{
    if (collision != nullptr)
    {
        if (objectA != nullptr)
        {
            objectA->triggerCollisionEnter(objectB);
        }
        if (objectB != nullptr)
        {
            objectB->triggerCollisionEnter(objectA);
        }
    }
}

void ColliderPair::triggerCollisionStay() const
{
    if (collision != nullptr)
    {
        if (objectA != nullptr)
        {
            objectA->triggerCollisionStay(objectB);
        }
        if (objectB != nullptr)
        {
            objectB->triggerCollisionStay(objectA);
        }
    }
}

void ColliderPair::triggerCollisionExit() const
{
    if (collision != nullptr)
    {
        if (objectA != nullptr)
        {
            objectA->triggerCollisionExit(objectB);
        }
        if (objectB != nullptr)
        {
            objectB->triggerCollisionExit(objectA);
        }
    }
}

void ColliderPair::setObjectA(Collider *colliderA)
{
    objectA = colliderA;
    objectAStateVersion = colliderA ? colliderA->getStateVersion() : -1;
}

void ColliderPair::setObjectB(Collider *colliderB)
{
    objectB = colliderB;
    objectBStateVersion = colliderB ? colliderB->getStateVersion() : -1;
}

Collider *ColliderPair::getObjectA() const
{
    return objectA;
}

Collider *ColliderPair::getObjectB() const
{
    return objectB;
}

bool ColliderPair::shouldUpdate() const
{
    uint64_t currentObjectAVersion = objectA ? objectA->getStateVersion() : -1;
    uint64_t currentObjectBVersion = objectB ? objectB->getStateVersion() : -1;

    return currentObjectAVersion != objectAStateVersion || currentObjectBVersion != objectBStateVersion;
}

void ColliderPair::updateCachedCollisionVersions() const
{
    objectAStateVersion = objectA ? objectA->getStateVersion() : -1;
    objectBStateVersion = objectB ? objectB->getStateVersion() : -1;
}

bool ColliderPair::operator==(const ColliderPair &other) const
{
    return (objectA == other.objectA && objectB == other.objectB) || (objectA == other.objectB && objectB == other.objectA);
}

void ColliderPair::incrementWitnessCount(Axis axis) const
{
    if (axis == Axis::X)
    {
        witnessCountX++;
    }
    else if (axis == Axis::Y)
    {
        witnessCountY++;
    }
}

void ColliderPair::decrementWitnessCount(Axis axis) const
{
    if (axis == Axis::X)
    {
        if (witnessCountX > 0)
        {
            witnessCountX--;
        }
    }
    else if (axis == Axis::Y)
    {
        if (witnessCountY > 0)
        {
            witnessCountY--;
        }
    }
}

size_t ColliderPair::getWitnessCountMin() const
{
    return std::min(witnessCountX, witnessCountY);
}

size_t ColliderPair::getWitnessCountMax() const
{
    return witnessCountX + witnessCountY;
}