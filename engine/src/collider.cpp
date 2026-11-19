#include "platformator/collider.h"

#include <cassert>
#include "constants.h"
#include "gamemanager.h"
#include "localsortarray.h"
#include "gridcell.h"
#include "runtimeaccess.h"
#include "segmentedintervallist.h"
#include "platformator/scriptcomponent.h"
#include "collision.h"
#include "workprobe.h"

Collider::Collider(GameObject *gameObject, ComponentType type, ColliderType colliderType)
    : Component(gameObject, type), collisionGroup(1), collisionMask(1), colliderType(colliderType), stateVersion(0),
      offset(Eigen::Vector2f::Zero()), xProjections(this, 0.0f, 0.0f), yProjections(this, 0.0f, 0.0f),
      physicsManagerIndex(SIZE_MAX), gridCellRange(), flags(static_cast<ColliderFlags>(0)), gridCellRangeCache()
{
}

Collider::Collider(ComponentType type, ColliderType colliderType)
    : Component(type), collisionGroup(1), collisionMask(1), colliderType(colliderType), stateVersion(0),
      offset(Eigen::Vector2f::Zero()), xProjections(this, 0.0f, 0.0f), yProjections(this, 0.0f, 0.0f),
      physicsManagerIndex(SIZE_MAX), gridCellRange(), flags(static_cast<ColliderFlags>(0)), gridCellRangeCache()
{
}

Collider::~Collider()
{
}

const Eigen::Vector2f &Collider::getOffset() const
{
    return offset;
}

void Collider::setOffset(const Eigen::Vector2f &offset)
{
    if (!offset.allFinite())
        throw std::invalid_argument("collider.offset must be finite");
    if (this->offset == offset)
        return;
    this->offset = offset;

    if (getGameObject() != nullptr)
    {
        scheduleSync();
    }
}

Eigen::Vector2f Collider::getWorldPosition() const
{
    GameObject *gameObject = getGameObject();
    const float rotatedOffsetX = offset.x() * gameObject->getCosRotation() - offset.y() * gameObject->getSinRotation();
    const float rotatedOffsetY = offset.x() * gameObject->getSinRotation() + offset.y() * gameObject->getCosRotation();

    return gameObject->getPosition() + Eigen::Vector2f(rotatedOffsetX, rotatedOffsetY);
}

void Collider::setIsTrigger(const bool isTrigger)
{
    if (getIsTrigger() == isTrigger)
        return;
    if (isTrigger)
        flags = static_cast<ColliderFlags>(flags | IS_TRIGGER);
    else
        flags = static_cast<ColliderFlags>(flags & ~IS_TRIGGER);
    invalidateResponse();
}

void Collider::scheduleSync()
{
    PLATFORMATOR_WORK_ADD(colliderSyncRequests, 1);
    dirty |= Geometry;
    if (getIsRegisteredInPhysicsManager())
        platformator_detail::RuntimeAccess::physicsManager()->queueColliderSync(this);
}

void Collider::prepareSync()
{
    platformator_detail::RuntimeAccess::gameManager().synchronizeTransforms();
    prepareSyncFromCache();
}

void Collider::prepareSyncFromCache()
{
    const auto *object = getGameObject();
    cachedWorldPosition =
        object->worldPosition +
        Eigen::Vector2f(offset.x() * object->getCosRotation() - offset.y() * object->getSinRotation(),
                        offset.x() * object->getSinRotation() + offset.y() * object->getCosRotation());
    updateCollider();
    calculateGridCellRange();
    stateVersion++;
}

void Collider::triggerCollisionEnter(const Collision *collision, Collider *other, double timeDelta) const
{
    if (!getIsCallbackEligible())
    {
        return;
    }
    assert(!isRetired());
    if (ScriptComponent *scriptComponent = getGameObject()->getComponent<ScriptComponent>())
    {
        scriptComponent->dispatchCollisionEnter(collision, other, timeDelta);
    }
}

void Collider::triggerCollisionExit(Collider *other, double timeDelta) const
{
    if (!getIsCallbackEligible())
    {
        return;
    }
    assert(!isRetired());
    if (ScriptComponent *scriptComponent = getGameObject()->getComponent<ScriptComponent>())
    {
        scriptComponent->dispatchCollisionExit(other, timeDelta);
    }
}

void Collider::setCollisionGroup(const uint64_t collisionGroup)
{
    if (this->collisionGroup == collisionGroup)
        return;
    this->collisionGroup = collisionGroup;
    dirty |= Filter;
    if (getIsRegisteredInPhysicsManager())
        platformator_detail::RuntimeAccess::physicsManager()->queueColliderSync(this);
}

void Collider::setCollisionMask(const uint64_t collisionMask)
{
    if (this->collisionMask == collisionMask)
        return;
    this->collisionMask = collisionMask;
    dirty |= Filter;
    if (getIsRegisteredInPhysicsManager())
        platformator_detail::RuntimeAccess::physicsManager()->queueColliderSync(this);
}

void Collider::calculateGridCellRange()
{
    int minX = static_cast<int>(std::floor(xProjections.getMin()->getProjectedPosition() / GRID_CELL_SIZE));
    int maxX = static_cast<int>(std::floor(xProjections.getMax()->getProjectedPosition() / GRID_CELL_SIZE));
    int minY = static_cast<int>(std::floor(yProjections.getMin()->getProjectedPosition() / GRID_CELL_SIZE));
    int maxY = static_cast<int>(std::floor(yProjections.getMax()->getProjectedPosition() / GRID_CELL_SIZE));

    gridCellRangeCache = GridCellRange(minX, maxX, minY, maxY);
}

void Collider::invalidateResponse()
{
    dirty |= Response;
    if (getIsRegisteredInPhysicsManager())
        platformator_detail::RuntimeAccess::physicsManager()->queueColliderSync(this);
}
