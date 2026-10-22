#pragma once

#include <cmath>
#include <stdexcept>
#include <string>

namespace platformator_detail
{
inline void finite(float value, const char *field)
{
    if (!std::isfinite(value))
        throw std::invalid_argument(std::string(field) + " must be finite");
}
inline void positive(float value, const char *field)
{
    if (!std::isfinite(value) || value <= 0)
        throw std::invalid_argument(std::string(field) + " must be positive and finite");
}
inline void nonnegative(float value, const char *field)
{
    if (!std::isfinite(value) || value < 0)
        throw std::invalid_argument(std::string(field) + " must be nonnegative and finite");
}
} // namespace platformator_detail
