#pragma once

#include "rigidbody.h"
#include "collider.h"
#include "collision.h"
#include "segmentedintervallist.h"
#include <chrono>
#include "aabb.h"
#include "helpers.h"

class PhysicsManager
{
public:
    PhysicsManager();
    ~PhysicsManager();

    void applyPhysics(double timeDelta);

    void checkForCollisions();
    void resolveCollisions();

    void addRigidBodyComponent(Rigidbody *rigidBodyComponent);
    void addColliderComponent(Collider *colliderComponent);

    void removeRigidBodyComponent(Rigidbody *rigidBodyComponent);
    void removeColliderComponent(Collider *colliderComponent);

private:
    void broadPhase();
    void narrowPhase();

    void createCollision(const ColliderPair &pair);

    bool checkProjections(const std::vector<Eigen::Vector2f> &normals, const Collider *referenceCollider, const Collider *incidentCollider, float &minOverlap, Eigen::Vector2f &minNormal, Eigen::Vector2f &incidentProjection, const Collider *&realIncidentCollider);
    void resolveCollision(const Collision *collision);

    std::list<Rigidbody *> rigidBodyComponents;
    std::list<Collision> collisions;
    AABB aabb;

    Eigen::Vector2f gravityVector;
};