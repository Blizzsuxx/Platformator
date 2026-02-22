#include <eigen3/Eigen/Dense>

const Eigen::Vector2f X_AXIS(1.0f, 0.0f);
const Eigen::Vector2f Y_AXIS(0.0f, 1.0f);

const int SCREEN_WIDTH = 640;
const int SCREEN_HEIGHT = 480;
const int TARGET_FPS = 60;
const double FRAME_TIME = 1.0 / static_cast<double>(TARGET_FPS);