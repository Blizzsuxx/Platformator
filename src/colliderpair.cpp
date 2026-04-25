#include "colliderpair.h"
#include "aabb.h"
#include "gamemanager.h"

ColliderPair::ColliderPair(Collider *a, Collider *b) : objectA(nullptr), objectB(nullptr), collision(nullptr), objectAStateVersion(-1), objectBStateVersion(-1), isQueuedForNarrowPhase(false), narrowPhaseQueueIndex(SIZE_MAX), adjacencyIndexA(SIZE_MAX), adjacencyIndexB(SIZE_MAX), witnessCount(0)
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
        queueCollisionEnter();
    }
    else
    {
        queueCollisionStay();
    }

    updateCachedCollisionVersions();

    return collision;
}

void ColliderPair::clearCollision() const
{
    if (collision != nullptr)
    {
        queueCollisionExit();
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

void ColliderPair::queueCollisionEnter() const
{
    GameManager::getInstance().getPhysicsManager()->addPendingPhysicsEvent(PhysicsEvent{PhysicsEvent::COLLISION_ENTER, collision, objectA, objectB});
}

void ColliderPair::queueCollisionStay() const
{
    GameManager::getInstance().getPhysicsManager()->addPendingPhysicsEvent(PhysicsEvent{PhysicsEvent::COLLISION_STAY, collision, objectA, objectB});
}

void ColliderPair::queueCollisionExit() const
{
    GameManager::getInstance().getPhysicsManager()->addPendingPhysicsEvent(PhysicsEvent{PhysicsEvent::COLLISION_EXIT, nullptr, objectA, objectB});
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

void ColliderPair::incrementWitnessCount() const
{
    witnessCount++;
}

void ColliderPair::decrementWitnessCount() const
{
    if (witnessCount > 0)
    {
        witnessCount--;
    }
}

size_t ColliderPair::getWitnessCount() const
{
    return witnessCount;
}
