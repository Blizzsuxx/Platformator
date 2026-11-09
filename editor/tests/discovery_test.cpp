#include "document.h"
#include "discovery.h"
#include "actions.h"
#include "editcommands.h"
#include "panels/hierarchy.h"
#include "panels/runsettings.h"
#include "panels/referencefields.h"
#include "selection.h"
#include "test_support.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace platformator_editor
{
struct HierarchyPanelTestProbe
{
    static void collapse_all(HierarchyPanel &panel)
    {
        panel.expandedObjects_.clear();
        panel.invalidate();
    }

    static std::vector<int> visible_object_ids(HierarchyPanel &panel,
                                               const DocumentSession &document,
                                               std::optional<int> selectedObjectId = std::nullopt)
    {
        panel.update_search_matches(document);
        if (selectedObjectId)
            panel.reveal_object(document, *selectedObjectId);
        const auto &rows = panel.visible_rows(document);
        panel.update_search_visibility(document, selectedObjectId);
        std::vector<int> result;
        for (std::size_t index = 0; index < rows.size(); ++index)
            if (panel.searchVisibleRows_[index] && rows[index].kind == HierarchyRowKind::Object)
                result.push_back(rows[index].id);
        return result;
    }
};
} // namespace platformator_editor

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

Json object(int id, std::string name, Json components = Json::array(),
            Json children = Json::array())
{
    return Json{{"id", id}, {"name", std::move(name)}, {"tag", ""}, {"active", true},
                {"position", Json{{"x", 0.0}, {"y", 0.0}}},
                {"scale", Json{{"x", 1.0}, {"y", 1.0}}}, {"rotation", 0.0},
                {"children", std::move(children)}, {"components", std::move(components)}};
}

Json component(int id, int type, Json data = Json::object())
{
    data["id"] = id;
    data["type"] = type;
    return data;
}

void run_search_and_identity_cases()
{
    require(discovery_query_matches("hero", "Assets/Sprites/Hero.PNG (#12)") &&
                discovery_query_matches("SPRITE", "Image Texture Sprite") &&
                discovery_query_matches("textures/hero", "Assets/Textures/Hero.PNG") &&
                discovery_query_matches("#12", "Sprite #12") &&
                discovery_query_matches("", "any value") &&
                !discovery_query_matches("missing", "Assets/Sprites/Hero.PNG"),
            "shared discovery matching must cover case-insensitive names, types, paths, IDs, and misses");

    DocumentSession document;
    document.documentGeneration = 140;
    document.root = Json::array({object(10, "Root", Json::array(),
        Json::array({object(11, "Duplicate"), object(12, "Duplicate"), object(13, "Other")}))});
    ++document.structureRevision;
    rebuild_document_indexes(document);

    SelectionModel selection;
    selection.set_objects(document, {11}, 11, 11);
    selection.select_range(document, 13, {11, 12, 13});
    require(selection.current().selectedObjectIds == std::vector<int>({11, 12, 13}) &&
                selection.current().objectId == 13 && selection.current().rangeAnchorObjectId == 11,
            "range discovery must preserve displayed order and the range anchor");
    selection.select_range(document, 12, {11, 12, 13}, true);
    require(selection.current().selectedObjectIds == std::vector<int>({11, 12, 13}) &&
                selected_top_level_roots(document, selection.current()) == std::vector<int>({11, 12, 13}),
            "additive range discovery must remain unique and keep unrelated selected roots");

    const auto first = inspect_scene_reference(document, 12, "GameObject");
    const auto second = inspect_scene_reference(document, 11, "GameObject");
    require(first.state == ReferenceState::Resolved && second.state == ReferenceState::Resolved &&
                first.label.find("(#12)") != std::string::npos &&
                second.label.find("(#11)") != std::string::npos && first.label != second.label,
            "same-name reference targets must expose distinct immutable identity context");
    require(first.label.find("Root / Duplicate") != std::string::npos &&
                second.label.find("Root / Duplicate") != std::string::npos,
            "duplicate reference targets must include their hierarchy location");
    HierarchyPanel duplicateHierarchy;
    const auto &duplicateRows = duplicateHierarchy.visible_rows(document);
    const auto firstDuplicate = std::find_if(duplicateRows.begin(), duplicateRows.end(),
        [](const HierarchyRow &row) { return row.kind == HierarchyRowKind::Object && row.id == 11; });
    const auto secondDuplicate = std::find_if(duplicateRows.begin(), duplicateRows.end(),
        [](const HierarchyRow &row) { return row.kind == HierarchyRowKind::Object && row.id == 12; });
    require(firstDuplicate != duplicateRows.end() && secondDuplicate != duplicateRows.end() &&
                firstDuplicate->label == "Duplicate (#11)" &&
                secondDuplicate->label == "Duplicate (#12)",
            "duplicate-name hierarchy rows must display stable IDs so users can reveal the intended object");
    std::string referenceError;
    require(validate_scene_reference_assignment(document, 11, "GameObject", false, referenceError) &&
                validate_scene_reference_assignment(document, 12, "GameObject", false, referenceError),
            "duplicate display names must remain assignable by their explicit stable object IDs");
}

void run_hierarchy_search_cache_cases()
{
    DocumentSession document;
    document.documentGeneration = 142;
    document.root = Json::array({object(20, "Root", Json::array(), Json::array({
        object(21, "Target"), object(22, "Neighbor")}))});
    ++document.structureRevision;
    rebuild_document_indexes(document);

    HierarchyPanel panel;
    (void)panel.visible_rows(document);
    HierarchyPanelTestProbe::collapse_all(panel);
    require(panel.visible_rows(document).size() == 1,
            "an initially collapsed hierarchy must expose only its root row");
    panel.set_search_query("target");
    require(HierarchyPanelTestProbe::visible_object_ids(panel, document) ==
                std::vector<int>({20, 21}),
            "hierarchy search must reveal a matching descendant with its ancestor and filter siblings");
    panel.set_search_query("root / neighbor");
    require(HierarchyPanelTestProbe::visible_object_ids(panel, document) ==
                std::vector<int>({20, 22}),
            "hierarchy search must match the full ancestor path without reordering rows");
    panel.set_search_query("#22");
    require(HierarchyPanelTestProbe::visible_object_ids(panel, document) ==
                std::vector<int>({20, 22}),
            "hierarchy search must find identity labels while preserving document row order");

    document.root[0]["children"][1]["name"] = "Renamed Neighbor";
    ++document.valueRevision;
    const auto &renamedRows = panel.visible_rows(document);
    const auto renamedRow = std::find_if(renamedRows.begin(), renamedRows.end(),
        [](const HierarchyRow &row) { return row.kind == HierarchyRowKind::Object && row.id == 22; });
    require(renamedRow != renamedRows.end() && renamedRow->label == "Renamed Neighbor (#22)",
            "value-revision refresh must retain the stable identity suffix after rename");
    panel.set_search_query("neighbor");
    require(HierarchyPanelTestProbe::visible_object_ids(panel, document) ==
                std::vector<int>({20, 22}),
            "hierarchy search must recompute label matches after the document value revision changes");
    panel.set_search_query("");
    require(HierarchyPanelTestProbe::visible_object_ids(panel, document) == std::vector<int>({20}),
            "clearing search must restore the hierarchy's prior collapsed expansion state");

    panel.set_search_query("no matching object");
    require(HierarchyPanelTestProbe::visible_object_ids(panel, document, 22) ==
                std::vector<int>({20, 22}),
            "selecting an object during search must reveal it with its ancestors even when it does not match the query");
}

void run_hierarchy_shortcut_capture_cases()
{
    const EditorInputCapture hierarchy{true, false, false, false, false, true};
    require(dispatch_editor_key(EditorKey::F2, false, false, hierarchy) ==
                EditorAction::RenameSelection &&
                dispatch_editor_key(EditorKey::Delete, false, false, hierarchy) ==
                EditorAction::DeleteSelection &&
                dispatch_editor_key(EditorKey::D, false, true, hierarchy) ==
                EditorAction::DuplicateSelection,
            "F2, Delete, and Ctrl+D must be routed while Hierarchy owns keyboard focus");
    for (const auto capture : {EditorInputCapture{true, true, false, false, false, true},
                               EditorInputCapture{true, false, true, false, false, true},
                               EditorInputCapture{true, false, false, true, false, true},
                               EditorInputCapture{false, false, false, false, false, true},
                               EditorInputCapture{true, false, false, false, true, false}})
        require(!dispatch_editor_key(EditorKey::F2, false, false, capture) &&
                    !dispatch_editor_key(EditorKey::Delete, false, false, capture) &&
                    !dispatch_editor_key(EditorKey::D, false, true, capture),
                "hierarchy mutation shortcuts must respect text, modal, gesture, app, and panel capture");
}

void run_assignment_preserves_previous_value()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets / "Textures");
    std::ofstream(assets / "Textures" / "Valid.png", std::ios::binary) << "image";
    std::ofstream(assets / "Textures" / "Old.png", std::ios::binary) << "old image";
    std::ofstream(temporary.path() / "Outside.png", std::ios::binary) << "outside";

    DocumentSession document;
    document.documentGeneration = 141;
    document.projectRoot = project;
    document.assetRoot = assets;
    document.root = Json::array({object(20, "Sprite", Json::array({component(21, 6,
        Json{{"textureFilePath", "assets/Textures/Old.png"}, {"width", 16.0},
             {"height", 16.0}})}))});
    ++document.structureRevision;
    rebuild_document_indexes(document);

    EditCommandService commands;
    commands.reset_history(document.documentGeneration);
    commands.mark_saved();
    const auto address = address_for_document(document, 20, 21, std::nullopt,
                                              {std::string("textureFilePath")});
    std::string error;
    const auto assign_if_valid = [&](const Json &value) {
        if (!validate_asset_reference_assignment(document, value, "Texture", false, error))
            return false;
        return commands.set_property(document, address, value).status == CommandStatus::Changed;
    };
    require(assign_if_valid("assets/Textures/Valid.png"),
            "a contained asset path must remain assignable through the shared validator");
    const Json previous = document.root[0]["components"][0]["textureFilePath"];
    require(!assign_if_valid("assets/../Outside.png") &&
                document.root[0]["components"][0]["textureFilePath"] == previous &&
                inspect_asset_reference(document, previous, "Texture").state == ReferenceState::Resolved &&
                commands.history_status().undoCommands == 1,
            "a rejected escaping assignment must preserve the previous valid reference without history");
}

void run_settings_semantics_cases()
{
    TemporaryDirectory temporary;
    const auto build = temporary.path() / "build" / "debug";
    const auto reply = build / ".cmake" / "api" / "v1" / "reply";
    std::filesystem::create_directories(reply);
    std::ofstream(temporary.path() / "CMakePresets.json")
        << R"({"version":6,"configurePresets":[{"name":"debug","binaryDir":"build/debug"},{"name":"release"}],)"
           R"("buildPresets":[{"name":"debug-build"},{"name":"release-build"}]})";
    std::ofstream(reply / "index-test.json")
        << R"({"reply":{"codemodel-v2":{"jsonFile":"codemodel-v2-test.json"}}})";
    std::ofstream(reply / "codemodel-v2-test.json")
        << R"({"configurations":[{"targets":[{"name":"game","jsonFile":"target-game.json"}]}]})";
    std::ofstream(reply / "target-game.json")
        << R"({"type":"EXECUTABLE","artifacts":[{"path":"game"}]})";
    std::filesystem::create_directories(build);
    std::ofstream(build / "game") << "executable fixture";

    RunSettingsPanel panel;
    panel.set_project_root(temporary.path());
    const auto queryFile = build / ".cmake" / "api" / "v1" / "query" /
        "client-platformator" / "codemodel-v2";
    require(!std::filesystem::exists(queryFile),
            "opening Run Settings must not write into the project build directory");
    panel.refresh_discovery(true);
    require(std::filesystem::is_regular_file(queryFile),
            "the explicit Run Settings refresh must register its File API target query");
    RunPreferences preferences;
    preferences.configurePreset = "debug";
    preferences.buildPreset = "debug-build";
    preferences.target = "game";
    preferences.executable = (temporary.path() / "build" / "game").string();
    preferences.height = 360;
    preferences.embeddedGameView = true;
    preferences.production = true;
    preferences.startPaused = true;
    preferences.overlays = {"colliders"};

    auto nodes = panel.semantic_nodes(preferences, "main/panel/run");
    const auto find = [&](std::string_view id) -> const SemanticNode * {
        const auto node = std::find_if(nodes.begin(), nodes.end(), [&](const SemanticNode &candidate) {
            return candidate.id == id;
        });
        return node == nodes.end() ? nullptr : &*node;
    };
    const auto *discovery = find("main/panel/run/options/discovery");
    const auto *embedded = find("main/panel/run/options/embedded-game");
    const auto *catalog = find("main/panel/run/options/catalog");
    const auto *paused = find("main/panel/run/options/start-paused");
    const auto *overlay = find("main/panel/run/options/overlay/colliders");
    const auto *windowWidth = find("main/panel/run/options/window-width");
    const auto *windowHeight = find("main/panel/run/options/window-height");
    const auto choice = [&](std::string_view group, std::string_view name) -> const SemanticNode * {
        const auto node = std::find_if(nodes.begin(), nodes.end(), [&](const SemanticNode &candidate) {
            return candidate.parentId == group && candidate.name == name;
        });
        return node == nodes.end() ? nullptr : &*node;
    };
    const auto *releasePreset = choice("main/panel/run/options/choices/configure-presets", "release");
    const auto *gameTarget = choice("main/panel/run/options/choices/executable-targets", "game");
    require(discovery != nullptr &&
                discovery->value.find("queued for the next configure") != std::string::npos &&
                embedded != nullptr && embedded->selected &&
                catalog != nullptr && catalog->error &&
                catalog->help.find("common-runner-v1") != std::string::npos &&
                paused != nullptr && !paused->selected &&
                overlay != nullptr && !overlay->selected &&
                windowWidth != nullptr && windowWidth->role == SemanticRole::TextField &&
                windowWidth->minimum == 1.0 && windowWidth->maximum == 16384.0 &&
                windowWidth->actions == std::vector<std::string>{"set_value"} &&
                windowHeight != nullptr && windowHeight->value == "360" &&
                releasePreset != nullptr && releasePreset->actions == std::vector<std::string>{"activate"} &&
                gameTarget != nullptr && gameTarget->value == (build / "game").string() &&
                gameTarget->actions == std::vector<std::string>{"activate"},
            "Run Settings semantics must expose current values and actionable bounded preset and executable choices");

    require(panel.dispatch_semantic_action(preferences,
                {HostLayoutId{1}, WindowHostGeneration{1}, 1, releasePreset->id, "activate", {}}) &&
                preferences.configurePreset == "release",
            "an accessible configure-preset choice must update the same preference as the visible combo");
    require(panel.dispatch_semantic_action(preferences,
                {HostLayoutId{1}, WindowHostGeneration{1}, 1, gameTarget->id, "activate", {}}) &&
                preferences.target == "game" && preferences.executable == (build / "game").string(),
            "an accessible built-target choice must update both target and executable identity");
    require(panel.dispatch_semantic_action(preferences,
                {HostLayoutId{1}, WindowHostGeneration{1}, 1, windowWidth->id, "set_value", "1280"}) &&
                preferences.width == 1280 &&
                !panel.dispatch_semantic_action(preferences,
                    {HostLayoutId{1}, WindowHostGeneration{1}, 1, windowHeight->id,
                     "set_value", "99999"}),
            "accessible Game dimensions update through the same bounded numeric preference path");
    require(panel.dispatch_semantic_action(preferences,
                {HostLayoutId{1}, WindowHostGeneration{1}, 1,
                 "main/panel/run/options/production", "toggle", {}}) && !preferences.production,
            "the Production profile checkbox must expose a validated owner-thread toggle action");
    auto enabledNodes = panel.semantic_nodes(preferences, "main/panel/run");
    const auto enabledOverlay = std::find_if(enabledNodes.begin(), enabledNodes.end(), [](const SemanticNode &node) {
        return node.id == "main/panel/run/options/overlay/colliders";
    });
    require(enabledOverlay != enabledNodes.end() && enabledOverlay->selected &&
                enabledOverlay->actions == std::vector<std::string>{"toggle"},
            "overlay controls become actionable again when Production is disabled");
    require(panel.dispatch_semantic_action(preferences,
                {HostLayoutId{1}, WindowHostGeneration{1}, 2, enabledOverlay->id, "toggle", {}}) &&
                std::find(preferences.overlays.begin(), preferences.overlays.end(), "colliders") ==
                    preferences.overlays.end(),
            "an accessible overlay toggle must preserve the visible panel's default-all semantics");

    const std::string staleChoice = releasePreset->id;
    const auto secondProject = temporary.path() / "second-project";
    std::filesystem::create_directories(secondProject);
    panel.set_project_root(secondProject);
    require(!panel.dispatch_semantic_action(preferences,
                {HostLayoutId{1}, WindowHostGeneration{1}, 1, staleChoice, "activate", {}}),
            "project replacement must retire preset and target actions from the prior discovery snapshot");

    auto busyNodes = panel.semantic_nodes(preferences, "main/panel/run", true);
    require(std::none_of(busyNodes.begin(), busyNodes.end(), [](const SemanticNode &node) {
                return node.id == "main/panel/run/options/production" && node.enabled;
            }),
            "Run Settings actions are disabled while their visible panel is busy");

    SemanticNode root;
    root.id = "main/panel/run";
    root.role = SemanticRole::Window;
    root.name = "Build and Run";
    root.visible = true;
    busyNodes.insert(busyNodes.begin(), std::move(root));
    AccessibilitySnapshots snapshots;
    require(snapshots.publish(SemanticSnapshot{HostLayoutId{1}, WindowHostGeneration{1},
                                                1, std::move(busyNodes), {}}),
            "Run Settings semantic nodes must satisfy shared snapshot identity and parent checks");
}
} // namespace

int main()
{
    try
    {
        run_search_and_identity_cases();
        run_hierarchy_search_cache_cases();
        run_hierarchy_shortcut_capture_cases();
        run_assignment_preserves_previous_value();
        run_settings_semantics_cases();
        std::cout << "Discovery tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Discovery tests failed: " << error.what() << '\n';
        return 1;
    }
}
