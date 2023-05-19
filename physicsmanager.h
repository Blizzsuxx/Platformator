#pragma once

#include "rigidbody.h"
#include "collider.h"
#include "collision.h"

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
    std::list<Rigidbody*> rigidBodyComponents;
    std::list<Collider*> colliderComponents;

    float gravity;
    float timeDelta;

    std::list<Collision> *broadPhase();
    std::list<Collision> *narrowPhase(std::list<Collision> *broadPhaseCollisions);
};