#include "document.h"
#include "editcommands.h"
#include "panels/hierarchy.h"
#include "selection.h"

#include <imgui.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace platformator_editor;

namespace
{
void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

Json object(int id, double x, double y, Json children = Json::array())
{
    return Json{{"id", id}, {"name", "object-" + std::to_string(id)}, {"tag", ""},
                {"active", true}, {"position", {{"x", x}, {"y", y}}},
                {"scale", {{"x", 1.0}, {"y", 1.0}}}, {"rotation", 0.0},
                {"components", Json::array()}, {"children", std::move(children)}};
}

DocumentSession move_fixture()
{
    DocumentSession document = make_default_document();
    Json child = object(11, 2.0, 3.0);
    child["unknownMovePayload"] = Json{{"preserve", true}};
    document.root = Json::array({object(10, 100.0, 200.0, Json::array({child})),
                                 object(20, 10.0, 20.0), object(30, -4.0, 7.0),
                                 object(40, 5.0, 9.0)});
    ++document.structureRevision;
    rebuild_document_indexes(document);
    return document;
}

void transactional_moves_preserve_world_order_and_identity()
{
    auto document = move_fixture();
    const Json original = document.root;
    EditCommandService commands;
    SelectionModel selection;
    selection.set(document, Selection{11, std::nullopt, std::nullopt, FocusedPanel::Hierarchy});
    const Selection selected = selection.current();
    commands.reset_history(document.documentGeneration);
    commands.mark_saved();

    const auto originalStructureRevision = document.structureRevision;
    const auto moved = commands.move_object(document, 11, 20, 0, {selected, selected});
    require(moved.status == CommandStatus::Changed && moved.change.structural &&
                document.structureRevision == originalStructureRevision + 1 &&
                moved.change.identity == DocumentIdentity{ProjectGeneration{document.projectGeneration},
                    document.tabId, DocumentGeneration{document.documentGeneration}},
            "cross-parent move must commit as one structural transaction");
    const Json movedRoot = document.root;
    require(document.root[0]["children"].empty() && document.root[1]["children"].size() == 1 &&
                document.root[1]["children"][0]["id"] == 11 &&
                document.root[1]["children"][0]["unknownMovePayload"]["preserve"] == true,
            "move must preserve the complete subtree and unknown payload under its new parent");
    require(document.root[1]["children"][0]["position"]["x"] == 92.0 &&
                document.root[1]["children"][0]["position"]["y"] == 183.0,
            "cross-parent move must subtract destination world translation from the old world position");
    require(commands.history_status().undoCommands == 1 && commands.dirty(),
            "one successful move must create exactly one dirty history entry");

    require(static_cast<bool>(commands.undo(document, selection)) && document.root == original &&
                selection.current() == selected,
            "Undo must restore exact source parent, sibling index, local values and selection");
    require(static_cast<bool>(commands.redo(document, selection)) && document.root == movedRoot &&
                selection.current() == selected,
            "Redo must restore exact destination placement and local values in one step");

    const Json beforeReorder = document.root;
    const auto reordered = commands.move_object(document, 30, std::nullopt, 4, {selected, selected});
    require(reordered.status == CommandStatus::Changed && document.root[2]["id"] == 40 &&
                document.root[3]["id"] == 30,
            "same-parent move must normalize the insertion index after source removal");
    require(static_cast<bool>(commands.undo(document, selection)) && document.root == beforeReorder,
            "same-parent Undo must restore the exact sibling order");
    const auto noOp = commands.move_object(document, 30, std::nullopt, 3, {selected, selected});
    require(noOp.status == CommandStatus::NoChange && document.root == beforeReorder,
            "a same-parent drop at the normalized current index must be a no-op");

    const Json beforeCycle = document.root;
    const auto historyBeforeCycle = commands.history_status();
    const auto cycle = commands.move_object(document, 20, 11, 0, {selected, selected});
    require(cycle.status == CommandStatus::Rejected && document.root == beforeCycle &&
                commands.history_status().undoCommands == historyBeforeCycle.undoCommands,
            "cycle rejection must leave content, revisions and retained history unchanged");
    const auto invalidIndex = commands.move_object(document, 30, 20, 99, {selected, selected});
    require(invalidIndex.status == CommandStatus::Rejected && document.root == beforeCycle,
            "an invalid destination index must fail without partial removal");

    auto staleDocument = move_fixture();
    EditCommandService staleCommands;
    const Json beforeStale = staleDocument.root;
    const auto staleRevision = staleDocument.structureRevision;
    const auto stale = staleCommands.move_object(staleDocument, 999, 20, 0);
    require(stale.status == CommandStatus::StaleAddress && staleDocument.root == beforeStale &&
                staleDocument.structureRevision == staleRevision,
            "a stale source identity must be rejected without mutating the document");
}

void root_nested_moves_round_trip_exactly()
{
    auto document = move_fixture();
    const Json original = document.root;
    EditCommandService commands;
    SelectionModel selection;
    selection.set(document, Selection{30, std::nullopt, std::nullopt, FocusedPanel::Hierarchy});
    const auto selected = selection.current();
    commands.reset_history(document.documentGeneration);
    commands.mark_saved();

    const auto intoNested = commands.move_object(document, 30, 20, 0, {selected, selected});
    require(intoNested.status == CommandStatus::Changed && document.root.size() == 3 &&
                document.root[1]["children"].size() == 1 &&
                document.root[1]["children"][0]["id"] == 30 &&
                document.root[1]["children"][0]["position"]["x"] == -14.0 &&
                document.root[1]["children"][0]["position"]["y"] == -13.0,
            "root-to-nested move must preserve world translation and insert at the requested child index");
    const Json nestedState = document.root;
    require(static_cast<bool>(commands.undo(document, selection)) && document.root == original,
            "Undo of a root-to-nested move must restore the exact root position and payload");
    require(static_cast<bool>(commands.redo(document, selection)) && document.root == nestedState,
            "Redo of a root-to-nested move must restore the exact nested state");

    const auto backToRoot = commands.move_object(document, 30, std::nullopt, 1, {selected, selected});
    require(backToRoot.status == CommandStatus::Changed && document.root[1]["id"] == 30 &&
                document.root[1]["position"]["x"] == -4.0 &&
                document.root[1]["position"]["y"] == 7.0 &&
                document.root[1]["children"].empty(),
            "nested-to-root move must restore root-local coordinates and remove the child from its parent");
    const Json rootState = document.root;
    require(static_cast<bool>(commands.undo(document, selection)) && document.root == nestedState,
            "Undo of a nested-to-root move must restore the exact parent and child index");
    require(static_cast<bool>(commands.redo(document, selection)) && document.root == rootState,
            "Redo of a nested-to-root move must restore the exact root placement");

    Selection group;
    group.selectedObjectIds = {10, 11, 20};
    group.objectId = 11;
    require(selected_top_level_roots(document, group) == std::vector<int>({10, 20}),
            "move selection normalization must omit selected descendants of selected ancestors");
}

void hierarchy_focus_without_delete_key_does_not_delete_selection()
{
    auto document = move_fixture();
    SelectionModel selection;
    selection.set(document, Selection{10, std::nullopt, std::nullopt,
                                      FocusedPanel::Hierarchy});
    EditCommandService commands;
    HierarchyPanel panel;

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    const auto draw = [&](bool deleteKeyPressed = false,
                          bool mouseButtonEventThisFrame = false) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({600.0f, 420.0f}, ImGuiCond_Always);
        ImGui::Begin("Hierarchy selection regression", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings);
        panel.draw(document, selection, commands, true, nullptr, nullptr, nullptr, nullptr,
                   {}, {}, deleteKeyPressed, mouseButtonEventThisFrame);
        const ImVec2 rowMinimum = ImGui::GetItemRectMin();
        const ImVec2 rowMaximum = ImGui::GetItemRectMax();
        ImGui::End();
        ImGui::Render();
        return std::pair{rowMinimum, rowMaximum};
    };

    draw();
    require(document.indexes.objectsById.contains(10) && commands.history_status().undoCommands == 0,
            "a focused Hierarchy selection with no Delete key press must remain intact across a frame");
    auto semanticNodes = panel.semantic_nodes(document, selection.current(), "test/tab/1/document/1");
    const SemanticNode *selectedSemanticObject = nullptr;
    bool hasHierarchyRoot = false;
    for (const auto &node : semanticNodes)
    {
        hasHierarchyRoot |= node.id == "test/tab/1/document/1/hierarchy" &&
                            node.name == "Hierarchy";
        if (node.id == "test/tab/1/document/1/object/10")
            selectedSemanticObject = &node;
    }
    require(hasHierarchyRoot && selectedSemanticObject != nullptr &&
                selectedSemanticObject->selected && selectedSemanticObject->focused &&
                selectedSemanticObject->bounds && selectedSemanticObject->bounds->width > 0.0 &&
                selectedSemanticObject->actions == std::vector<std::string>({"activate", "reveal"}),
            "visible Hierarchy rows expose stable accessible identity, selection, focus, bounds and actions");
    SemanticSnapshot semanticSnapshot;
    semanticSnapshot.hostLayoutId = HostLayoutId{8};
    semanticSnapshot.hostGeneration = WindowHostGeneration{1};
    semanticSnapshot.revision = 1;
    semanticSnapshot.nodes = semanticNodes;
    AccessibilitySnapshots semanticStore;
    std::string semanticError;
    require(semanticStore.publish(std::move(semanticSnapshot), &semanticError),
            "Hierarchy semantic parent references must form a valid immutable snapshot: " + semanticError);

    const Json beforeAccessibleAction = document.root;
    require(semanticStore.enqueue_action({HostLayoutId{8}, WindowHostGeneration{1}, 1,
                "test/tab/1/document/1/object/20", "activate", {}}),
            "visible object activation must be accepted only from the published semantic snapshot");
    bool accessibleActionHandled = false;
    require(semanticStore.dispatch_pending(1,
                [&](const SemanticAction &action, const SemanticNode &) {
                    accessibleActionHandled = panel.dispatch_semantic_action(
                        document, selection, action, "test/tab/1/document/1", WindowHostGeneration{1});
                }) == 1 && accessibleActionHandled && selection.current().objectId == 20 &&
                selection.current().focusedHost == WindowHostGeneration{1} &&
                document.root == beforeAccessibleAction && commands.history_status().undoCommands == 0,
            "validated Hierarchy activation selects on the owner thread without mutating authored data");

    selection.set(document, Selection{10, std::nullopt, std::nullopt,
                                      FocusedPanel::Hierarchy});
    const Json beforeMouseSelection = document.root;
    std::size_t clickedObjectRows = 0;
    for (const auto &node : semanticNodes)
    {
        constexpr std::string_view objectPrefix = "test/tab/1/document/1/object/";
        if (!node.id.starts_with(objectPrefix) || !node.bounds)
            continue;
        const int objectId = std::stoi(node.id.substr(objectPrefix.size()));
        io.AddMousePosEvent(static_cast<float>(node.bounds->x + node.bounds->width * 0.5),
                            static_cast<float>(node.bounds->y + node.bounds->height * 0.5));
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        draw();
        require(selection.current().objectId == objectId && document.root == beforeMouseSelection &&
                    commands.history_status().undoCommands == 0,
                "clicking each visible Hierarchy object must select it without deleting authored data");
        ++clickedObjectRows;
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        draw();
    }
    require(clickedObjectRows >= 4,
            "the Hierarchy click regression must exercise each visible fixture object row");

    // ImGui key state by itself is not authoritative for destructive input;
    // the application must pass the edge from SDL's physical keyboard event.
    io.AddKeyEvent(ImGuiKey_Delete, true);
    draw();
    require(document.root == beforeMouseSelection &&
                commands.history_status().undoCommands == 0,
            "an ImGui Delete state without an SDL keyboard event must not delete the selected object");
    io.AddKeyEvent(ImGuiKey_Delete, false);
    draw();
    draw();

    draw(true, true);
    require(document.root == beforeMouseSelection &&
                commands.history_status().undoCommands == 0,
            "a raw SDL mouse-button event must suppress Delete even if ImGui reports no active mouse press");

    selection.set(document, Selection{10, std::nullopt, std::nullopt,
                                      FocusedPanel::Hierarchy});
    draw(true);
    require(document.indexes.objectsById.contains(10) &&
                commands.history_status().undoCommands == 0,
            "an SDL Delete edge without a current ImGui key edge must not delete the selection");
    io.AddKeyEvent(ImGuiKey_Delete, true);
    draw(true);
    require(!document.indexes.objectsById.contains(10) && commands.history_status().undoCommands == 1,
            "an explicit Delete key press must still delete the selected object in one history step");
    io.AddKeyEvent(ImGuiKey_Delete, false);
    draw();
    selection.set(document, Selection{20, std::nullopt, std::nullopt,
                                      FocusedPanel::Hierarchy});
    const auto lastRowAfterDelete = draw();
    const ImVec2 secondClickedRowCenter{
        (lastRowAfterDelete.first.x + lastRowAfterDelete.second.x) * 0.5f,
        (lastRowAfterDelete.first.y + lastRowAfterDelete.second.y) * 0.5f};
    io.AddMousePosEvent(secondClickedRowCenter.x, secondClickedRowCenter.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    io.AddKeyEvent(ImGuiKey_Delete, true);
    draw(true);
    require(selection.current().objectId == 40 && document.indexes.objectsById.contains(20) &&
                commands.history_status().undoCommands == 1,
            "a Delete key event coincident with a Hierarchy click must not delete the previous selection");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    io.AddKeyEvent(ImGuiKey_Delete, false);
    draw();

    io.AddMousePosEvent(secondClickedRowCenter.x, secondClickedRowCenter.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    draw();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    draw(true);
    require(selection.current().objectId == 40 && document.indexes.objectsById.contains(40) &&
                commands.history_status().undoCommands == 1,
            "a Delete key event coincident with mouse release must not delete the clicked object");
    draw();
    ImGui::DestroyContext();
}

void selected_block_moves_preserve_order_and_are_atomic()
{
    auto document = move_fixture();
    const Json original = document.root;
    EditCommandService commands;
    SelectionModel selection;
    selection.set_objects(document, {20, 40}, 40, 20);
    const auto selected = selection.current();
    commands.reset_history(document.documentGeneration);
    commands.mark_saved();
    const auto originalStructureRevision = document.structureRevision;

    const auto reordered = commands.move_objects(document, {20, 40}, std::nullopt, 4,
                                                  {selected, selected});
    require(reordered.status == CommandStatus::Changed && document.root[0]["id"] == 10 &&
                document.root[1]["id"] == 30 && document.root[2]["id"] == 20 &&
                document.root[3]["id"] == 40 &&
                document.structureRevision == originalStructureRevision + 1 &&
                commands.history_status().undoCommands == 1,
            "selected roots must move as one ordered block and one structural history transaction");
    const Json reorderedState = document.root;
    require(static_cast<bool>(commands.undo(document, selection)) && document.root == original,
            "Undo must restore every block member to its exact prior parent and sibling index");
    require(static_cast<bool>(commands.redo(document, selection)) && document.root == reorderedState,
            "Redo must preserve the selected block order exactly");
    const auto noOp = commands.move_objects(document, {20, 40}, std::nullopt, 2,
                                             {selected, selected});
    require(noOp.status == CommandStatus::NoChange && document.root == reorderedState,
            "dropping an already ordered selected block at its current index must be a no-op");

    const Json beforeNestedBlock = document.root;
    const auto nested = commands.move_objects(document, {10, 11, 30}, 20, 0,
                                               {selected, selected});
    require(nested.status == CommandStatus::Changed && document.root[0]["id"] == 20 &&
                document.root[0]["children"].size() == 2 &&
                document.root[0]["children"][0]["id"] == 10 &&
                document.root[0]["children"][0]["position"]["x"] == 90.0 &&
                document.root[0]["children"][0]["position"]["y"] == 180.0 &&
                document.root[0]["children"][0]["children"][0]["id"] == 11 &&
                document.root[0]["children"][1]["id"] == 30 &&
                document.root[0]["children"][1]["position"]["x"] == -14.0 &&
                document.root[0]["children"][1]["position"]["y"] == -13.0,
            "block normalization must omit selected descendants while retaining their subtree and block order");
    const Json nestedState = document.root;
    require(static_cast<bool>(commands.undo(document, selection)) && document.root == beforeNestedBlock,
            "Undo must roll back all cross-parent block removals and insertions atomically");
    require(static_cast<bool>(commands.redo(document, selection)) && document.root == nestedState,
            "Redo must restore the exact cross-parent block and descendant payload");

    const auto historyBeforeReject = commands.history_status();
    const auto duplicate = commands.move_objects(document, {10, 10}, std::nullopt, 0,
                                                  {selected, selected});
    require(duplicate.status == CommandStatus::Rejected && document.root == nestedState &&
                commands.history_status().undoCommands == historyBeforeReject.undoCommands,
            "duplicate block members must be rejected without changing content or retained history");
    const auto invalid = commands.move_objects(document, {10, 30}, std::nullopt, 99,
                                                {selected, selected});
    require(invalid.status == CommandStatus::Rejected && document.root == nestedState &&
                commands.history_status().undoCommands == historyBeforeReject.undoCommands,
            "invalid block insertion must be rejected before any member is removed");
}

void selected_block_duplicate_and_delete_are_single_history_steps()
{
    auto document = move_fixture();
    document.root[0]["components"].push_back(Json{{"id", 50}, {"type", 2},
        {"width", 320.0}, {"height", 200.0}});
    ++document.structureRevision;
    rebuild_document_indexes(document);
    const Json original = document.root;
    EditCommandService commands;
    SelectionModel selection;
    selection.set_objects(document, {10, 11, 30}, 11, 10);
    const Selection before = selection.current();
    commands.reset_history(document.documentGeneration);
    commands.mark_saved();

    const auto duplicated = commands.duplicate_objects(document, {10, 11, 30}, {before, before});
    require(duplicated.status == CommandStatus::Changed && duplicated.affectedObjectIds.size() == 2 &&
                document.root.size() == 6 && commands.history_status().undoCommands == 1 &&
                document.root[1]["name"] == "object-10 Copy" &&
                document.root[1]["children"].size() == 1 &&
                document.root[1]["children"][0]["id"] != 11 &&
                document.root[1]["children"][0]["unknownMovePayload"]["preserve"] == true &&
                document.root[1]["components"][0]["id"] != 50,
            "duplicate must normalize selected descendants, remap the complete subtree, and commit once");
    const Json duplicatedState = document.root;
    selection.set_objects(document, duplicated.affectedObjectIds, duplicated.affectedObjectIds.back(),
                          duplicated.affectedObjectIds.front());
    require(static_cast<bool>(commands.undo(document, selection)) && document.root == original &&
                selection.current().objectId == 11 &&
                selection.current().selectedObjectIds == before.selectedObjectIds,
            "Undo of a block duplicate must restore exact content and prior multi-selection");
    require(static_cast<bool>(commands.redo(document, selection)) && document.root == duplicatedState &&
                selection.current().selectedObjectIds == duplicated.affectedObjectIds,
            "Redo of a block duplicate must restore exact allocated identities and select the duplicates");

    const Json beforeDelete = document.root;
    selection.set(document, before);
    const Selection deleteSelection = selection.current();
    const auto deleted = commands.delete_objects(document, {10, 11, 30},
                                                  {deleteSelection, deleteSelection});
    require(deleted.status == CommandStatus::Changed && document.root.size() == 4 &&
                document.indexes.objectsById.contains(20) && document.indexes.objectsById.contains(40) &&
                !document.indexes.objectsById.contains(10) && !document.indexes.objectsById.contains(30) &&
                commands.history_status().undoCommands == 2,
            "delete must normalize selected descendants and remove all roots as one transaction");
    selection.repair(document);
    const Selection afterDelete = selection.current();
    commands.set_last_selection_state({deleteSelection, afterDelete});
    require(static_cast<bool>(commands.undo(document, selection)) && document.root == beforeDelete &&
                selection.current() == deleteSelection,
            "Undo of a block delete must restore exact hierarchy and selection");
    require(static_cast<bool>(commands.redo(document, selection)) && document.root.size() == 4,
            "Redo of a block delete must remove the same selected roots in one step");
}
} // namespace

int main()
{
    try
    {
        transactional_moves_preserve_world_order_and_identity();
        root_nested_moves_round_trip_exactly();
        hierarchy_focus_without_delete_key_does_not_delete_selection();
        selected_block_moves_preserve_order_and_are_atomic();
        selected_block_duplicate_and_delete_are_single_history_steps();
        std::cout << "[PASS] platformator_editor_hierarchy_move_test\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "[FAIL] platformator_editor_hierarchy_move_test: " << error.what() << '\n';
        return 1;
    }
}
