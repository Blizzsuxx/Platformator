#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace platformator_detail
{
class SimulationClock
{
  public:
    SimulationClock(double step, unsigned maximumSteps) : step(step), maximumSteps(maximumSteps)
    {
        if (!std::isfinite(step) || step <= 0 || !maximumSteps)
            throw std::invalid_argument("Invalid fixed-step clock configuration");
    }
    unsigned advance(double elapsed, bool paused, bool manual)
    {
        if (!std::isfinite(elapsed) || elapsed < 0)
            throw std::invalid_argument("Clock elapsed time must be finite and nonnegative");
        if (paused)
        {
            accumulator = 0;
            wasPaused = true;
            return manual ? 1 : 0;
        }
        if (wasPaused)
        {
            wasPaused = false;
            accumulator = 0;
            return 0;
        }
        accumulator += elapsed;
        const auto whole = static_cast<uint64_t>(std::floor((accumulator + step * 1e-10) / step));
        const auto count = static_cast<unsigned>(std::min<uint64_t>(whole, maximumSteps));
        accumulator = std::max(0., accumulator - whole * step);
        dropped += whole - count;
        return count;
    }
    uint64_t droppedSteps() const
    {
        return dropped;
    }
    double remainder() const
    {
        return accumulator;
    }

  private:
    double step, accumulator = 0;
    unsigned maximumSteps;
    uint64_t dropped = 0;
    bool wasPaused = false;
};
} // namespace platformator_detail
