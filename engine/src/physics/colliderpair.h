#pragma once

#include <cstdint>
#include "platformator/collider.h"
#include "platformator/collision.h"

class ColliderPair
{
    friend class Grid;
    friend class PhysicsManager;

  public:
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
        objectAStateVersion = objectA ? objectA->getStateVersion() : static_cast<uint64_t>(-1);
        objectBStateVersion = objectB ? objectB->getStateVersion() : static_cast<uint64_t>(-1);
    }
    bool shouldUpdate() const noexcept
    {
        const uint64_t currentObjectAVersion = objectA ? objectA->getStateVersion() : static_cast<uint64_t>(-1);
        const uint64_t currentObjectBVersion = objectB ? objectB->getStateVersion() : static_cast<uint64_t>(-1);
        return currentObjectAVersion != objectAStateVersion || currentObjectBVersion != objectBStateVersion;
    }
    void clearCollisionNoEvent() noexcept;

    bool getIsQueuedForNarrowPhase() const noexcept
    {
        return narrowPhaseQueueIndex != SIZE_MAX;
    }
    size_t getNarrowPhaseQueueIndex() const noexcept
    {
        return narrowPhaseQueueIndex;
    }
    void setNarrowPhaseQueueIndex(size_t index) noexcept
    {
        narrowPhaseQueueIndex = index;
    }
    size_t getAdjacencyIndexA() const noexcept
    {
        return adjacencyIndexA;
    }
    void setAdjacencyIndexA(size_t index) noexcept
    {
        adjacencyIndexA = index;
    }
    size_t getAdjacencyIndexB() const noexcept
    {
        return adjacencyIndexB;
    }
    void setAdjacencyIndexB(size_t index) noexcept
    {
        adjacencyIndexB = index;
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
        return solverIslandId != -1 || kinematicSolverIndex != SIZE_MAX;
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
    uint64_t objectAStateVersion;
    uint64_t objectBStateVersion;
    size_t narrowPhaseQueueIndex;
    size_t adjacencyIndexA;
    size_t adjacencyIndexB;
    int witnessCount;
    size_t contactIndex = SIZE_MAX;
    int32_t solverIslandId = -1;
    size_t solverIslandContactIndex = SIZE_MAX;
    size_t kinematicSolverIndex = SIZE_MAX;
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
};
