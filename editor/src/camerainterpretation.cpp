#include "camerainterpretation.h"

#include <cmath>

namespace platformator_editor
{
namespace
{
std::optional<double> numeric_value(const Json *value) noexcept
{
    if (value == nullptr || !value->is_number())
        return std::nullopt;
    try
    {
        const double number = value->get<double>();
        return std::isfinite(number) ? std::optional<double>(number) : std::nullopt;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

const Json *member(const Json &object, const char *key) noexcept
{
    if (!object.is_object())
        return nullptr;
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &*found;
}

void interpret_dimension(const Json &component, const char *canonicalName,
                         const char *legacyName, std::optional<double> &result,
                         bool &conflict, bool &usesLegacy, bool &malformed) noexcept
{
    const Json *canonical = member(component, canonicalName);
    const Json *legacyObject = member(component, "camera");
    const Json *legacy = legacyObject != nullptr ? member(*legacyObject, legacyName) : nullptr;
    const auto canonicalValue = numeric_value(canonical);
    const auto legacyValue = numeric_value(legacy);
    malformed = (canonical != nullptr && !canonicalValue) || (legacy != nullptr && !legacyValue);
    if (canonicalValue)
    {
        result = canonicalValue;
        conflict = legacyValue && *canonicalValue != *legacyValue;
    }
    else if (legacyValue)
    {
        result = legacyValue;
        usesLegacy = true;
    }
}
} // namespace

CameraDimensions interpret_camera_dimensions(const Json &component) noexcept
{
    CameraDimensions result;
    interpret_dimension(component, "width", "w", result.width,
                        result.widthConflict, result.widthUsesLegacy, result.widthMalformed);
    interpret_dimension(component, "height", "h", result.height,
                        result.heightConflict, result.heightUsesLegacy, result.heightMalformed);
    return result;
}
} // namespace platformator_editor
