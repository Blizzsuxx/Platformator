#pragma once

#include "document.h"

#include <optional>

namespace platformator_editor
{
struct CameraDimensions
{
    std::optional<double> width;
    std::optional<double> height;
    bool widthConflict = false;
    bool heightConflict = false;
    bool widthUsesLegacy = false;
    bool heightUsesLegacy = false;
    bool widthMalformed = false;
    bool heightMalformed = false;
};

CameraDimensions interpret_camera_dimensions(const Json &component) noexcept;
} // namespace platformator_editor
