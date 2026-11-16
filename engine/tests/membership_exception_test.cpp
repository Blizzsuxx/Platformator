#include "physicsmembership.h"

#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

namespace
{
thread_local bool failNextAllocation = false;
}

void *operator new(std::size_t size)
{
    if (failNextAllocation)
    {
        failNextAllocation = false;
        throw std::bad_alloc();
    }
    if (void *memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }

namespace
{
struct Body { size_t slot = SIZE_MAX; };
struct Pair { mutable size_t slot = SIZE_MAX; };

template <typename T, typename U>
void checkFailureAndRetry(U *item, size_t T::*index)
{
    std::vector<U *> items;
    failNextAllocation = true;
    bool failed = false;
    try { membership(items, item, index, true); }
    catch (const std::bad_alloc &) { failed = true; }
    if (!failed || !items.empty() || item->*index != SIZE_MAX)
        throw std::runtime_error("Failed insertion must not publish membership");
    membership(items, item, index, true);
    if (items.size() != 1 || items.front() != item || item->*index != 0)
        throw std::runtime_error("Retry must add membership exactly once");
    membership(items, item, index, false);
    if (!items.empty() || item->*index != SIZE_MAX)
        throw std::runtime_error("Removal after retry must clear membership");
}
}

int main()
{
    try
    {
        Body body;
        Pair pair;
        checkFailureAndRetry(&body, &Body::slot);
        checkFailureAndRetry(static_cast<const Pair *>(&pair), &Pair::slot);
        std::cout << "[PASS] membership allocation failure and retry\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
