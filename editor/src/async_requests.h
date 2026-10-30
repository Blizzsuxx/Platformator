#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace platformator_editor
{
enum class AsyncRequestKind : std::uint8_t
{
    OpenScene,
    OpenAsset,
    SaveScene,
    SelectFolder
};

enum class AsyncRequestOutcome : std::uint8_t
{
    Selected,
    Cancelled,
    Failed
};

struct AsyncRequestIdentity
{
    std::uint64_t requestId = 0;
    std::uint64_t sessionGeneration = 0;
    std::uint64_t documentGeneration = 0;
    AsyncRequestKind kind = AsyncRequestKind::OpenScene;
};

struct AsyncRequestResult
{
    AsyncRequestIdentity identity;
    AsyncRequestOutcome outcome = AsyncRequestOutcome::Failed;
    std::filesystem::path selectedPath;
    std::string error;
};

struct AsyncRequestSharedState;
struct AsyncRequestTicket;

class AsyncRequestToken
{
  public:
    AsyncRequestToken() = default;

    explicit operator bool() const noexcept;
    const AsyncRequestIdentity &identity() const noexcept;
    void complete_selected(std::string_view path) const;
    void complete_cancelled() const;
    void complete_error(std::string_view message) const;

  private:
    friend class AsyncRequestQueue;
    explicit AsyncRequestToken(std::shared_ptr<AsyncRequestTicket> ticket);

    std::shared_ptr<AsyncRequestTicket> ticket_;
};

class AsyncRequestQueue
{
  public:
    explicit AsyncRequestQueue(std::uint64_t sessionGeneration);
    ~AsyncRequestQueue();
    AsyncRequestQueue(const AsyncRequestQueue &) = delete;
    AsyncRequestQueue &operator=(const AsyncRequestQueue &) = delete;

    AsyncRequestToken begin(AsyncRequestKind kind, std::uint64_t documentGeneration);
    std::vector<AsyncRequestResult> take_ready(std::uint64_t currentDocumentGeneration);
    void shutdown() noexcept;

  private:
    std::shared_ptr<AsyncRequestSharedState> state_;
};
} // namespace platformator_editor
