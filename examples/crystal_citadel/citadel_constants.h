#pragma once

#include <cstdint>

namespace citadel
{
    constexpr float PLAYER_WALK_SPEED = 195.0f;
    constexpr float PLAYER_JUMP_SPEED = 275.0f;
    constexpr float PLAYER_GRAVITY = 98.10f * 4.2f;
    constexpr float PLAYER_MAX_FALL_SPEED = 480.0f;
    constexpr float BOLT_SPEED = 380.0f;
    constexpr float ENEMY_WALK_SPEED = 65.0f;
    constexpr float SLIME_WALK_SPEED = 75.0f;
    constexpr double ENEMY_SQUASH_DURATION = 0.25;

    constexpr std::uint64_t LAYER_DEFAULT = 1ULL << 0;
    constexpr std::uint64_t LAYER_PLAYER = 1ULL << 1;
    constexpr std::uint64_t LAYER_ENEMY = 1ULL << 2;
    constexpr std::uint64_t LAYER_PLATFORMS = 1ULL << 3;
    constexpr std::uint64_t LAYER_PROJECTILE = 1ULL << 4;
    constexpr std::uint64_t LAYER_COLLECTIBLE = 1ULL << 5;
    constexpr std::uint64_t LAYER_HAZARD = 1ULL << 6;
} // namespace citadel
