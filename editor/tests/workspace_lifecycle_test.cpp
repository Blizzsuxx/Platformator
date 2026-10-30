#include "workspace.h"
#include "actions.h"
#include "editcommands.h"
#include "preferences.h"
#include "style.h"
#include "test_support.h"
#include "validation.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <string_view>

#include <imgui.h>
#include <platformator/windowsettings.h>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace
{
Json workspace_scene(std::string name)
{
    auto root = object(0, std::move(name));
    root["unfamiliar"] = Json{{"kept", true}};
    return Json::array({root});
}

void write_scene(const std::filesystem::path &path, const Json &sceneData)
{
    std::ofstream stream(path, std::ios::binary);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    stream << sceneData.dump(2);
}
} // namespace

void run_workspace_lifecycle_cases()
{
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    std::filesystem::create_directories(assetRoot);
    const auto firstPath = projectRoot / "first.scene";
    const auto secondPath = projectRoot / "second.scene";
    const auto thirdPath = projectRoot / "third.scene";
    write_scene(secondPath, workspace_scene("Second"));
    write_scene(thirdPath, workspace_scene("Third"));

    DocumentWorkspace workspace(projectRoot, assetRoot);
    require(workspace.document().root[0]["components"][0]["type"] == 2 && !workspace.dirty(),
            "a new workspace must begin with a clean starter camera document");
    const Json cleanWorkspace = workspace.document().root;
    const auto cleanValidation = workspace.validate();
    require(cleanValidation.valid_for_run() && workspace.document().root == cleanWorkspace && !workspace.dirty(),
            "Validate must inspect the active document without changing its contents or saved state");
    auto action = workspace.save_as(firstPath);
    require(action && !workspace.dirty() && workspace.document().sourcePath == std::filesystem::canonical(firstPath),
            "Save As must assign the destination only after a successful write");
    require(read_json(firstPath) == workspace.document().root,
            "Save As must serialize the authoritative document root");

    workspace.document_for_edit().root[0]["name"] = "Dirty First";
    ++workspace.document_for_edit().valueRevision;
    const auto firstDirtyRoot = workspace.document().root;
    action = workspace.open_document(std::nullopt, UnsavedDecision::Discard);
    require(action.status == WorkspaceActionStatus::Cancelled && workspace.dirty() &&
                workspace.document().root == firstDirtyRoot,
            "picker cancellation must preserve the document and its dirty marker");
    action = workspace.open_document(secondPath, UnsavedDecision::Cancel);
    require(action.status == WorkspaceActionStatus::Cancelled && workspace.dirty() &&
                workspace.document().root == firstDirtyRoot,
            "Cancel in the unsaved-work prompt must leave the current document untouched");
    action = workspace.open_document(secondPath, UnsavedDecision::Save);
    require(action && workspace.document().root[0]["name"] == "Second" && !workspace.dirty() &&
                read_json(firstPath)[0]["name"] == "Dirty First",
            "Save before Open must preserve the old document on disk before adopting the candidate");

    workspace.document_for_edit().root[0]["name"] = "Dirty Second";
    ++workspace.document_for_edit().valueRevision;
    action = workspace.open_document(thirdPath, UnsavedDecision::Discard);
    require(action && workspace.document().root[0]["name"] == "Third" && !workspace.dirty() &&
                read_json(secondPath)[0]["name"] == "Second",
            "Discard before Open must adopt the candidate without writing discarded edits");

    workspace.document_for_edit().root[0]["name"] = "Dirty Third";
    ++workspace.document_for_edit().valueRevision;
    std::filesystem::remove(thirdPath);
    std::filesystem::create_directory(thirdPath);
    write_json(thirdPath / "marker", Json{{"preserve", true}});
    action = workspace.open_document(secondPath, UnsavedDecision::Save);
    require(action.status == WorkspaceActionStatus::Failed && workspace.dirty() &&
                workspace.document().root[0]["name"] == "Dirty Third" &&
                workspace.document().sourcePath == std::filesystem::canonical(thirdPath) &&
                read_json(thirdPath / "marker") == Json{{"preserve", true}} &&
                read_json(secondPath)[0]["name"] == "Second",
            "failed Save before replacement must preserve current state and both file destinations");

    action = workspace.new_document(UnsavedDecision::Cancel);
    require(action.status == WorkspaceActionStatus::Cancelled && workspace.dirty() &&
                workspace.document().root[0]["name"] == "Dirty Third",
            "Cancel before New must preserve current edits and saved state");
    action = workspace.new_document(UnsavedDecision::Discard);
    require(action && !workspace.dirty() && workspace.document().root[0]["name"] == "Camera" &&
                !workspace.document().sourcePath,
            "New after Discard must adopt a clean default-camera document without a source path");

    action = workspace.save();
    require(action.status == WorkspaceActionStatus::Failed && !workspace.document().sourcePath,
            "Save on an untitled document must fail without assigning a destination");

    workspace.document_for_edit().root[0]["name"] = "Recoverable Current";
    ++workspace.document_for_edit().valueRevision;
    action = workspace.open_document(projectRoot / "missing.scene", UnsavedDecision::Discard);
    require(action.status == WorkspaceActionStatus::Failed && workspace.dirty() &&
                workspace.document().root[0]["name"] == "Recoverable Current",
            "a failed candidate load must leave the current dirty document adopted");

    workspace.document_for_edit().root[0]["scale"]["x"] = 0.0;
    ++workspace.document_for_edit().valueRevision;
    const Json invalidKnownValue = workspace.document().root;
    const auto invalidValidation = workspace.validate();
    require(invalidValidation.structurallyValid && invalidValidation.blocksRun &&
                workspace.document().root == invalidKnownValue && workspace.dirty() &&
                std::any_of(invalidValidation.issues.begin(), invalidValidation.issues.end(),
                            [](const ValidationIssue &issue) {
                                return issue.fieldPath == "scale.x" && issue.blocksRun;
                            }),
            "Validate must report known invalid values by field, block Run and preserve editable dirty data");

    const auto blockedPath = projectRoot / "blocked.scene";
    std::filesystem::create_directory(blockedPath);
    write_scene(blockedPath / "marker", Json("keep"));
    const auto generationBeforeFailure = workspace.document().documentGeneration;
    action = workspace.save_as(blockedPath);
    require(action.status == WorkspaceActionStatus::Failed && workspace.dirty() &&
                workspace.document().documentGeneration == generationBeforeFailure &&
                !workspace.document().sourcePath,
            "failed Save As must preserve document identity, dirty state and untitled destination");
    require(read_json(blockedPath / "marker") == "keep",
            "failed Save As must leave the existing destination contents untouched");
}

void run_preference_cases()
{
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    std::filesystem::create_directories(assetRoot);
    const auto firstScene = projectRoot / "first.scene";
    const auto missingScene = projectRoot / "gone.scene";
    write_scene(firstScene, workspace_scene("First"));

    auto explicitChoice = resolve_startup_scene(firstScene, missingScene, false);
    require(explicitChoice.scenePath == firstScene && !explicitChoice.usedRestoration &&
                !explicitChoice.missingRestoredScene,
            "an explicit startup scene must take precedence over the restored scene");
    auto missingRestore = resolve_startup_scene(std::nullopt, missingScene, false);
    require(!missingRestore.scenePath && missingRestore.usedRestoration &&
                missingRestore.missingRestoredScene && missingRestore.startWithStarterDocument,
            "a missing restored scene must fall back to a starter document with a recoverable warning");
    auto noRestore = resolve_startup_scene(std::nullopt, firstScene, true);
    require(!noRestore.scenePath && !noRestore.usedRestoration && noRestore.startWithStarterDocument,
            "--no-restore must start a new starter document");

    const auto paths = make_project_preference_paths(temporary.path() / "prefs", projectRoot);
    require(paths.projectKey == std::filesystem::canonical(projectRoot).string(),
            "preference identity must use the canonical project path");
    auto loaded = load_workspace_preferences(paths.settingsPath, paths.layoutPath, projectRoot);
    require(!loaded.settingsPresent && loaded.preferences.projectKey == paths.projectKey && !loaded.preferences.recentScene &&
                loaded.preferences.userScale == 1.0f,
            "missing preferences must produce usable project-scoped defaults");

    const auto defaults = default_panel_visibility();
    require(defaults.size() == 9 && is_panel_visible(loaded.preferences, "panel.hierarchy") &&
                is_panel_visible(loaded.preferences, "panel.scene") &&
                !is_panel_visible(loaded.preferences, "panel.settings"),
            "stable panel IDs must provide the documented visible workspace and optional tool panels");
    const auto layoutSeedPath = std::filesystem::path(__FILE__).parent_path().parent_path() /
                                "resources" / "default-layout.ini";
    std::ifstream layoutSeedStream(layoutSeedPath, std::ios::binary);
    const std::string layoutSeed{std::istreambuf_iterator<char>(layoutSeedStream),
                                 std::istreambuf_iterator<char>()};
    require(layoutSeedStream.good() || layoutSeedStream.eof(), "default docking seed must be readable");
    require(!layout_needs_recovery(layoutSeed, 1280, 800) &&
                layoutSeed.find("[Window][panel.hierarchy]") != std::string::npos &&
                layoutSeed.find("[Window][panel.behaviors]") != std::string::npos,
            "checked-in docking seed must contain stable Hierarchy and Behaviors panel identities");
    require(layout_needs_recovery("[Docking][Data]\nDockSpace ID=0x1 Pos=0,0 Size=1280,800\n"
                                  "[Window][Scene]\nPos=20,20\nSize=600,400\n", 1280, 800) == false &&
                layout_needs_recovery("[Docking][Data]\nnot a valid docking node\n", 1280, 800) &&
                layout_needs_recovery("[Docking][Data]\n[Window][Floating]\nPos=5000,5000\nSize=200,100\n",
                                      1280, 800),
            "layout recovery must keep valid windows and reset corrupt or offscreen state");

    WorkspacePreferences resetPreferences = loaded.preferences;
    resetPreferences.panelVisibility["panel.hierarchy"] = false;
    resetPreferences.userScale = 1.5f;
    reset_workspace_layout(resetPreferences);
    require(is_panel_visible(resetPreferences, "panel.hierarchy") && resetPreferences.userScale == 1.0f &&
                resetPreferences.navigationZoom == 1.0f,
            "Reset Layout must restore panel visibility and display/navigation defaults without scene state");

    loaded.preferences.panelVisibility["panel.hierarchy"] = false;
    loaded.preferences.userScale = 1.5f;
    loaded.preferences.run.target = "platformator_game";
    loaded.layout = "[Window][Workspace]\nPos=12,24\n";
    auto saved = save_workspace_preferences(paths, loaded.preferences, loaded.layout);
    require(saved.succeeded(), "settings and layout must save atomically as separate files");

    record_adopted_scene(loaded.preferences, firstScene);
    saved = save_workspace_preferences(paths, loaded.preferences, loaded.layout);
    require(saved.succeeded(), "a successfully adopted scene path must be persistable");
    auto roundTrip = load_workspace_preferences(paths.settingsPath, paths.layoutPath, projectRoot);
    require(roundTrip.settingsPresent && roundTrip.preferences.recentScene == std::filesystem::canonical(firstScene) &&
                roundTrip.preferences.panelVisibility.at("panel.hierarchy") == false &&
                roundTrip.preferences.userScale == 1.5f &&
                roundTrip.preferences.run.target == "platformator_game" && roundTrip.layout == loaded.layout,
            "project settings, stable panel visibility, layout and adopted scene must round-trip");

    auto otherProject = make_project_preference_paths(temporary.path() / "prefs", temporary.path());
    require(otherProject.settingsPath != paths.settingsPath,
            "different canonical projects must not share preference files");

    write_json(paths.settingsPath, Json{{"version", 999}, {"projectKey", paths.projectKey}, {"recentScene", "bad"}});
    auto corrupt = load_workspace_preferences(paths.settingsPath, paths.layoutPath, projectRoot);
    require(corrupt.recovered && !corrupt.preferences.recentScene &&
                corrupt.preferences.userScale == 1.0f && corrupt.preferences.projectKey == paths.projectKey,
            "unsupported or corrupt preference versions must reset preferences without affecting scene state");

    write_json(paths.settingsPath,
               Json{{"version", 1}, {"projectKey", paths.projectKey},
                    {"run", {{"catalogProtocol", "arbitrary-v1"}}}});
    auto invalidProtocol = load_workspace_preferences(paths.settingsPath, paths.layoutPath, projectRoot);
    require(invalidProtocol.recovered && invalidProtocol.preferences.run.catalogProtocol == "unavailable",
            "catalog preferences must accept only common-runner-v1 or unavailable");

    WorkspacePreferences bundledDefaults;
    const auto sourceRoot = std::filesystem::canonical(
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path());
    apply_known_bundled_run_defaults(bundledDefaults, sourceRoot);
    require(bundledDefaults.run.target == "main" &&
                bundledDefaults.run.executable == "bin/debug/main" &&
                bundledDefaults.run.catalogProtocol == "common-runner-v1",
            "only the recognized shared-runner repository entry should default to catalog compatible");

    WorkspacePreferences customDefaults;
    apply_known_bundled_run_defaults(customDefaults, projectRoot);
    require(customDefaults.run.catalogProtocol == "unavailable",
            "custom project entries must remain unavailable until explicitly declared compatible");
}

void run_editor_action_and_style_cases()
{
    const WindowSettings defaultWindowSettings;
    require(!defaultWindowSettings.highPixelDensity,
            "the new high-density window option must preserve the runtime default");
    const auto actions = editor_action_descriptors();
    require(actions.size() == 15, "all workspace commands must have discoverable action descriptors");
    for (const auto &action : actions)
        require(!action.label.empty() && !action.shortcut.empty(),
                "each available workspace action must expose its label and shortcut");
    const auto binding = [&](EditorAction action) -> std::string_view {
        for (const auto &descriptor : actions)
            if (descriptor.action == action)
                return descriptor.shortcut;
        return {};
    };
    require(binding(EditorAction::FrameAll) == "F" &&
                binding(EditorAction::FrameSelected) == "Shift+F" &&
                binding(EditorAction::Validate) == "F6" &&
                binding(EditorAction::Build) == "Ctrl+B",
            "discoverable action bindings must match legacy framing and current workspace commands");

    EditorInputCapture clearCapture{true, false, false, false, true};
    require(editor_action_allowed(EditorAction::Save, clearCapture) &&
                editor_action_allowed(EditorAction::FrameSelected, clearCapture),
            "global actions and focused Scene actions must be available when input is uncaptured");
    for (const auto capture : {EditorInputCapture{true, true, false, false, true},
                               EditorInputCapture{true, false, true, false, true},
                               EditorInputCapture{true, false, false, true, true},
                               EditorInputCapture{false, false, false, false, true}})
        require(!editor_action_allowed(EditorAction::Save, capture) &&
                    !editor_action_allowed(EditorAction::FrameSelected, capture),
                "text input, modal dialogs, active gestures and application focus loss must suppress shortcuts");
    clearCapture.sceneFocused = false;
    require(!editor_action_allowed(EditorAction::FrameSelected, clearCapture) &&
                editor_action_allowed(EditorAction::Save, clearCapture),
            "Scene framing shortcuts must stay in the Scene while document shortcuts remain global");

    ImGui::CreateContext();
    apply_editor_style(1.0f);
    const ImVec2 baseSpacing = ImGui::GetStyle().ItemSpacing;
    apply_editor_style(1.5f);
    const ImVec2 scaledSpacing = ImGui::GetStyle().ItemSpacing;
    apply_editor_style(1.5f);
    const ImVec2 repeatedSpacing = ImGui::GetStyle().ItemSpacing;
    require(std::abs(scaledSpacing.x - std::trunc(baseSpacing.x * 1.5f)) < 0.01f &&
                std::abs(scaledSpacing.y - std::trunc(baseSpacing.y * 1.5f)) < 0.01f &&
                std::abs(repeatedSpacing.x - scaledSpacing.x) < 0.01f &&
                std::abs(repeatedSpacing.y - scaledSpacing.y) < 0.01f,
            "reapplying a 150 percent scale must always start from the unscaled style base");
    apply_editor_style(1.0f);
    require(std::abs(ImGui::GetStyle().ItemSpacing.x - baseSpacing.x) < 0.01f &&
                std::abs(ImGui::GetStyle().ItemSpacing.y - baseSpacing.y) < 0.01f,
            "returning to 100 percent must restore base spacing without cumulative scaling");
    ImGui::DestroyContext();

    auto document = make_default_document();
    EditCommandService history;
    const auto beforeRoot = document.root;
    const auto beforeStructure = document.structureRevision;
    const auto beforeValues = document.valueRevision;
    const auto beforeHistory = history.history_status();
    WorkspacePreferences preferences;
    reset_workspace_layout(preferences);
    preferences.userScale = 1.5f;
    preferences.userScale = 1.0f;
    require(document.root == beforeRoot && document.structureRevision == beforeStructure &&
                document.valueRevision == beforeValues && history.history_status().undoCommands == beforeHistory.undoCommands &&
                history.history_status().redoCommands == beforeHistory.redoCommands,
            "layout reset and display scaling must not edit the scene or create history");
}

void run_known_validation_cases()
{
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    std::filesystem::create_directories(assetRoot);

    auto cameraDocument = make_default_document(projectRoot, assetRoot, 31);
    cameraDocument.root[0]["components"][0]["camera"] = Json{{"w", 320.0}, {"h", 480.0}};
    const auto cameraIssues = validate_document(cameraDocument);
    require(cameraIssues.blocksRun &&
                std::any_of(cameraIssues.issues.begin(), cameraIssues.issues.end(),
                            [](const ValidationIssue &issue) { return issue.fieldPath == "width"; }),
            "conflicting legacy and canonical camera dimensions must be reported without choosing one");

    auto bodyDocument = make_default_document(projectRoot, assetRoot, 32);
    bodyDocument.root[0]["components"].push_back(
        component(2, 4, Json{{"bodyType", 0}, {"gravity", true}, {"velocity", {{"x", 0.0}, {"y", 1.0}}},
                             {"force", {{"x", 0.0}, {"y", 0.0}}}, {"mass", 0.0},
                             {"angularVelocity", 0.0}, {"torque", 0.0}, {"friction", -1.0},
                             {"restitution", 1.5}}));
    ++bodyDocument.structureRevision;
    const auto bodyIssues = validate_document(bodyDocument);
    auto has_field = [](const ValidationResult &validation, std::string_view field) {
        return std::any_of(validation.issues.begin(), validation.issues.end(), [&](const ValidationIssue &issue) {
            return issue.fieldPath == field && issue.blocksRun;
        });
    };
    require(bodyIssues.blocksRun && has_field(bodyIssues, "mass") && has_field(bodyIssues, "friction") &&
                has_field(bodyIssues, "restitution"),
            "rigid body validation must enforce dynamic mass, nonnegative friction and restitution bounds");

    auto colliderDocument = make_default_document(projectRoot, assetRoot, 33);
    colliderDocument.root[0]["components"].push_back(
        component(2, 3, Json{{"colliderType", 1}, {"width", 0.0}, {"height", 10.0},
                             {"offset", {{"x", 0.0}, {"y", std::numeric_limits<double>::infinity()}}},
                             {"trigger", false}, {"collisionGroup", -1}, {"collisionMask", 0}}));
    ++colliderDocument.structureRevision;
    const auto colliderIssues = validate_document(colliderDocument);
    require(has_field(colliderIssues, "width") && has_field(colliderIssues, "offset.y") &&
                has_field(colliderIssues, "collisionGroup"),
            "collider validation must cover positive dimensions, finite offsets and nonnegative masks");

    auto structuralDocument = make_default_document(projectRoot, assetRoot, 34);
    structuralDocument.root[0]["children"] = "not-an-array";
    ++structuralDocument.structureRevision;
    const auto structuralIssues = validate_document(structuralDocument);
    require(!structuralIssues.structurallyValid && structuralIssues.blocksRun,
            "validation must distinguish rejected structural input from recoverable field issues");
}
