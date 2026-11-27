#pragma once

#include <cstdint>
#include "platformator/collider.h"
#include "platformator/collision.h"

class ColliderPair
{
    friend class Grid;
    friend class PhysicsManager;

  public:
    static constexpr uint32_t INVALID_INDEX = UINT32_MAX;

    ColliderPair(Collider *a, Collider *b);
    ~ColliderPair();
    ColliderPair(const ColliderPair &) = delete;
    ColliderPair &operator=(const ColliderPair &) = delete;

    Collision *getCollision() noexcept
    {
        return hasCollision ? &collisionStorage : nullptr;
    }
    const Collision *getCollision() const noexcept
    {
        return hasCollision ? &collisionStorage : nullptr;
    }
    Collision *getOrCreateCollisionNoEvent();

    void updateCachedCollisionVersions() noexcept
    {
        objectAStateVersion = objectA ? static_cast<uint32_t>(objectA->getStateVersion()) : UINT32_MAX;
        objectBStateVersion = objectB ? static_cast<uint32_t>(objectB->getStateVersion()) : UINT32_MAX;
    }
    bool shouldUpdate() const noexcept
    {
        const uint32_t currentObjectAVersion = objectA ? static_cast<uint32_t>(objectA->getStateVersion()) : UINT32_MAX;
        const uint32_t currentObjectBVersion = objectB ? static_cast<uint32_t>(objectB->getStateVersion()) : UINT32_MAX;
        return currentObjectAVersion != objectAStateVersion || currentObjectBVersion != objectBStateVersion;
    }
    void clearCollisionNoEvent() noexcept;

    bool getIsQueuedForNarrowPhase() const noexcept
    {
        return narrowPhaseQueueIndex != INVALID_INDEX;
    }
    size_t getNarrowPhaseQueueIndex() const noexcept
    {
        return narrowPhaseQueueIndex == INVALID_INDEX ? SIZE_MAX : static_cast<size_t>(narrowPhaseQueueIndex);
    }
    void setNarrowPhaseQueueIndex(size_t index) noexcept
    {
        narrowPhaseQueueIndex = index == SIZE_MAX ? INVALID_INDEX : static_cast<uint32_t>(index);
    }
    size_t getAdjacencyIndexA() const noexcept
    {
        return adjacencyIndexA == INVALID_INDEX ? SIZE_MAX : static_cast<size_t>(adjacencyIndexA);
    }
    void setAdjacencyIndexA(size_t index) noexcept
    {
        adjacencyIndexA = index == SIZE_MAX ? INVALID_INDEX : static_cast<uint32_t>(index);
    }
    size_t getAdjacencyIndexB() const noexcept
    {
        return adjacencyIndexB == INVALID_INDEX ? SIZE_MAX : static_cast<size_t>(adjacencyIndexB);
    }
    void setAdjacencyIndexB(size_t index) noexcept
    {
        adjacencyIndexB = index == SIZE_MAX ? INVALID_INDEX : static_cast<uint32_t>(index);
    }

    Collider *getObjectA() const noexcept
    {
        return objectA;
    }
    Collider *getObjectB() const noexcept
    {
        return objectB;
    }

    bool isInSolver() const noexcept
    {
        return solverIslandId != -1 || kinematicSolverIndex != INVALID_INDEX;
    }

    int getWitnessCount() const noexcept
    {
        return witnessCount;
    }
    void addToWitnessCount(int delta) noexcept
    {
        witnessCount += delta;
    }

  private:
    Collider *objectA;
    Collider *objectB;

    Collision collisionStorage;
    uint32_t objectAStateVersion;
    uint32_t objectBStateVersion;
    uint32_t narrowPhaseQueueIndex;
    uint32_t adjacencyIndexA;
    uint32_t adjacencyIndexB;
    int32_t witnessCount;
    uint32_t contactIndex = INVALID_INDEX;
    int32_t solverIslandId = -1;
    uint32_t solverIslandContactIndex = INVALID_INDEX;
    uint32_t kinematicSolverIndex = INVALID_INDEX;
    uint32_t solverColorSlot = UINT32_MAX;
    uint8_t solverColor = 0xFFu;
    bool hasCollision = false;
    bool resultHadContact = false;
#if PLATFORMATOR_ENABLE_WORK_PROBES
    bool resultTestedGeometry = false;
#endif
    // Exact recipients of this pair's applied-or-queued support credit, not current component caches.
    Rigidbody *contributedSupportBodyA = nullptr;
    Rigidbody *contributedSupportBodyB = nullptr;
    // Cached endpoints for island contacts (eliminates BoxCollider dereferences during solver packing).
    Rigidbody *cachedRbA = nullptr;
    Rigidbody *cachedRbB = nullptr;
};
