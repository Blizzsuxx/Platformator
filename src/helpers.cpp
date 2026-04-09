#include "helpers.h"
#include "debugdraw.h"

void ClipPoints::averagePoints()
{
    if (count == 0)
    {
        return;
    }

    Eigen::Vector2f sum = Eigen::Vector2f::Zero();
    for (size_t i = 0; i < count; ++i)
    {
        sum += points[i];
        DebugDraw::getInstance().addPointDebugObject(points[i], 255, 0, 255, 255, false, "Contact Point");
    }

    points[0] = sum / static_cast<float>(count);
    DebugDraw::getInstance().addPointDebugObject(points[0], 255, 255, 0, 255, true, "Average Contact Point");
    count = 1;
}