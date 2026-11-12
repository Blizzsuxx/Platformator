#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <stdexcept>
#include "platformator/objecthandle.h"

namespace platformator_detail
{
class ObjectRegistry;
}
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
    template <typename T> struct ReleaseReference
    {
        void operator()(T *asset) const
        {
            if (asset != nullptr)
            {
                asset->removeReferenceAndFreeIfNoReferences();
            }
        }
    };

    template <typename T> using Reference = std::unique_ptr<T, ReleaseReference<T>>;

    template <typename T> static Reference<T> retain(T *asset)
    {
        if (asset != nullptr)
        {
            asset->addReference();
        }
        return Reference<T>(asset);
    }

    virtual ~Asset() = default;

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

    size_t getReferenceCount() const
    {
        return referenceCount;
    }

    void addReference()
    {
        ++referenceCount;
    }

    bool removeReferenceAndFreeIfNoReferences()
    {
        if (referenceCount > 0)
        {
            --referenceCount;
        }
        if (referenceCount == 0 && !filePath.empty())
        {
            freeFromRuntime();
            return true;
        }
        return false;
    }

  protected:
    virtual void freeFromRuntime()
    {
    }

  private:
    uint64_t runtimeSerial = 0;
    std::string filePath;
    size_t referenceCount = 0;
};
