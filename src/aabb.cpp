#include "aabb.h"
#include <iostream>

AABB::AABB()
    : intervalListX(this, Axis::X),
      intervalListY(this, Axis::Y),
      candidateCollisions(),
      pairStates()
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

const std::unordered_set<ColliderPair, ColliderPair::HashFunction> *AABB::getCandidatePairSet() const
{
    return &candidateCollisions;
}

void AABB::axisOverlapBegin(Collider *colliderA, Collider *colliderB, Axis axis)
{
    if (colliderA->getGameObject()->getActive() == false || colliderB->getGameObject()->getActive() == false)
    {
        return;
    }

    ColliderPair collisionKey = ColliderPair(colliderA, colliderB);
    PairState &collisionState = pairStates[collisionKey];

    collisionState.axisMask |= static_cast<uint8_t>(axis);

    if (collisionState.axisMask == static_cast<uint8_t>(Axis::ALL_AXES) && !collisionState.isInCandidateSet)
    {
        candidateCollisions.insert(collisionKey);
        collisionState.isInCandidateSet = true;
    }
}

void AABB::axisOverlapEnd(Collider *colliderA, Collider *colliderB, Axis axis)
{
    ColliderPair key(colliderA, colliderB);

    auto iterator = pairStates.find(key);
    if (iterator == pairStates.end())
    {
        return;
    }

    PairState &state = iterator->second;

    if (state.isInCandidateSet && state.axisMask == static_cast<uint8_t>(Axis::ALL_AXES))
    {
        candidateCollisions.erase(key);
        state.isInCandidateSet = false;
    }

    state.axisMask &= static_cast<uint8_t>(~static_cast<uint8_t>(axis));

    if (state.axisMask == 0)
    {
        pairStates.erase(key);
    }
}

void AABB::sort()
{
    // TODO: parallelize this
    intervalListX.sort();
    intervalListY.sort();
}
