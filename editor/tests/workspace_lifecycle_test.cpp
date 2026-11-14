#include "workspace.h"
#include "actions.h"
#include "editcommands.h"
#include "editorapp.h"
#include "editorwork.h"
#include "panels/hierarchy.h"
#include "preferences.h"
#include "project.h"
#include "style.h"
#include "test_support.h"
#include "validation.h"
#include "gui_fixtures.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
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
                            [](const ValidationIssue &issue)
                            {
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

    const auto unicodeScenePath = projectRoot / "scene Ω with spaces.scene";
    write_scene(unicodeScenePath, workspace_scene("Unicode path"));
    action = workspace.open_document(unicodeScenePath, UnsavedDecision::Discard);
    require(action && workspace.document().sourcePath == std::filesystem::canonical(unicodeScenePath) &&
                workspace.document().root[0]["name"] == "Unicode path",
            "Open Scene must validate and adopt a manually entered path containing spaces and Unicode");

    require(workspace.source_file_status().state == SourceFileState::Current,
            "an opened scene must begin with a current on-disk identity");
    workspace.document_for_edit().root[0]["name"] = "Local unsaved work";
    ++workspace.document_for_edit().valueRevision;
    write_scene(unicodeScenePath, workspace_scene("External change with a different file size"));
    require(workspace.source_file_status().state == SourceFileState::Changed,
            "file identity polling must detect external edits without relying on the asset root watcher");
    const Json localRoot = workspace.document().root;
    action = workspace.save();
    require(action.status == WorkspaceActionStatus::Failed && workspace.dirty() &&
                workspace.document().root == localRoot &&
                workspace.source_file_status().state == SourceFileState::Changed,
            "a save conflict must preserve the local draft and its saved identity");

    {
        std::ofstream malformed(unicodeScenePath, std::ios::binary | std::ios::trunc);
        malformed << "{";
    }
    action = workspace.reload_source_file();
    require(action.status == WorkspaceActionStatus::Failed && workspace.dirty() &&
                workspace.document().root == localRoot &&
                workspace.source_file_status().state == SourceFileState::Changed,
            "failed explicit reload must preserve the current dirty draft and history identity");

    write_scene(unicodeScenePath, workspace_scene("Reloaded external copy"));
    action = workspace.reload_source_file();
    require(action && !workspace.dirty() &&
                workspace.document().root[0]["name"] == "Reloaded external copy" &&
                workspace.source_file_status().state == SourceFileState::Current,
            "explicit reload must adopt valid external content and refresh the source identity");
}

void run_workspace_root_replacement_cases()
{
    TemporaryDirectory temporary;
    const auto firstProject = temporary.path() / "first-project";
    const auto firstAssets = firstProject / "assets";
    const auto secondProject = temporary.path() / "second-project";
    const auto secondAssets = secondProject / "assets";
    std::filesystem::create_directories(firstAssets);
    std::filesystem::create_directories(secondAssets);
    std::ofstream(firstProject / "CMakeLists.txt") << "project(first)";
    std::ofstream(secondProject / "CMakeLists.txt") << "project(second)";

    DocumentWorkspace workspace(firstProject, firstAssets);
    EditCommandService history;
    workspace.bind_history(history);
    const PropertyAddress nameAddress{workspace.document().documentGeneration, 0, std::nullopt, std::nullopt, {std::string("name")}};
    require(static_cast<bool>(history.set_property(workspace.document_for_edit(), nameAddress,
                                                   "Edited before root switch")),
            "root-switch fixture must contain a real dirty history command");
    const auto beforeDocumentGeneration = workspace.document().documentGeneration;
    const auto beforeHistory = history.history_status();
    const auto beforeRoot = workspace.document().root;

    auto changed = workspace.set_project_roots(secondProject, secondAssets, 2);
    require(changed && workspace.document().projectRoot == std::filesystem::canonical(secondProject) &&
                workspace.document().assetRoot == std::filesystem::canonical(secondAssets) &&
                workspace.document().projectGeneration == 2,
            "root replacement must update document resource context and its project generation");
    require(workspace.document().documentGeneration == beforeDocumentGeneration &&
                workspace.document().root == beforeRoot && workspace.dirty() &&
                history.history_status().currentToken == beforeHistory.currentToken &&
                history.history_status().undoCommands == beforeHistory.undoCommands,
            "root replacement must preserve authoring data, dirty state, document identity and undo history");

    const auto invalidBefore = workspace.document();
    changed = workspace.set_project_roots(temporary.path() / "missing-project", secondAssets, 3);
    require(changed.status == WorkspaceActionStatus::Failed &&
                workspace.document().projectRoot == invalidBefore.projectRoot &&
                workspace.document().assetRoot == invalidBefore.assetRoot &&
                workspace.document().projectGeneration == invalidBefore.projectGeneration &&
                history.history_status().currentToken == beforeHistory.currentToken,
            "unavailable roots must be rejected without changing the usable document or history");
}

void run_preference_cases()
{
    TemporaryDirectory temporary;
    DeferredPreferenceSave deferredSave;
    const auto firstEdit = DeferredPreferenceSave::Clock::now();
    deferredSave.schedule(firstEdit);
    require(deferredSave.pending() && deferredSave.deadline() == firstEdit + std::chrono::milliseconds(500) &&
                !deferredSave.ready(firstEdit + std::chrono::milliseconds(499)),
            "preference edits must remain pending until the deferred flush deadline");
    deferredSave.schedule(firstEdit + std::chrono::milliseconds(250));
    require(deferredSave.deadline() == firstEdit + std::chrono::milliseconds(750),
            "new edits must move the deferred save deadline forward");
    require(!deferredSave.ready(firstEdit + std::chrono::milliseconds(749)) &&
                deferredSave.ready(firstEdit + std::chrono::milliseconds(750)),
            "repeated edits must coalesce by moving one flush deadline instead of writing per keystroke");
    std::size_t deferredWrites = 0;
    if (deferredSave.take_if_ready(firstEdit + std::chrono::milliseconds(749)))
        ++deferredWrites;
    if (deferredSave.take_if_ready(firstEdit + std::chrono::milliseconds(750)))
        ++deferredWrites;
    if (deferredSave.take_if_ready(firstEdit + std::chrono::seconds(2)))
        ++deferredWrites;
    require(deferredWrites == 1,
            "a burst of preference edits must produce one deferred write and no duplicate flush");
    require(!deferredSave.pending(), "a completed deadline flush clears deferred state");

    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    std::filesystem::create_directories(assetRoot);
    const auto firstScene = projectRoot / "first.scene";
    const auto missingScene = projectRoot / "gone.scene";
    write_scene(firstScene, workspace_scene("First"));

    auto visibilityDocument = make_default_document(projectRoot, assetRoot, 13);
    EditCommandService visibilityHistory;
    visibilityHistory.reset_history(visibilityDocument.documentGeneration);
    visibilityHistory.mark_saved();
    const Json visibilitySceneBefore = visibilityDocument.root;
    const auto visibilityRevisionBefore = visibilityDocument.valueRevision;
    std::vector<int> editorOnlyIds{4, 8};
    require(set_editor_object_id_state(editorOnlyIds, 6, true) &&
                editorOnlyIds == std::vector<int>({4, 6, 8}) &&
                !set_editor_object_id_state(editorOnlyIds, 6, true),
            "editor-only visibility preferences must stay sorted, unique, and idempotent");
    require(prune_editor_object_ids(editorOnlyIds, std::unordered_set<int>{4, 8}) &&
                editorOnlyIds == std::vector<int>({4, 8}) &&
                !prune_editor_object_ids(editorOnlyIds, std::unordered_set<int>{4, 8}),
            "editor-only visibility preferences must prune stale document IDs once");
    require(visibilityDocument.root == visibilitySceneBefore &&
                visibilityDocument.valueRevision == visibilityRevisionBefore &&
                visibilityHistory.history_status().undoCommands == 0 &&
                visibilityHistory.history_status().currentToken == visibilityHistory.history_status().savedToken,
            "editor-only hidden/locked changes must not alter scene data, revisions, or history");

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
                loaded.preferences.userScale == 1.0f &&
                loaded.preferences.duplicationReferencePolicy == "preserve-existing-targets",
            "missing preferences must produce usable project-scoped defaults");

    const auto defaults = default_panel_visibility();
    require(defaults.size() == 8 && is_panel_visible(loaded.preferences, "panel.hierarchy") &&
                is_panel_visible(loaded.preferences, "panel.scene") &&
                is_panel_visible(loaded.preferences, "panel.inspector") &&
                is_panel_visible(loaded.preferences, "panel.project") &&
                is_panel_visible(loaded.preferences, "panel.console") &&
                !is_panel_visible(loaded.preferences, "panel.game") &&
                !is_panel_visible(loaded.preferences, "panel.project_settings") &&
                !is_panel_visible(loaded.preferences, "panel.validation") &&
                !defaults.contains("panel.settings"),
            "stable panel IDs must provide the documented visible workspace and optional tool panels");
    const auto layoutSeedPath = std::filesystem::path(__FILE__).parent_path().parent_path() /
                                "resources" / "default-layout.ini";
    std::ifstream layoutSeedStream(layoutSeedPath, std::ios::binary);
    const std::string layoutSeed{std::istreambuf_iterator<char>(layoutSeedStream),
                                 std::istreambuf_iterator<char>()};
    require(layoutSeedStream.good() || layoutSeedStream.eof(), "default docking seed must be readable");
    require(!layout_needs_recovery(layoutSeed, 1280, 800) &&
                layoutSeed.find("[Window][panel.hierarchy]") != std::string::npos &&
                layoutSeed.find("[Window][panel.inspector]") != std::string::npos &&
                layoutSeed.find("[Window][panel.behaviors]") == std::string::npos &&
                layoutSeed.find("[Window][panel.settings]") == std::string::npos,
            "checked-in docking seed must omit retired Behaviors and Editor Settings panels");
    std::string legacySceneLayout = layoutSeed;
    const auto sceneHeader = legacySceneLayout.find("[Window][panel.scene]");
    require(sceneHeader != std::string::npos, "docking seed must contain the stable Scene identity");
    legacySceneLayout.replace(sceneHeader, std::string("[Window][panel.scene]").size(), "[Window][Scene]");
    const auto migratedSceneLayout = migrate_scene_window_identity(legacySceneLayout);
    require(migratedSceneLayout.find("[Window][panel.scene]") != std::string::npos &&
                migratedSceneLayout.find("[Window][Scene]") == std::string::npos &&
                migratedSceneLayout.find("DockId=0x00003000") != std::string::npos,
            "legacy Scene docking preferences must migrate to the stable runtime identity with placement intact");
    require(migrate_scene_window_identity(migratedSceneLayout) == migratedSceneLayout,
            "Scene identity migration must be idempotent");
    const std::string staleBehaviorLayout = layoutSeed +
                                            "[Window][panel.behaviors]\nPos=20,20\nSize=300,200\n"
                                            "[Window][panel.settings]\nPos=30,30\nSize=300,200\n";
    const auto migratedPanelLayout = migrate_scene_window_identity(staleBehaviorLayout);
    require(migratedPanelLayout.find("[Window][panel.behaviors]") == std::string::npos &&
                migratedPanelLayout.find("[Window][panel.settings]") == std::string::npos &&
                migratedPanelLayout.find("[Window][panel.project]") != std::string::npos &&
                migrate_scene_window_identity(migratedPanelLayout) == migratedPanelLayout,
            "saved docking layouts must discard the retired Behaviors panel while retaining other panels");
    require(layout_needs_recovery("[Docking][Data]\nDockSpace ID=0x1 Pos=0,0 Size=1280,800\n"
                                  "[Window][Scene]\nPos=20,20\nSize=600,400\n",
                                  1280, 800) == false &&
                layout_needs_recovery("[Docking][Data]\nnot a valid docking node\n", 1280, 800) &&
                layout_needs_recovery("[Docking][Data]\n[Window][Floating]\nPos=5000,5000\nSize=200,100\n",
                                      1280, 800),
            "layout recovery must keep valid windows and reset corrupt or offscreen state");

    const std::string multiSpaceLayout =
        "[Docking][Data]\n"
        "DockSpace       ID=0x6E706EF3 Window=0x12345678 Pos=0,0 Size=1280,800 Split=Y\n"
        "  DockNode      ID=0x00001000 Parent=0x6E706EF3 SizeRef=1280,500 Split=X\n"
        "    DockNode    ID=0x00002000 Parent=0x00001000 SizeRef=260,500\n"
        "    DockNode    ID=0x00003000 Parent=0x00001000 SizeRef=1020,500 CentralNode=1\n"
        "[Window][panel.scene]\n"
        "Pos=260,0\n"
        "Size=720,570\n"
        "DockId=0x00003000,0\n"
        "[Window][panel.game]\n"
        "Pos=-5000,-5000\n"
        "Size=720,570\n"
        "DockId=0x00003000,1\n";
    require(!layout_needs_recovery(multiSpaceLayout, 1280, 800),
            "layout_needs_recovery must accept multi-space DockSpace layouts emitted by ImGui and docked windows with out-of-bounds positions");

    require(layoutSeed.find("[Window][panel.game]") != std::string::npos &&
                layoutSeed.find("DockId=0x00003000,1") != std::string::npos,
            "default-layout.ini must dock [Window][panel.game] alongside [Window][panel.scene] into central node 0x00003000");
    require(loaded.preferences.panelHostAssignments.at("panel.game") == main_host_layout_id,
            "default panelHostAssignments must include panel.game mapped to main host");

    const std::string layoutWithoutGame =
        "[Docking][Data]\n"
        "DockSpace ID=0x6E706EF3 Pos=0,0 Size=1280,800 Split=Y\n"
        "  DockNode ID=0x00003000 CentralNode=1\n"
        "[Window][panel.scene]\n"
        "Pos=260,0\n"
        "Size=720,570\n"
        "DockId=0x00003000,0\n";
    const auto migratedWithGame = migrate_scene_window_identity(layoutWithoutGame);
    require(migratedWithGame.find("[Window][panel.game]") != std::string::npos &&
                migratedWithGame.find("DockId=0x00003000") != std::string::npos,
            "migrate_scene_window_identity must dock [Window][panel.game] into central node 0x00003000 if missing");

    const auto roundTripPaths = make_project_preference_paths(temporary.path() / "docking-roundtrip", projectRoot);
    auto dockingPrefs = loaded.preferences;
    auto saveDocking = save_workspace_preferences(roundTripPaths, dockingPrefs, multiSpaceLayout);
    require(saveDocking.succeeded(), "saving modified docking layout must succeed");
    auto loadedDocking = load_workspace_preferences(roundTripPaths.settingsPath, roundTripPaths.layoutPath, projectRoot);
    require(!layout_needs_recovery(loadedDocking.layout, 1280, 800) &&
                loadedDocking.layout == multiSpaceLayout,
            "docking modifications must round-trip across preference save/load without resetting to defaultLayout");

    WorkspacePreferences resetPreferences = loaded.preferences;
    resetPreferences.panelVisibility["panel.hierarchy"] = false;
    resetPreferences.userScale = 1.5f;
    resetPreferences.panelHostAssignments["panel.console"] = HostLayoutId{2};
    resetPreferences.hostGeometry[HostLayoutId{2}] = HostGeometryPreferences{200, 140, 800, 600, false};
    resetPreferences.hostLayoutSettings[HostLayoutId{2}] = "secondary layout";
    reset_workspace_layout(resetPreferences);
    require(is_panel_visible(resetPreferences, "panel.hierarchy") && resetPreferences.userScale == 1.0f &&
                resetPreferences.navigationZoom == 1.0f &&
                resetPreferences.panelHostAssignments.at("panel.console") == main_host_layout_id &&
                !resetPreferences.hostGeometry.contains(HostLayoutId{2}) &&
                resetPreferences.hostLayoutSettings.empty(),
            "Reset Layout must redock panels and clear detached host geometry/layouts without scene state");

    loaded.preferences.panelVisibility["panel.hierarchy"] = false;
    loaded.preferences.userScale = 1.5f;
    loaded.preferences.run.target = "platformator_game";
    loaded.preferences.panelHostAssignments["panel.scene"] = HostLayoutId{2};
    loaded.preferences.hostGeometry[HostLayoutId{2}] = HostGeometryPreferences{220, 120, 960, 640, true};
    loaded.preferences.hostLayoutSettings[HostLayoutId{2}] = "[Docking][Data]\nsecondary-layout\n";
    loaded.preferences.openTabs.push_back(
        SavedTabPreferences{firstScene, 12.5f, -7.0f, 1.75f, {1, 4}, true});
    const auto templatePath = projectRoot / "assets" / "templates" / "crate.pttemplate";
    const auto animationPath = projectRoot / "assets" / "walk.animset";
    loaded.preferences.openTabs.push_back(SavedTabPreferences{
        templatePath, -2.0f, 3.0f, 1.25f, {}, false, SavedDocumentKind::Template});
    loaded.preferences.openTabs.push_back(SavedTabPreferences{
        animationPath, 4.0f, 5.0f, 0.8f, {}, false, SavedDocumentKind::Animation});
    loaded.preferences.activeTabPath = animationPath;
    loaded.preferences.activeDocumentKind = SavedDocumentKind::Animation;
    loaded.preferences.hiddenObjectIdsByScene[firstScene.string()] = {4};
    loaded.preferences.lockedObjectIdsByScene[firstScene.string()] = {8, 9};
    loaded.preferences.snap = SnapPreferences{0.5f, 45.0f, 0.25f, "local"};
    loaded.preferences.duplicationReferencePolicy = "remap-internal-targets";
    loaded.preferences.resourceLimits["decodeWorkingBytes"] = 268435456;
    loaded.layout = "[Window][Workspace]\nPos=12,24\n";
    auto saved = save_workspace_preferences(paths, loaded.preferences, loaded.layout);
    require(saved.succeeded(), "settings and layout must save atomically as separate files");

    record_adopted_scene(loaded.preferences, firstScene);
    saved = save_workspace_preferences(paths, loaded.preferences, loaded.layout);
    require(saved.succeeded(), "a successfully adopted scene path must be persistable");
    const auto blockedParent = temporary.path() / "not-a-directory";
    std::ofstream(blockedParent) << "occupied";
    const ProjectPreferencePaths brokenPaths{blockedParent / "settings.json", blockedParent / "layout.ini",
                                             paths.projectKey};
    const auto failedSave = save_workspace_preferences(brokenPaths, loaded.preferences, loaded.layout);
    require(!failedSave.succeeded() && !failedSave.message.empty(),
            "deferred preference flushes must preserve durability failures for user-visible reporting");
    auto roundTrip = load_workspace_preferences(paths.settingsPath, paths.layoutPath, projectRoot);
    require(roundTrip.settingsPresent && roundTrip.preferences.recentScene == std::filesystem::canonical(firstScene) &&
                roundTrip.preferences.panelVisibility.at("panel.hierarchy") == false &&
                roundTrip.preferences.userScale == 1.5f &&
                roundTrip.preferences.run.target == "platformator_game" && roundTrip.layout == loaded.layout &&
                roundTrip.preferences.panelHostAssignments.at("panel.scene") == HostLayoutId{2} &&
                roundTrip.preferences.hostGeometry.at(HostLayoutId{2}) ==
                    HostGeometryPreferences{220, 120, 960, 640, true} &&
                roundTrip.preferences.hostLayoutSettings.at(HostLayoutId{2}) ==
                    "[Docking][Data]\nsecondary-layout\n" &&
                roundTrip.preferences.openTabs.size() == 3 &&
                roundTrip.preferences.openTabs[0] ==
                    SavedTabPreferences{firstScene, 12.5f, -7.0f, 1.75f, {1, 4}, true} &&
                roundTrip.preferences.openTabs[1].kind == SavedDocumentKind::Template &&
                roundTrip.preferences.openTabs[1].documentPath == templatePath &&
                roundTrip.preferences.openTabs[2].kind == SavedDocumentKind::Animation &&
                roundTrip.preferences.openTabs[2].documentPath == animationPath &&
                roundTrip.preferences.activeTabPath == animationPath &&
                roundTrip.preferences.activeDocumentKind == SavedDocumentKind::Animation &&
                roundTrip.preferences.hiddenObjectIdsByScene.at(firstScene.string()) == std::vector<int>{4} &&
                roundTrip.preferences.lockedObjectIdsByScene.at(firstScene.string()) == std::vector<int>{8, 9} &&
                roundTrip.preferences.snap == SnapPreferences{0.5f, 45.0f, 0.25f, "local"} &&
                roundTrip.preferences.duplicationReferencePolicy == "remap-internal-targets" &&
                roundTrip.preferences.resourceLimits.at("decodeWorkingBytes") == 268435456,
            "v2 project settings, host layouts, per-tab state and authoring preferences must round-trip");

    {
        std::ifstream input(paths.settingsPath, std::ios::binary);
        Json legacy = Json::parse(std::string{std::istreambuf_iterator<char>(input),
                                              std::istreambuf_iterator<char>()});
        legacy.erase("activeTabKind");
        for (auto &tab : legacy["openTabs"])
            tab.erase("kind");
        std::ofstream output(paths.settingsPath, std::ios::binary | std::ios::trunc);
        output << legacy.dump(2) << '\n';
    }
    const auto legacyKinds = load_workspace_preferences(paths.settingsPath, paths.layoutPath, projectRoot);
    require(legacyKinds.preferences.activeDocumentKind == SavedDocumentKind::Scene &&
                std::all_of(legacyKinds.preferences.openTabs.begin(), legacyKinds.preferences.openTabs.end(),
                    [](const SavedTabPreferences &tab) { return tab.kind == SavedDocumentKind::Scene; }),
            "older saved tabs without document kinds must continue to restore as Scenes");

    const auto read_text = [](const std::filesystem::path &path)
    {
        std::ifstream stream(path, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    };
    const auto layoutBeforeSettingsOnly = read_text(paths.layoutPath);
    const auto settingsBeforeSettingsOnly = read_text(paths.settingsPath);
    auto settingsOnlyPreferences = loaded.preferences;
    settingsOnlyPreferences.userScale = 1.75f;
    work::reset();
    const auto settingsOnly = save_workspace_preferences(paths, settingsOnlyPreferences,
                                                         loaded.layout, true, false);
    require(settingsOnly.succeeded() && read_text(paths.layoutPath) == layoutBeforeSettingsOnly &&
                read_text(paths.settingsPath) != settingsBeforeSettingsOnly,
            "a settings-only commit must preserve the layout file and report its own write outcome");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    auto preferenceCounts = work::snapshot();
    require(preferenceCounts[static_cast<std::size_t>(work::Counter::PreferenceSettingsWriteAttempts)] == 1 &&
                preferenceCounts[static_cast<std::size_t>(work::Counter::PreferenceLayoutWriteAttempts)] == 0,
            "the preference work probe must count one settings write and no layout write for a settings-only save");
#endif
    const auto settingsAfterSettingsOnly = read_text(paths.settingsPath);
    const auto layoutOnlyValue = std::string("[Window][Workspace]\nPos=32,48\n");
    const auto layoutOnly = save_workspace_preferences(paths, loaded.preferences,
                                                       layoutOnlyValue, false, true);
    require(layoutOnly.succeeded() && read_text(paths.settingsPath) == settingsAfterSettingsOnly &&
                read_text(paths.layoutPath) == layoutOnlyValue,
            "a layout-only commit must preserve settings bytes and report its own write outcome");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    preferenceCounts = work::snapshot();
    require(preferenceCounts[static_cast<std::size_t>(work::Counter::PreferenceSettingsWriteAttempts)] == 1 &&
                preferenceCounts[static_cast<std::size_t>(work::Counter::PreferenceLayoutWriteAttempts)] == 1,
            "settings and docking layout writes must have independently attributable work counters");
#endif

    WorkspacePreferences livePreferences = settingsOnlyPreferences;
    std::string liveLayout = layoutOnlyValue;
    WorkspacePreferencePersistence preferencePersistence(livePreferences, liveLayout);
    DeferredPreferenceSave interactionSave;
    std::size_t interactionSettingsWrites = 0;
    std::size_t interactionLayoutWrites = 0;
    const auto interactionWriter = [&](bool writeSettings, bool writeLayout)
    {
        interactionSettingsWrites += writeSettings ? 1U : 0U;
        interactionLayoutWrites += writeLayout ? 1U : 0U;
        return save_workspace_preferences(paths, livePreferences, liveLayout, writeSettings, writeLayout);
    };
    require(preferencePersistence.flush(livePreferences, liveLayout, interactionWriter).succeeded() &&
                interactionSettingsWrites == 0 && interactionLayoutWrites == 0,
            "an unchanged preference frame must not invoke settings, layout or durability writes");

    auto burstOrigin = firstEdit + std::chrono::seconds(10);
    const auto run_interaction_burst = [&](auto update, int frames, std::string_view interaction)
    {
        const auto settingsBefore = interactionSettingsWrites;
        const auto layoutBefore = interactionLayoutWrites;
        auto lastFrame = burstOrigin;
        for (int frame = 0; frame < frames; ++frame)
        {
            update(frame);
            lastFrame = burstOrigin + std::chrono::milliseconds(frame * 16);
            interactionSave.schedule(lastFrame);
            require(!interactionSave.take_if_ready(lastFrame),
                    std::string(interaction) + " updates must not flush preferences during active input");
        }
        const auto flushAt = interactionSave.deadline();
        require(interactionSave.take_if_ready(flushAt),
                std::string(interaction) + " must become eligible after the bounded quiet period");
        const auto stored = preferencePersistence.flush(livePreferences, liveLayout, interactionWriter);
        require(stored.succeeded() && interactionSettingsWrites == settingsBefore + 1 &&
                    interactionLayoutWrites == layoutBefore,
                std::string(interaction) + " must write changed settings once and leave docking layout untouched");
        burstOrigin = flushAt + std::chrono::seconds(1);
    };

    run_interaction_burst([&](int frame)
                          { livePreferences.snap.position = 1.0f + static_cast<float>(frame) * 0.01f; }, 30, "snap DragFloat");
    livePreferences.openTabs.push_back(SavedTabPreferences{firstScene});
    run_interaction_burst([&](int frame)
                          { livePreferences.openTabs.front().viewZoom = 1.0f + static_cast<float>(frame) * 0.005f; }, 30, "viewport wheel zoom");
    run_interaction_burst([&](int)
                          { livePreferences.run.target.push_back('x'); }, 24, "Run Settings typing");
    run_interaction_burst([&](int frame)
                          { livePreferences.openTabs.front().viewCenterX = static_cast<float>(frame) * 0.5f; }, 120, "continuous viewport updates");

    WorkspacePreferencePersistence retryPersistence(livePreferences, liveLayout);
    auto retryPreferences = livePreferences;
    retryPreferences.snap.rotationDegrees = 30.0f;
    std::size_t failedWriterCalls = 0;
    const auto failedFlush = retryPersistence.flush(retryPreferences, liveLayout,
                                                    [&](bool writeSettings, bool writeLayout)
                                                    {
                                                        ++failedWriterCalls;
                                                        return save_workspace_preferences(brokenPaths, retryPreferences, liveLayout,
                                                                                          writeSettings, writeLayout);
                                                    });
    require(!failedFlush.succeeded() && !failedFlush.settingsSaved && failedFlush.layoutSaved &&
                failedWriterCalls == 1,
            "an injected settings failure must be reported while leaving that section dirty for retry");
    const auto retriedFlush = retryPersistence.flush(retryPreferences, liveLayout,
                                                     [&](bool writeSettings, bool writeLayout)
                                                     {
                                                         ++failedWriterCalls;
                                                         return save_workspace_preferences(paths, retryPreferences, liveLayout,
                                                                                           writeSettings, writeLayout);
                                                     });
    require(retriedFlush.succeeded() && failedWriterCalls == 2 &&
                retryPersistence.flush(retryPreferences, liveLayout, {}).succeeded() && failedWriterCalls == 2,
            "a failed preference flush must be retryable and successful sections must not be rewritten");

    write_json(paths.settingsPath,
               Json{{"version", 1}, {"projectKey", paths.projectKey}, {"recentScene", firstScene.string()}, {"panelVisibility", {{"panel.scene", false}}}, {"userScale", 1.5}, {"navigationZoom", 2.0}, {"editorWindow", {{"width", 1440}, {"height", 900}, {"maximized", true}}}, {"run", {{"target", "legacy-game"}, {"buildPreset", "release"}, {"production", true}, {"catalogProtocol", "common-runner-v1"}}}});
    const std::string legacyLayout = "[Window][Scene]\nPos=10,20\nSize=500,400\n";
    {
        std::ofstream legacyLayoutStream(paths.layoutPath, std::ios::binary | std::ios::trunc);
        legacyLayoutStream << legacyLayout;
    }
    auto migrated = load_workspace_preferences(paths.settingsPath, paths.layoutPath, projectRoot);
    require(migrated.migrated && !migrated.recovered && migrated.preferences.version == 2 &&
                migrated.preferences.recentScene == firstScene &&
                migrated.preferences.panelVisibility.at("panel.scene") == false &&
                migrated.preferences.userScale == 1.5f && migrated.preferences.navigationZoom == 2.0f &&
                migrated.preferences.editorWindow.width == 1440 && migrated.preferences.editorWindow.maximized &&
                migrated.preferences.run.target == "legacy-game" &&
                migrated.preferences.run.buildPreset == "release" && migrated.preferences.run.production &&
                migrated.preferences.duplicationReferencePolicy == "preserve-existing-targets" &&
                migrated.preferences.panelHostAssignments.at("panel.scene") == main_host_layout_id &&
                migrated.preferences.hostGeometry.contains(main_host_layout_id) &&
                migrated.preferences.hostLayoutSettings.at(main_host_layout_id) ==
                    "[Window][panel.scene]\nPos=10,20\nSize=500,400\n",
            "v1 migration must preserve existing settings, seed the main host, and retain the duplicate-policy default");

    auto otherProject = make_project_preference_paths(temporary.path() / "prefs", temporary.path());
    require(otherProject.settingsPath != paths.settingsPath,
            "different canonical projects must not share preference files");

    write_json(paths.settingsPath, Json{{"version", 999}, {"projectKey", paths.projectKey}, {"recentScene", "bad"}});
    auto corrupt = load_workspace_preferences(paths.settingsPath, paths.layoutPath, projectRoot);
    require(corrupt.recovered && !corrupt.preferences.recentScene &&
                corrupt.preferences.userScale == 1.0f && corrupt.preferences.projectKey == paths.projectKey,
            "unsupported or corrupt preference versions must reset preferences without affecting scene state");

    WorkspacePreferences bundledDefaults;
    const auto sourceRoot = std::filesystem::canonical(
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path());
    apply_known_bundled_run_defaults(bundledDefaults, sourceRoot);
    require(bundledDefaults.run.target == "main" &&
                bundledDefaults.run.executable == "bin/debug/main",
            "bundled defaults should configure main target and executable");

    // GlobalPreferences recent projects & create_new_project / generate_project_cmakelists
    GlobalPreferences gp;
    record_recent_project(gp, projectRoot, "TestProject", 100);
    require(gp.recentProjects.size() == 1 && gp.recentProjects[0].name == "TestProject",
            "record_recent_project must store project entry");
    const auto gpFile = temporary.path() / "global.json";
    std::string gpError;
    require(save_global_preferences(gpFile, gp, gpError), "save_global_preferences must succeed");
    const auto gpLoaded = load_global_preferences(gpFile);
    require(gpLoaded.preferences.recentProjects.size() == 1 &&
                gpLoaded.preferences.recentProjects[0].name == "TestProject",
            "load_global_preferences must roundtrip recent projects");

    ProjectScaffoldRequest scaffoldReq;
    scaffoldReq.parentDirectory = temporary.path();
    scaffoldReq.projectName = "SampleGame";
    scaffoldReq.platformatorSourceRoot = sourceRoot;
    const auto scaffoldRes = create_new_project(scaffoldReq);
    require(scaffoldRes.succeeded &&
                std::filesystem::is_regular_file(scaffoldRes.projectRoot / "CMakeLists.txt") &&
                std::filesystem::is_directory(scaffoldRes.projectRoot / "assets" / "scenes") &&
                std::filesystem::is_directory(scaffoldRes.projectRoot / "assets" / "behaviors") &&
                std::filesystem::is_regular_file(scaffoldRes.projectRoot / "assets" / "scenes" / "main.scene"),
            "create_new_project must scaffold project CMakeLists.txt and asset directories");
    WorkspacePreferences generatedDefaults;
    apply_generated_project_run_defaults(generatedDefaults, scaffoldRes.projectRoot);
    require(generatedDefaults.run.target == "SampleGame" &&
                generatedDefaults.run.executable == "bin/debug/SampleGame" &&
                generatedDefaults.run.configurePreset.empty() && generatedDefaults.run.buildPreset.empty() &&
                generatedDefaults.recentScene == scaffoldRes.projectRoot / "assets" / "scenes" / "main.scene" &&
                generatedDefaults.activeTabPath == generatedDefaults.recentScene,
            "generated project defaults must initialize target, executable, and startup scene choices");
    const auto generatedPreferences = make_project_preference_paths(
        temporary.path() / "generated-prefs", scaffoldRes.projectRoot);
    generatedDefaults.projectKey = generatedPreferences.projectKey;
    const auto generatedSaved = save_workspace_preferences(generatedPreferences, generatedDefaults, "", true, false);
    const auto generatedLoaded = load_workspace_preferences(generatedPreferences.settingsPath,
                                                             generatedPreferences.layoutPath,
                                                             scaffoldRes.projectRoot);
    require(generatedSaved.succeeded() && generatedLoaded.settingsPresent &&
                generatedLoaded.preferences.run == generatedDefaults.run,
            "generated target and executable defaults must persist in project preferences");
    auto existingPreferences = generatedLoaded.preferences;
    existingPreferences.run.target = "authored-target";
    existingPreferences.run.executable = "custom/bin/game";
    existingPreferences.run.buildPreset = "custom-preset";
    existingPreferences.recentScene = scaffoldRes.projectRoot / "assets" / "scenes" / "main.scene";
    existingPreferences.activeTabPath = existingPreferences.recentScene;
    apply_generated_project_run_defaults(existingPreferences, scaffoldRes.projectRoot);
    apply_known_bundled_run_defaults(existingPreferences, scaffoldRes.projectRoot);
    require(existingPreferences.run.target == "authored-target" &&
                existingPreferences.run.executable == "custom/bin/game" &&
                existingPreferences.run.buildPreset == "custom-preset" &&
                existingPreferences.recentScene == scaffoldRes.projectRoot / "assets" / "scenes" / "main.scene" &&
                existingPreferences.activeTabPath == existingPreferences.recentScene,
            "generated defaults must preserve authored run and scene preferences");

    const auto assetOnlyDir = temporary.path() / "AssetOnlyProject";
    std::filesystem::create_directories(assetOnlyDir / "assets" / "scenes");
    const auto authoredScene = assetOnlyDir / "assets" / "scenes" / "intro.scene";
    {
        std::ofstream sceneFile(authoredScene, std::ios::binary);
        sceneFile << "[]\n";
    }
    const auto candidate = validate_project_folder(assetOnlyDir);
    require(candidate.missingCmake, "validate_project_folder must mark missing CMakeLists.txt");
    const auto genCmake = generate_project_cmakelists(assetOnlyDir, "AssetOnlyProject", sourceRoot);
    std::ifstream generatedMain(assetOnlyDir / "main.cpp", std::ios::binary);
    const std::string generatedMainText((std::istreambuf_iterator<char>(generatedMain)),
                                        std::istreambuf_iterator<char>());
    std::ifstream preservedScene(authoredScene, std::ios::binary);
    std::string preservedSceneText;
    std::getline(preservedScene, preservedSceneText);
    WorkspacePreferences assetOnlyDefaults;
    apply_generated_project_run_defaults(assetOnlyDefaults, assetOnlyDir);
    require(genCmake.succeeded && std::filesystem::is_regular_file(assetOnlyDir / "CMakeLists.txt") &&
                std::filesystem::is_regular_file(assetOnlyDir / "main.cpp") &&
                generatedMainText.find("assets/scenes/intro.scene") != std::string::npos &&
                preservedSceneText == "[]" && !std::filesystem::exists(assetOnlyDir / "assets" / "scenes" / "main.scene") &&
                assetOnlyDefaults.run.target == "AssetOnlyProject" &&
                assetOnlyDefaults.run.executable == "bin/debug/AssetOnlyProject" &&
                assetOnlyDefaults.recentScene == authoredScene &&
                assetOnlyDefaults.activeTabPath == authoredScene,
            "asset-only generation must add a runnable entry point, select an authored scene, and preserve it");
}

void test_editor_wait_uses_earliest_deadline_and_bounded_polling()
{
    using Clock = EditorWaitClock;
    const auto now = Clock::time_point{} + std::chrono::seconds(20);
    const std::array deadlines{now + std::chrono::milliseconds(30),
                               now + std::chrono::milliseconds(5)};
    require(editor_wait_duration(now, deadlines, std::chrono::milliseconds(100)) ==
                std::chrono::milliseconds(5),
            "the outer wait must stop at its earliest scheduled maintenance deadline");
    const auto eventDrivenMaximum = std::chrono::milliseconds(std::numeric_limits<int>::max());
    require(editor_wait_duration(now, deadlines, eventDrivenMaximum) == std::chrono::milliseconds(5),
            "event-driven idle scheduling must still wake at the earliest maintenance deadline");
    require(editor_wait_duration(now, {}, eventDrivenMaximum) == eventDrivenMaximum,
            "a static foreground frame must wait for input or a completion instead of redrawing periodically");
    require(editor_wait_duration(now, deadlines, std::chrono::milliseconds(100),
                                 std::chrono::milliseconds(8)) == std::chrono::milliseconds(5),
            "poll fallback must not delay an earlier event or retry deadline");
    require(editor_wait_duration(now, {}, std::chrono::milliseconds(100),
                                 std::chrono::milliseconds(8)) == std::chrono::milliseconds(8),
            "services without completion wakeups must retain a bounded polling interval");
    const std::array overdue{now - std::chrono::milliseconds(1)};
    require(editor_wait_duration(now, overdue, std::chrono::milliseconds(100)) ==
                std::chrono::milliseconds(0),
            "an overdue deadline must return immediately without a negative SDL timeout");
}

void run_editor_wait_schedule_cases()
{
    test_editor_wait_uses_earliest_deadline_and_bounded_polling();

    CoalescedWakeGate wakeGate;
    require(!wakeGate.pending() && wakeGate.try_queue() && wakeGate.pending() &&
                !wakeGate.try_queue(),
            "concurrent service completions must coalesce behind one queued SDL wake event");
    wakeGate.event_consumed();
    require(!wakeGate.pending() && wakeGate.try_queue(),
            "consuming the wake event must permit a later completion to wake the owner again");
    wakeGate.queue_failed();
    require(!wakeGate.pending() && wakeGate.try_queue(),
            "a failed SDL queue insertion must leave the wake gate recoverable");
    wakeGate.event_consumed();

    EditorAppTickDriver hoverProbe(EditorAppProbeOptions{12, 2, true});
    require(!hoverProbe.should_inject_hover_input(),
            "input workload events must not contaminate the warmup frames");
    for (int frame = 0; frame < 2; ++frame)
        require(hoverProbe.tick([]
                                { return true; }, [] {}),
                "the GUI probe must continue through its configured warmup");
    require(hoverProbe.should_inject_hover_input(),
            "the selected hover workload must begin with its first measured frame");
    require(hoverProbe.tick([]
                            { return true; }, [] {}) &&
                !hoverProbe.should_inject_hover_input(),
            "the hover workload must inject one scene-window event every ten measured frames");

    std::ostringstream captured;
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    EditorAppTickDriver metricsProbe(EditorAppProbeOptions{1, 0, false});
    const auto started = EditorWaitClock::now();
    metricsProbe.note_input_event(started);
    metricsProbe.note_present(started + std::chrono::milliseconds(3), started - std::chrono::milliseconds(10));
    metricsProbe.tick([]
                      { return true; }, [] {});
    std::cout.rdbuf(previous);
    const auto output = captured.str();
    require(output.find("GUI_COLD_PREP_US,13000") != std::string::npos &&
                output.find("GUI_EVENT_PRESENT_US,0,3000") != std::string::npos &&
                output.find("GUI_ACTIVE_FRAME_US,0,") != std::string::npos &&
                output.find("GUI_WAIT_INCLUSIVE_US,0,") != std::string::npos &&
                output.find("GUI_IDLE_CPU_US,0,") != std::string::npos,
            "the app probe must report cold preparation, input-to-present, active, wait-inclusive, and idle CPU samples");
}

void run_editor_action_and_style_cases()
{
    const WindowSettings defaultWindowSettings;
    require(!defaultWindowSettings.highPixelDensity,
            "the new high-density window option must preserve the runtime default");
    const auto actions = editor_action_descriptors();
    require(actions.size() == 22, "all workspace commands must have discoverable action descriptors");
    for (const auto &action : actions)
        require(!action.label.empty() && !action.shortcut.empty(),
                "each available workspace action must expose its label and shortcut");
    const auto binding = [&](EditorAction action) -> std::string_view
    {
        for (const auto &descriptor : actions)
            if (descriptor.action == action)
                return descriptor.shortcut;
        return {};
    };
    require(binding(EditorAction::FrameAll) == "Shift+F" &&
                binding(EditorAction::FrameSelected) == "F" &&
                binding(EditorAction::SelectHandTool) == "Q" &&
                binding(EditorAction::SelectMoveTool) == "W" &&
                binding(EditorAction::SelectRotateTool) == "E" &&
                binding(EditorAction::SelectScaleTool) == "R" &&
                binding(EditorAction::SelectRectangleTool) == "T" &&
                binding(EditorAction::Build) == "Ctrl+B" &&
                binding(EditorAction::Preferences) == "Ctrl+,",
            "discoverable action bindings must include framing, transform tools and workspace commands");

    EditorInputCapture clearCapture{true, false, false, false, true};
    require(editor_action_allowed(EditorAction::Save, clearCapture) &&
                editor_action_allowed(EditorAction::Preferences, clearCapture) &&
                editor_action_allowed(EditorAction::FrameSelected, clearCapture) &&
                editor_action_allowed(EditorAction::SelectHandTool, clearCapture),
            "global actions and focused Scene actions must be available when input is uncaptured");
    require(dispatch_editor_key(EditorKey::F5, true, false, clearCapture) == EditorAction::Stop &&
                dispatch_editor_key(EditorKey::F, true, false, clearCapture) == EditorAction::FrameAll &&
                dispatch_editor_key(EditorKey::F, false, false, clearCapture) == EditorAction::FrameSelected &&
                dispatch_editor_key(EditorKey::Q, false, false, clearCapture) == EditorAction::SelectHandTool &&
                dispatch_editor_key(EditorKey::Comma, false, true, clearCapture) == EditorAction::Preferences,
            "production key routing must map active-run Stop and Scene framing commands");
    for (const auto blockedCapture : {EditorInputCapture{false, false, false, false, true},
                                      EditorInputCapture{true, true, false, false, true},
                                      EditorInputCapture{true, false, true, false, true},
                                      EditorInputCapture{true, false, false, true, true}})
        require(!dispatch_editor_key(EditorKey::F5, true, false, blockedCapture) &&
                    !dispatch_editor_key(EditorKey::F, false, false, blockedCapture) &&
                    !dispatch_editor_key(EditorKey::Comma, false, true, blockedCapture),
                "focus loss, text/modal ownership and active gestures must suppress global key dispatch");
    require(!dispatch_editor_key(EditorKey::F, false, false,
                                 EditorInputCapture{true, false, false, false, false}),
            "Scene framing key dispatch must require Scene focus");
    require(!dispatch_editor_key(EditorKey::Comma, false, true,
                                 EditorInputCapture{true, false, false, false, true, false, false}),
            "global shortcuts must not dispatch from a host that does not own keyboard focus");
    EditorInputCapture hierarchyCapture{true, false, false, false, false, true};
    require(editor_action_allowed(EditorAction::RenameSelection, hierarchyCapture) &&
                editor_action_allowed(EditorAction::DeleteSelection, hierarchyCapture) &&
                editor_action_allowed(EditorAction::DuplicateSelection, hierarchyCapture) &&
                dispatch_editor_key(EditorKey::F2, false, false, hierarchyCapture) ==
                    EditorAction::RenameSelection &&
                dispatch_editor_key(EditorKey::Delete, false, false, hierarchyCapture) ==
                    EditorAction::DeleteSelection &&
                dispatch_editor_key(EditorKey::D, false, true, hierarchyCapture) ==
                    EditorAction::DuplicateSelection &&
                !dispatch_editor_key(EditorKey::D, false, true, clearCapture),
            "rename/delete/duplicate shortcuts must require focused Hierarchy capture");
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
                            [](const ValidationIssue &issue)
                            { return issue.fieldPath == "width"; }),
            "conflicting legacy and canonical camera dimensions must be reported without choosing one");

    auto bodyDocument = make_default_document(projectRoot, assetRoot, 32);
    bodyDocument.root[0]["components"].push_back(
        component(2, 4, Json{{"bodyType", 0}, {"gravity", true}, {"velocity", {{"x", 0.0}, {"y", 1.0}}}, {"force", {{"x", 0.0}, {"y", 0.0}}}, {"mass", 0.0}, {"angularVelocity", 0.0}, {"torque", 0.0}, {"friction", -1.0}, {"restitution", 1.5}}));
    ++bodyDocument.structureRevision;
    const auto bodyIssues = validate_document(bodyDocument);
    auto has_field = [](const ValidationResult &validation, std::string_view field)
    {
        return std::any_of(validation.issues.begin(), validation.issues.end(), [&](const ValidationIssue &issue)
                           { return issue.fieldPath == field && issue.blocksRun; });
    };

    auto optionalAssetDocument = make_default_document(projectRoot, assetRoot, 35);
    optionalAssetDocument.root[0]["components"].push_back(
        component(2, 0, Json{{"currentFrameIndex", 0}, {"playbackSpeed", 1.0}, {"playing", false}, {"animationClipFilePath", ""}}));
    optionalAssetDocument.root[0]["components"].push_back(
        component(3, 1, Json{{"filePath", ""}, {"gain", 1.0}, {"loopCount", 0}, {"autoPlay", false}}));
    optionalAssetDocument.root[0]["components"].push_back(
        component(4, 6, Json{{"textureFilePath", ""}, {"flip", 0}, {"width", 32.0}, {"height", 32.0}, {"sourceRectEnabled", false}}));
    ++optionalAssetDocument.structureRevision;
    const auto optionalAssets = validate_document(optionalAssetDocument);
    require(!optionalAssets.blocksRun && !has_field(optionalAssets, "animationClipFilePath") &&
                !has_field(optionalAssets, "filePath") && !has_field(optionalAssets, "textureFilePath"),
            "empty Sprite, Animator and Audio asset references are valid optional runtime values");

    optionalAssetDocument.root[0]["components"][1]["animationClipFilePath"] = "assets/missing.animset";
    optionalAssetDocument.root[0]["components"][2]["filePath"] = "assets/missing.wav";
    optionalAssetDocument.root[0]["components"][3]["textureFilePath"] = "assets/missing.png";
    const auto unresolvedAssets = validate_document(optionalAssetDocument);
    require(has_field(unresolvedAssets, "animationClipFilePath") && has_field(unresolvedAssets, "filePath") &&
                has_field(unresolvedAssets, "textureFilePath"),
            "non-empty unresolved asset references must continue to block Run");

    auto missingAssetFieldDocument = make_default_document(projectRoot, assetRoot, 36);
    missingAssetFieldDocument.root[0]["components"].push_back(
        component(2, 0, Json{{"currentFrameIndex", 0}, {"playbackSpeed", 1.0}, {"playing", false}, {"animationClipFilePath", ""}}));
    ++missingAssetFieldDocument.structureRevision;
    missingAssetFieldDocument.root[0]["components"][1].erase("animationClipFilePath");
    require(has_field(validate_document(missingAssetFieldDocument), "animationClipFilePath"),
            "omitting the serialized asset field must remain an error even when an empty reference is valid");

    const auto repositoryRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    const auto marioRoot = repositoryRoot / "examples" / "mario";
    const auto marioScenePath = marioRoot / "level1.scene";
    std::ifstream marioSceneStream(marioScenePath, std::ios::binary);
    require(static_cast<bool>(marioSceneStream), "the Mario validation regression fixture must be available");
    Json marioScene;
    marioSceneStream >> marioScene;
    auto marioDocument = parse_document(std::move(marioScene), 37, marioScenePath,
                                        marioRoot, marioRoot / "assets");
    require(static_cast<bool>(marioDocument), "the Mario validation regression scene must parse");
    require(!validate_document(*marioDocument.document).blocksRun,
            "the checked-in Mario example must validate with its intentionally empty optional audio/animation paths");

    require(bodyIssues.blocksRun && has_field(bodyIssues, "mass") && has_field(bodyIssues, "friction") &&
                has_field(bodyIssues, "restitution"),
            "rigid body validation must enforce dynamic mass, nonnegative friction and restitution bounds");

    auto colliderDocument = make_default_document(projectRoot, assetRoot, 33);
    colliderDocument.root[0]["components"].push_back(
        component(2, 3, Json{{"colliderType", 1}, {"width", 0.0}, {"height", 10.0}, {"offset", {{"x", 0.0}, {"y", std::numeric_limits<double>::infinity()}}}, {"trigger", false}, {"collisionGroup", -1}, {"collisionMask", 0}}));
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

void run_hierarchy_label_cache_cases()
{
    auto document = make_default_document();
    HierarchyPanel hierarchy;
    work::reset();
    const auto &initialRows = hierarchy.visible_rows(document);
    require(!initialRows.empty() && initialRows.front().label == "Camera (#0)",
            "hierarchy topology builds with the current object labels");

    EditCommandService commands;
    commands.subscribe([&](const DocumentChange &change, const std::vector<int> &)
                       { hierarchy.apply_change(document, change); });
    const auto address = address_for_document(document, 0, std::nullopt, std::nullopt,
                                              {std::string("name")});
    require(commands.set_property(document, address, "Renamed Camera").status == CommandStatus::Changed,
            "a committed label edit advances the document value revision");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    require(work::snapshot()[static_cast<std::size_t>(work::Counter::ChangedRecords)] == 1,
            "a real property transaction must increment the changed-record work counter");
#endif
    const auto &renamedRows = hierarchy.visible_rows(document);
    require(renamedRows.front().label == "Renamed Camera (#0)",
            "cached hierarchy labels refresh after a value revision without rebuilding topology: " +
                renamedRows.front().label);
    const auto afterRename = work::snapshot();
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    require(afterRename[static_cast<std::size_t>(work::Counter::HierarchyLabelRows)] == 1,
            "a committed rename must refresh only its affected hierarchy label");
#endif
    hierarchy.visible_rows(document);
    const auto afterIdleFrame = work::snapshot();
    require(afterIdleFrame[static_cast<std::size_t>(work::Counter::HierarchyLabelRows)] ==
                afterRename[static_cast<std::size_t>(work::Counter::HierarchyLabelRows)],
            "unchanged hierarchy frames must not revisit object and component labels");
    const auto positionAddress = address_for_document(document, 0, std::nullopt, std::nullopt,
                                                      {std::string("position"), std::string("x")});
    require(commands.set_property(document, positionAddress, 12.0).status == CommandStatus::Changed,
            "a pose edit must produce a typed non-label document delta");
    hierarchy.visible_rows(document);
    const auto afterPose = work::snapshot();
    require(afterPose[static_cast<std::size_t>(work::Counter::HierarchyLabelRows)] ==
                afterRename[static_cast<std::size_t>(work::Counter::HierarchyLabelRows)],
            "pose edits must advance the hierarchy value revision without refreshing labels");
    const auto beforeStructure = renamedRows.size();
    require(commands.create_object(document, 0, "Child").status == CommandStatus::Changed,
            "a structural hierarchy edit must commit through the command service");
    require(hierarchy.visible_rows(document).size() == beforeStructure + 1,
            "a structural revision must rebuild cached hierarchy rows and reveal the new child");
}

void run_large_hierarchy_cache_cases()
{
    DocumentSession flat;
    flat.documentGeneration = 801;
    flat.root = gui_fixtures::many_objects(10000);
    rebuild_document_indexes(flat);
    HierarchyPanel flatPanel;
    const auto &flatRows = flatPanel.visible_rows(flat);
    require(flatRows.size() == 10000,
            "large expanded hierarchies must preserve all object rows in authored order");
    const auto &cachedFlatRows = flatPanel.visible_rows(flat);
    require(cachedFlatRows.size() == 10000 && &cachedFlatRows == &flatRows,
            "unchanged large hierarchies must reuse their cached topology");

    work::reset();
    flat.root[5000]["name"] = "Renamed middle object";
    ++flat.valueRevision;
    const DocumentChange rename{flat.documentGeneration, flat.structureRevision, flat.valueRevision, false, {ProjectGeneration{flat.projectGeneration}, flat.tabId, DocumentGeneration{flat.documentGeneration}}, {5000}, {"name"}, {DocumentChangeCategory::Label}};
    flatPanel.apply_change(flat, rename);
    const auto &renamedFlatRows = flatPanel.visible_rows(flat);
    require(renamedFlatRows[5000].label == "Renamed middle object (#5000)",
            "an incremental label delta must update the correct object in a 10,000-row hierarchy");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    require(work::snapshot()[static_cast<std::size_t>(work::Counter::HierarchyLabelRows)] == 1,
            "a 10,000-row rename must visit one affected label row");
#endif

    DocumentSession deep;
    deep.documentGeneration = 802;
    deep.root = gui_fixtures::deep_sprite_chain(128);
    rebuild_document_indexes(deep);
    HierarchyPanel deepPanel;
    const auto &deepRows = deepPanel.visible_rows(deep);
    require(deepRows.size() == 128,
            "depth-128 sprite chains must retain each object in the hierarchy tree");
}

void run_hierarchy_visible_connector_work_cases()
{
    DocumentSession document;
    document.documentGeneration = 803;
    document.root = gui_fixtures::deep_sprite_chain(128);
    rebuild_document_indexes(document);

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({600.0f, 420.0f}, ImGuiCond_Always);
    ImGui::Begin("Hierarchy connectors", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings);
    HierarchyPanel panel;
    SelectionModel selection;
    EditCommandService commands;
    work::reset();
    panel.draw(document, selection, commands);
    ImGui::End();
    ImGui::Render();
    const auto counts = work::snapshot();
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    require(counts[static_cast<std::size_t>(work::Counter::HierarchyVisibleRows)] > 0 &&
                counts[static_cast<std::size_t>(work::Counter::HierarchyVisibleRows)] < 128,
            "hierarchy drawing must visit only the clipped depth-128 row range");
    require(counts[static_cast<std::size_t>(work::Counter::HierarchyConnectorCandidates)] < 128,
            "a clipped depth-128 hierarchy must draw connectors only for visible rows and their ancestors");
    require(counts[static_cast<std::size_t>(work::Counter::HierarchyConnectorParentVisits)] < 128 * 128,
            "connector ancestry checks must remain bounded by visible rows times hierarchy depth");
    require(counts[static_cast<std::size_t>(work::Counter::HierarchyScratchCapacityGrowths)] == 0,
            "cached hierarchy drawing must not grow its connector scratch buffer per frame");
#endif

    DocumentSession flat;
    flat.documentGeneration = 804;
    flat.root = gui_fixtures::many_objects(10'000);
    rebuild_document_indexes(flat);
    HierarchyPanel flatPanel;
    SelectionModel flatSelection;
    EditCommandService flatCommands;
    const auto drawFlat = [&](bool scrollToMiddle)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({600.0f, 420.0f}, ImGuiCond_Always);
        ImGui::Begin("Hierarchy 10,000 row scroll", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings);
        if (scrollToMiddle)
            ImGui::SetScrollY(ImGui::GetScrollMaxY() * 0.5f);
        flatPanel.draw(flat, flatSelection, flatCommands);
        ImGui::End();
        ImGui::Render();
    };
    drawFlat(false);
    work::reset();
    drawFlat(true);
    const auto scrolledCounts = work::snapshot();
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    require(scrolledCounts[static_cast<std::size_t>(work::Counter::HierarchyVisibleRows)] > 0 &&
                scrolledCounts[static_cast<std::size_t>(work::Counter::HierarchyVisibleRows)] < 100,
            "scrolling a 10,000-row hierarchy must visit a bounded visible row range");
    require(scrolledCounts[static_cast<std::size_t>(work::Counter::HierarchyScratchCapacityGrowths)] == 0,
            "scrolling a cached 10,000-row hierarchy must reuse panel scratch storage");
#endif
    ImGui::DestroyContext();
}
