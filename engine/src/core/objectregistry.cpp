#include "core/objectregistry.h"

#include <atomic>
#include <limits>
#include <stdexcept>

namespace platformator_detail
{
ObjectRegistry::ObjectRegistry()
{
    static std::atomic<uint64_t> nextSerial{1};
    runtimeSerial = nextSerial.load(std::memory_order_relaxed);
    do
    {
        if (runtimeSerial == std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("Runtime identity exhausted");
    } while (!nextSerial.compare_exchange_weak(runtimeSerial, runtimeSerial + 1, std::memory_order_relaxed));
}
} // namespace platformator_detail
