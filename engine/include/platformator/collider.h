#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "platformator/collisiontypes.h"
#include "platformator/gameobject.h"
#include "platformator/gridtypes.h"

class Collider;
class Rigidbody;
class LocalSortArray;
class GridCell;
class SegmentedIntervalList;
class Collision;
class ColliderPair;
class Grid;
class AABB;

class BoundingRadiusProjection
{
  public:
    BoundingRadiusProjection() : collider(nullptr), projectedPosition(0.0f), isMaxima(false)
    {
    }
    BoundingRadiusProjection(Collider *collider, float projectedPosition, bool isEnd)
        : collider(collider), projectedPosition(projectedPosition), isMaxima(isEnd)
    {
    }
    ~BoundingRadiusProjection() = default;

    Collider *getCollider()
    {
        return collider;
    }
    float getProjectedPosition() const
    {
        return projectedPosition;
    }
    bool getIsMaxima() const
    {
        return isMaxima;
    }

    void setCollider(Collider *collider)
    {
        this->collider = collider;
    }
    void setProjectedPosition(float projectedPosition)
    {
        this->projectedPosition = projectedPosition;
    }
    void setIsMaxima(bool isMaxima)
    {
        this->isMaxima = isMaxima;
    }

    bool operator==(const BoundingRadiusProjection &other) const
    {
        return projectedPosition == other.projectedPosition && isMaxima == other.isMaxima;
    }
    bool operator!=(const BoundingRadiusProjection &other) const
    {
        return !(*this == other);
    }
    bool operator<(const BoundingRadiusProjection &other) const
    {
        return projectedPosition < other.projectedPosition ||
               (projectedPosition == other.projectedPosition && !isMaxima && other.isMaxima);
    }
    bool operator>(const BoundingRadiusProjection &other) const
    {
        return projectedPosition > other.projectedPosition ||
               (projectedPosition == other.projectedPosition && isMaxima && !other.isMaxima);
    }
    bool operator<=(const BoundingRadiusProjection &other) const
    {
        return *this < other || *this == other;
    }
    bool operator>=(const BoundingRadiusProjection &other) const
    {
        return *this > other || *this == other;
    }

  private:
    Collider *collider;
    float projectedPosition;
    bool isMaxima;
};

class BoundingRadiusProjectionProxy
{
  public:
    static constexpr uint8_t INVALID_CHUNK_INDEX = 0xFFu;

    BoundingRadiusProjectionProxy(BoundingRadiusProjection *projection, LocalSortArray *chunk)
        : projection(projection), chunk(chunk), cachedProjectedPosition(projection->getProjectedPosition()),
          chunkIndex(INVALID_CHUNK_INDEX), isMaxima(projection->getIsMaxima())
    {
    }
    ~BoundingRadiusProjectionProxy() = default;

    BoundingRadiusProjection *getProjection() const
    {
        return projection;
    }
    LocalSortArray *getChunk() const
    {
        return chunk;
    }
    size_t getChunkIndex() const
    {
        return chunkIndex == INVALID_CHUNK_INDEX ? SIZE_MAX : static_cast<size_t>(chunkIndex);
    }

    void setChunk(LocalSortArray *chunk)
    {
        this->chunk = chunk;
        if (chunk == nullptr)
        {
            chunkIndex = INVALID_CHUNK_INDEX;
        }
    }
    void setChunkIndex(size_t chunkIndex)
    {
        this->chunkIndex = chunkIndex == SIZE_MAX ? INVALID_CHUNK_INDEX : static_cast<uint8_t>(chunkIndex);
    }
    void updateCachedProjectedPosition()
    {
        cachedProjectedPosition = projection->getProjectedPosition();
    }

    Collider *getCollider()
    {
        return projection->getCollider();
    }
    float getProjectedPosition() const
    {
        return projection->getProjectedPosition();
    }
    bool getIsMaxima() const
    {
        return isMaxima;
    }
    float getCachedProjectedPosition() const
    {
        return cachedProjectedPosition;
    }

    bool operator==(const BoundingRadiusProjectionProxy &other) const
    {
        return cachedProjectedPosition == other.cachedProjectedPosition && isMaxima == other.isMaxima;
    }
    bool operator!=(const BoundingRadiusProjectionProxy &other) const
    {
        return !(*this == other);
    }
    bool operator<(const BoundingRadiusProjectionProxy &other) const
    {
        return cachedProjectedPosition < other.cachedProjectedPosition ||
               (cachedProjectedPosition == other.cachedProjectedPosition && !isMaxima && other.isMaxima);
    }
    bool operator>(const BoundingRadiusProjectionProxy &other) const
    {
        return cachedProjectedPosition > other.cachedProjectedPosition ||
               (cachedProjectedPosition == other.cachedProjectedPosition && isMaxima && !other.isMaxima);
    }
    bool operator<=(const BoundingRadiusProjectionProxy &other) const
    {
        return *this < other || *this == other;
    }
    bool operator>=(const BoundingRadiusProjectionProxy &other) const
    {
        return *this > other || *this == other;
    }

  private:
    BoundingRadiusProjection *projection;
    LocalSortArray *chunk;
    float cachedProjectedPosition;
    uint8_t chunkIndex;
    bool isMaxima;
};

class BoundingRadiusProjectionAxis
{
  public:
    BoundingRadiusProjectionAxis(Collider *collider, float min, float max)
        : min(BoundingRadiusProjection(collider, min, false)),
          max(BoundingRadiusProjection(collider, max, true))
    {
    }
    ~BoundingRadiusProjectionAxis() = default;

    BoundingRadiusProjection *getMin()
    {
        return &min;
    }
    BoundingRadiusProjection *getMax()
    {
        return &max;
    }

    bool operator==(const BoundingRadiusProjectionAxis &other) const
    {
        return min == other.min && max == other.max;
    }
    bool operator!=(const BoundingRadiusProjectionAxis &other) const
    {
        return !(*this == other);
    }

  private:
    BoundingRadiusProjection min;
    BoundingRadiusProjection max;
};

struct BoundingRadiusProjectionAxisBinding
{
    BoundingRadiusProjectionProxy minProxy;
    BoundingRadiusProjectionProxy maxProxy;

    explicit BoundingRadiusProjectionAxisBinding(BoundingRadiusProjectionAxis *axis)
        : minProxy(axis->getMin(), nullptr), maxProxy(axis->getMax(), nullptr)
    {
    }
};

enum class ColliderType
{
    CircleCollider,
    BoxCollider
};

enum ColliderFlags : uint8_t
{
    IS_TRIGGER = 1 << 0,
    IS_REGISTERED_IN_GRID = 1 << 4,
    IS_REGISTERED_IN_PHYSICS_MANAGER = 1 << 5
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
    virtual Eigen::Vector2f projectOntoAxis(const Eigen::Vector2f &axis) const = 0;
    virtual std::vector<Eigen::Vector2f> getNormals(const Collider *other) const = 0;

    void triggerCollisionEnter(const Collision *collision, Collider *other, double timeDelta) const;
    void triggerCollisionExit(Collider *other, double timeDelta) const;

    BoundingRadiusProjectionAxis *getXProjections()
    {
        return &xProjections;
    }
    BoundingRadiusProjectionAxis *getYProjections()
    {
        return &yProjections;
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
    void calculateGridCellRange();
    void calculateGridCellRange(float cellSize);
    const GridCellRange &getGridCellRangeCache() const
    {
        return gridCellRangeCache;
    }

    bool getIsRegisteredInGrid() const
    {
        return (flags & IS_REGISTERED_IN_GRID) != 0;
    }
    void setIsRegisteredInGrid(bool isRegisteredInGrid)
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
    bool getIsRegisteredInPhysicsManager() const
    {
        return (flags & IS_REGISTERED_IN_PHYSICS_MANAGER) != 0;
    }
    void setIsRegisteredInPhysicsManager(bool isRegisteredInPhysicsManager)
    {
        if (isRegisteredInPhysicsManager)
        {
            flags = static_cast<ColliderFlags>(flags | IS_REGISTERED_IN_PHYSICS_MANAGER);
        }
        else
        {
            flags = static_cast<ColliderFlags>(flags & ~IS_REGISTERED_IN_PHYSICS_MANAGER);
        }
    }
    size_t getPhysicsManagerIndex() const
    {
        return physicsManagerIndex;
    }
    void setPhysicsManagerIndex(size_t physicsManagerIndex)
    {
        this->physicsManagerIndex = physicsManagerIndex;
    }

    virtual Edge getEdgeWithNormal(const Eigen::Vector2f &normal) const = 0;

  protected:
    uint64_t collisionGroup;
    uint64_t collisionMask;

    ColliderType colliderType;
    uint64_t stateVersion;
    Eigen::Vector2f offset;
    Eigen::Vector2f cachedWorldPosition = Eigen::Vector2f::Zero();

    BoundingRadiusProjectionAxis xProjections;
    BoundingRadiusProjectionAxis yProjections;
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
    uint8_t dirty = Geometry;
    bool desiredInGrid = false;
    bool fatBoundsValid = false;
    bool broadPhaseSyncNeeded = true;
    size_t gridQueueIndex = SIZE_MAX;
    static constexpr uint8_t MAX_CACHED_GRID_CELLS = 4;
    GridCell *cachedCells[MAX_CACHED_GRID_CELLS] = {};
    uint8_t cachedCellCount = 0;
    Rigidbody *attachedRigidbody = nullptr;
    std::vector<const ColliderPair *> adjacentPairs;
    void prepareSyncFromCache();
    void prepareSyncFromCache(float cellSize);
};

template <> struct ComponentTypeFor<Collider>
{
    static constexpr ComponentType value = ComponentType::COLLIDER;
};
