#pragma once

#include <Eigen/Core>

class ColliderPair;

namespace physics_geometry
{
// Updates only the pair-owned contact and geometry stamps; shared physics state is reduced after workers join.
void updateContactGeometry(const ColliderPair &pair, const Eigen::Vector2f &gravityDirection);
} // namespace physics_geometry
