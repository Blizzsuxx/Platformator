#pragma once

#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace platformator_editor
{
namespace numeric_scrub_detail
{
template <typename Integer>
Integer apply_integer_delta(Integer original, double delta,
                            std::optional<double> minVal,
                            std::optional<double> maxVal)
{
    constexpr Integer lowest = std::numeric_limits<Integer>::min();
    constexpr double lowerLimit = static_cast<double>(lowest);
    constexpr double upperLimit = std::numeric_limits<Integer>::is_signed ? 0x1p63 : 0x1p64;
    constexpr auto unsignedMax = std::numeric_limits<std::uint64_t>::max();
    const double rounded = std::round(std::abs(delta));
    if (rounded == 0.0)
        return original;

    const std::uint64_t distance = rounded >= 0x1p64 ? unsignedMax : static_cast<std::uint64_t>(rounded);

    // Measure positions from the type's minimum so the full range fits in uint64_t.
    constexpr auto origin = static_cast<std::uint64_t>(lowest);
    const std::uint64_t position = static_cast<std::uint64_t>(original) - origin;
    const std::uint64_t next = delta < 0.0
                                   ? position - std::min(distance, position)
                                   : position + std::min(distance, unsignedMax - position);

    std::uint64_t minPosition = 0;
    std::uint64_t maxPosition = unsignedMax;
    if (minVal && std::isfinite(*minVal))
    {
        const double bound = std::ceil(*minVal);
        if (bound >= upperLimit)
            return original;
        if (bound > lowerLimit)
            minPosition = static_cast<std::uint64_t>(static_cast<Integer>(bound)) - origin;
    }
    if (maxVal && std::isfinite(*maxVal))
    {
        const double bound = std::floor(*maxVal);
        if (bound < lowerLimit)
            return original;
        if (bound < upperLimit)
            maxPosition = static_cast<std::uint64_t>(static_cast<Integer>(bound)) - origin;
    }
    if (minPosition > maxPosition)
        return original;

    return static_cast<Integer>(origin + std::clamp(next, minPosition, maxPosition));
}
} // namespace numeric_scrub_detail

// Applies a scrub delta to a JSON number without losing exact original type or
// >2^53 integer precision. Returns `original` unchanged when `delta == 0.0`.
inline nlohmann::json apply_numeric_delta(const nlohmann::json &original,
                                          double delta,
                                          std::optional<double> minVal = std::nullopt,
                                          std::optional<double> maxVal = std::nullopt)
{
    if (!std::isfinite(delta) || delta == 0.0)
        return original;
    if (original.is_number_unsigned())
        return numeric_scrub_detail::apply_integer_delta(original.get<std::uint64_t>(), delta, minVal, maxVal);
    if (original.is_number_integer())
        return numeric_scrub_detail::apply_integer_delta(original.get<std::int64_t>(), delta, minVal, maxVal);
    if (original.is_number())
    {
        double next = original.get<double>() + delta;
        if (!std::isfinite(next))
            return original;
        if (minVal && next < *minVal)
            next = *minVal;
        if (maxVal && next > *maxVal)
            next = *maxVal;
        return nlohmann::json(next);
    }
    return original;
}
} // namespace platformator_editor
