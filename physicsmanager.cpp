#include "physicsmanager.h"

PhysicsManager::PhysicsManager()
    : gravity(9.8f), timeDelta(0.0f), rigidBodyComponents(), colliderComponents(), colliderProjectionsX(), colliderProjectionsY()
{
}

PhysicsManager::~PhysicsManager()
{
}

void PhysicsManager::applyPhysics()
{
    for (Rigidbody *rigidBodyComponent : rigidBodyComponents)
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

void PhysicsManager::addRigidBodyComponent(Rigidbody *rigidBodyComponent)
{
    rigidBodyComponents.push_back(rigidBodyComponent);
}

void PhysicsManager::addColliderComponent(Collider *colliderComponent)
{
    colliderComponents.push_back(colliderComponent);

    colliderComponent->generateProjections();
    colliderProjectionsX.add(colliderComponent, 0UL);
    colliderProjectionsY.add(colliderComponent, 2UL);
}

std::list<Collision> *PhysicsManager::broadPhase()
{
    std::list<Collision *> *broadPhaseCollisions = new std::list<Collision *>();
    std::vector<Collider *> potentialCollisions = std::vector<Collider *>();

    // TODO: parallelize this loop
    for (LocalSortArray *chunk : *colliderProjectionsX.getChunks())
    {
        checkForPotentialCollisionsInsideChunk(chunk, potentialCollisions, broadPhaseCollisions);
        checkForCollisionsWithCheckpoint(chunk, potentialCollisions, broadPhaseCollisions);
    }
}

void PhysicsManager::checkForPotentialCollisionsInsideChunk(LocalSortArray *chunk, std::vector<Collider *> &potentialCollisions, std::list<Collision *> *broadPhaseCollisions)
{
    for (size_t i = 0; i < chunk->getSize(); i++)
    {
        BoundingRadiusProjection *projection = chunk->get(i);
        Collider *collider = projection->getCollider();

        if (collider->getGameObject()->getActive() == false)
        {
            continue;
        }

        if (projection->isEnd())
        {
            for (auto previousProjection = potentialCollisions.rbegin(); previousProjection != potentialCollisions.rend(); previousProjection++)
            {
                if ((*previousProjection) == collider)
                {
                    break;
                }
                broadPhaseCollisions->push_back(new Collision(collider->getGameObject(), (*previousProjection)->getGameObject()));
            }
        }
        else
        {
            potentialCollisions.push_back(collider);
        }
    }
}

void PhysicsManager::checkForCollisionsWithCheckpoint(LocalSortArray *chunk, std::vector<Collider *> &potentialCollisions, std::list<Collision *> *broadPhaseCollisions)
{
    for (Collider *checkpoint : *(chunk->getCheckpoint()))
    {
        if (checkpoint->getGameObject()->getActive() == false)
        {
            continue;
        }

        for (auto previousProjection = potentialCollisions.rbegin(); previousProjection != potentialCollisions.rend(); previousProjection++)
        {
            if ((*previousProjection) == checkpoint)
            {
                break;
            }
            broadPhaseCollisions->push_back(new Collision(checkpoint->getGameObject(), (*previousProjection)->getGameObject()));
        }
    }
}

std::list<Collision> *PhysicsManager::narrowPhase(std::list<Collision> *broadPhaseCollisions)
{
    // we can use the separating axis theorem to determine if there is a collision
    for (auto &collision : *broadPhaseCollisions)
    {
        GameObject *gameObject1 = collision.getGameObjectA();
        GameObject *gameObject2 = collision.getGameObjectB();

        Collider *collider1 = (Collider *)gameObject1->getComponent(ComponentType::COLLIDER);
        Collider *collider2 = (Collider *)gameObject2->getComponent(ComponentType::COLLIDER);

        std::auto_ptr<Eigen::Vector2f>
    }
}

std::auto_ptr<Eigen::Vector2f> PhysicsManager::findDeepestCollision(Collider *collider1, Collider *collider2, std::auto_ptr<std::vector<Eigen::Vector2f>> &normals)
{
    float minOverlap = std::numeric_limits<float>::max();
    Eigen::Vector2f *minNormal = nullptr;

    for (size_t i = 0; i < normals->size(); i++)
    {
        Eigen::Vector2f &normal = (*normals)[i];
        std::auto_ptr<std::vector<float>> projections1 = collider1->projectOntoAxis(normal, i); // we are using the index on the owner of the normal to further optimize projection
        std::auto_ptr<std::vector<float>> projections2 = collider2->projectOntoAxis(normal);

        if (projections1->at(1) < projections2->at(0) || projections2->at(1) < projections1->at(0))
        {
            return std::auto_ptr<Eigen::Vector2f>(nullptr);
        }
        else
        {
            float overlap = std::min(projections1->at(1), projections2->at(1)) - std::max(projections1->at(0), projections2->at(0));
            if (overlap < minOverlap)
            {
                minOverlap = overlap;
                minNormal = &normal;
            }
        }
    }

    return std::auto_ptr<Eigen::Vector2f>(minNormal);
}
