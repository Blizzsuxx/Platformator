#pragma once

#include <Eigen/Dense>

inline const Eigen::Vector2f X_AXIS(1.0f, 0.0f);
inline const Eigen::Vector2f Y_AXIS(0.0f, 1.0f);

constexpr float SCREEN_WIDTH = 640.0f;
constexpr float SCREEN_HEIGHT = 480.0f;
constexpr int TARGET_FPS = 120;
constexpr const double FRAME_TIME = 1.0 / static_cast<double>(TARGET_FPS);

constexpr float SUPPORT_NORMAL_THRESHOLD = 0.85f;
constexpr float LINEAR_SLEEP_THRESHOLD = 5.0f;
constexpr float ANGULAR_SLEEP_THRESHOLD = 0.05f;
constexpr double SLEEP_DELAY = 0.25;

constexpr float WAKE_LINEAR_EPSILON = 1e-3f;
constexpr float WAKE_LINEAR_EPSILON_SQUARED = WAKE_LINEAR_EPSILON * WAKE_LINEAR_EPSILON;
constexpr float WAKE_ANGULAR_EPSILON = 1e-4f;

constexpr bool ENABLE_LOGGING = false;

constexpr float GRID_CELL_SIZE = 200.0f;
constexpr float GRAVITY_VECTOR_X = 0.0f;
constexpr float GRAVITY_VECTOR_Y = 98.10f;

constexpr size_t COLLISION_SOLVER_ITERATIONS = 10;
constexpr float COLLISION_ALLOWED_PENETRATION = 0.01f;
constexpr float COLLISION_BIAS_FACTOR = 0.2f;
constexpr float COLLISION_POSITION_CORRECTION_PERCENT = 0.2f;
constexpr float COLLISION_WARM_START_DISTANCE_SQUARED = 4.0f;
