#include "editcommands.h"
#include "test_support.h"
#include "workspace.h"
#include "viewport.h"

#include <platformator/gameobject.h>

#include <string>
#include <vector>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace
{
DocumentSession fixture()
{
    Json behavior{{"type", "FollowTarget"}, {"fields", {{"targetId", 4}, {"label", ""}}}};
    Json script = component(3, SCRIPT, Json{{"behaviors", Json::array({behavior})}});
    Json camera = component(2, CAMERA);
    Json root = object(1, "Root", Json::array({camera, script}),
                       Json::array({object(4, "Target")}));
    DocumentSession document;
    document.documentGeneration = 7;
    document.root = scene({root});
    rebuild_document_indexes(document);
    return document;
}

PropertyAddress address(std::uint64_t generation, int objectId,
                        std::vector<FieldPathSegment> path,
                        std::optional<int> componentId = std::nullopt,
                        std::optional<std::size_t> behaviorOrdinal = std::nullopt)
{
    return PropertyAddress{generation, objectId, componentId, behaviorOrdinal, std::move(path)};
}

void test_mixed_commands_and_selection()
{
    auto document = fixture();
    const Json startingDocument = document.root;
    EditCommandService commands;
    SelectionModel selection;
    selection.set(document, Selection{1, 2, std::nullopt, FocusedPanel::Hierarchy});
    commands.set_selection_provider([&] { return selection.current(); });
    commands.reset_history(document.documentGeneration);
    commands.mark_saved();

    auto result = commands.set_property(document, address(7, 1, {std::string("name")}), "Renamed");
    require(result.status == CommandStatus::Changed, "property edits must be recorded");

    auto viewportDrag = begin_object_position_gesture(commands, document, 1,
                                                       {selection.current(), selection.current()});
    require(viewportDrag && viewportDrag->update(commands, document,
                Json{{"x", 20.0}, {"y", 30.0}}).status == CommandStatus::Changed,
            "viewport object movement must use the history gesture path");
    viewportDrag->commit();

    const Selection beforeCreate = selection.current();
    result = CommandResult{};
    auto created = commands.create_object(document, 1, "Child");
    require(created.status == CommandStatus::Changed && created.command,
            "object creation must be recorded as a structural command");
    const int childId = created.command->payload.at("id").get<int>();
    const Selection afterCreate{childId, std::nullopt, std::nullopt, FocusedPanel::Hierarchy};
    commands.set_last_selection_state({beforeCreate, afterCreate});
    selection.set(document, afterCreate);

    auto componentAdded = commands.add_component(document, childId, Json{{"type", 1001}, {"custom", "kept"}});
    require(componentAdded.status == CommandStatus::Changed && componentAdded.command,
            "component insertion must be recorded");
    const int customComponentId = componentAdded.command->payload.at("id").get<int>();

    auto behaviorAdded = commands.add_behavior(document, 1, 3, Json{{"type", "Extra"}});
    require(behaviorAdded.status == CommandStatus::Changed && behaviorAdded.command,
            "behavior insertion must be recorded");

    auto reference = address(7, 1, {std::string("fields"), std::string("targetId")}, 3, 0);
    result = commands.set_property(document, reference, 1);
    require(result.status == CommandStatus::Changed &&
                document.root[0]["components"][1]["behaviors"][0]["fields"]["targetId"] == 1,
            "numeric scene references must remain reversible property values");

    auto removedBehavior = commands.remove_behavior(document, 1, 3, 1);
    require(removedBehavior.status == CommandStatus::Changed,
            "behavior removal must be retained as a reversible command");
    auto removedComponent = commands.remove_component(document, childId, customComponentId);
    require(removedComponent.status == CommandStatus::Changed,
            "component removal must be retained as a reversible command");
    auto deleted = commands.delete_object(document, childId);
    require(deleted.status == CommandStatus::Changed,
            "object deletion must retain its affected subtree");
    selection.repair(document);
    commands.set_last_selection_state({afterCreate, selection.current()});

    const std::size_t editCount = commands.history_status().undoCommands;
    require(editCount == 9, "mixed property, viewport, object, component, behavior, reference and delete edits must each have history entries");
    for (std::size_t i = 0; i < editCount; ++i)
        require(static_cast<bool>(commands.undo(document, selection)), "each mixed edit must undo successfully");
    require(document.root == startingDocument && !commands.dirty(),
            "undoing to the saved start token must restore the exact document and clean state");
    require(selection.current() == beforeCreate,
            "undoing object creation must restore its before-selection");

    require(static_cast<bool>(commands.redo(document, selection)), "the property edit must redo successfully");
    require(static_cast<bool>(commands.redo(document, selection)), "the viewport gesture must redo successfully");
    require(static_cast<bool>(commands.redo(document, selection)), "object creation must redo successfully");
    require(selection.current() == afterCreate,
            "redoing object creation must restore its after-selection");
    for (std::size_t i = 3; i < editCount; ++i)
        require(static_cast<bool>(commands.redo(document, selection)), "each later mixed edit must redo successfully");
    require(document.root[0]["name"] == "Renamed",
            "redo must replay the mixed property edit");
    require(document.root[0]["components"][1]["behaviors"][0]["fields"]["targetId"] == 1,
            "redo must replay the mixed reference edit");
    require(!document.indexes.objectsById.contains(childId),
            "redo must replay the later deletion of the created object");
    require(!selection.current().objectId,
            "redoing a deletion must restore a valid repaired selection");
}

void test_targeted_payload_and_workspace_saved_state()
{
    auto document = fixture();
    document.root[0]["unfamiliarLargeData"] = std::string(1024 * 1024, 'x');
    EditCommandService commands;
    SelectionModel selection;
    commands.set_selection_provider([&] { return selection.current(); });
    commands.reset_history(document.documentGeneration);
    commands.mark_saved();
    require(commands.set_property(document, address(7, 1, {std::string("name")}), "Small Edit").status ==
                CommandStatus::Changed && commands.history_status().retainedBytes < 1024,
            "property history storage must depend on the changed field, not a large unrelated document field");

    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    DocumentWorkspace workspace(project, assets);
    EditCommandService workspaceCommands;
    SelectionModel workspaceSelection;
    workspaceCommands.set_selection_provider([&] { return workspaceSelection.current(); });
    workspace.bind_history(workspaceCommands);
    const auto generation = workspace.document().documentGeneration;
    require(workspaceCommands.set_property(workspace.document_for_edit(),
                address(generation, 0, {std::string("name")}), "Edited").status == CommandStatus::Changed &&
                workspace.dirty(),
            "document workspace dirtiness must follow command history");
    require(workspace.save_as(project / "saved.scene") && !workspace.dirty(),
            "a successful save must mark the current history token clean");
    require(workspaceCommands.undo(workspace.document_for_edit(), workspaceSelection) && workspace.dirty(),
            "undo after save must move away from the saved token and become dirty");
    require(workspace.new_document(UnsavedDecision::Discard) && !workspace.dirty() &&
                !workspaceCommands.history_status().canUndo && !workspaceCommands.history_status().canRedo,
            "successful document replacement must clear the prior history branch and saved token");
}

void test_gestures_saved_tokens_and_stale_rejection()
{
    auto document = fixture();
    EditCommandService commands;
    SelectionModel selection;
    selection.set(document, Selection{1, 2, std::nullopt, FocusedPanel::Inspector});
    commands.set_selection_provider([&] { return selection.current(); });
    commands.reset_history(document.documentGeneration);
    commands.mark_saved();

    const auto positionX = address(7, 1, {std::string("position"), std::string("x")});
    auto first = commands.begin_gesture(document, positionX);
    require(first && first->active(), "continuous edits must begin as one gesture");
    require(first->update(commands, document, 12.0).status == CommandStatus::Changed && commands.dirty(),
            "a changed active gesture must make the document dirty");
    require(first->update(commands, document, 18.0).status == CommandStatus::Changed,
            "gesture updates must apply without creating separate history entries");
    const auto committed = first->commit();
    require(committed && committed->before == PropertyValue::of(0.0) &&
                committed->after == PropertyValue::of(18.0) &&
                commands.history_status().undoCommands == 1,
            "a continuous gesture must commit as one history entry");
    commands.mark_saved();

    auto second = commands.begin_gesture(document,
        address(7, 1, {std::string("position"), std::string("y")}));
    require(second.has_value(), "separate controls must start separate gestures");
    second->update(commands, document, 5.0);
    second->commit();
    require(commands.history_status().undoCommands == 2,
            "separate gestures must not merge by object or control identity");

    auto noOp = commands.begin_gesture(document, positionX);
    require(noOp.has_value(), "a no-op gesture can start");
    noOp->commit();
    require(commands.history_status().undoCommands == 2,
            "a no-op gesture must not create a history entry");

    const auto draft = address(7, 1, {std::string("draft")});
    auto missingKey = commands.begin_gesture(document, draft);
    require(missingKey.has_value(), "a missing object key can be edited through a gesture");
    missingKey->update(commands, document, "temporary");
    require(missingKey->cancel(commands, document).status == CommandStatus::Changed &&
                !document.root[0].contains("draft"),
            "cancelling a missing-key gesture must remove the materialized key exactly");

    require(commands.undo(document, selection) && !commands.dirty(),
            "undoing to the reachable saved token must become clean");
    require(commands.undo(document, selection) && commands.dirty(),
            "undoing past the saved token must become dirty");
    require(commands.redo(document, selection) && !commands.dirty(),
            "redoing to the saved token must become clean again");
    require(static_cast<bool>(commands.undo(document, selection)),
            "the saved state should remain reachable before branching");
    require(commands.set_property(document, positionX, 42.0).status == CommandStatus::Changed,
            "a new edit after undo must create a new branch");
    require(!commands.history_status().savedTokenReachable && commands.dirty() &&
                !commands.history_status().canRedo,
            "branching must discard redo and make the saved token unreachable");

    const auto historyBeforeStale = commands.history_status();
    document.root[0]["position"]["x"] = -999.0;
    const Json beforeRejectedUndo = document.root;
    require(!commands.undo(document, selection) && document.root == beforeRejectedUndo &&
                commands.history_status().currentToken == historyBeforeStale.currentToken,
            "a stale undo precondition must reject atomically without moving the history cursor");
}

void test_budget_pruning_and_single_oversized_entry()
{
    auto document = fixture();
    EditCommandService commands(256);
    SelectionModel selection;
    commands.set_selection_provider([&] { return selection.current(); });
    commands.reset_history(document.documentGeneration);
    commands.mark_saved();

    const std::string first(220, 'a');
    const std::string newest(480, 'b');
    require(commands.set_property(document, address(7, 1, {std::string("tag")}), first).status ==
                CommandStatus::Changed,
            "the first payload should be retained before the budget is exceeded");
    require(commands.set_property(document, address(7, 1, {std::string("tag")}), newest).status ==
                CommandStatus::Changed,
            "the newest payload should be retained even when it exceeds the history budget");
    const auto status = commands.history_status();
    require(status.retainedBytes > status.budgetBytes && status.overBudget && status.undoCommands == 1,
            "history pruning must keep the newest single oversized command and report the limit state");
    require(commands.undo(document, selection) && document.root[0]["tag"] == first,
            "the retained oversized command must remain undoable to the pruned history base");
}
} // namespace

int main()
{
    try
    {
        test_mixed_commands_and_selection();
        test_gestures_saved_tokens_and_stale_rejection();
        test_budget_pruning_and_single_oversized_entry();
        test_targeted_payload_and_workspace_saved_state();
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "editor history test failed: %s\n", error.what());
        return 1;
    }
    return 0;
}
