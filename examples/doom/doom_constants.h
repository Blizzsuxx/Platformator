#pragma once

#include <cstdint>

namespace doom
{
    inline constexpr uint64_t LAYER_DEFAULT = 1ULL << 0;
    inline constexpr uint64_t LAYER_WALL = 1ULL << 1;
    inline constexpr uint64_t LAYER_PLAYER = 1ULL << 2;
    inline constexpr uint64_t LAYER_ENEMY = 1ULL << 3;
    inline constexpr uint64_t LAYER_PICKUP = 1ULL << 4;
    inline constexpr uint64_t LAYER_PROJECTILE = 1ULL << 5;
    inline constexpr uint64_t LAYER_DOOR = 1ULL << 6;
    inline constexpr uint64_t LAYER_BARREL = 1ULL << 7;
    inline constexpr uint64_t ALL_LAYERS_MASK = 0xFFFFFFFFULL;

    inline constexpr uint64_t WALL_RAY_MASK = LAYER_WALL | LAYER_DOOR;
    inline constexpr uint64_t HITSCAN_RAY_MASK = LAYER_WALL | LAYER_DOOR | LAYER_ENEMY | LAYER_BARREL;
    inline constexpr uint64_t LOS_RAY_MASK = LAYER_WALL | LAYER_DOOR | LAYER_PLAYER;
    inline constexpr uint64_t PROJECTILE_FRIENDLY_MASK = LAYER_WALL | LAYER_DOOR | LAYER_ENEMY | LAYER_BARREL;
    inline constexpr uint64_t PROJECTILE_HOSTILE_MASK = LAYER_WALL | LAYER_DOOR | LAYER_PLAYER;

    // 3D viewport stage anchored away from the [0, 1400] x [0, 1400] 2D physics arena
    // while staying comfortably inside RenderCullingState's certified coordinate envelope.
    inline constexpr float STAGE_ORIGIN_X = 3200.0f;
    inline constexpr float STAGE_ORIGIN_Y = 0.0f;
    inline constexpr float VIEW_WIDTH = 640.0f;
    inline constexpr float VIEW_HEIGHT = 360.0f;
    inline constexpr float HUD_HEIGHT = 56.0f;
    inline constexpr float VIEW3D_HEIGHT = VIEW_HEIGHT - HUD_HEIGHT; // 304.0f
    inline constexpr float PARK_X = STAGE_ORIGIN_X - 400.0f;
    inline constexpr float PARK_Y = STAGE_ORIGIN_Y - 400.0f;

    inline constexpr int RAY_COLUMNS = 320;
    inline constexpr int MAX_BILLBOARD_STRIPS = 128;
    inline constexpr float FOV_RADIANS = 66.0f * 3.14159265358979323846f / 180.0f;
    inline constexpr float MAX_VIEW_DIST = 1100.0f;
    inline constexpr float WALL_TILE_WORLD = 64.0f;
    inline constexpr float WALL_WORLD_HEIGHT = 64.0f;

    inline constexpr std::int32_t Z_CEILING = -2000000;
    inline constexpr std::int32_t Z_FLOOR = -1999999;
    inline constexpr std::int32_t Z_WALL = -1000000;
    inline constexpr std::int32_t Z_BILLBOARD = -500000;
    inline constexpr std::int32_t Z_WEAPON = 100000;
    inline constexpr std::int32_t Z_FLASH = 110000;
    inline constexpr std::int32_t Z_HUD_BG = 120000;
    inline constexpr std::int32_t Z_HUD_FG = 120100;
    inline constexpr std::int32_t Z_CROSSHAIR = 120200;

    enum WeaponKind : int
    {
        WEAPON_PISTOL = 0,
        WEAPON_SHOTGUN = 1,
        WEAPON_PLASMA = 2
    };

    enum EnemyKind : int
    {
        ENEMY_TROOPER = 0,
        ENEMY_IMP = 1,
        ENEMY_BARON = 2
    };

    enum PickupKind : int
    {
        PICKUP_MEDKIT = 0,
        PICKUP_ARMOR = 1,
        PICKUP_AMMO = 2,
        PICKUP_SHOTGUN = 3,
        PICKUP_PLASMA = 4,
        PICKUP_RED_KEYCARD = 5,
        PICKUP_EXIT_PORTAL = 6
    };
} // namespace doom
