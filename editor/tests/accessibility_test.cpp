#include "accessibility.h"
#include "assetdocument.h"
#include "panels/animation.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace platformator_editor;

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

SemanticSnapshot make_snapshot(WindowHostGeneration generation, std::uint64_t revision,
                               std::string value = "ready")
{
    SemanticSnapshot snapshot;
    snapshot.hostLayoutId = HostLayoutId{8};
    snapshot.hostGeneration = generation;
    snapshot.revision = revision;
    snapshot.nodes = {
        SemanticNode{.id = "animation.window", .role = SemanticRole::Window,
                     .name = "Animation Assets", .visible = true, .enabled = true,
                     .expanded = true},
        SemanticNode{.id = "animation.save", .parentId = "animation.frames",
                     .role = SemanticRole::Button, .name = "Save animation",
                     .value = std::move(value), .visible = true, .enabled = true,
                     .focused = true, .actions = {"activate"}},
        SemanticNode{.id = "animation.loop", .parentId = "animation.window",
                     .role = SemanticRole::CheckBox, .name = "Loop animation",
                     .value = "on", .visible = true, .enabled = true,
                     .selected = true, .actions = {"toggle"}},
        SemanticNode{.id = "animation.error", .parentId = "animation.window",
                     .role = SemanticRole::Text, .name = "Animation validation status",
                     .value = "Invalid duration", .visible = true, .enabled = false,
                     .error = true},
        SemanticNode{.id = "animation.frames", .parentId = "animation.window",
                     .role = SemanticRole::Group, .name = "Animation frames",
                     .visible = true, .enabled = true, .expanded = true}};
    snapshot.focusedRevealPath = {"animation.window", "animation.frames", "animation.save"};
    return snapshot;
}

void test_immutable_snapshot_publication_and_action_routing()
{
    AccessibilitySnapshots snapshots;
    const HostLayoutId host{8};
    const WindowHostGeneration firstGeneration{1};
    bool ownerWakeRequested = false;
    bool wakeObservedQueuedAction = false;
    snapshots.set_wake_callback([&] {
        ownerWakeRequested = true;
        wakeObservedQueuedAction = snapshots.pending_actions() == 1;
    });
    std::string error;
    require(snapshots.publish(make_snapshot(firstGeneration, 1), &error),
            "valid semantic snapshots must publish on their owner thread");
    const auto first = snapshots.acquire(host, firstGeneration);
    require(first && first->nodes.size() == 5 && first->focusedRevealPath.size() == 3 &&
                first->nodes[0].role == SemanticRole::Window && first->nodes[0].expanded &&
                first->nodes[1].role == SemanticRole::Button &&
                first->nodes[1].name == "Save animation" && first->nodes[1].value == "ready" &&
                first->nodes[1].focused && first->nodes[2].selected && first->nodes[3].error &&
                first->nodes[4].role == SemanticRole::Group && first->nodes[4].expanded,
            "snapshots expose meaningful roles, names, values, state flags and the focused reveal path");

    require(snapshots.publish(make_snapshot(firstGeneration, 2), &error),
            "a repeated semantic frame with a newer producer revision is accepted");
    const auto unchanged = snapshots.acquire(host, firstGeneration);
    require(unchanged == first && unchanged->revision == 1,
            "unchanged semantic frames retain the immutable publication and avoid invalidating actions");

    SemanticAction staleAction{host, firstGeneration, 1, "animation.save", "activate", {}};
    require(snapshots.enqueue_action(staleAction),
            "native callbacks may enqueue only an advertised enabled action");
    require(ownerWakeRequested && wakeObservedQueuedAction,
            "a validated action is queued before the wake callback runs outside the snapshot lock");
    require(snapshots.publish(make_snapshot(firstGeneration, 3, "saved draft"), &error),
            "a newer immutable snapshot must replace the current publication");
    require(first->revision == 1 && first->nodes[1].value == "ready",
            "provider-held snapshots remain immutable and alive after publication changes");
    std::size_t called = 0;
    require(snapshots.dispatch_pending(4, [&](const SemanticAction &, const SemanticNode &) {
                ++called;
            }) == 0 && called == 0,
            "queued actions from a superseded snapshot must be discarded");

    const SemanticAction current{host, firstGeneration, 3, "animation.save", "activate", {}};
    require(snapshots.enqueue_action(current),
            "a current action with the advertised identity may be queued");
    require(!snapshots.enqueue_action({host, firstGeneration, 3, "animation.error", "activate", {}}) &&
                !snapshots.enqueue_action({host, firstGeneration, 3, "animation.loop", "delete", {}}),
            "disabled controls and unadvertised actions must be rejected");
    require(snapshots.dispatch_pending(1, [&](const SemanticAction &action, const SemanticNode &node) {
                require(action.nodeId == "animation.save" && node.name == "Save animation",
                        "the owner handler receives validated current action data");
                ++called;
            }) == 1 && called == 1,
            "only the owner thread dispatches a validated action to the control handler");
}

void test_wake_callback_exception_does_not_drop_action()
{
    AccessibilitySnapshots snapshots;
    const HostLayoutId host{8};
    const WindowHostGeneration generation{1};
    require(snapshots.publish(make_snapshot(generation, 1)),
            "the wake exception case publishes its host snapshot");
    snapshots.set_wake_callback([] { throw std::runtime_error("wake failed"); });
    require(snapshots.enqueue_action({host, generation, 1, "animation.save", "activate", {}}),
            "an exception from the event-loop wake callback must not escape the native enqueue boundary");
    std::size_t dispatched = 0;
    require(snapshots.dispatch_pending(1, [&](const SemanticAction &, const SemanticNode &) {
                ++dispatched;
            }) == 1 && dispatched == 1,
            "a queued owner-thread action remains dispatchable after wake failure");
}

void test_retired_host_generation_and_queue_bound()
{
    AccessibilitySnapshots snapshots;
    const HostLayoutId host{8};
    const WindowHostGeneration generation{1};
    require(snapshots.publish(make_snapshot(generation, 1)),
            "the first host incarnation must publish");
    for (std::size_t index = 0; index < AccessibilitySnapshots::MaximumQueuedActions; ++index)
        require(snapshots.enqueue_action({host, generation, 1, "animation.save", "activate", {}}),
                "the bounded provider action queue accepts entries up to its cap");
    require(!snapshots.enqueue_action({host, generation, 1, "animation.save", "activate", {}}) &&
                snapshots.pending_actions() == AccessibilitySnapshots::MaximumQueuedActions,
            "provider backpressure rejects actions without exceeding the fixed queue bound");

    snapshots.retire(host, generation);
    require(!snapshots.acquire(host, generation) &&
                !snapshots.enqueue_action({host, generation, 1, "animation.save", "activate", {}}) &&
                !snapshots.publish(make_snapshot(generation, 2)) && snapshots.pending_actions() == 0,
            "retirement invalidates acquisition, drains retained actions and rejects late callbacks");
    const WindowHostGeneration replacement{2};
    require(snapshots.publish(make_snapshot(replacement, 1), nullptr),
            "a replacement native window may reuse its persistent layout under a fresh generation");
    std::size_t dispatched = 0;
    require(snapshots.dispatch_pending(AccessibilitySnapshots::MaximumQueuedActions,
                [&](const SemanticAction &, const SemanticNode &) { ++dispatched; }) == 0 &&
                dispatched == 0 && snapshots.pending_actions() == 0,
            "retired-generation actions drain without touching the replacement host");
}

void test_snapshot_validation_and_owner_thread()
{
    AccessibilitySnapshots snapshots;
    auto duplicate = make_snapshot(WindowHostGeneration{1}, 1);
    duplicate.nodes.push_back(duplicate.nodes.front());
    std::string error;
    require(!snapshots.publish(std::move(duplicate), &error) && !error.empty(),
            "duplicate semantic IDs are rejected before publication");

    auto invalidBounds = make_snapshot(WindowHostGeneration{1}, 1);
    invalidBounds.nodes[1].bounds = SemanticBounds{0, 0, -1, 4};
    require(!snapshots.publish(std::move(invalidBounds), &error),
            "invalid control bounds are rejected");

    bool workerPublishAccepted = true;
    std::thread worker([&] {
        workerPublishAccepted = snapshots.publish(make_snapshot(WindowHostGeneration{1}, 1));
    });
    worker.join();
    require(!workerPublishAccepted,
            "native worker callbacks cannot publish or mutate owner-thread snapshots");

    bool wrongThreadDispatchRejected = false;
    std::thread dispatcher([&] {
        try
        {
            snapshots.dispatch_pending(1, [](const SemanticAction &, const SemanticNode &) {});
        }
        catch (const std::logic_error &)
        {
            wrongThreadDispatchRejected = true;
        }
    });
    dispatcher.join();
    require(wrongThreadDispatchRejected,
            "semantic action handlers can run only on the registered owner thread");
}

void test_numeric_value_actions_match_the_published_contract()
{
    AccessibilitySnapshots snapshots;
    SemanticSnapshot snapshot;
    snapshot.hostLayoutId = HostLayoutId{8};
    snapshot.hostGeneration = WindowHostGeneration{1};
    snapshot.revision = 1;
    snapshot.nodes = {
        SemanticNode{.id = "panel", .role = SemanticRole::Group,
                     .name = "Run settings", .visible = true, .enabled = true},
        SemanticNode{.id = "panel/width", .parentId = "panel",
                     .role = SemanticRole::Slider, .name = "Window width",
                     .value = "640", .minimum = 1.0, .maximum = 16384.0,
                     .visible = true, .enabled = true, .actions = {"set_value"}}};
    require(snapshots.publish(std::move(snapshot)),
            "numeric accessibility fields must publish their value-setting contract");

    const SemanticAction current{HostLayoutId{8}, WindowHostGeneration{1}, 1,
                                 "panel/width", "set_value", "1280"};
    require(snapshots.enqueue_action(current) &&
                !snapshots.enqueue_action({HostLayoutId{8}, WindowHostGeneration{1}, 1,
                                           "panel/width", "set-value", "1280"}) &&
                !snapshots.enqueue_action({HostLayoutId{8}, WindowHostGeneration{1}, 1,
                    "panel/width", "set_value",
                    std::string(AccessibilitySnapshots::MaximumActionValueBytes + 1, '9')}),
            "provider value changes use the published set_value spelling and stay within the fixed payload bound");
    std::size_t dispatched = 0;
    require(snapshots.dispatch_pending(1, [&](const SemanticAction &action, const SemanticNode &node) {
                require(action.value == "1280" && node.name == "Window width",
                        "the owner thread receives the requested numeric value and current semantic node");
                ++dispatched;
            }) == 1 && dispatched == 1,
            "the advertised numeric value action reaches its validated owner-thread handler");
}

void test_native_action_metadata_is_bounded()
{
    AccessibilitySnapshots snapshots;
    SemanticSnapshot longNodeId;
    longNodeId.hostLayoutId = HostLayoutId{8};
    longNodeId.hostGeneration = WindowHostGeneration{1};
    longNodeId.revision = 1;
    longNodeId.nodes.push_back(SemanticNode{
        .id = std::string(AccessibilitySnapshots::MaximumActionNodeIdBytes + 1, 'n'),
        .role = SemanticRole::Button, .name = "Long identifier target",
        .visible = true, .enabled = true, .actions = {"activate"}});
    require(snapshots.publish(std::move(longNodeId)),
            "semantic snapshots may retain stable IDs independently of incoming action bounds");
    require(!snapshots.enqueue_action({HostLayoutId{8}, WindowHostGeneration{1}, 1,
                std::string(AccessibilitySnapshots::MaximumActionNodeIdBytes + 1, 'n'),
                "activate", {}}),
            "native callbacks must reject an oversized node ID before queueing work");

    SemanticSnapshot longAction;
    longAction.hostLayoutId = HostLayoutId{8};
    longAction.hostGeneration = WindowHostGeneration{1};
    longAction.revision = 2;
    longAction.nodes.push_back(SemanticNode{
        .id = "bounded-action", .role = SemanticRole::Button,
        .name = "Long action target", .visible = true, .enabled = true,
        .actions = {std::string(AccessibilitySnapshots::MaximumActionNameBytes + 1, 'a')}});
    require(snapshots.publish(std::move(longAction)),
            "a newer provider snapshot may advertise an oversized action for rejection coverage");
    require(!snapshots.enqueue_action({HostLayoutId{8}, WindowHostGeneration{1}, 2,
                "bounded-action",
                std::string(AccessibilitySnapshots::MaximumActionNameBytes + 1, 'a'), {}}),
            "native callbacks must reject oversized action names before scanning advertised actions");
}

void test_semantic_snapshots_follow_detached_panel_hosts()
{
    SemanticSnapshot source;
    source.hostLayoutId = main_host_layout_id;
    source.hostGeneration = WindowHostGeneration{1};
    source.revision = 7;
    source.nodes = {
        SemanticNode{.id = "main/window", .role = SemanticRole::Window,
                     .name = "Platformator Editor", .visible = true, .expanded = true},
        SemanticNode{.id = "main/panel/scene", .parentId = "main/window",
                     .role = SemanticRole::Group, .name = "Scene", .visible = true,
                     .expanded = true},
        SemanticNode{.id = "main/panel/scene/select", .parentId = "main/panel/scene",
                     .role = SemanticRole::Button, .name = "Select scene object",
                     .visible = true, .enabled = true, .actions = {"activate"}},
        SemanticNode{.id = "main/panel/inspector", .parentId = "main/window",
                     .role = SemanticRole::Group, .name = "Inspector", .visible = true,
                     .expanded = true}};
    source.focusedRevealPath = {"main/panel/scene", "main/panel/scene/select"};

    const std::array detachedHosts{
        SemanticHostIdentity{HostLayoutId{2}, WindowHostGeneration{4}, "Scene"}};
    const std::map<std::string, HostLayoutId> assignments{
        {"panel.scene", HostLayoutId{2}}, {"panel.inspector", main_host_layout_id}};
    auto partitions = partition_semantic_snapshot(
        std::move(source), {main_host_layout_id, WindowHostGeneration{1}, "Main"},
        detachedHosts, assignments);
    require(partitions.size() == 2,
            "the semantic publication must provide one snapshot per active host");
    const auto detached = std::find_if(partitions.begin(), partitions.end(), [](const auto &snapshot) {
        return snapshot.hostLayoutId == HostLayoutId{2};
    });
    const auto main = std::find_if(partitions.begin(), partitions.end(), [](const auto &snapshot) {
        return snapshot.hostLayoutId == main_host_layout_id;
    });
    require(detached != partitions.end() && main != partitions.end() &&
                detached->hostGeneration == WindowHostGeneration{4} &&
                detached->nodes.size() == 3 && detached->nodes[0].id == "host/window/2" &&
                detached->nodes[1].id == "main/panel/scene" &&
                detached->nodes[1].parentId == "host/window/2" &&
                detached->nodes[2].parentId == "main/panel/scene" &&
                detached->focusedRevealPath == std::vector<std::string>{
                    "host/window/2", "main/panel/scene", "main/panel/scene/select"} &&
                main->nodes.size() == 2 && main->nodes[1].id == "main/panel/inspector",
            "detached semantics must retain a host window root, valid parents, focus path and generation");

    AccessibilitySnapshots snapshots;
    for (const auto &partition : partitions)
        require(snapshots.publish(partition),
                "each host partition must publish under its own incarnation");
    require(snapshots.acquire_registered().size() == 2,
            "providers can enumerate current immutable snapshots for every registered host");
    require(snapshots.enqueue_action({HostLayoutId{2}, WindowHostGeneration{4}, 7,
                "main/panel/scene/select", "activate", {}}),
            "an action from a detached panel must validate against its detached incarnation");
    snapshots.retire(HostLayoutId{2}, WindowHostGeneration{4});
    const auto registered = snapshots.acquire_registered();
    require(registered.size() == 1 && registered.front()->hostLayoutId == main_host_layout_id &&
                snapshots.pending_actions() == 0,
            "retiring a detached host drains its actions and leaves only the main snapshot registered");
}

void test_virtualized_animation_target_is_present_in_semantic_snapshot()
{
    Json clip{{"frames", Json::array()}, {"framesPerSecond", 8.0},
              {"loop", true}, {"width", 16.0}, {"height", 16.0}};
    for (int index = 0; index < 1000; ++index)
        clip["frames"].push_back(Json{{"duration", 0.125},
                                      {"textureWrapperFilePath", "assets/frame.png"}});
    AssetDocument document(clip);
    AnimationPanel panel;
    panel.scrub_to(document.root(), 500);
    const auto nodes = panel.semantic_nodes(document, {}, "animation/7");
    const auto target = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &node) {
        return node.id == "animation/7/frame/500";
    });
    require(nodes.size() <= 48 && target != nodes.end() && target->visible && target->selected,
            "a focused virtualized animation frame must be included in the bounded semantic target window");
}
} // namespace

int main()
{
    try
    {
        test_immutable_snapshot_publication_and_action_routing();
        test_wake_callback_exception_does_not_drop_action();
        test_retired_host_generation_and_queue_bound();
        test_snapshot_validation_and_owner_thread();
        test_numeric_value_actions_match_the_published_contract();
        test_native_action_metadata_is_bounded();
        test_semantic_snapshots_follow_detached_panel_hosts();
        test_virtualized_animation_target_is_present_in_semantic_snapshot();
        std::cout << "accessibility snapshot tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception &error)
    {
        std::cerr << "accessibility snapshot tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
