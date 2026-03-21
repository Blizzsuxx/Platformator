#pragma once

#include "collider.h"
#include "collision.h"

class ColliderPair
{
public:
    ColliderPair(Collider *a, Collider *b);
    ~ColliderPair();

    Collision *getCollision() const;
    Collision *getOrCreateCollision() const;

    void updateCachedCollisionVersions() const;
    bool shouldUpdate() const;
    void clearCollision() const;

    void triggerCollisionEnter() const;
    void triggerCollisionStay() const;
    void triggerCollisionExit() const;

    void setObjectA(Collider *colliderA);
    void setObjectB(Collider *colliderB);

    Collider *getObjectA() const;
    Collider *getObjectB() const;

    bool operator==(const ColliderPair &other) const;

    class HashFunction
    {
    public:
        size_t operator()(const ColliderPair &collision) const
        {
            // sort them so that the order of the objects doesn't matter
            const Collider *objectA = collision.objectA;
            const Collider *objectB = collision.objectB;

            if (objectA > objectB)
            {
                std::swap(objectA, objectB);
            }

            size_t hash1 = std::hash<const Collider *>()(objectA);
            size_t hash2 = std::hash<const Collider *>()(objectB);
            return hash1 ^ (hash2 << 1); // Combine the two hashes
        }
    };

private:
    Collider *objectA;
    Collider *objectB;

    mutable Collision *collision;
    mutable uint64_t objectAStateVersion;
    mutable uint64_t objectBStateVersion;
};