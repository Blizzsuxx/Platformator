#pragma once

#include "collider.h"

class SwapCallback
{
public:
    virtual void swap(BoundingRadiusProjectionProxy *leftRadiusProjection, size_t leftRadiusProjectionIndex, BoundingRadiusProjectionProxy *rightRadiusProjection, size_t rightRadiusProjectionIndex) = 0;
};