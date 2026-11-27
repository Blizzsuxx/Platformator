#include "platformator/collider.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "benchmark/workprobe.h"
#include "core/constants.h"
#include "core/gamemanager.h"
#include "core/inputvalidation.h"
#include "core/runtimeaccess.h"
#include "platformator/rigidbody.h"
#include "platformator/scriptcomponent.h"

Collider::Collider(GameObject *gameObject, ComponentType type, ColliderType colliderType)
    : Component(gameObject, type), stateVersion(0), colliderType(colliderType), flags(static_cast<ColliderFlags>(0)),
      offset(Eigen::Vector2f::Zero()), collisionGroup(1), collisionMask(1), physicsManagerIndex(SIZE_MAX),
      gridCellRange(), gridCellRangeCache(),
      attachedRigidbody(gameObject ? gameObject->getComponent<Rigidbody>() : nullptr)
{
    setOwnerActive(gameObject != nullptr && gameObject->getActive());
    setHasNonStaticRigidbody(attachedRigidbody != nullptr && attachedRigidbody->getBodyType() != BodyType::STATIC);
}

Collider::Collider(ComponentType type, ColliderType colliderType)
    : Component(type), stateVersion(0), colliderType(colliderType), flags(static_cast<ColliderFlags>(0)),
      offset(Eigen::Vector2f::Zero()), collisionGroup(1), collisionMask(1), physicsManagerIndex(SIZE_MAX),
      gridCellRange(), gridCellRangeCache()
{
}

Collider::~Collider() = default;

const Eigen::Vector2f &Collider::getOffset() const
{
    return offset;
}

void Collider::setOffset(const Eigen::Vector2f &offset)
{
    platformator_detail::finite(offset, "collider.offset");
    if (this->offset == offset)
        return;
    this->offset = offset;

    if (getGameObject() != nullptr)
    {
        invalidateFatBounds();
        scheduleSync();
    }
}

void Collider::notifyShapeChanged()
{
    if (getGameObject() == nullptr)
    {
        return;
    }

    if (attachedRigidbody != nullptr)
    {
        attachedRigidbody->refreshMomentOfInertiaCache();
    }

    invalidateFatBounds();
    scheduleSync();
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
    const auto *physics = platformator_detail::RuntimeAccess::physicsManager();
    prepareSyncFromCache(physics ? physics->getGridCellSize() : GRID_CELL_SIZE);
}

void Collider::prepareSyncFromCache(float cellSize)
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
        const float minExtent = std::min(tightMaxX - tightMinX, tightMaxY - tightMinY);
        const float maxExtent = std::max(tightMaxX - tightMinX, tightMaxY - tightMinY);
        const float margin = std::clamp(minExtent * 0.125f, 0.1f, FAT_AABB_MARGIN);
        newMinX -= margin;
        newMaxX += margin;
        newMinY -= margin;
        newMaxY += margin;

        const Eigen::Vector2f &vel = rb->getVelocity();
        const float leadScale = static_cast<float>(FIXED_TIME_STEP) * FAT_AABB_VELOCITY_LEAD_STEPS;
        const float maxLead = std::clamp(maxExtent * 0.75f, 0.5f, FAT_AABB_MAX_VELOCITY_LEAD);
        const float leadX = std::clamp(vel.x() * leadScale, -maxLead, maxLead);
        const float leadY = std::clamp(vel.y() * leadScale, -maxLead, maxLead);
        if (leadX > 0.0f)
            newMaxX += leadX;
        else
            newMinX += leadX;
        if (leadY > 0.0f)
            newMaxY += leadY;
        else
            newMinY += leadY;
    }

    fatMinX = newMinX;
    fatMaxX = newMaxX;
    fatMinY = newMinY;
    fatMaxY = newMaxY;
    fatBoundsValid = true;
    broadPhaseSyncNeeded = true;
    calculateGridCellRange(cellSize);
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

void Collider::calculateGridCellRange(float cellSize)
{
    const auto cellIndex = [cellSize](float projection) {
        const double index = std::floor(projection / static_cast<double>(cellSize));
        // GridCellRange reserves both integer limits and steps past inclusive endpoints.
        if (!(index > std::numeric_limits<int>::min() && index < std::numeric_limits<int>::max()))
            throw std::out_of_range("Collider projection is outside the supported grid range");
        return static_cast<int>(index);
    };

    gridCellRangeCache = GridCellRange(cellIndex(fatMinX), cellIndex(fatMaxX), cellIndex(fatMinY), cellIndex(fatMaxY));
}

void Collider::invalidateResponse()
{
    dirty |= Response;
    if (getIsRegisteredInPhysicsManager())
        platformator_detail::RuntimeAccess::physicsManager()->queueColliderSync(this);
}
