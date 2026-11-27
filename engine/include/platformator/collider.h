#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "platformator/collisiontypes.h"
#include "platformator/gameobject.h"
#include "platformator/gridtypes.h"

class Rigidbody;
class GridCell;
class Collision;
class ColliderPair;
class Grid;

enum class ColliderType : uint8_t
{
    CircleCollider,
    BoxCollider
};

enum ColliderFlags : uint8_t
{
    IS_TRIGGER = 1 << 0,
    IS_REGISTERED_IN_GRID = 1 << 1,
    IS_REGISTERED_IN_PHYSICS_MANAGER = 1 << 2,
    IS_OWNER_ACTIVE = 1 << 3,
    HAS_NONSTATIC_RIGIDBODY = 1 << 4
};

class Collider : public Component
{
    friend class GameObject;
    friend class PhysicsManager;
    friend class Rigidbody;
    friend class Grid;

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
        return gridQueueIndex != INVALID_INDEX && desiredInGrid && dirty;
    }
    void scheduleSync();
    void invalidateResponse();

    bool getIsQueuedForRemove() const
    {
        return gridQueueIndex != INVALID_INDEX && !desiredInGrid;
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
    bool getIsOwnerActive() const noexcept
    {
        return (flags & IS_OWNER_ACTIVE) != 0;
    }
    bool hasNonStaticRigidbody() const noexcept
    {
        return (flags & HAS_NONSTATIC_RIGIDBODY) != 0;
    }
    size_t getPhysicsManagerIndex() const
    {
        return physicsManagerIndex == INVALID_INDEX ? SIZE_MAX : static_cast<size_t>(physicsManagerIndex);
    }

  protected:
    static constexpr uint32_t INVALID_INDEX = UINT32_MAX;

    // Fills bytes 60..63 after Component so CL 1 starts aligned on cachedWorldPosition.
    uint32_t stateVersion;

    // Cache Line 1 (bytes 64..127): all hot broadphase & narrow-phase bounds/state.
    Eigen::Vector2f cachedWorldPosition = Eigen::Vector2f::Zero();
    float tightMinX = 0.0f;
    float tightMaxX = 0.0f;
    float tightMinY = 0.0f;
    float tightMaxY = 0.0f;
    float fatMinX = 0.0f;
    float fatMaxX = 0.0f;
    float fatMinY = 0.0f;
    float fatMaxY = 0.0f;
    Rigidbody *attachedRigidbody = nullptr;
    uint32_t gridQueueIndex = INVALID_INDEX;
    ColliderType colliderType;
    ColliderFlags flags;
    uint8_t dirty = Geometry;
    bool desiredInGrid = false;
    bool fatBoundsValid = false;
    bool broadPhaseSyncNeeded = true;
    uint8_t cachedCellCount = 0;

    // Cache Line 2+ (bytes 128+): adjacency, cell cache, and cold filter/grid/offset metadata.
    std::vector<ColliderPair *> adjacentPairs;
    static constexpr uint8_t MAX_CACHED_GRID_CELLS = 4;
    GridCell *cachedCells[MAX_CACHED_GRID_CELLS] = {};
    uint64_t collisionGroup;
    uint64_t collisionMask;
    Eigen::Vector2f offset;
    uint32_t physicsManagerIndex;
    GridCellRange gridCellRange;
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
    void setOwnerActive(bool ownerActive) noexcept
    {
        flags = static_cast<ColliderFlags>(ownerActive ? (flags | IS_OWNER_ACTIVE) : (flags & ~IS_OWNER_ACTIVE));
    }
    void setHasNonStaticRigidbody(bool hasNonStatic) noexcept
    {
        flags = static_cast<ColliderFlags>(hasNonStatic ? (flags | HAS_NONSTATIC_RIGIDBODY)
                                                        : (flags & ~HAS_NONSTATIC_RIGIDBODY));
    }
    void setPhysicsManagerIndex(size_t index) noexcept
    {
        physicsManagerIndex = index == SIZE_MAX ? INVALID_INDEX : static_cast<uint32_t>(index);
    }

    void prepareSyncFromCache();
    void prepareSyncFromCache(float cellSize);
};

template <> struct ComponentTypeFor<Collider>
{
    static constexpr ComponentType value = ComponentType::COLLIDER;
};
