#pragma once

#include "sessionids.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace platformator_editor
{
enum class SemanticRole
{
    Window,
    Group,
    Tab,
    Button,
    CheckBox,
    Slider,
    Text,
    TextField,
    ListItem,
    ProgressBar
};

struct SemanticBounds
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    bool operator==(const SemanticBounds &) const = default;
};

struct SemanticNode
{
    std::string id;
    std::string parentId;
    SemanticRole role = SemanticRole::Text;
    std::string name;
    std::string help;
    std::string value;
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<SemanticBounds> bounds;
    bool visible = true;
    bool enabled = true;
    bool selected = false;
    bool expanded = false;
    bool focused = false;
    bool error = false;
    std::vector<std::string> actions;

    bool operator==(const SemanticNode &) const = default;
};

struct SemanticSnapshot
{
    HostLayoutId hostLayoutId;
    WindowHostGeneration hostGeneration;
    std::uint64_t revision = 0;
    std::vector<SemanticNode> nodes;
    std::vector<std::string> focusedRevealPath;
};

struct SemanticHostIdentity
{
    HostLayoutId layoutId;
    WindowHostGeneration generation;
    std::string name;
};

struct SemanticAction
{
    HostLayoutId hostLayoutId;
    WindowHostGeneration hostGeneration;
    std::uint64_t snapshotRevision = 0;
    std::string nodeId;
    std::string action;
    std::string value;
};

std::vector<SemanticSnapshot> partition_semantic_snapshot(
    SemanticSnapshot snapshot, const SemanticHostIdentity &mainHost,
    std::span<const SemanticHostIdentity> detachedHosts,
    const std::map<std::string, HostLayoutId> &panelAssignments);

class AccessibilitySnapshots
{
  public:
    using ActionHandler = std::function<void(const SemanticAction &, const SemanticNode &)>;
    static constexpr std::size_t MaximumQueuedActions = 256;
    static constexpr std::size_t MaximumActionNodeIdBytes = 1024;
    static constexpr std::size_t MaximumActionNameBytes = 64;
    static constexpr std::size_t MaximumActionValueBytes = 4096;

    AccessibilitySnapshots();
    void set_wake_callback(std::function<void()> callback);
    bool publish(SemanticSnapshot snapshot, std::string *error = nullptr);
    std::shared_ptr<const SemanticSnapshot> acquire(
        HostLayoutId hostLayoutId, WindowHostGeneration generation) const noexcept;
    std::vector<std::shared_ptr<const SemanticSnapshot>> acquire_registered() const;
    bool enqueue_action(SemanticAction action) noexcept;
    std::size_t dispatch_pending(std::size_t maximumActions, const ActionHandler &handler);
    void retire(HostLayoutId hostLayoutId, WindowHostGeneration generation) noexcept;
    std::size_t pending_actions() const noexcept;

  private:
    struct HostState
    {
        WindowHostGeneration latestGeneration;
        std::shared_ptr<const SemanticSnapshot> snapshot;
        bool registered = false;
    };
    struct QueuedAction
    {
        SemanticAction action;
        std::shared_ptr<const SemanticSnapshot> snapshot;
    };

    const std::thread::id ownerThread_;
    mutable std::mutex mutex_;
    std::map<HostLayoutId, HostState> hosts_;
    std::deque<QueuedAction> actions_;
    std::function<void()> wakeCallback_;
};
} // namespace platformator_editor
