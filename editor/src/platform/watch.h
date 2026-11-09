#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../projectchanges.h"

namespace platformator_editor
{
inline constexpr std::size_t ProjectWatchDirectoryLimit = 4096;

class ProjectWatchBudget
{
  public:
    explicit ProjectWatchBudget(std::size_t limit = ProjectWatchDirectoryLimit) noexcept
        : limit_(limit) {}

    bool try_acquire() noexcept
    {
        auto current = active_.load(std::memory_order_relaxed);
        while (current < limit_)
            if (active_.compare_exchange_weak(current, current + 1,
                                              std::memory_order_acq_rel,
                                              std::memory_order_relaxed))
                return true;
        return false;
    }

    void release(std::size_t count = 1) noexcept
    {
        auto current = active_.load(std::memory_order_relaxed);
        while (current != 0)
        {
            const auto released = count < current ? count : current;
            if (active_.compare_exchange_weak(current, current - released,
                                              std::memory_order_acq_rel,
                                              std::memory_order_relaxed))
                return;
        }
    }

    std::size_t active() const noexcept { return active_.load(std::memory_order_acquire); }
    std::size_t limit() const noexcept { return limit_; }

  private:
    const std::size_t limit_;
    std::atomic<std::size_t> active_{0};
};

struct ProjectWatchOptions
{
    // Watch only directories that can contain configured CMake/source inputs. This
    // allows project source monitoring to avoid build/vendor trees with no source value.
    bool sourceInputsOnly = false;
    std::shared_ptr<ProjectWatchBudget> budget;
    // Source-only project watches can include a separately configured asset tree
    // without starting a second native watcher when it is nested under the root.
    std::vector<std::filesystem::path> includedSubtrees;
    // Disjoint roots share the same native notification owner when the platform
    // backend supports monitoring multiple trees in one service.
    std::vector<std::filesystem::path> additionalRoots;
};

class ProjectWatchService
{
  public:
    using Callback = std::function<void(ProjectChangeEvent)>;

    virtual ~ProjectWatchService() = default;
    virtual bool start(ProjectGeneration generation, const std::filesystem::path &projectRoot,
                       Callback callback, std::string &error,
                       ProjectWatchOptions options = {}) = 0;
    virtual void stop() noexcept = 0;
    virtual std::size_t watched_directories() const noexcept = 0;
    virtual bool supported() const noexcept = 0;
};

std::unique_ptr<ProjectWatchService> make_project_watch_service();
} // namespace platformator_editor
