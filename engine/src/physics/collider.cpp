#include "platformator/collider.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "core/constants.h"
#include "core/gamemanager.h"
#include "physics/broadphase/localsortarray.h"
#include "physics/broadphase/gridcell.h"
#include "core/runtimeaccess.h"
#include "physics/broadphase/segmentedintervallist.h"
#include "platformator/rigidbody.h"
#include "platformator/scriptcomponent.h"
#include "platformator/collision.h"
#include "benchmark/workprobe.h"

Collider::Collider(GameObject *gameObject, ComponentType type, ColliderType colliderType)
    : Component(gameObject, type), collisionGroup(1), collisionMask(1), colliderType(colliderType), stateVersion(0),
      offset(Eigen::Vector2f::Zero()), xProjections(this, 0.0f, 0.0f), yProjections(this, 0.0f, 0.0f),
      physicsManagerIndex(SIZE_MAX), gridCellRange(), flags(static_cast<ColliderFlags>(0)), gridCellRangeCache(),
      attachedRigidbody(gameObject ? gameObject->getComponent<Rigidbody>() : nullptr)
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
        invalidateFatBounds();
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
    if (offset.x() == 0.0f && offset.y() == 0.0f)
    {
        cachedWorldPosition = object->worldPosition;
    }
    else
    {
        const float c = object->getCosRotation();
        const float s = object->getSinRotation();
        cachedWorldPosition =
            object->worldPosition + Eigen::Vector2f(offset.x() * c - offset.y() * s, offset.x() * s + offset.y() * c);
    }
    updateCollider();
    stateVersion++;

    const float fatMinX = xProjections.getMin()->getProjectedPosition();
    const float fatMaxX = xProjections.getMax()->getProjectedPosition();
    const float fatMinY = yProjections.getMin()->getProjectedPosition();
    const float fatMaxY = yProjections.getMax()->getProjectedPosition();

    if (fatBoundsValid && getIsRegisteredInGrid() && tightMinX >= fatMinX && tightMaxX <= fatMaxX &&
        tightMinY >= fatMinY && tightMaxY <= fatMaxY)
    {
        broadPhaseSyncNeeded = false;
        return;
    }

    float newMinX = tightMinX;
    float newMaxX = tightMaxX;
    float newMinY = tightMinY;
    float newMaxY = tightMaxY;
    if (const auto *rb = attachedRigidbody; rb != nullptr && rb->getBodyType() != BodyType::STATIC)
    {
        newMinX -= FAT_AABB_MARGIN;
        newMaxX += FAT_AABB_MARGIN;
        newMinY -= FAT_AABB_MARGIN;
        newMaxY += FAT_AABB_MARGIN;

        const Eigen::Vector2f &vel = rb->getVelocity();
        const float leadScale = static_cast<float>(FIXED_TIME_STEP) * FAT_AABB_VELOCITY_LEAD_STEPS;
        const float leadX = std::clamp(vel.x() * leadScale, -FAT_AABB_MAX_VELOCITY_LEAD, FAT_AABB_MAX_VELOCITY_LEAD);
        const float leadY = std::clamp(vel.y() * leadScale, -FAT_AABB_MAX_VELOCITY_LEAD, FAT_AABB_MAX_VELOCITY_LEAD);
        if (leadX > 0.0f)
            newMaxX += leadX;
        else
            newMinX += leadX;
        if (leadY > 0.0f)
            newMaxY += leadY;
        else
            newMinY += leadY;
    }

    xProjections.getMin()->setProjectedPosition(newMinX);
    xProjections.getMax()->setProjectedPosition(newMaxX);
    yProjections.getMin()->setProjectedPosition(newMinY);
    yProjections.getMax()->setProjectedPosition(newMaxY);
    fatBoundsValid = true;
    broadPhaseSyncNeeded = true;
    calculateGridCellRange();
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
    const auto cellIndex = [](float projection) {
        const double index = std::floor(projection / GRID_CELL_SIZE);
        // GridCellRange reserves both integer limits and steps past inclusive endpoints.
        if (!(index > std::numeric_limits<int>::min() && index < std::numeric_limits<int>::max()))
            throw std::out_of_range("Collider projection is outside the supported grid range");
        return static_cast<int>(index);
    };

    const int minX = cellIndex(xProjections.getMin()->getProjectedPosition());
    const int maxX = cellIndex(xProjections.getMax()->getProjectedPosition());
    const int minY = cellIndex(yProjections.getMin()->getProjectedPosition());
    const int maxY = cellIndex(yProjections.getMax()->getProjectedPosition());

    gridCellRangeCache = GridCellRange(minX, maxX, minY, maxY);
}

void Collider::invalidateResponse()
{
    dirty |= Response;
    if (getIsRegisteredInPhysicsManager())
        platformator_detail::RuntimeAccess::physicsManager()->queueColliderSync(this);
}
