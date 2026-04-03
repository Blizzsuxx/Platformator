#pragma once

#include "rigidbody.h"
#include "collider.h"
#include "collision.h"
#include "segmentedintervallist.h"
#include <algorithm>
#include <chrono>
#include <vector>
#include "helpers.h"
#include "grid.h"

class PhysicsManager
{
public:
    PhysicsManager();
    ~PhysicsManager();

    void applyPhysics(double timeDelta);

    void checkForCollisions();
    void resolveCollisions(double timeDelta);
    const Grid &getGrid() const;

    void addRigidBodyComponent(Rigidbody *rigidBodyComponent);
    void addColliderComponent(Collider *colliderComponent);
    void refreshColliderComponent(Collider *colliderComponent);
    void queueColliderSync(Collider *colliderComponent);

    void removeRigidBodyComponent(Rigidbody *rigidBodyComponent);
    void removeColliderComponent(Collider *colliderComponent);

private:
    void flushPendingColliderSyncs();
    void flushPendingColliderComponents();
    void markSupportContact(Rigidbody *rigidBody, const Eigen::Vector2f &contactDirection);
    void updateSleepingStates(double timeDelta);
    void broadPhase();
    void narrowPhase();

    void satCreateCollision(const ColliderPair &pair);

    bool checkProjections(const std::vector<Eigen::Vector2f> &normals, const Collider *referenceCollider, const Collider *incidentCollider, float &minOverlap, Eigen::Vector2f &minNormal, Eigen::Vector2f &incidentProjection, const Collider *&realIncidentCollider);
    void resolveCollision(const Collision *collision);

    std::vector<Rigidbody *> rigidBodyComponents;
    std::vector<Collider *> pendingColliderComponents;
    std::vector<Collider *> pendingColliderSyncs;
    std::vector<Collision *> activeCollisions;
    Grid grid;

    Eigen::Vector2f gravityVector;
};