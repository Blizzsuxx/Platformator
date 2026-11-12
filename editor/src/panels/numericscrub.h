#pragma once

#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace platformator_editor
{
// Applies a scrub delta to a JSON number without losing exact original type or
// >2^53 integer precision. Returns `original` unchanged when `delta == 0.0`.
inline nlohmann::json apply_numeric_delta(const nlohmann::json &original,
                                          double delta,
                                          std::optional<double> minVal = std::nullopt,
                                          std::optional<double> maxVal = std::nullopt)
{
    if (!std::isfinite(delta) || delta == 0.0)
        return original;
    auto clamp_delta_int128 = [](double d) -> __int128 {
        const long double rounded = std::roundl(static_cast<long double>(d));
        constexpr long double kMaxU64 = static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
        if (rounded >= kMaxU64)
            return static_cast<__int128>(std::numeric_limits<std::uint64_t>::max());
        if (rounded <= -kMaxU64)
            return -static_cast<__int128>(std::numeric_limits<std::uint64_t>::max());
        return static_cast<__int128>(rounded);
    };
    if (original.is_number_integer() && !original.is_number_unsigned())
    {
        const std::int64_t orig = original.get<std::int64_t>();
#if defined(__SIZEOF_INT128__)
        const __int128 rounded = clamp_delta_int128(delta);
        if (rounded == 0)
            return original;
        const __int128 sum = static_cast<__int128>(orig) + rounded;
        __int128 minBound = static_cast<__int128>(std::numeric_limits<std::int64_t>::min());
        __int128 maxBound = static_cast<__int128>(std::numeric_limits<std::int64_t>::max());
        if (minVal && std::isfinite(*minVal))
        {
            const long double c = std::ceill(static_cast<long double>(*minVal));
            if (c >= static_cast<long double>(maxBound))
                minBound = maxBound + 1;
            else if (c > static_cast<long double>(minBound))
                minBound = static_cast<__int128>(c);
        }
        if (maxVal && std::isfinite(*maxVal))
        {
            const long double f = std::floorl(static_cast<long double>(*maxVal));
            if (f <= static_cast<long double>(minBound))
                maxBound = minBound - 1;
            else if (f < static_cast<long double>(maxBound))
                maxBound = static_cast<__int128>(f);
        }
        if (minBound > maxBound)
            return original;
        return nlohmann::json(static_cast<std::int64_t>(std::clamp(sum, minBound, maxBound)));
#else
        const long double rounded = std::roundl(static_cast<long double>(delta));
        if (rounded == 0.0L)
            return original;
        const long double sum = static_cast<long double>(orig) + rounded;
        const long double minL = static_cast<long double>(std::numeric_limits<std::int64_t>::min());
        const long double maxL = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
        return nlohmann::json(static_cast<std::int64_t>(std::clamp(sum, minL, maxL)));
#endif
    }
    if (original.is_number_unsigned())
    {
        const std::uint64_t orig = original.get<std::uint64_t>();
#if defined(__SIZEOF_INT128__)
        const __int128 rounded = clamp_delta_int128(delta);
        if (rounded == 0)
            return original;
        const __int128 sum = static_cast<__int128>(orig) + rounded;
        __int128 minBound = 0;
        __int128 maxBound = static_cast<__int128>(std::numeric_limits<std::uint64_t>::max());
        if (minVal && std::isfinite(*minVal) && *minVal > 0.0)
        {
            const long double c = std::ceill(static_cast<long double>(*minVal));
            if (c >= static_cast<long double>(maxBound))
                minBound = maxBound + 1;
            else if (c > 0.0L)
                minBound = static_cast<__int128>(c);
        }
        if (maxVal && std::isfinite(*maxVal))
        {
            const long double f = std::floorl(static_cast<long double>(*maxVal));
            if (f < 0.0L)
                maxBound = -1;
            else if (f < static_cast<long double>(maxBound))
                maxBound = static_cast<__int128>(f);
        }
        if (minBound > maxBound)
            return original;
        return nlohmann::json(static_cast<std::uint64_t>(std::clamp(sum, minBound, maxBound)));
#else
        const long double rounded = std::roundl(static_cast<long double>(delta));
        if (rounded == 0.0L)
            return original;
        const long double sum = std::max<long double>(0.0L, static_cast<long double>(orig) + rounded);
        const long double maxL = static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
        return nlohmann::json(static_cast<std::uint64_t>(std::min(sum, maxL)));
#endif
    }
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
