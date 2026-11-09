#include "accessibility.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace platformator_editor
{
namespace
{
bool validate_snapshot(const SemanticSnapshot &snapshot, std::string &error)
{
    if (!snapshot.hostLayoutId || !snapshot.hostGeneration || snapshot.revision == 0)
    {
        error = "semantic snapshots require nonzero host identity and revision";
        return false;
    }
    std::unordered_map<std::string, const SemanticNode *> nodes;
    nodes.reserve(snapshot.nodes.size());
    for (const auto &node : snapshot.nodes)
    {
        if (node.id.empty() || node.name.empty() ||
            !nodes.emplace(node.id, &node).second)
        {
            error = "semantic nodes require unique stable IDs and meaningful names";
            return false;
        }
        if (node.minimum && !std::isfinite(*node.minimum))
        {
            error = "semantic minimum values must be finite";
            return false;
        }
        if (node.maximum && !std::isfinite(*node.maximum))
        {
            error = "semantic maximum values must be finite";
            return false;
        }
        if (node.minimum && node.maximum && *node.minimum > *node.maximum)
        {
            error = "semantic value ranges must be ordered";
            return false;
        }
        if (node.bounds && (!std::isfinite(node.bounds->x) || !std::isfinite(node.bounds->y) ||
            !std::isfinite(node.bounds->width) || !std::isfinite(node.bounds->height) ||
            node.bounds->width < 0.0 || node.bounds->height < 0.0))
        {
            error = "semantic bounds must be finite and nonnegative in size";
            return false;
        }
        std::set<std::string> uniqueActions;
        for (const auto &action : node.actions)
            if (action.empty() || !uniqueActions.insert(action).second)
            {
                error = "semantic action names must be nonempty and unique per node";
                return false;
            }
    }
    for (const auto &node : snapshot.nodes)
        if (!node.parentId.empty() && !nodes.contains(node.parentId))
        {
            error = "semantic parent IDs must refer to nodes in the same snapshot";
            return false;
        }
    for (const auto &nodeId : snapshot.focusedRevealPath)
        if (!nodes.contains(nodeId))
        {
            error = "focused reveal paths must refer to nodes in the published snapshot";
            return false;
        }
    return true;
}

const SemanticNode *find_node(const SemanticSnapshot &snapshot, const std::string &id)
{
    const auto found = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
        [&](const SemanticNode &node) { return node.id == id; });
    return found == snapshot.nodes.end() ? nullptr : &*found;
}

std::optional<std::string_view> panel_for_semantic_node(std::string_view id) noexcept
{
    constexpr std::array panelRoots{
        std::pair{std::string_view{"panel.project"}, std::string_view{"main/panel/project"}},
        std::pair{std::string_view{"panel.scene"}, std::string_view{"main/panel/scene"}},
        std::pair{std::string_view{"panel.inspector"}, std::string_view{"main/panel/inspector"}},
        std::pair{std::string_view{"panel.console"}, std::string_view{"main/panel/console"}},
        std::pair{std::string_view{"panel.run"}, std::string_view{"main/panel/run"}},
        std::pair{std::string_view{"panel.validation"}, std::string_view{"main/panel/validation"}},
        std::pair{std::string_view{"panel.game"}, std::string_view{"main/panel/game"}}};
    for (const auto &[panelId, rootId] : panelRoots)
        if (id == rootId || (id.starts_with(rootId) && id.size() > rootId.size() &&
                             id[rootId.size()] == '/'))
            return panelId;

    constexpr std::string_view hierarchyRoot = "/hierarchy";
    if (id.starts_with("main/tab/"))
    {
        const auto hierarchy = id.find(hierarchyRoot);
        if (hierarchy != std::string_view::npos &&
            (hierarchy + hierarchyRoot.size() == id.size() ||
             id[hierarchy + hierarchyRoot.size()] == '/'))
            return "panel.hierarchy";
    }
    return std::nullopt;
}
} // namespace

std::vector<SemanticSnapshot> partition_semantic_snapshot(
    SemanticSnapshot snapshot, const SemanticHostIdentity &mainHost,
    std::span<const SemanticHostIdentity> detachedHosts,
    const std::map<std::string, HostLayoutId> &panelAssignments)
{
    std::map<HostLayoutId, SemanticHostIdentity> hosts;
    if (mainHost.layoutId && mainHost.generation)
        hosts.emplace(mainHost.layoutId, mainHost);
    for (const auto &host : detachedHosts)
        if (host.layoutId && host.generation && host.layoutId != mainHost.layoutId)
            hosts.insert_or_assign(host.layoutId, host);

    std::map<HostLayoutId, SemanticSnapshot> partitions;
    for (const auto &[layoutId, host] : hosts)
    {
        SemanticSnapshot partition;
        partition.hostLayoutId = layoutId;
        partition.hostGeneration = host.generation;
        partition.revision = snapshot.revision;
        std::string windowId;
        if (layoutId != mainHost.layoutId)
        {
            windowId = "host/window/" + std::to_string(layoutId.value);
            SemanticNode window;
            window.id = windowId;
            window.role = SemanticRole::Window;
            window.name = host.name.empty() ? "Detached editor window" : host.name;
            window.visible = true;
            window.expanded = true;
            partition.nodes.push_back(std::move(window));
        }
        partitions.emplace(layoutId, std::move(partition));
    }
    if (!partitions.contains(mainHost.layoutId))
        return {};

    std::unordered_map<std::string, const SemanticNode *> nodesById;
    nodesById.reserve(snapshot.nodes.size());
    for (const auto &node : snapshot.nodes)
        nodesById.emplace(node.id, &node);
    std::map<std::string, HostLayoutId> owners;
    std::function<HostLayoutId(const SemanticNode &, std::unordered_set<std::string> &)> resolveOwner;
    resolveOwner = [&](const SemanticNode &node, std::unordered_set<std::string> &visiting) {
        if (const auto known = owners.find(node.id); known != owners.end())
            return known->second;
        if (!visiting.insert(node.id).second)
            return mainHost.layoutId;

        HostLayoutId owner = mainHost.layoutId;
        if (const auto panelId = panel_for_semantic_node(node.id))
        {
            const auto assignment = panelAssignments.find(std::string(*panelId));
            if (assignment != panelAssignments.end() && hosts.contains(assignment->second))
                owner = assignment->second;
        }
        else if (!node.parentId.empty())
        {
            const auto parent = nodesById.find(node.parentId);
            if (parent != nodesById.end())
                owner = resolveOwner(*parent->second, visiting);
        }
        visiting.erase(node.id);
        owners.emplace(node.id, owner);
        return owner;
    };

    for (auto &node : snapshot.nodes)
    {
        std::unordered_set<std::string> visiting;
        HostLayoutId owner = resolveOwner(node, visiting);
        auto partition = partitions.find(owner);
        if (partition == partitions.end())
        {
            owner = mainHost.layoutId;
            partition = partitions.find(owner);
        }
        if (owner != mainHost.layoutId)
        {
            const auto parentOwner = owners.find(node.parentId);
            if (node.parentId.empty() || parentOwner == owners.end() || parentOwner->second != owner)
                node.parentId = "host/window/" + std::to_string(owner.value);
        }
        partition->second.nodes.push_back(std::move(node));
    }

    if (!snapshot.focusedRevealPath.empty())
    {
        const auto owner = owners.find(snapshot.focusedRevealPath.back());
        const HostLayoutId hostId = owner == owners.end() ? mainHost.layoutId : owner->second;
        const auto partition = partitions.find(hostId);
        if (partition != partitions.end())
        {
            bool sameHost = true;
            for (const auto &nodeId : snapshot.focusedRevealPath)
            {
                const auto pathOwner = owners.find(nodeId);
                if (pathOwner == owners.end() || pathOwner->second != hostId)
                {
                    sameHost = false;
                    break;
                }
            }
            if (sameHost)
            {
                if (hostId != mainHost.layoutId)
                    partition->second.focusedRevealPath.push_back(
                        "host/window/" + std::to_string(hostId.value));
                partition->second.focusedRevealPath.insert(
                    partition->second.focusedRevealPath.end(), snapshot.focusedRevealPath.begin(),
                    snapshot.focusedRevealPath.end());
            }
        }
    }

    std::vector<SemanticSnapshot> result;
    result.reserve(partitions.size());
    for (auto &[layoutId, partition] : partitions)
    {
        (void)layoutId;
        result.push_back(std::move(partition));
    }
    return result;
}

AccessibilitySnapshots::AccessibilitySnapshots() : ownerThread_(std::this_thread::get_id())
{
}

void AccessibilitySnapshots::set_wake_callback(std::function<void()> callback)
{
    std::lock_guard lock(mutex_);
    wakeCallback_ = std::move(callback);
}

bool AccessibilitySnapshots::publish(SemanticSnapshot snapshot, std::string *error)
{
    if (std::this_thread::get_id() != ownerThread_)
    {
        if (error != nullptr)
            *error = "semantic snapshots must be published by their owner thread";
        return false;
    }
    std::string validationError;
    if (!validate_snapshot(snapshot, validationError))
    {
        if (error != nullptr)
            *error = std::move(validationError);
        return false;
    }

    std::lock_guard lock(mutex_);
    auto &host = hosts_[snapshot.hostLayoutId];
    if (host.latestGeneration && snapshot.hostGeneration < host.latestGeneration)
    {
        if (error != nullptr)
            *error = "semantic snapshot belongs to a retired host generation";
        return false;
    }
    if (host.latestGeneration == snapshot.hostGeneration)
    {
        if (!host.registered)
        {
            if (error != nullptr)
                *error = "retired host generations cannot be republished";
            return false;
        }
        if (host.snapshot && snapshot.revision <= host.snapshot->revision)
        {
            if (error != nullptr)
                *error = "semantic snapshot revisions must increase monotonically";
            return false;
        }
        if (host.snapshot && snapshot.nodes == host.snapshot->nodes &&
            snapshot.focusedRevealPath == host.snapshot->focusedRevealPath)
            return true;
    }
    auto immutable = std::make_shared<const SemanticSnapshot>(std::move(snapshot));
    host.latestGeneration = immutable->hostGeneration;
    host.snapshot = std::move(immutable);
    host.registered = true;
    return true;
}

std::shared_ptr<const SemanticSnapshot> AccessibilitySnapshots::acquire(
    HostLayoutId hostLayoutId, WindowHostGeneration generation) const noexcept
{
    std::lock_guard lock(mutex_);
    const auto found = hosts_.find(hostLayoutId);
    if (found == hosts_.end() || !found->second.registered ||
        found->second.latestGeneration != generation)
        return {};
    return found->second.snapshot;
}

std::vector<std::shared_ptr<const SemanticSnapshot>>
AccessibilitySnapshots::acquire_registered() const
{
    std::lock_guard lock(mutex_);
    std::vector<std::shared_ptr<const SemanticSnapshot>> snapshots;
    snapshots.reserve(hosts_.size());
    for (const auto &[layoutId, host] : hosts_)
    {
        (void)layoutId;
        if (host.registered && host.snapshot)
            snapshots.push_back(host.snapshot);
    }
    return snapshots;
}

bool AccessibilitySnapshots::enqueue_action(SemanticAction action) noexcept
{
    if (action.nodeId.size() > MaximumActionNodeIdBytes ||
        action.action.size() > MaximumActionNameBytes ||
        action.value.size() > MaximumActionValueBytes)
        return false;

    std::function<void()> wake;
    try
    {
        std::lock_guard lock(mutex_);
        const auto host = hosts_.find(action.hostLayoutId);
        if (host == hosts_.end() || !host->second.registered ||
            host->second.latestGeneration != action.hostGeneration ||
            !host->second.snapshot || host->second.snapshot->revision != action.snapshotRevision ||
            actions_.size() >= MaximumQueuedActions)
            return false;
        const auto *node = find_node(*host->second.snapshot, action.nodeId);
        if (node == nullptr || !node->visible || !node->enabled ||
            std::find(node->actions.begin(), node->actions.end(), action.action) == node->actions.end())
            return false;
        wake = wakeCallback_;
        actions_.push_back({std::move(action), host->second.snapshot});
    }
    catch (...)
    {
        // Native callbacks must reject work cleanly under allocation or lock
        // failures; an exception escaping this noexcept boundary would abort
        // the editor process.
        return false;
    }
    if (wake)
    {
        try
        {
            wake();
        }
        catch (...)
        {
        }
    }
    return true;
}

std::size_t AccessibilitySnapshots::dispatch_pending(std::size_t maximumActions,
                                                       const ActionHandler &handler)
{
    if (std::this_thread::get_id() != ownerThread_)
        throw std::logic_error("semantic actions must be dispatched by their owner thread");
    if (!handler || maximumActions == 0)
        return 0;
    std::size_t dispatched = 0;
    while (dispatched < maximumActions)
    {
        QueuedAction queued;
        {
            std::lock_guard lock(mutex_);
            if (actions_.empty())
                break;
            queued = std::move(actions_.front());
            actions_.pop_front();
            const auto host = hosts_.find(queued.action.hostLayoutId);
            if (host == hosts_.end() || !host->second.registered ||
                host->second.latestGeneration != queued.action.hostGeneration ||
                host->second.snapshot != queued.snapshot ||
                host->second.snapshot->revision != queued.action.snapshotRevision)
                continue;
        }
        const auto *node = find_node(*queued.snapshot, queued.action.nodeId);
        if (node == nullptr || !node->visible || !node->enabled ||
            std::find(node->actions.begin(), node->actions.end(), queued.action.action) == node->actions.end())
            continue;
        handler(queued.action, *node);
        ++dispatched;
    }
    return dispatched;
}

void AccessibilitySnapshots::retire(HostLayoutId hostLayoutId,
                                    WindowHostGeneration generation) noexcept
{
    if (std::this_thread::get_id() != ownerThread_)
        return;
    std::lock_guard lock(mutex_);
    const auto host = hosts_.find(hostLayoutId);
    if (host == hosts_.end() || host->second.latestGeneration != generation)
        return;
    host->second.snapshot.reset();
    host->second.registered = false;
    actions_.erase(std::remove_if(actions_.begin(), actions_.end(), [&](const QueuedAction &queued) {
        return queued.action.hostLayoutId == hostLayoutId &&
               queued.action.hostGeneration == generation;
    }), actions_.end());
}

std::size_t AccessibilitySnapshots::pending_actions() const noexcept
{
    std::lock_guard lock(mutex_);
    return actions_.size();
}
} // namespace platformator_editor
