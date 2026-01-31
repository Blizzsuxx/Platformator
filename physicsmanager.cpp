#include "physicsmanager.h"

PhysicsManager::PhysicsManager()
    : gravity(9.8f), timeDelta(0.0f)
{
}

PhysicsManager::~PhysicsManager()
{
}

void PhysicsManager::applyPhysics()
{
    // TODO: parallelize this loop
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

std::list<std::shared_ptr<Collision>> *PhysicsManager::checkForCollisions()
{
    return narrowPhase(broadPhase());
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

std::list<std::shared_ptr<Collision>> *PhysicsManager::broadPhase()
{
    std::list<std::shared_ptr<Collision>> *broadPhaseCollisions = new std::list<std::shared_ptr<Collision>>();

    // TODO: parallelize this loop
    for (LocalSortArray *chunk : *colliderProjectionsX.getChunks())
    {
        checkForPotentialCollisionsInsideChunk(chunk, broadPhaseCollisions);
        checkForCollisionsWithCheckpoint(chunk, broadPhaseCollisions);
    }

    return broadPhaseCollisions;
}

void PhysicsManager::checkForPotentialCollisionsInsideChunk(LocalSortArray *chunk, std::list<std::shared_ptr<Collision>> *broadPhaseCollisions)
{
    for (int i = 0; i < chunk->getSize(); i++)
    {
        BoundingRadiusProjection *projection = chunk->get(i);
        Collider *collider = projection->getCollider();

        if (collider->getGameObject()->getActive() == false)
        {
            continue;
        }

        if (projection->isEnd())
        {
            for (int j = i - 1; j >= 0; j--)
            {
                Collider *previousProjection = chunk->get(j)->getCollider();
                if (previousProjection == collider)
                {
                    break;
                }
                broadPhaseCollisions->push_back(std::make_shared<Collision>(collider->getGameObject(), previousProjection->getGameObject()));
            }
        }
    }
}

void PhysicsManager::checkForCollisionsWithCheckpoint(LocalSortArray *chunk, std::list<std::shared_ptr<Collision>> *broadPhaseCollisions)
{
    for (Collider *checkpoint : *(chunk->getCheckpoint()))
    {
        if (checkpoint->getGameObject()->getActive() == false)
        {
            continue;
        }

        for (int i = chunk->getSize() - 1; i >= 0; i--)
        {
            Collider *previousProjection = chunk->get(i)->getCollider();
            if (previousProjection == checkpoint)
            {
                break;
            }
            broadPhaseCollisions->push_back(std::make_shared<Collision>(checkpoint->getGameObject(), previousProjection->getGameObject()));
        }
    }
}

std::list<std::shared_ptr<Collision>> *PhysicsManager::narrowPhase(std::list<std::shared_ptr<Collision>> *broadPhaseCollisions)
{
    std::list<std::shared_ptr<Collision>> *narrowPhaseCollisions = new std::list<std::shared_ptr<Collision>>();

    // we can use the separating axis theorem to determine if there is a collision
    auto collisionIterator = broadPhaseCollisions->begin();
    auto endOfList = broadPhaseCollisions->end();
    while (collisionIterator != endOfList)
    {
        Collision *collision = (*collisionIterator).get();

        if (checkCollisions(collision))
        {
            broadPhaseCollisions->erase(collisionIterator);
        }
        else
        {
            collisionIterator++;
        }
    }

    return broadPhaseCollisions;
}

void PhysicsManager::deleteNormals(std::vector<Eigen::Vector2f *> *normals)
{
    for (Eigen::Vector2f *normal : *normals)
    {
        delete normal;
    }
    delete normals;
}

void PhysicsManager::deleteNormals(std::vector<Eigen::Vector2f *> *normals, Eigen::Vector2f *normalNotToDelete)
{
    for (Eigen::Vector2f *normal : *normals)
    {
        if (normal == normalNotToDelete)
        {
            continue;
        }
        delete normal;
    }
    delete normals;
}

bool PhysicsManager::checkCollisions(Collision *collision)
{
    Collider *referenceCollider = (Collider *)collision->getReferenceObject()->getComponent(ComponentType::COLLIDER);
    Collider *incidentCollider = (Collider *)collision->getIncidentObject()->getComponent(ComponentType::COLLIDER);

    float minOverlap = std::numeric_limits<float>::max();
    Eigen::Vector2f *minNormal = nullptr;
    std::vector<Eigen::Vector2f *> *normals = referenceCollider->getNormals(incidentCollider);
    Eigen::Vector2f *incidentProjection = nullptr;
    Collider *realIncidentCollider = incidentCollider;

    for (long unsigned int i = 0; i < normals->size(); i++)
    {
        Eigen::Vector2f &normal = *((*normals)[i]);
        auto projections1 = referenceCollider->projectOntoAxis(normal, i); // we are using the index on the owner of the normal to further optimize projection
        auto projections2 = incidentCollider->projectOntoAxis(normal);     // TODO: see if it's better to approximate from the perspective of the incident or reference object

        if (projections1->y() < projections2->x() || projections2->y() < projections1->x())
        {
            deleteNormals(normals);
            return false;
        }
        else
        {
            float overlap = std::min(projections1->y(), projections2->y()) - std::max(projections1->x(), projections2->x());
            if (overlap < minOverlap)
            {
                minOverlap = overlap;
                minNormal = &normal;
                incidentProjection = projections2.get();
            }
        }
    }
    deleteNormals(normals, minNormal);

    const Eigen::Vector2f *minNormalBefore = minNormal;
    std::swap(incidentCollider, referenceCollider);
    normals = referenceCollider->getNormals(incidentCollider);

    for (long unsigned int i = 0; i < normals->size(); i++)
    {
        Eigen::Vector2f &normal = *((*normals)[i]);
        auto projections1 = referenceCollider->projectOntoAxis(normal, i); // we are using the index on the owner of the normal to further optimize projection
        auto projections2 = incidentCollider->projectOntoAxis(normal);     // TODO: see if it's better to approximate from the perspective of the incident or reference object

        if (projections1->y() < projections2->x() || projections2->y() < projections1->x())
        {
            deleteNormals(normals);
            delete minNormalBefore;
            return false;
        }
        else
        {
            float overlap = std::min(projections1->y(), projections2->y()) - std::max(projections1->x(), projections2->x());
            if (overlap < minOverlap)
            {
                minOverlap = overlap;
                minNormal = &normal;
                incidentProjection = projections1.get();
                realIncidentCollider = incidentCollider;
            }
        }
    }

    if (minNormal != minNormalBefore)
    {
        delete minNormalBefore;
    }

    deleteNormals(normals, minNormal);
    collision->setNormal(minNormal);
    collision->setPenetration(minOverlap);
    collision->setIncidentObject(realIncidentCollider->getGameObject());
    collision->setReferenceObject(realIncidentCollider == incidentCollider ? referenceCollider->getGameObject() : incidentCollider->getGameObject());

    auto directionVector = incidentCollider->getGameObject()->getPosition() - referenceCollider->getGameObject()->getPosition();
    if (minNormal->dot(directionVector) < 0)
    {
        *minNormal *= -1.0f;
    }

    float projectionDelta = (incidentProjection->y() - incidentProjection->x()) / 2;
    collision->setContactPoint(new Eigen::Vector2f((realIncidentCollider->getGameObject()->getPosition() + ((*minNormal) * projectionDelta))));

    return true;
}

void PhysicsManager::resolveCollisions(std::list<std::shared_ptr<Collision>> *narrowPhaseCollisions)
{
}