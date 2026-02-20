#include "aabb.h"

AABB::AABB()
    : intervalListX()
{
}

AABB::~AABB()
{
}

void AABB::add(Collider *element)
{
    intervalListX.add(element->getXProjections());
    // intervalListY.add(element->getYProjections());
}

void AABB::remove(Collider *element)
{
    intervalListX.remove(element->getXProjections());
    // intervalListY.remove(element->getYProjections());
}

void AABB::updateCandidateList()
{
    candidateCollisions.clear();

    for (LocalSortArray *chunk : *intervalListX.getChunks())
    {
        checkForPotentialCollisionsInsideChunk(chunk);
        checkForCollisionsWithCheckpoint(chunk);
    }

    // for (LocalSortArray *chunk : *intervalListY.getChunks())
    // {
    //     checkForPotentialCollisionsInsideChunk(chunk);
    //     checkForCollisionsWithCheckpoint(chunk);
    // }
}

SegmentedIntervalList *AABB::getIntervalListX()
{
    return &intervalListX;
}

// SegmentedIntervalList *AABB::getIntervalListY()
// {
//     return &intervalListY;
// }

std::list<Collision> *AABB::getCandidateCollisions()
{
    return &candidateCollisions;
}

void AABB::checkForPotentialCollisionsInsideChunk(LocalSortArray *chunk)
{
    for (int i = 0; i < chunk->getSize(); i++)
    {
        BoundingRadiusProjection *projection = chunk->get(i);
        Collider *collider = projection->getCollider();

        if (collider->getGameObject()->getActive() == false)
        {
            continue;
        }

        if (projection->getIsEnd())
        {
            for (int j = i - 1; j >= 0; j--)
            {
                Collider *previousProjection = chunk->get(j)->getCollider();
                if (previousProjection == collider)
                {
                    break;
                }
                addCandidateCollision(collider, previousProjection);
            }
        }
    }
}

void AABB::checkForCollisionsWithCheckpoint(LocalSortArray *chunk)
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
            addCandidateCollision(checkpoint, previousProjection);
        }
    }
}

void AABB::addCandidateCollision(Collider *colliderA, Collider *colliderB)
{
    float aYMin = colliderA->getYProjections()->getMin()->getProjectedPosition();
    float aYMax = colliderA->getYProjections()->getMax()->getProjectedPosition();
    float bYMin = colliderB->getYProjections()->getMin()->getProjectedPosition();
    float bYMax = colliderB->getYProjections()->getMax()->getProjectedPosition();

    if (aYMax >= bYMin && bYMax >= aYMin)
    {
        candidateCollisions.push_back(Collision(colliderA->getGameObject(), colliderB->getGameObject()));
    }
}