#pragma once

#include "documentmanager.h"
#include "project.h"
#include "projectchanges.h"
#include "platform/watch.h"
#include "resourceindex.h"

#include <filesystem>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace platformator_editor
{
bool should_skip_project_directory(std::string_view name) noexcept;

// Project-scoped editor state. The application owns its authoring Runtime;
// this aggregate owns the project context, document tabs, resource index and
// project-scoped change monitoring. GamePipeline owns child play-session state.
class ProjectSession
{
  public:
    ProjectSession(std::filesystem::path projectRoot,
                   std::filesystem::path assetRoot);
    ~ProjectSession();
    ProjectSession(const ProjectSession &) = delete;
    ProjectSession &operator=(const ProjectSession &) = delete;

    ProjectContext &project_context() noexcept;
    const ProjectContext &project_context() const noexcept;
    DocumentManager &documents() noexcept;
    const DocumentManager &documents() const noexcept;
    ProjectGeneration generation() const noexcept;
    ProjectChanges &project_changes() noexcept;
    ResourceIndex &resource_index() noexcept;
    ProjectChangeBatch take_change_batch(std::size_t maxEvents = 256);
    const std::string &monitor_error() const noexcept;
    std::uint64_t source_change_revision() const noexcept;
    bool source_notifications_available() const noexcept;
    void set_wake_callback(std::function<void()> callback);

    WorkspaceActionResult set_project_roots(const std::filesystem::path &projectRoot,
                                            const std::filesystem::path &assetRoot,
                                            bool discardDirtyDocuments = false);

  private:
    friend struct ProjectSessionTestProbe;
    void start_monitoring(const ProjectSnapshot &snapshot);
    void stop_monitoring() noexcept;
    void publish_source_change(const ProjectSnapshot &snapshot,
                               const ProjectChangeEvent &event);
    void queue_asset_change(ProjectChangeEvent event);

    ProjectContext projectContext_;
    DocumentManager documents_;
    ProjectChanges projectChanges_;
    ResourceIndex resourceIndex_;
    std::shared_ptr<ProjectWatchBudget> watchBudget_;
    std::unique_ptr<ProjectWatchService> watcher_;
    std::atomic<std::uint64_t> sourceChangeRevision_{0};
    std::atomic<bool> sourceNotificationsAvailable_{false};
    std::mutex inboxMutex_;
    std::deque<ProjectChangeEvent> inbox_;
    std::size_t inboxBytes_ = 0;
    bool inboxOverflow_ = false;
    std::mutex wakeMutex_;
    std::function<void()> wakeCallback_;
    std::string monitorError_;
};
} // namespace platformator_editor
