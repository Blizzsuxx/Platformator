#pragma once

#include "collider.h"

class SwapCallback
{
public:
    virtual void swap(BoundingRadiusProjection *leftRadiusProjection, size_t leftRadiusProjectionIndex, BoundingRadiusProjection *rightRadiusProjection, size_t rightRadiusProjectionIndex) = 0;
};