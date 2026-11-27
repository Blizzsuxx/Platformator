#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "platformator/collisiontypes.h"
#include "platformator/gameobject.h"
#include "platformator/gridtypes.h"

class Collider;
class Rigidbody;
class GridCell;
class Collision;
class ColliderPair;
class Grid;

enum class ColliderType
{
    CircleCollider,
    BoxCollider
};

enum ColliderFlags : uint8_t
{
    IS_TRIGGER = 1 << 0,
    IS_REGISTERED_IN_GRID = 1 << 1,
    IS_REGISTERED_IN_PHYSICS_MANAGER = 1 << 2
};

class Collider : public Component
{
    friend class GameObject;
    friend class PhysicsManager;
    friend class Grid;
    friend class GridCell;

  public:
    Collider(GameObject *gameObject, ComponentType type, ColliderType colliderType);
    Collider(ComponentType type, ColliderType colliderType);
    ~Collider();

    ColliderType getColliderType() const
    {
        return colliderType;
    }
    Rigidbody *getAttachedRigidbody() const noexcept
    {
        return attachedRigidbody;
    }

    void triggerCollisionEnter(const Collision *collision, Collider *other, double timeDelta) const;
    void triggerCollisionExit(Collider *other, double timeDelta) const;

    float getFatMinX() const noexcept
    {
        return fatMinX;
    }
    float getFatMaxX() const noexcept
    {
        return fatMaxX;
    }
    float getFatMinY() const noexcept
    {
        return fatMinY;
    }
    float getFatMaxY() const noexcept
    {
        return fatMaxY;
    }
    float getTightMinX() const noexcept
    {
        return tightMinX;
    }
    float getTightMaxX() const noexcept
    {
        return tightMaxX;
    }
    float getTightMinY() const noexcept
    {
        return tightMinY;
    }
    float getTightMaxY() const noexcept
    {
        return tightMaxY;
    }
    bool overlapsTightBounds(float minX, float maxX, float minY, float maxY) const noexcept
    {
        return tightMaxX >= minX && tightMinX <= maxX && tightMaxY >= minY && tightMinY <= maxY;
    }
    bool overlapsTightBounds(const Collider &other) const noexcept
    {
        return overlapsTightBounds(other.tightMinX, other.tightMaxX, other.tightMinY, other.tightMaxY);
    }
    bool overlapsFatBounds(const Collider &other) const noexcept
    {
        return fatMaxX >= other.fatMinX && fatMinX <= other.fatMaxX && fatMaxY >= other.fatMinY &&
               fatMinY <= other.fatMaxY;
    }

    uint64_t getCollisionGroup() const
    {
        return collisionGroup;
    }
    void setCollisionGroup(uint64_t collisionGroup);

    uint64_t getCollisionMask() const
    {
        return collisionMask;
    }
    void setCollisionMask(uint64_t collisionMask);

    bool getIsTrigger() const
    {
        return (flags & IS_TRIGGER) != 0;
    }
    void setIsTrigger(bool isTrigger);

    const Eigen::Vector2f &getOffset() const;
    void setOffset(const Eigen::Vector2f &offset);
    Eigen::Vector2f getWorldPosition() const;
    const Eigen::Vector2f &getCachedWorldPosition() const
    {
        return cachedWorldPosition;
    }

    bool getIsQueuedForSync() const
    {
        return gridQueueIndex != SIZE_MAX && desiredInGrid && dirty;
    }
    void scheduleSync();
    void invalidateResponse();

    bool getIsQueuedForRemove() const
    {
        return gridQueueIndex != SIZE_MAX && !desiredInGrid;
    }

    void prepareSync();
    uint64_t getStateVersion() const
    {
        return stateVersion;
    }

    const GridCellRange &getGridCellRange() const
    {
        return gridCellRange;
    }
    void calculateGridCellRange(float cellSize);
    const GridCellRange &getGridCellRangeCache() const
    {
        return gridCellRangeCache;
    }

    bool getIsRegisteredInGrid() const
    {
        return (flags & IS_REGISTERED_IN_GRID) != 0;
    }
    bool getIsRegisteredInPhysicsManager() const
    {
        return (flags & IS_REGISTERED_IN_PHYSICS_MANAGER) != 0;
    }
    size_t getPhysicsManagerIndex() const
    {
        return physicsManagerIndex;
    }

  protected:
    uint64_t collisionGroup;
    uint64_t collisionMask;

    ColliderType colliderType;
    uint64_t stateVersion;
    Eigen::Vector2f offset;
    Eigen::Vector2f cachedWorldPosition = Eigen::Vector2f::Zero();

    float fatMinX = 0.0f;
    float fatMaxX = 0.0f;
    float fatMinY = 0.0f;
    float fatMaxY = 0.0f;
    float tightMinX = 0.0f;
    float tightMaxX = 0.0f;
    float tightMinY = 0.0f;
    float tightMaxY = 0.0f;
    size_t physicsManagerIndex;

    GridCellRange gridCellRange;
    ColliderFlags flags;
    GridCellRange gridCellRangeCache;

    void setTightBounds(float minX, float maxX, float minY, float maxY) noexcept
    {
        tightMinX = minX;
        tightMaxX = maxX;
        tightMinY = minY;
        tightMaxY = maxY;
    }

    void invalidateFatBounds() noexcept
    {
        fatBoundsValid = false;
        broadPhaseSyncNeeded = true;
    }

    void notifyShapeChanged();

    void updateGridCellRange()
    {
        gridCellRange = gridCellRangeCache;
    }

    virtual void updateCollider() = 0;

  private:
    enum Dirty : uint8_t
    {
        Geometry = 1,
        Filter = 2,
        Response = 4
    };

    void setIsRegisteredInGrid(bool isRegisteredInGrid) noexcept
    {
        flags = static_cast<ColliderFlags>(isRegisteredInGrid ? (flags | IS_REGISTERED_IN_GRID)
                                                              : (flags & ~IS_REGISTERED_IN_GRID));
    }
    void setIsRegisteredInPhysicsManager(bool isRegisteredInPhysicsManager) noexcept
    {
        flags = static_cast<ColliderFlags>(isRegisteredInPhysicsManager ? (flags | IS_REGISTERED_IN_PHYSICS_MANAGER)
                                                                        : (flags & ~IS_REGISTERED_IN_PHYSICS_MANAGER));
    }
    void setPhysicsManagerIndex(size_t index) noexcept
    {
        physicsManagerIndex = index;
    }

    uint8_t dirty = Geometry;
    bool desiredInGrid = false;
    bool fatBoundsValid = false;
    bool broadPhaseSyncNeeded = true;
    size_t gridQueueIndex = SIZE_MAX;
    static constexpr uint8_t MAX_CACHED_GRID_CELLS = 4;
    GridCell *cachedCells[MAX_CACHED_GRID_CELLS] = {};
    uint8_t cachedCellCount = 0;
    Rigidbody *attachedRigidbody = nullptr;
    std::vector<ColliderPair *> adjacentPairs;
    void prepareSyncFromCache();
    void prepareSyncFromCache(float cellSize);
};

template <> struct ComponentTypeFor<Collider>
{
    static constexpr ComponentType value = ComponentType::COLLIDER;
};
