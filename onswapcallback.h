#pragma once

#include "collider.h"

class OnSwapCallback
{
public:
    virtual void onSwap(BoundingRadiusProjection *movedLeft, BoundingRadiusProjection *movedRight) = 0;
};