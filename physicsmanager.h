#pragma once

#include "rigidbody.h"
#include "collider.h"
#include "collision.h"
#include "boundingradiousprojection.h"

class PhysicsManager
{
public:
    PhysicsManager();
    ~PhysicsManager();

    void applyPhysics();
    void resolveCollisions();
    void addRigidBodyComponent(Rigidbody* rigidBodyComponent);
    void addColliderComponent(Collider* colliderComponent);

private:
    std::list<Rigidbody*> rigidBodyComponents;
    std::list<Collider*> colliderComponents;
    std::list<BoundingRadiusProjection> boundingRadiusProjectionsX;
    std::list<BoundingRadiusProjection> boundingRadiusProjectionsY;

    float gravity;
    float timeDelta;

    std::list<Collision>* broadPhase();
    std::list<Collision>* narrowPhase(std::list<Collision>* broadPhaseCollisions);
};