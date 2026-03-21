#include "colliderpair.h"

ColliderPair::ColliderPair(Collider *a, Collider *b) : objectA(nullptr), objectB(nullptr), collision(nullptr), objectAStateVersion(-1), objectBStateVersion(-1)
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