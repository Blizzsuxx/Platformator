#pragma once

#include <Eigen/Dense>

const Eigen::Vector2f X_AXIS(1.0f, 0.0f);
const Eigen::Vector2f Y_AXIS(0.0f, 1.0f);

const float SCREEN_WIDTH = 1920.0f;
const float SCREEN_HEIGHT = 1080.0f;
const int TARGET_FPS = 120;
const double FRAME_TIME = 1.0 / static_cast<double>(TARGET_FPS);