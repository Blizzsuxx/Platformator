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
    void applyMovement(double timeDelta);

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
    void updateSleepingStates(double timeDelta);
    void broadPhase();
    void narrowPhase();

    void satCreateCollision(const ColliderPair &pair);

    bool checkProjections(const std::vector<Eigen::Vector2f> &normals, const Collider *referenceCollider, const Collider *incidentCollider, float &minOverlap, Eigen::Vector2f &minNormal, const Collider *&realIncidentCollider);
    void preStepCollision(Collision *collision, float inverseTimeDelta);
    void resolveCollision(const Collision *collision);
    bool shouldResolveKinematicCollision(const Collision *collision) const;
    void resolveKinematicCollision(const Collision *collision);
    void resolveKinematicBodyAgainstNormal(Rigidbody *rigidBody, const Eigen::Vector2f &approachNormal, float correctionDistance);
    void calculateContactPoint(Collision *collision);

    std::vector<Rigidbody *> rigidBodyComponents;
    std::vector<Collider *> pendingColliderComponents;
    std::vector<Collider *> pendingColliderSyncs;
    std::vector<Collision *> activeCollisions;
    Grid grid;

    Eigen::Vector2f gravityVector;
    Eigen::Vector2f gravityVectorNormalized;
};