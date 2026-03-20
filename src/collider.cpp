#include "collider.h"
#include "localsortarray.h"

Collider::Collider(GameObject *gameObject, ComponentType type)
    : Component(gameObject, type), collisionGroup(1), collisionMask(1), isTrigger(false), isDirty(true), xProjections(this, 0.0f, 0.0f), yProjections(this, 0.0f, 0.0f)
{
}

Collider::~Collider()
{
}

BoundingRadiusProjectionAxis *Collider::getXProjections()
{
    return &xProjections;
}

BoundingRadiusProjectionAxis *Collider::getYProjections()
{
    return &yProjections;
}

bool Collider::getIsTrigger() const
{
    return isTrigger;
}

void Collider::setIsTrigger(const bool isTrigger)
{
    this->isTrigger = isTrigger;
}

bool Collider::getIsDirty() const
{
    return isDirty;
}

void Collider::setChunkDirtyIfNotNull(LocalSortArray *chunk, const bool isDirty)
{
    if (chunk != nullptr)
    {
        chunk->setIsDirty(isDirty);
    }
}

void Collider::setIsDirty(const bool isDirty)
{
    this->isDirty = isDirty;

    setChunkDirtyIfNotNull(this->getXProjections()->getMax()->getChunk(), isDirty);
    setChunkDirtyIfNotNull(this->getXProjections()->getMin()->getChunk(), isDirty);
    setChunkDirtyIfNotNull(this->getYProjections()->getMax()->getChunk(), isDirty);
    setChunkDirtyIfNotNull(this->getYProjections()->getMin()->getChunk(), isDirty);
}

void Collider::triggerCollisionEnter(const Collider *other) const
{
    // Placeholder for collision enter event handling
    // In a full implementation, this would notify the game object or other systems of the collision event
}

void Collider::triggerCollisionExit(const Collider *other) const
{
    // Placeholder for collision exit event handling
    // In a full implementation, this would notify the game object or other systems of the collision event
}

void Collider::setCollisionGroup(const uint32_t collisionGroup)
{
    this->collisionGroup = collisionGroup;
}

uint32_t Collider::getCollisionGroup() const
{
    return collisionGroup;
}

void Collider::setCollisionMask(const uint32_t collisionMask)
{
    this->collisionMask = collisionMask;
}

uint32_t Collider::getCollisionMask() const
{
    return collisionMask;
}