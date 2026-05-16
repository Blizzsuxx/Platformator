#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "platformator/collisiontypes.h"
#include "platformator/gameobject.h"
#include "platformator/gridtypes.h"

class Collider;
class LocalSortArray;
class GridCell;
class SegmentedIntervalList;
class Collision;
class Grid;

class BoundingRadiusProjection
{
public:
	BoundingRadiusProjection();
	BoundingRadiusProjection(Collider *collider, float projectedPosition, bool isEnd);
	~BoundingRadiusProjection();

	Collider *getCollider();
	float getProjectedPosition() const;
	bool getIsMaxima() const;

	void setCollider(Collider *collider);
	void setProjectedPosition(float projectedPosition);
	void setIsMaxima(bool isMaxima);

	bool operator==(const BoundingRadiusProjection &other) const;
	bool operator!=(const BoundingRadiusProjection &other) const;
	bool operator<(const BoundingRadiusProjection &other) const;
	bool operator>(const BoundingRadiusProjection &other) const;
	bool operator<=(const BoundingRadiusProjection &other) const;
	bool operator>=(const BoundingRadiusProjection &other) const;

private:
	Collider *collider;
	float projectedPosition;
	bool isMaxima;
};

class BoundingRadiusProjectionProxy
{
public:
	BoundingRadiusProjectionProxy(BoundingRadiusProjection *projection, LocalSortArray *chunk);
	~BoundingRadiusProjectionProxy();

	BoundingRadiusProjection *getProjection() const;
	LocalSortArray *getChunk() const;
	size_t getChunkIndex() const;

	void setBoundingProjection(BoundingRadiusProjection *projection);
	void setChunk(LocalSortArray *chunk);
	void setChunkIndex(size_t chunkIndex);
	void updateCachedProjectedPosition();

	Collider *getCollider();
	float getProjectedPosition() const;
	bool getIsMaxima() const;
	float getCachedProjectedPosition() const;

	bool operator==(const BoundingRadiusProjectionProxy &other) const;
	bool operator!=(const BoundingRadiusProjectionProxy &other) const;
	bool operator<(const BoundingRadiusProjectionProxy &other) const;
	bool operator>(const BoundingRadiusProjectionProxy &other) const;
	bool operator<=(const BoundingRadiusProjectionProxy &other) const;
	bool operator>=(const BoundingRadiusProjectionProxy &other) const;

private:
	BoundingRadiusProjection *projection;
	LocalSortArray *chunk;
	size_t chunkIndex;
	float cachedProjectedPosition;
};

class BoundingRadiusProjectionAxis
{
public:
	BoundingRadiusProjectionAxis(Collider *collider, float min, float max);
	~BoundingRadiusProjectionAxis();

	BoundingRadiusProjection *getMin();
	BoundingRadiusProjection *getMax();

	bool operator==(const BoundingRadiusProjectionAxis &other) const;
	bool operator!=(const BoundingRadiusProjectionAxis &other) const;

private:
	BoundingRadiusProjection min;
	BoundingRadiusProjection max;
};

struct BoundingRadiusProjectionAxisBinding
{
	BoundingRadiusProjectionProxy minProxy;
	BoundingRadiusProjectionProxy maxProxy;

	explicit BoundingRadiusProjectionAxisBinding(BoundingRadiusProjectionAxis *axis);
	void bind(BoundingRadiusProjectionAxis *axis);
};

enum class ColliderType
{
	CircleCollider,
	BoxCollider
};

enum ColliderFlags : uint8_t
{
	IS_TRIGGER = 1 << 0,
	IS_QUEUED_FOR_ADD = 1 << 1,
	IS_QUEUED_FOR_SYNC = 1 << 2,
	IS_QUEUED_FOR_REMOVE = 1 << 3,
	IS_REGISTERED_IN_GRID = 1 << 4
};

class Collider : public Component
{
	friend class GameObject;
	friend class PhysicsManager;
	friend class Grid;
	friend class GridCell;

public:
	Collider(GameObject *gameObject, ComponentType type);
	Collider(ComponentType type);
	~Collider();

	virtual ColliderType getColliderType() const = 0;
	virtual Eigen::Vector2f projectOntoAxis(const Eigen::Vector2f &axis) const = 0;
	virtual std::vector<Eigen::Vector2f> getNormals(const Collider *other) const = 0;

	void triggerCollisionEnter(const Collision *collision, Collider *other, double timeDelta) const;
	void triggerCollisionExit(Collider *other, double timeDelta) const;
	void triggerCollisionStay(const Collision *collision, Collider *other, double timeDelta) const;

	BoundingRadiusProjectionAxis *getXProjections();
	BoundingRadiusProjectionAxis *getYProjections();

	uint64_t getCollisionGroup() const;
	void setCollisionGroup(uint64_t collisionGroup);

	uint64_t getCollisionMask() const;
	void setCollisionMask(uint64_t collisionMask);

	bool getIsTrigger() const;
	void setIsTrigger(bool isTrigger);

	const Eigen::Vector2f &getOffset() const;
	void setOffset(const Eigen::Vector2f &offset);
	Eigen::Vector2f getWorldPosition() const;

	bool getIsQueuedForAdd() const;
	void markQueuedForAdd();
	void clearQueuedForAdd();

	bool getIsQueuedForSync() const;
	void scheduleSync();
	void removeSync();

	bool getIsQueuedForRemove() const;
	void markQueuedForRemove();
	void clearQueuedForRemove();

	bool getIsQueuedForAnything() const;

	void prepareSync();
	uint64_t getStateVersion() const;

	const GridCellRange &getGridCellRange() const;
	void calculateGridCellRange();
	const GridCellRange &getGridCellRangeCache() const;
	bool hasValidGridCellRange() const;

	bool getIsRegisteredInGrid() const;
	void setIsRegisteredInGrid(bool isRegisteredInGrid);

	virtual Edge getEdgeWithNormal(const Eigen::Vector2f &normal) const = 0;

protected:
	uint64_t collisionGroup;
	uint64_t collisionMask;

	uint64_t stateVersion;
	Eigen::Vector2f offset;

	BoundingRadiusProjectionAxis xProjections;
	BoundingRadiusProjectionAxis yProjections;

	GridCellRange gridCellRange;
	ColliderFlags flags;
	GridCellRange gridCellRangeCache;

	void updateGridCellRange();

	virtual void updateCollider() = 0;
};

template <>
struct ComponentTypeFor<Collider>
{
	static constexpr ComponentType value = ComponentType::COLLIDER;
};