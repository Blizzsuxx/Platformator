#pragma once

#include "gameobject.h"

class Collision
{
public:
    Collision();
    Collision(GameObject *gameObjectA, GameObject *gameObjectB);
    Collision(const Eigen::Vector2f &normal, float penetration, const Eigen::Vector2f &contactPoint, GameObject *gameObjectA, GameObject *gameObjectB);
    ~Collision();

    // Getters
    const Eigen::Vector2f &getNormal() const;
    float getPenetration() const;
    const Eigen::Vector2f &getContactPoint() const;

    const GameObject *getReferenceObject() const;
    const GameObject *getIncidentObject() const;

    // Setters
    void setNormal(const Eigen::Vector2f &normal) const;
    void setPenetration(const float penetration) const;
    void setContactPoint(const Eigen::Vector2f &contactPoint) const;
    void setReferenceObject(GameObject *gameObjectA) const;
    void setIncidentObject(GameObject *gameObjectB) const;

    bool operator==(const Collision &other) const;
    bool operator!=(const Collision &other) const;

    class HashFunction
    {
    public:
        size_t operator()(const Collision &collision) const
        {
            // sort them so that the order of the objects doesn't matter
            const GameObject *objectA = collision.getReferenceObject();
            const GameObject *objectB = collision.getIncidentObject();

            if (objectA > objectB)
            {
                std::swap(objectA, objectB);
            }

            size_t hash1 = std::hash<const GameObject *>()(objectA);
            size_t hash2 = std::hash<const GameObject *>()(objectB);
            return hash1 ^ (hash2 << 1); // Combine the two hashes
        }
    };

private:
    mutable Eigen::Vector2f normal;
    mutable float penetration;
    mutable Eigen::Vector2f contactPoint;
    mutable GameObject *referenceObject;
    mutable GameObject *incidentObject;
};