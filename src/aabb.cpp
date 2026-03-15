#include "aabb.h"

AABB::AABB()
    : intervalListX(),
      intervalListY(),
      candidateCollisions()
{
}

AABB::~AABB()
{
}

void AABB::add(Collider *element)
{
    intervalListX.add(element->getXProjections());
    intervalListY.add(element->getYProjections());
}

void AABB::remove(Collider *element)
{
    intervalListX.remove(element->getXProjections());
    intervalListY.remove(element->getYProjections());
}

// void AABB::updateCandidateList()
// {
//     candidateCollisions.clear();

//     for (LocalSortArray *chunk : *intervalListX.getChunks())
//     {
//         checkForPotentialCollisionsInsideChunk(chunk);
//         checkForCollisionsWithCheckpoint(chunk);
//     }

//     // for (LocalSortArray *chunk : *intervalListY.getChunks())
//     // {
//     //     checkForPotentialCollisionsInsideChunk(chunk);
//     //     checkForCollisionsWithCheckpoint(chunk);
//     // }
// }

SegmentedIntervalList *AABB::getIntervalListX()
{
    return &intervalListX;
}

SegmentedIntervalList *AABB::getIntervalListY()
{
    return &intervalListY;
}

const std::vector<Collision> *AABB::getCandidateCollisions() const
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
        candidateCollisions.push_back(Collision(colliderA->getGameObject(), colliderB->getGameObject()));
    }
}

void AABB::sort()
{
    intervalListX.sort();
    intervalListY.sort();
}

void AABB::onSwap(BoundingRadiusProjection *leftRadiusProjection, size_t leftRadiusProjectionIndex, BoundingRadiusProjection *rightRadiusProjection, size_t rightRadiusProjectionIndex)
{
    // minimum of left crossing maximum of right means that the two projections are now overlapping
    // maximum of left crossing minimum of right means that the two projections are no longer overlapping
    // if both projections are from the same collider, then ignore (it's probably either a very fast object or a really small object)
    // we also need to check if it's a cross chunk swap (need to update checkpoints)
    // cross chunk:
    // left is minimum - remove from checkpoint
    // left is maximum - add to checkpoint
    // right is minimum - add to checkpoint
    // right is maximum - remove from checkpoint

    Collider *colliderA = leftRadiusProjection->getCollider();
    Collider *colliderB = rightRadiusProjection->getCollider();

    float aYMin = colliderA->getYProjections()->getMin()->getProjectedPosition();
    float aYMax = colliderA->getYProjections()->getMax()->getProjectedPosition();
    float bYMin = colliderB->getYProjections()->getMin()->getProjectedPosition();
    float bYMax = colliderB->getYProjections()->getMax()->getProjectedPosition();

    bool currentlyColliding = aYMax >= bYMin && bYMax >= aYMin;

    // Check if the swap created a new potential collision
    if (currentlyColliding)
    {
        addCandidateCollision(colliderA, colliderB);
    }
}