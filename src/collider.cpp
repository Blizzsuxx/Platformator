#include "collider.h"
#include "gamemanager.h"
#include "localsortarray.h"
#include "gridcell.h"

Collider::Collider(GameObject *gameObject, ComponentType type)
    : Component(gameObject, type), collisionGroup(1), collisionMask(1), isTrigger(false), stateVersion(0), xProjections(this, 0.0f, 0.0f), yProjections(this, 0.0f, 0.0f), cells(), hasGridCellRangeCache(false), cachedGridCellMinX(0), cachedGridCellMaxX(0), cachedGridCellMinY(0), cachedGridCellMaxY(0), isRegisteredInGrid(false)
{
    cells.reserve(4);
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

void Collider::setChunkDirtyIfNotNull(LocalSortArray *chunk, const bool isDirty)
{
    if (chunk != nullptr)
    {
        chunk->setIsDirty(isDirty);
    }
}

void Collider::updateStateVersion()
{
    if (!isRegisteredInGrid)
    {
        return;
    }

    stateVersion++;

    std::vector<BoundingRadiusProjectionAxisProxy *> &xProxies = xProjections.getProxies();
    std::vector<BoundingRadiusProjectionAxisProxy *> &yProxies = yProjections.getProxies();

    for (BoundingRadiusProjectionAxisProxy *proxy : xProxies)
    {
        setChunkDirtyIfNotNull(proxy->minProxy.getChunk(), true);
        setChunkDirtyIfNotNull(proxy->maxProxy.getChunk(), true);
    }

    for (BoundingRadiusProjectionAxisProxy *proxy : yProxies)
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

void Collider::setCachedGridCellRange(int minX, int maxX, int minY, int maxY)
{
    hasGridCellRangeCache = true;
    cachedGridCellMinX = minX;
    cachedGridCellMaxX = maxX;
    cachedGridCellMinY = minY;
    cachedGridCellMaxY = maxY;
}

bool Collider::hasCachedGridCellRange() const
{
    return hasGridCellRangeCache;
}

int Collider::getCachedGridCellMinX() const
{
    return cachedGridCellMinX;
}

int Collider::getCachedGridCellMaxX() const
{
    return cachedGridCellMaxX;
}

int Collider::getCachedGridCellMinY() const
{
    return cachedGridCellMinY;
}

int Collider::getCachedGridCellMaxY() const
{
    return cachedGridCellMaxY;
}

void Collider::clearCachedGridCellRange()
{
    hasGridCellRangeCache = false;
}

void Collider::setIsRegisteredInGrid(bool isRegistered)
{
    isRegisteredInGrid = isRegistered;
}

bool Collider::getIsRegisteredInGrid() const
{
    return isRegisteredInGrid;
}