#include "aabb.h"

AABB::AABB()
    : intervalListX(),
      candidateCollisions()
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

const std::unordered_set<Collision, Collision::HashFunction> *AABB::getCandidateCollisions() const
{
    return &candidateCollisions;
}

void AABB::checkForPotentialCollisionsInsideChunk(LocalSortArray *chunk)
{
    for (size_t i = 0; i < chunk->getSize(); i++)
    {
        BoundingRadiusProjection *projection = chunk->get(i);
        Collider *collider = projection->getCollider();

        if (collider->getGameObject()->getActive() == false || collider->getIsDirty() == false)
        {
            continue;
        }

        if (projection->getIsEnd())
        {
            for (size_t j = i - 1; j != static_cast<size_t>(-1); j--)
            {

                Collider *previousProjection = chunk->get(j)->getCollider();
                if (previousProjection == collider)
                {
                    break;
                }

                if (!chunk->get(j)->getIsEnd())
                {
                    continue;
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
        if (checkpoint->getGameObject()->getActive() == false || checkpoint->getIsDirty() == false)
        {
            continue;
        }

        for (size_t i = chunk->getSize() - 1; i != static_cast<size_t>(-1); i--)
        {
            Collider *previousProjection = chunk->get(i)->getCollider();
            if (previousProjection == checkpoint)
            {
                break;
            }

            if (!chunk->get(i)->getIsEnd())
            {
                continue;
            }

            addCandidateCollision(checkpoint, previousProjection);
        }
    }
}

void AABB::addCandidateCollision(Collider *colliderA, Collider *colliderB)
{
    if (colliderA->getGameObject()->getActive() == false || colliderB->getGameObject()->getActive() == false)
    {
        return;
    }

    float aYMin = colliderA->getYProjections()->getMin()->getProjectedPosition();
    float aYMax = colliderA->getYProjections()->getMax()->getProjectedPosition();
    float bYMin = colliderB->getYProjections()->getMin()->getProjectedPosition();
    float bYMax = colliderB->getYProjections()->getMax()->getProjectedPosition();

    if (aYMax >= bYMin && bYMax >= aYMin)
    {
        candidateCollisions.insert(Collision(colliderA->getGameObject(), colliderB->getGameObject()));
    }
}

void AABB::sort()
{
    intervalListX.sort();
    // intervalListY.sort();
}