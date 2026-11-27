#include "core/membership.h"
#include "test_support.h"

#include <platformator/boxcollider.h>
#include <platformator/rigidbody.h>

#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

namespace
{
thread_local size_t allocationsUntilFailure = 0;
}

void *operator new(std::size_t size)
{
    if (allocationsUntilFailure && --allocationsUntilFailure == 0)
        throw std::bad_alloc();
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
    allocationsUntilFailure = 1;
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

bool checkComponentAttachment(bool replace, bool typed, size_t allocation)
{
    using namespace test_support;
    platformator::Runtime runtime(physicsOnly());
    auto *object = runtime.createGameObject()->addComponent<BoxCollider>(10.f, 20.f);
    if (replace)
        object->addComponent<Rigidbody>(DYNAMIC, false);
    auto *collider = object->getComponent<BoxCollider>();
    auto *old = object->getComponent<Rigidbody>();
    const auto oldHandle = old ? old->getHandle() : platformator::ObjectHandle{};
    auto candidate = std::make_unique<Rigidbody>(nullptr, DYNAMIC, false);
    auto *replacement = candidate.get();
    bool failed = false;
    runtime.simulateFrameWithCustomCallback(dt, [&](double) {
        allocationsUntilFailure = allocation;
        try
        {
            if (typed)
                object->addComponent<Rigidbody>(DYNAMIC, false);
            else
            {
                object->addComponent(replacement);
                candidate.release();
            }
        }
        catch (const std::bad_alloc &)
        {
            failed = true;
        }
        allocationsUntilFailure = 0;
        if (!failed)
        {
            replacement = object->getComponent<Rigidbody>();
            return;
        }
        require(object->getComponent<Rigidbody>() == old && collider->getAttachedRigidbody() == old,
                "Failed attachment must preserve the slot and physics link");
        require(!replacement->getHandle() && replacement->getGameObject() == nullptr,
                "Failed raw attachment must leave ownership with the caller");
        if (old)
            require(runtime.resolveHandle(oldHandle, platformator_detail::ObjectKind::Rigidbody) == old,
                    "A failed replacement must keep the predecessor live");
        object->addComponent(replacement);
        candidate.release();
    });
    require(object->getComponent<Rigidbody>() == replacement && collider->getAttachedRigidbody() == replacement &&
                runtime.getObjectById(replacement->getId()) == replacement,
            "Retry must register the component and link it to the collider");
    if (old)
        require(runtime.resolveHandle(oldHandle, platformator_detail::ObjectKind::Rigidbody) == nullptr,
                "Successful replacement must retire the predecessor");
    replacement->setVelocity({120.f, 0.f});
    runtime.simulateFrame(dt);
    require(std::abs(object->getX() - 1.f) < 0.001f, "Retried rigidbody must integrate once per step");
    object->destroy();
    return failed;
}

void checkRegistrationCapacity()
{
    using namespace test_support;
    platformator::Runtime runtime(physicsOnly());
    auto *first = runtime.createGameObject();
    const auto handle = first->getHandle();
    size_t increases = 1;
    for (int index = 1; index < 512; ++index)
    {
        const auto capacity = runtime.getAllGameObjects().capacity();
        runtime.createGameObject();
        increases += runtime.getAllGameObjects().capacity() != capacity;
    }
    require(increases <= 8 && runtime.resolveHandle(handle, platformator_detail::ObjectKind::GameObject) == first,
            "512 individual registrations must grow geometrically and retain the original handle");
    std::cout << "512 objects used " << increases << " capacity increases\n";
    const auto &objects = runtime.getAllGameObjects();
    runtime.simulateFrameWithCustomCallback(dt, [&](double) {
        for (auto *object : objects)
            object->addComponent<Rigidbody>(DYNAMIC, false);
    });
    for (auto *object : objects)
        require(object->getComponent<Rigidbody>()->getIsRegisteredInPhysicsManager(),
                "Components queued together must all reach the physics system");
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
        test_support::headlessEnvironment();
        for (bool replace : {false, true})
        {
            for (bool typed : {false, true})
            {
                size_t allocation = 1;
                while (allocation <= 16 && checkComponentAttachment(replace, typed, allocation))
                    ++allocation;
                test_support::require(allocation > 1 && allocation <= 16,
                                      "The attachment sweep must exercise failures and the successful boundary");
            }
        }
        checkRegistrationCapacity();
        std::cout << "[PASS] membership allocation failure and retry\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
