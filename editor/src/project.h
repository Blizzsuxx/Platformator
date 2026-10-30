#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <oneapi/tbb/task_group.h>

namespace platformator_editor
{
struct ProjectSnapshot
{
    std::filesystem::path projectRoot;
    std::filesystem::path assetRoot;
    std::uint64_t generation = 0;
};

struct ProjectRootCandidate
{
    std::filesystem::path projectRoot;
    std::filesystem::path assetRoot;
};

// Project-folder selection accepts only the repository's existing conventions.
ProjectRootCandidate validate_project_folder(const std::filesystem::path &projectRoot);

class ProjectContext
{
  public:
    ProjectContext() = default;
    void set_roots(const std::filesystem::path &projectRoot, const std::filesystem::path &assetRoot);
    const ProjectSnapshot &snapshot() const noexcept;

  private:
    ProjectSnapshot snapshot_;
};

struct ProjectScanOptions
{
    bool recursive = false;
    std::size_t maxFiles = 10000;
    bool includeDirectories = false;
    std::size_t maxDirectories = 2000;
    std::size_t maxEntries = 50000;
    bool sourceInputsOnly = false;
    bool trackFileMetadata = false;
};

struct ProjectScanResult
{
    std::uint64_t requestId = 0;
    std::uint64_t projectGeneration = 0;
    std::filesystem::path root;
    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> directories;
    std::string sourceInputIdentity;
    std::string fileIdentity;
    bool cancelled = false;
    bool truncated = false;
    std::string error;
};

struct ProjectScanTicketState;

class ProjectScanToken
{
  public:
    ProjectScanToken() = default;
    explicit operator bool() const noexcept;
    std::uint64_t request_id() const noexcept;
    void cancel() const noexcept;

  private:
    friend class ProjectScanService;
    explicit ProjectScanToken(std::shared_ptr<ProjectScanTicketState> state);
    std::shared_ptr<ProjectScanTicketState> state_;
};

struct ProjectScanSharedState;

class ProjectScanService
{
  public:
    explicit ProjectScanService(std::size_t maxPendingPublications = 8,
                                std::size_t maxOutstandingScans = 8);
    ~ProjectScanService();
    ProjectScanService(const ProjectScanService &) = delete;
    ProjectScanService &operator=(const ProjectScanService &) = delete;

    ProjectScanToken request_scan(const ProjectSnapshot &project,
                                  const std::filesystem::path &scanRoot,
                                  ProjectScanOptions options = {});
    std::vector<ProjectScanResult> take_ready(std::uint64_t currentProjectGeneration);
    void cancel_all() noexcept;
    // Cancel and join current work, clear old results, and leave the service reusable.
    void cancel_pending_and_wait();
    // Use at shutdown or in tests, never to wait during an interactive frame.
    void wait_for_idle();

  private:
    std::shared_ptr<ProjectScanSharedState> state_;
    std::vector<std::weak_ptr<ProjectScanTicketState>> tickets_;
    oneapi::tbb::task_group tasks_;
    std::uint64_t nextRequestId_ = 1;
    bool shuttingDown_ = false;
};
} // namespace platformator_editor
