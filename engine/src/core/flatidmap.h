#pragma once

#include <functional>

#include "core/flathash.h"
#include "core/flatmap.h"

class BaseObject;

namespace platformator_detail
{
// Open-addressing linear-probing ID table with backward-shift deletion (no tombstones).
// Uses value == nullptr as the empty sentinel so all 32-bit signed file IDs are valid keys.
using FlatIdMap = FlatHashMap<int, BaseObject *, FlatInt32Hash, std::equal_to<int>, 16>;
} // namespace platformator_detail
