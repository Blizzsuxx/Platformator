#pragma once

#include <filesystem>
#include <chrono>
#include <functional>
#include <atomic>
#include <limits>
#include <cstdint>
#include <optional>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "sessionids.h"

namespace platformator_editor
{
using EditorWaitClock = std::chrono::steady_clock;

class CoalescedWakeGate
{
  public:
    bool try_queue() noexcept
    {
        bool expected = false;
        return queued_.compare_exchange_strong(expected, true,
                                               std::memory_order_acq_rel,
                                               std::memory_order_relaxed);
    }
    void event_consumed() noexcept { queued_.store(false, std::memory_order_release); }
    void queue_failed() noexcept { queued_.store(false, std::memory_order_release); }
    bool pending() const noexcept { return queued_.load(std::memory_order_acquire); }

  private:
    std::atomic<bool> queued_{false};
};

// Selects the earliest known deadline, with a short polling cap only for
// services that still require owner-thread polling. The returned timeout is
// rounded up so sub-millisecond deadlines do not turn into a busy loop.
std::chrono::milliseconds editor_wait_duration(
    EditorWaitClock::time_point now,
    std::span<const EditorWaitClock::time_point> deadlines,
    EditorWaitClock::duration maximumWait,
    std::optional<EditorWaitClock::duration> pollingInterval = std::nullopt) noexcept;

struct DocumentSession;
class DocumentManager;
class EditCommandService;
class EditorPreview;
class ResourceIndex;
class SelectionModel;

struct LaunchOptions
{
    std::filesystem::path projectRoot;
    std::filesystem::path assetRoot;
    std::optional<std::filesystem::path> scenePath;
    bool projectRootProvided = false;
    bool assetRootProvided = false;
    float uiScale = 1.0f;
    bool uiScaleProvided = false;
    bool noRestore = false;
    bool help = false;
};

struct EditorAppProbeOptions
{
    std::size_t frameLimit = 0;
    std::size_t warmupFrames = 0;
    bool hoverInputWorkload = false;
};

struct RunValidationTicket
{
    DocumentIdentity document;
    std::uint64_t requestId = 0;
    constexpr auto operator<=>(const RunValidationTicket &) const noexcept = default;
};

// A validation wait is owned by one Run intent and one exact document
// incarnation. New intents supersede old tickets; context changes and Stop
// retire the ticket before a completion can start a pipeline.
class PendingRunValidation
{
  public:
    std::optional<RunValidationTicket> begin(DocumentIdentity document) noexcept
    {
        if (exhausted_)
            return std::nullopt;
        const RunValidationTicket ticket{document, nextRequestId_};
        if (nextRequestId_ == std::numeric_limits<std::uint64_t>::max())
            exhausted_ = true;
        else
            ++nextRequestId_;
        pending_ = ticket;
        return ticket;
    }

    const std::optional<RunValidationTicket> &ticket() const noexcept { return pending_; }
    bool active() const noexcept { return pending_.has_value(); }

    bool matches(const RunValidationTicket &ticket, DocumentIdentity activeDocument) const noexcept
    {
        return pending_ == ticket && ticket.document == activeDocument;
    }

    bool complete(const RunValidationTicket &ticket, DocumentIdentity activeDocument) noexcept
    {
        if (!matches(ticket, activeDocument))
            return false;
        pending_.reset();
        return true;
    }

    bool cancel() noexcept
    {
        const bool hadPending = pending_.has_value();
        pending_.reset();
        return hadPending;
    }

  private:
    std::optional<RunValidationTicket> pending_;
    std::uint64_t nextRequestId_ = 1;
    bool exhausted_ = false;
};

// Owns the outer tick boundary used by both the normal editor loop and the
// bounded GUI probe. The callback is the real application event/panel/preview/
// present path and returns false when the application requests shutdown.
class EditorAppTickDriver
{
  public:
    explicit EditorAppTickDriver(std::optional<EditorAppProbeOptions> probe = std::nullopt);
    bool tick(const std::function<bool()> &runFrame, const std::function<void()> &idleWait);
    bool should_inject_hover_input() const noexcept;
    void note_input_event(EditorWaitClock::time_point when) noexcept;
    void note_present(EditorWaitClock::time_point when,
                      EditorWaitClock::time_point applicationStart);
    std::size_t frame_count() const noexcept;

  private:
    std::optional<EditorAppProbeOptions> probe_;
    std::size_t frameCount_ = 0;
    std::optional<EditorWaitClock::time_point> inputEventStarted_;
    bool coldPreparationReported_ = false;
};

// Apply one project notification to the active Scene's resource consumers.
// Kept at the application boundary so the owner-thread event path can be
// exercised with a real preview in headless integration tests.
void apply_project_resource_change(ResourceIndex &resources, EditorPreview &preview,
                                   const DocumentSession &document,
                                   const std::filesystem::path &path, bool directory);

bool can_execute_focused_undo(bool animationFocused, const DocumentManager &documents,
                              const EditCommandService &editCommands);
bool can_execute_focused_redo(bool animationFocused, const DocumentManager &documents,
                              const EditCommandService &editCommands);
bool execute_focused_undo(bool animationFocused, DocumentManager &documents,
                          EditCommandService &editCommands, DocumentSession &sceneDocument,
                          SelectionModel &selection, std::string &statusMessage);
bool execute_focused_redo(bool animationFocused, DocumentManager &documents,
                          EditCommandService &editCommands, DocumentSession &sceneDocument,
                          SelectionModel &selection, std::string &statusMessage);
bool sync_saved_animation_tabs(const DocumentManager &documents, const std::vector<TabId> &savedTabs,
                               ResourceIndex &resources, EditorPreview &preview,
                               const DocumentSession &sceneDocument);

LaunchOptions parse_launch_options(int argc, const char *const argv[],
                                  const std::filesystem::path &invocationDirectory);
bool editor_project_root_has_manifest(const std::filesystem::path &projectRoot) noexcept;
std::string launch_help();
int run_editor(int argc, char *argv[]);
int run_editor_probe(int argc, char *argv[], EditorAppProbeOptions options);
} // namespace platformator_editor
