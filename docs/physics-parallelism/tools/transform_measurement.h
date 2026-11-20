#pragma once

#include <chrono>
#include <cstdlib>
#include <string_view>

namespace transform_measurement
{
using Clock = std::chrono::steady_clock;

inline long long elapsed(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}

struct Sample
{
    long long sync = 0, walk = 0, publish = 0;
};

struct Scope
{
    Clock::time_point start = Clock::now();
    ~Scope();
};

inline Sample sample;
inline Scope::~Scope()
{
    sample.sync += elapsed(start);
}

inline const std::string_view mode = [] {
    const auto *value = std::getenv("PLATFORMATOR_IMPLEMENTATION_TRANSFORMS");
    return value ? std::string_view(value) : std::string_view{};
}();
}
