#pragma once

#include <cstdint>
#include "platformator/collider.h"
#include "physics/collision.h"

class ColliderPair
{
    friend class Grid;
    friend class PhysicsManager;

  public:
    ColliderPair(Collider *a, Collider *b);
    ~ColliderPair();
    ColliderPair(const ColliderPair &) = delete;
    ColliderPair &operator=(const ColliderPair &) = delete;

    Collision *getCollision() const
    {
        return hasCollision ? &collisionStorage : nullptr;
    }
    Collision *getOrCreateCollisionNoEvent() const;

    void updateCachedCollisionVersions() const
    {
        objectAStateVersion = objectA ? objectA->getStateVersion() : static_cast<uint64_t>(-1);
        objectBStateVersion = objectB ? objectB->getStateVersion() : static_cast<uint64_t>(-1);
    }
    bool shouldUpdate() const
    {
        const uint64_t currentObjectAVersion = objectA ? objectA->getStateVersion() : static_cast<uint64_t>(-1);
        const uint64_t currentObjectBVersion = objectB ? objectB->getStateVersion() : static_cast<uint64_t>(-1);
        return currentObjectAVersion != objectAStateVersion || currentObjectBVersion != objectBStateVersion;
    }
    void clearCollisionNoEvent() const;

    bool getIsQueuedForNarrowPhase() const
    {
        return narrowPhaseQueueIndex != SIZE_MAX;
    }
    size_t getNarrowPhaseQueueIndex() const
    {
        return narrowPhaseQueueIndex;
    }
    void setNarrowPhaseQueueIndex(size_t index) const
    {
        narrowPhaseQueueIndex = index;
    }
    size_t getAdjacencyIndexA() const
    {
        return adjacencyIndexA;
    }
    void setAdjacencyIndexA(size_t index) const
    {
        adjacencyIndexA = index;
    }
    size_t getAdjacencyIndexB() const
    {
        return adjacencyIndexB;
    }
    void setAdjacencyIndexB(size_t index) const
    {
        adjacencyIndexB = index;
    }

    Collider *getObjectA() const
    {
        return objectA;
    }
    Collider *getObjectB() const
    {
        return objectB;
    }

    int getWitnessCount() const
    {
        return witnessCount;
    }
    void addToWitnessCount(int delta) const
    {
        witnessCount += delta;
    }

    bool operator==(const ColliderPair &other) const
    {
        return (objectA == other.objectA && objectB == other.objectB) ||
               (objectA == other.objectB && objectB == other.objectA);
    }

    class HashFunction
    {
      public:
        size_t operator()(const ColliderPair &collision) const
        {
            const auto a = collision.objectA->getHandle(), b = collision.objectB->getHandle();
            return std::hash<uint64_t>{}((uint64_t(a.slot) << 32) ^ b.slot ^ a.generation ^ (b.generation << 1));
        }
    };

  private:
    Collider *objectA;
    Collider *objectB;

    mutable Collision collisionStorage;
    mutable uint64_t objectAStateVersion;
    mutable uint64_t objectBStateVersion;
    mutable size_t narrowPhaseQueueIndex;
    mutable size_t adjacencyIndexA;
    mutable size_t adjacencyIndexB;
    mutable int witnessCount;
    mutable size_t contactIndex = SIZE_MAX;
    mutable size_t solverIndex = SIZE_MAX;
    mutable int32_t solverIslandId = -1;
    mutable size_t solverIslandContactIndex = SIZE_MAX;
    mutable size_t kinematicSolverIndex = SIZE_MAX;
    mutable bool hasCollision = false;
    mutable bool resultHadContact = false;
#if PLATFORMATOR_ENABLE_WORK_PROBES
    mutable bool resultTestedGeometry = false;
#endif
    // Exact recipients of this pair's applied-or-queued support credit, not current component caches.
    mutable Rigidbody *contributedSupportBodyA = nullptr;
    mutable Rigidbody *contributedSupportBodyB = nullptr;
};
