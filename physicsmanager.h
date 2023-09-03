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
    void checkForCollisions();
    void addRigidBodyComponent(Rigidbody *rigidBodyComponent);
    void addColliderComponent(Collider *colliderComponent);

private:
    std::list<std::shared_ptr<Collision>> *broadPhase();
    std::list<std::shared_ptr<Collision>> *narrowPhase(std::list<std::shared_ptr<Collision>> *broadPhaseCollisions);
    void resolveCollisions(std::list<std::shared_ptr<Collision>> *broadPhaseCollisions);
    void checkForPotentialCollisionsInsideChunk(LocalSortArray *chunk, std::list<std::shared_ptr<Collision>> *broadPhaseCollisions);
    void checkForCollisionsWithCheckpoint(LocalSortArray *chunk, std::list<std::shared_ptr<Collision>> *broadPhaseCollisions);
    bool checkCollisions(Collision *collision);
    void deleteNormals(std::vector<Eigen::Vector2f*> *normals);
    void deleteNormals(std::vector<Eigen::Vector2f*> *normals, Eigen::Vector2f *normalNotToDelete);

    std::vector<Rigidbody*> rigidBodyComponents;
    std::vector<Collider*> colliderComponents;
    SegmentedIntervalList colliderProjectionsX;
    SegmentedIntervalList colliderProjectionsY;

    float gravity;
    float timeDelta;
};