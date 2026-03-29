#include "collider.h"
#include "gamemanager.h"
#include "localsortarray.h"
#include "gridcell.h"

Collider::Collider(GameObject *gameObject, ComponentType type)
    : Component(gameObject, type), collisionGroup(1), collisionMask(1), isTrigger(false), stateVersion(0), xProjections(this, 0.0f, 0.0f), yProjections(this, 0.0f, 0.0f), cells(), projectionProxies()
{
}

Collider::~Collider()
{
    for (BoundingRadiusProjectionAxisProxy *proxy : projectionProxies)
    {
        delete proxy;
    }
    projectionProxies.clear();
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

void Collider::setChunkDirtyIfNotNull(LocalSortArray *chunk, const bool isDirty)
{
    if (chunk != nullptr)
    {
        chunk->setIsDirty(isDirty);
    }
}

void Collider::updateStateVersion()
{
    stateVersion++;

    for (BoundingRadiusProjectionAxisProxy *proxy : projectionProxies)
    {
        setChunkDirtyIfNotNull(proxy->minProxy.getChunk(), true);
        setChunkDirtyIfNotNull(proxy->maxProxy.getChunk(), true);
    }

    PhysicsManager *physicsManager = GameManager::getInstance().getPhysicsManager();
    if (physicsManager != nullptr)
    {
        physicsManager->notifyColliderUpdated(this);
    }
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

void Collider::triggerCollisionStay(const Collider *other) const
{
    // Placeholder for collision stay event handling
    // In a full implementation, this would notify the game object or other systems of the collision event
}

void Collider::setCollisionGroup(const uint64_t collisionGroup)
{
    if (this->collisionGroup == collisionGroup)
    {
        return;
    }

    this->collisionGroup = collisionGroup;

    PhysicsManager *physicsManager = GameManager::getInstance().getPhysicsManager();
    if (physicsManager != nullptr)
    {
        physicsManager->refreshColliderComponent(this);
    }
}

uint64_t Collider::getCollisionGroup() const
{
    return collisionGroup;
}

void Collider::setCollisionMask(const uint64_t collisionMask)
{
    if (this->collisionMask == collisionMask)
    {
        return;
    }

    this->collisionMask = collisionMask;

    PhysicsManager *physicsManager = GameManager::getInstance().getPhysicsManager();
    if (physicsManager != nullptr)
    {
        physicsManager->refreshColliderComponent(this);
    }
}

uint64_t Collider::getCollisionMask() const
{
    return collisionMask;
}

uint64_t Collider::getStateVersion() const
{
    return stateVersion;
}

void Collider::addToGridCell(GridCell *cell)
{
    if (cell != nullptr)
    {
        cells.push_back(cell);
    }
}

void Collider::removeFromGridCell(GridCell *cell)
{
    if (cell != nullptr)
    {
        auto it = std::find(cells.begin(), cells.end(), cell);
        if (it != cells.end())
        {
            cells.erase(it);
        }
    }
}

std::vector<GridCell *> &Collider::getGridCells()
{
    return cells;
}

void Collider::clearGridCells()
{
    cells.clear();
}

BoundingRadiusProjectionAxisProxy *Collider::addProjectionProxyAxis(BoundingRadiusProjection *minProjection, BoundingRadiusProjection *maxProjection, LocalSortArray *chunk)
{
    BoundingRadiusProjectionAxisProxy *proxy = new BoundingRadiusProjectionAxisProxy{BoundingRadiusProjectionProxy(minProjection, chunk), BoundingRadiusProjectionProxy(maxProjection, chunk)};
    projectionProxies.push_back(proxy);
    return proxy;
}

void Collider::removeProjectionProxy(BoundingRadiusProjectionAxisProxy *proxy)
{
    if (proxy != nullptr)
    {
        auto it = std::find(projectionProxies.begin(), projectionProxies.end(), proxy);
        if (it != projectionProxies.end())
        {
            projectionProxies.erase(it);
            delete proxy;
        }
    }
}

BoundingRadiusProjectionAxisProxy *Collider::getProjectionProxiesForList(SegmentedIntervalList *list)
{
    BoundingRadiusProjectionAxisProxy *proxyResult = nullptr;
    for (BoundingRadiusProjectionAxisProxy *proxy : projectionProxies)
    {
        LocalSortArray *chunk = proxy->minProxy.getChunk();
        if (chunk != nullptr && chunk->getOwner() == list)
        {
            proxyResult = proxy;
            break;
        }
    }

    return proxyResult;
}