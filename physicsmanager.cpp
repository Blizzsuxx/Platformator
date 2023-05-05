#include "physicsmanager.h"

PhysicsManager::PhysicsManager()
{
}

PhysicsManager::~PhysicsManager()
{
}

void PhysicsManager::applyPhysics()
{
    for (Rigidbody* rigidBodyComponent : rigidBodyComponents)
    {
        if (rigidBodyComponent->getBodyType() != DYNAMIC || rigidBodyComponent->getGameObject()->getActive() == false)
        {
            continue;
        }

        if (rigidBodyComponent->getGravity())
        {
            rigidBodyComponent->setForce(rigidBodyComponent->getForce() + Eigen::Vector2f(0.0f, -gravity * rigidBodyComponent->getMass()));
        }

        rigidBodyComponent->setVelocity(rigidBodyComponent->getVelocity() + rigidBodyComponent->getForce() * timeDelta / rigidBodyComponent->getMass());
        rigidBodyComponent->setAngularVelocity(rigidBodyComponent->getAngularVelocity() + rigidBodyComponent->getTorque() * timeDelta / rigidBodyComponent->getMomentOfInertia());

        rigidBodyComponent->getGameObject()->setPosition(rigidBodyComponent->getGameObject()->getPosition() + rigidBodyComponent->getVelocity() * timeDelta);
        rigidBodyComponent->getGameObject()->setRotation(rigidBodyComponent->getGameObject()->getRotation() + rigidBodyComponent->getAngularVelocity() * timeDelta);
    }
}

void PhysicsManager::resolveCollisions()
{
    narrowPhase(broadPhase());
}

void PhysicsManager::addRigidBodyComponent(Rigidbody* rigidBodyComponent)
{
    rigidBodyComponents.push_back(rigidBodyComponent);
}

void PhysicsManager::addColliderComponent(Collider* colliderComponent)
{
    colliderComponents.push_back(colliderComponent);
}

std::list<Collision>* PhysicsManager::broadPhase()
{

}