#include "aabb.h"

AABB::AABB()
    : intervalListX(this),
      intervalListY(this),
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

const std::unordered_set<Collision, Collision::HashFunction> *AABB::getCandidateCollisions() const
{
    return &candidateCollisions;
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
    // TODO: paralelize this
    intervalListX.sort();
    intervalListY.sort();
}
