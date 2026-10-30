#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "document.h"
#include "process.h"

namespace platformator_editor
{
enum class CatalogProtocol
{
    Unavailable,
    CommonRunnerV1
};

enum class CatalogState
{
    Unavailable,
    Stale,
    Current
};

struct CatalogIdentity
{
    std::string canonicalProjectPath;
    std::string profile;
    std::string target;
    std::string executablePath;
    std::string binaryIdentity;
    std::string sourceInputIdentity;

    bool operator==(const CatalogIdentity &) const = default;
};

struct BehaviorCatalogSnapshot
{
    CatalogIdentity identity;
    Json document;
};

class BehaviorCatalog
{
  public:
    void configure(CatalogProtocol protocol, CatalogIdentity identity);
    void invalidate(std::string message);
    CatalogProtocol protocol() const noexcept;
    CatalogState state() const noexcept;
    bool can_query() const noexcept;
    bool can_build_only() const noexcept;
    bool can_automatic_run() const noexcept;
    const std::string &last_error() const noexcept;
    std::shared_ptr<const BehaviorCatalogSnapshot> snapshot() const noexcept;

    bool begin_query();
    bool append_stdout(std::string_view bytes);
    bool append_stderr(std::string_view bytes);
    bool finish_query(int exitCode, std::chrono::milliseconds elapsed, bool timedOut = false);

  private:
    bool append(std::string &stream, std::string_view bytes);
    bool reject(std::string message);

    CatalogProtocol protocol_ = CatalogProtocol::Unavailable;
    CatalogIdentity identity_;
    CatalogState state_ = CatalogState::Unavailable;
    std::shared_ptr<const BehaviorCatalogSnapshot> snapshot_;
    std::string stdout_;
    std::string stderr_;
    std::string error_;
    bool queryActive_ = false;
    bool outputExceeded_ = false;
};

class BehaviorCatalogQuery
{
  public:
    BehaviorCatalogQuery() = default;
    ~BehaviorCatalogQuery();
    BehaviorCatalogQuery(const BehaviorCatalogQuery &) = delete;
    BehaviorCatalogQuery &operator=(const BehaviorCatalogQuery &) = delete;

    bool start(BehaviorCatalog &catalog, ProcessRequest request, std::uint64_t generation);
    void poll();
    void stop();
    bool running() const noexcept;

  private:
    ProcessService process_;
    BehaviorCatalog *catalog_ = nullptr;
    std::chrono::steady_clock::time_point startedAt_{};
    std::uint64_t generation_ = 0;
    bool stopRequested_ = false;
    bool timedOut_ = false;
};
} // namespace platformator_editor
