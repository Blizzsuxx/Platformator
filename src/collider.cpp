#include "collider.h"
#include "gamemanager.h"
#include "localsortarray.h"
#include "gridcell.h"
#include "segmentedintervallist.h"
#include "scriptcomponent.h"

Collider::Collider(GameObject *gameObject, ComponentType type)
    : Component(gameObject, type), collisionGroup(1), collisionMask(1), stateVersion(0), xProjections(this, 0.0f, 0.0f), yProjections(this, 0.0f, 0.0f), gridCellRange(), flags(static_cast<ColliderFlags>(0))
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
    return (flags & IS_TRIGGER) != 0;
}

void Collider::setIsTrigger(const bool isTrigger)
{
    if (isTrigger)
    {
        flags = static_cast<ColliderFlags>(flags | IS_TRIGGER);
    }
    else
    {
        flags = static_cast<ColliderFlags>(flags & ~IS_TRIGGER);
    }
}

bool Collider::getIsQueuedForAdd() const
{
    return (flags & IS_QUEUED_FOR_ADD) != 0;
}

void Collider::markQueuedForAdd()
{
    flags = static_cast<ColliderFlags>(flags | IS_QUEUED_FOR_ADD);
}

void Collider::clearQueuedForAdd()
{
    flags = static_cast<ColliderFlags>(flags & ~IS_QUEUED_FOR_ADD);
}

void Collider::repairMinProjectionProxiesForProjection(BoundingRadiusProjection *projection, BoundingRadiusProjectionAxis *axis)
{
    for (BoundingRadiusProjectionAxisProxy *proxy : axis->getProxies())
    {
        if (proxy->minProxy.getProjection() == projection)
        {
            proxy->ownerList->repairProjection(&proxy->minProxy);
        }
    }
}

void Collider::repairMaxProjectionProxiesForProjection(BoundingRadiusProjection *projection, BoundingRadiusProjectionAxis *axis)
{
    for (BoundingRadiusProjectionAxisProxy *proxy : axis->getProxies())
    {
        if (proxy->maxProxy.getProjection() == projection)
        {
            proxy->ownerList->repairProjection(&proxy->maxProxy);
        }
    }
}

void Collider::scheduleSync()
{
    PhysicsManager *physicsManager = GameManager::getInstance().getPhysicsManager();
    if (physicsManager != nullptr && !(flags & IS_QUEUED_FOR_SYNC) && hasValidGridCellRange())
    {
        physicsManager->queueColliderSync(this);
        flags = static_cast<ColliderFlags>(flags | IS_QUEUED_FOR_SYNC);
    }
}

void Collider::applySync()
{
    updateCollider();
    updateGridCellRange();
    stateVersion++;
    flags = static_cast<ColliderFlags>(flags & ~IS_QUEUED_FOR_SYNC);
}

void Collider::triggerCollisionEnter(const Collider *other, double timeDelta) const
{
    ScriptComponent *scriptComponent = getGameObject()->getComponent<ScriptComponent>();
    if (scriptComponent != nullptr)
    {
        scriptComponent->dispatchCollisionEnter(const_cast<Collider *>(other), timeDelta);
    }
}

void Collider::triggerCollisionExit(const Collider *other, double timeDelta) const
{
    ScriptComponent *scriptComponent = getGameObject()->getComponent<ScriptComponent>();
    if (scriptComponent != nullptr)
    {
        scriptComponent->dispatchCollisionExit(const_cast<Collider *>(other), timeDelta);
    }
}

void Collider::triggerCollisionStay(const Collider *other, double timeDelta) const
{
    ScriptComponent *scriptComponent = getGameObject()->getComponent<ScriptComponent>();
    if (scriptComponent != nullptr)
    {
        scriptComponent->dispatchCollisionStay(const_cast<Collider *>(other), timeDelta);
    }
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

void Collider::removeSync()
{
    flags = static_cast<ColliderFlags>(flags & ~IS_QUEUED_FOR_SYNC);
}

uint64_t Collider::getCollisionMask() const
{
    return collisionMask;
}

uint64_t Collider::getStateVersion() const
{
    return stateVersion;
}

void Collider::updateGridCellRange()
{
    gridCellRange = calculateGridCellRange();
}

const GridCellRange &Collider::getGridCellRange() const
{
    return gridCellRange;
}

void Collider::setGridCellRange(const GridCellRange &range)
{
    gridCellRange = range;
}

GridCellRange Collider::calculateGridCellRange()
{
    int minX = static_cast<int>(std::floor(xProjections.getMin()->getProjectedPosition() / GRID_CELL_SIZE));
    int maxX = static_cast<int>(std::floor(xProjections.getMax()->getProjectedPosition() / GRID_CELL_SIZE));
    int minY = static_cast<int>(std::floor(yProjections.getMin()->getProjectedPosition() / GRID_CELL_SIZE));
    int maxY = static_cast<int>(std::floor(yProjections.getMax()->getProjectedPosition() / GRID_CELL_SIZE));

    return GridCellRange(minX, maxX, minY, maxY);
}

bool Collider::getIsQueuedForSync() const
{
    return (flags & IS_QUEUED_FOR_SYNC) != 0;
}

bool Collider::hasValidGridCellRange() const
{
    return gridCellRange.getIsValid();
}

bool Collider::getIsRegisteredInGrid() const
{
    return (flags & IS_REGISTERED_IN_GRID) != 0;
}

void Collider::setIsRegisteredInGrid(bool isRegisteredInGrid)
{
    if (isRegisteredInGrid)
    {
        flags = static_cast<ColliderFlags>(flags | IS_REGISTERED_IN_GRID);
    }
    else
    {
        flags = static_cast<ColliderFlags>(flags & ~IS_REGISTERED_IN_GRID);
    }
}
