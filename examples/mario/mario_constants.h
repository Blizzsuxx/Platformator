#pragma once

#include "constants.h"

namespace mario
{
    constexpr float PLAYER_WALK_SPEED = 180.0f;
    constexpr float PLAYER_JUMP_SPEED = 250.0f;
    constexpr float PLAYER_GRAVITY = GRAVITY_VECTOR_Y * 3.65f;
    constexpr float PLAYER_MAX_FALL_SPEED = 460.0f;
    constexpr float ENEMY_WALK_SPEED = 55.0f;
    constexpr float STOMP_TOLERANCE = 14.0f;
    constexpr float CAMERA_LEAD = 90.0f;
    constexpr float LEVEL_WIDTH = 2240.0f;
    constexpr double ENEMY_SQUASH_DURATION = 0.28;
} // namespace mario