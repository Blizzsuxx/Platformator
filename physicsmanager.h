#pragma once

#include "rigidbody.h"
#include "collider.h"
#include "collision.h"
#include "segmentedintervallist.h"

class PhysicsManager
{
public:
    PhysicsManager();
    ~PhysicsManager();

    void applyPhysics();
    void resolveCollisions();
    void addRigidBodyComponent(Rigidbody *rigidBodyComponent);
    void addColliderComponent(Collider *colliderComponent);

private:
    std::list<Collision> *broadPhase();
    std::list<Collision> *narrowPhase(std::list<Collision> *broadPhaseCollisions);
    void checkForPotentialCollisionsInsideChunk(LocalSortArray *chunk, std::vector<Collider *> &potentialCollisions, std::list<Collision *> *broadPhaseCollisions);
    void checkForCollisionsWithCheckpoint(LocalSortArray *chunk, std::vector<Collider *> &potentialCollisions, std::list<Collision *> *broadPhaseCollisions);
    std::auto_ptr<Eigen::Vector2f> findDeepestCollision(Collider *collider1, Collider *collider2, std::auto_ptr<std::vector<Eigen::Vector2f>> &normals);

    std::vector<Rigidbody*> rigidBodyComponents;
    std::vector<Collider*> colliderComponents;
    SegmentedIntervalList colliderProjectionsX;
    SegmentedIntervalList colliderProjectionsY;

    float gravity;
    float timeDelta;
};