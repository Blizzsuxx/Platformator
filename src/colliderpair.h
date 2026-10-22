#pragma once

#include <cstdint>
#include "platformator/collider.h"
#include "collision.h"

enum Axis : uint8_t;

class ColliderPair
{
    friend class Grid;
    friend class PhysicsManager;

  public:
    ColliderPair(Collider *a, Collider *b);
    ~ColliderPair();
    ColliderPair(const ColliderPair &) = delete;
    ColliderPair &operator=(const ColliderPair &) = delete;

    Collision *getCollision() const;
    Collision *getOrCreateCollisionNoEvent() const;

    void updateCachedCollisionVersions() const;
    bool shouldUpdate() const;
    void clearCollisionNoEvent() const;
    void clearCollision() const;

    bool getIsQueuedForNarrowPhase() const;
    void setIsQueuedForNarrowPhase(bool queued) const;
    size_t getNarrowPhaseQueueIndex() const;
    void setNarrowPhaseQueueIndex(size_t index) const;
    size_t getAdjacencyIndexA() const;
    void setAdjacencyIndexA(size_t index) const;
    size_t getAdjacencyIndexB() const;
    void setAdjacencyIndexB(size_t index) const;

    void queueCollisionExit() const;

    void setObjectA(Collider *colliderA);
    void setObjectB(Collider *colliderB);

    Collider *getObjectA() const;
    Collider *getObjectB() const;

    void incrementWitnessCount() const;
    void decrementWitnessCount() const;
    int getWitnessCount() const;
    void addToWitnessCount(int delta) const;

    bool operator==(const ColliderPair &other) const;

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

    mutable Collision *collision;
    mutable uint64_t objectAStateVersion;
    mutable uint64_t objectBStateVersion;
    mutable bool isQueuedForNarrowPhase;
    mutable size_t narrowPhaseQueueIndex;
    mutable size_t adjacencyIndexA;
    mutable size_t adjacencyIndexB;
    mutable int witnessCount;
    mutable size_t pairSlot = SIZE_MAX;
    mutable size_t activeIndex = SIZE_MAX;
    mutable size_t solverIndex = SIZE_MAX;
    mutable size_t stayIndex = SIZE_MAX;
    mutable uint64_t enteredStep = 0;
    mutable bool resultHadContact = false;
    mutable bool resultTestedGeometry = false;
    mutable Rigidbody *supportBodyA = nullptr;
    mutable Rigidbody *supportBodyB = nullptr;
    mutable bool supportsA = false;
    mutable bool supportsB = false;
};
