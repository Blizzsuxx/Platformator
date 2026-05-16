#pragma once

#include "rigidbody.h"
#include "collider.h"
#include "collision.h"
#include "segmentedintervallist.h"
#include <algorithm>
#include <chrono>
#include <oneapi/tbb/concurrent_vector.h>
#include <vector>
#include "helpers.h"
#include "grid.h"

class GameManager;

struct PhysicsEvent
{
    enum EventType
    {
        COLLISION_ENTER,
        COLLISION_EXIT,
        COLLISION_STAY
    };

    EventType type;
    Collision *collision;
    Collider *colliderA;
    Collider *colliderB;
};

class PhysicsManager
{
    friend class GameManager;

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
    // void refreshColliderComponent(Collider *colliderComponent);
    void queueColliderSync(Collider *colliderComponent);

    void removeRigidBodyComponent(Rigidbody *rigidBodyComponent);
    void removeColliderComponent(Collider *colliderComponent);

    void addPendingPhysicsEvent(const PhysicsEvent &event);
    const std::vector<PhysicsEvent> &getPendingPhysicsEvents() const;
    void clearPendingPhysicsEvents();
    void handlePendingPhysicsEvents(double timeDelta);

    const Eigen::Vector2f &getGravityVector() const;
    const Eigen::Vector2f &getGravityVectorNormalized() const;

private:
    void flushPendingColliderOperations();

    void updateSleepingStates(double timeDelta);
    void broadPhase();
    void narrowPhase();

    void satCreateCollision(const ColliderPair &pair);

    bool checkProjections(const std::vector<Eigen::Vector2f> &normals, const Collider *referenceCollider, const Collider *incidentCollider, float &minOverlap, Eigen::Vector2f &minNormal, const Collider *&realIncidentCollider);
    void preStepCollision(Collision *collision, float inverseTimeDelta);
    void resolveCollision(const Collision *collision);
    void resolveKinematicCollision(const Collision *collision);
    void resolveKinematicBodyAgainstNormal(Rigidbody *rigidBody, const Eigen::Vector2f &approachNormal, float correctionDistance);
    void calculateContactPoint(Collision *collision);

    std::vector<Rigidbody *> rigidBodyComponents;
    std::vector<Collision *> activeCollisions;
    std::vector<PhysicsEvent> pendingPhysicsEvents;
    Grid grid;

    Eigen::Vector2f gravityVector;
    Eigen::Vector2f gravityVectorNormalized;
};