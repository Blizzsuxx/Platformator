#pragma once

#include <string>
#include <utility>
#include <stdexcept>
#include "platformator/objecthandle.h"

namespace platformator_detail
{
class ObjectRegistry;
}
class PhysicsPairState;
enum class ObjectLifecycle : uint8_t
{
    Staged,
    Live,
    PendingRetire,
    DestroyCallbacks,
    Unregistered,
    Freed
};

class BaseObject
{
    friend class platformator_detail::ObjectRegistry;
    friend class PhysicsPairState;
    friend class GameManager;

  protected:
    BaseObject() = default;
    explicit BaseObject(int loadedId) : id(loadedId)
    {
    }

  public:
    BaseObject(const BaseObject &) = delete;
    BaseObject &operator=(const BaseObject &) = delete;
    platformator::ObjectHandle getHandle() const
    {
        return handle;
    }
    ObjectLifecycle getLifecycle() const
    {
        return lifecycle;
    }
    bool isRetired() const
    {
        return lifecycle >= ObjectLifecycle::PendingRetire;
    }
    int getId() const
    {
        return id;
    }

    void setId(int value)
    {
        if (handle)
            throw std::logic_error("Registered IDs cannot be changed");
        if (value < 0)
            throw std::invalid_argument("File IDs must be nonnegative");
        id = value;
    }

  protected:
    int id = -1;

  private:
    platformator::ObjectHandle handle;
    ObjectLifecycle lifecycle = ObjectLifecycle::Staged;
};

class Asset
{
    friend class GameManager;

  protected:
    Asset(std::string filePath) : filePath(std::move(filePath))
    {
    }

    Asset() : filePath("")
    {
    }

  public:
    uint64_t getRuntimeSerial() const
    {
        return runtimeSerial;
    }
    const std::string &getFilePath() const
    {
        return filePath;
    }

    void setFilePath(const std::string &newFilePath)
    {
        filePath = newFilePath;
    }

  private:
    uint64_t runtimeSerial = 0;
    std::string filePath;
};
