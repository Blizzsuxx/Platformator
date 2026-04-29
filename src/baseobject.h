#pragma once

#include <cstdint>
#include <limits>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_set>

class BaseObject
{
protected:
    BaseObject() : id(generateId())
    {
    }

    explicit BaseObject(uint64_t id) : id(0)
    {
        assignId(id);
    }

    BaseObject(const BaseObject &) = delete;
    BaseObject &operator=(const BaseObject &) = delete;
    BaseObject(BaseObject &&) = delete;
    BaseObject &operator=(BaseObject &&) = delete;

    ~BaseObject()
    {
        releaseId(id);
    }

public:
    uint64_t getId() const
    {
        return id;
    }

    void setId(uint64_t value)
    {
        assignId(value);
    }

protected:
    uint64_t id;

private:
    struct IdRegistry
    {
        std::mutex mutex;
        std::unordered_set<uint64_t> usedIds;
        std::mt19937_64 generator;
        std::uniform_int_distribution<uint64_t> distribution;

        IdRegistry()
            : mutex(), usedIds(), generator(std::random_device{}()),
              distribution(1, static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        {
        }
    };

    static uint64_t generateId()
    {
        IdRegistry &registry = idRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);

        while (true)
        {
            const uint64_t value = registry.distribution(registry.generator);
            if (registry.usedIds.insert(value).second)
            {
                return value;
            }
        }
    }

    static void releaseId(uint64_t value)
    {
        if (value == 0)
        {
            return;
        }

        IdRegistry &registry = idRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        registry.usedIds.erase(value);
    }

    void assignId(uint64_t value)
    {
        if (value == 0 || value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        {
            throw std::invalid_argument("BaseObject ids must be positive signed 64-bit values.");
        }

        IdRegistry &registry = idRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        if (id == value)
        {
            return;
        }

        if (registry.usedIds.contains(value))
        {
            throw std::runtime_error("Duplicate BaseObject id " + std::to_string(value) + '.');
        }

        if (id != 0)
        {
            registry.usedIds.erase(id);
        }

        registry.usedIds.insert(value);
        id = value;
    }

    static IdRegistry &idRegistry()
    {
        // The engine tears down GameManager-owned objects during static shutdown,
        // so the id registry must outlive other function-local statics.
        static IdRegistry *registry = new IdRegistry();
        return *registry;
    }
};