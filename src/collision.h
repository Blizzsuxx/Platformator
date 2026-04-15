#pragma once

#include "collider.h"
#include "helpers.h"

class Collision
{
public:
    Collision();
    Collision(Collider *colliderA, Collider *colliderB);
    Collision(const Eigen::Vector2f &normal, float penetration, const ClipPointsWithData &contactPoints, Collider *colliderA, Collider *colliderB);
    ~Collision();

    // Getters
    const Eigen::Vector2f &getNormal() const;
    ClipPointsWithData &getContactPoints() const;
    float getFriction() const;
    float getRestitution() const;

    const Collider *getReferenceObject() const;
    const Collider *getIncidentObject() const;

    bool shouldResolve() const;
    void updateSupportState(const Eigen::Vector2f &gravityVector) const;
    void clearSupportState() const;

    // Setters
    void setNormal(const Eigen::Vector2f &normal) const;
    void setContactPoints(const ClipPoints &contactPoints) const;
    void setReferenceObject(const Collider *colliderA) const;
    void setIncidentObject(const Collider *colliderB) const;

    bool operator==(const Collision &other) const;
    bool operator!=(const Collision &other) const;

    class HashFunction
    {
    public:
        size_t operator()(const Collision &collision) const
        {
            // sort them so that the order of the objects doesn't matter
            const Collider *objectA = collision.getReferenceObject();
            const Collider *objectB = collision.getIncidentObject();

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
    mutable Eigen::Vector2f normal;
    mutable ClipPointsWithData contactPoints;
    mutable const Collider *referenceObject;
    mutable const Collider *incidentObject;
    mutable bool supportsReferenceBody;
    mutable bool supportsIncidentBody;
};