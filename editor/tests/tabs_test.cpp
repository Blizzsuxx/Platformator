#include "documentmanager.h"
#include "async_requests.h"
#include "editcommands.h"
#include "editorapp.h"
#include "preview.h"
#include "resourceindex.h"
#include "selection.h"
#include "test_support.h"
#include <platformator/runtime.h>
#include "platform/nativefile.h"
#include "platform/nativefile_testhooks.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace
{
void test_external_scene_changes_are_tab_scoped_and_recoverable()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    const auto outside = temporary.path() / "external scenes";
    std::filesystem::create_directories(assets);
    std::filesystem::create_directories(outside);
    std::ofstream(project / "CMakeLists.txt") << "project(tabs-test)\n";
    const auto firstPath = outside / "first scene.scene";
    const auto secondPath = outside / "second scene.scene";
    write_json(firstPath, Json::array({object(10, "First disk copy")}));
    write_json(secondPath, Json::array({object(20, "Second disk copy")}));

    DocumentManager documents(project, assets);
    const auto firstTabId = documents.open_scene(firstPath);
    const auto secondTabId = documents.open_scene(secondPath);
    require(firstTabId && secondTabId && *firstTabId != *secondTabId,
            "two source scenes outside the asset root must open in independent tabs");
    auto *first = documents.find(*firstTabId);
    auto *second = documents.find(*secondTabId);
    require(first != nullptr && second != nullptr,
            "both scene tabs must remain addressable by stable identity");
    require(documents.activate_tab(*firstTabId), "the first scene tab must activate by stable TabId");
    const auto launchSnapshot = documents.capture_active_scene_launch_snapshot();
    require(launchSnapshot && launchSnapshot->identity == first->identity() &&
                launchSnapshot->sourcePath == std::filesystem::canonical(firstPath) &&
                launchSnapshot->scene[0]["name"] == "First disk copy",
            "Run must capture the active tab's immutable identity, canonical path, and scene payload");
    require(documents.activate_tab(*secondTabId) &&
                launchSnapshot->identity.tabId == *firstTabId &&
                launchSnapshot->scene[0]["name"] == "First disk copy",
            "switching tabs must not retarget a captured Run scene snapshot");
    first->view_state() = SceneViewState{12.0f, 24.0f, 1.5f};
    second->view_state() = SceneViewState{-8.0f, 3.0f, 0.75f};
    first->set_expanded_hierarchy_ids({10});
    second->set_expanded_hierarchy_ids({20});
    first->selection().set_objects(first->workspace().document(), {10}, 10, 10);
    second->selection().set_objects(second->workspace().document(), {20}, 20, 20);
    const auto firstInitialIdentity = first->identity();
    const auto secondInitialIdentity = second->identity();
    require(firstInitialIdentity.tabId == *firstTabId && secondInitialIdentity.tabId == *secondTabId &&
                firstInitialIdentity.documentGeneration == secondInitialIdentity.documentGeneration,
            "document generation collisions across tabs must remain disambiguated by immutable TabId");
    {
        AsyncRequestQueue requests(77);
        auto pending = requests.begin(AsyncRequestKind::OpenScene, firstInitialIdentity);
        pending.complete_selected(firstPath.string());
        require(requests.take_ready(secondInitialIdentity).empty(),
                "switching tabs must not route a pending dialog result to the newly active document");
        const auto result = requests.take_ready(firstInitialIdentity);
        require(result.size() == 1 && result.front().selectedPath == firstPath,
                "the original still-open tab can consume its own pending dialog result");
    }
    const auto currentStatuses = documents.source_file_statuses();
    require(currentStatuses.size() == documents.tab_count() &&
                std::any_of(currentStatuses.begin(), currentStatuses.end(), [&](const auto &status) {
                    return status.tabId == *firstTabId && status.file.state == SourceFileState::Current;
                }) &&
                std::any_of(currentStatuses.begin(), currentStatuses.end(), [&](const auto &status) {
                    return status.tabId == *secondTabId && status.file.state == SourceFileState::Current;
                }),
            "the manager must poll every saved document independently of asset-root notifications");

    const auto firstAddress = address_for_document(first->workspace().document(), 10, std::nullopt,
                                                    std::nullopt, {std::string("name")});
    const auto edit = first->history().set_property(first->workspace().document_for_edit(),
                                                     firstAddress, "First local draft");
    require(edit && first->workspace().dirty() && !second->workspace().dirty(),
            "external monitoring must preserve independent dirty state between tabs");
    require(launchSnapshot->scene[0]["name"] == "First disk copy" &&
                launchSnapshot->identity == firstInitialIdentity,
            "later edits to the authoring tab cannot change an already captured launch target");
    write_json(firstPath, Json::array({object(10, "External first version with new size")}));
    const auto changedStatuses = documents.source_file_statuses();
    const auto firstChangedStatus = std::find_if(changedStatuses.begin(), changedStatuses.end(),
        [&](const auto &status) { return status.tabId == *firstTabId; });
    require(std::any_of(changedStatuses.begin(), changedStatuses.end(), [&](const auto &status) {
                return status.tabId == *firstTabId && status.file.state == SourceFileState::Changed;
            }) &&
                std::any_of(changedStatuses.begin(), changedStatuses.end(), [&](const auto &status) {
                    return status.tabId == *secondTabId && status.file.state == SourceFileState::Current;
                }),
            "a source change must be reported only for its owning tab, including outside-root scenes");
    require(firstChangedStatus != changedStatuses.end() && firstChangedStatus->file.diskIdentity &&
                !source_file_allows_run(firstChangedStatus->file) &&
                source_file_allows_run(firstChangedStatus->file, true,
                                       firstChangedStatus->file.diskIdentity),
            "Run must wait for an external decision, then accept the exact acknowledged file version");
    write_json(secondPath, Json::array({object(20, "External second version")}));
    const auto secondChangedStatus = second->workspace().source_file_status();
    first->acknowledge_external_change();
    require(first->source_file_allows_run(firstChangedStatus->file) &&
                !second->source_file_allows_run(secondChangedStatus),
            "keeping work acknowledges only the selected tab's exact external file version");
    auto laterExternalIdentity = firstChangedStatus->file;
    ++laterExternalIdentity.diskIdentity->size;
    require(!source_file_allows_run(firstChangedStatus->file, true,
                                    laterExternalIdentity.diskIdentity),
            "a new external version after Keep Work must block Run until it is reviewed");
    const SourceFileStatus missingSource{SourceFileState::Missing, std::nullopt,
                                         "the saved scene file is missing"};
    require(!source_file_allows_run(missingSource) &&
                source_file_allows_run(missingSource, true, std::nullopt),
            "a missing source must block Run until the user explicitly keeps the in-memory scene");

    const auto firstRoot = first->workspace().document().root;
    const auto firstHistory = first->history().history_status();
    const auto conflictedSave = first->workspace().save();
    require(conflictedSave.status == WorkspaceActionStatus::Failed &&
                first->workspace().document().root == firstRoot && first->workspace().dirty() &&
                first->history().history_status().currentToken == firstHistory.currentToken,
            "save conflicts must preserve the affected tab's draft and undo history");
    const auto overwritten = first->workspace().save(true);
    require(overwritten && !first->workspace().dirty() &&
                first->workspace().source_file_status().state == SourceFileState::Current &&
                read_json(firstPath)[0]["name"] == "First local draft",
            "explicit overwrite must update the selected tab's saved identity and retain its local work");
    write_json(firstPath, Json::array({object(10, "External first version with new size again")}));
    require(!first->source_file_allows_run(first->workspace().source_file_status()),
            "a later disk version invalidates the prior tab-scoped acknowledgement");
    const auto reloaded = first->workspace().reload_source_file();
    require(reloaded && !first->workspace().dirty() &&
                first->workspace().document().root[0]["name"] ==
                    "External first version with new size again" &&
                first->workspace().source_file_status().state == SourceFileState::Current &&
                second->workspace().document().root[0]["name"] == "Second disk copy",
            "explicit reload must adopt only the selected tab and refresh its file identity");
    const auto firstReloadedIdentity = first->identity();
    require(documents.open_scene(firstPath) == firstTabId && documents.active_tab_id() == *firstTabId &&
                first->identity() == firstReloadedIdentity &&
                first->view_state().centerX == 12.0f && first->view_state().centerY == 24.0f &&
                first->view_state().zoom == 1.5f &&
                first->expanded_hierarchy_ids() == std::vector<int>{10} &&
                first->selection().current().selectedObjectIds == std::vector<int>{10},
            "reopening an already-open canonical path activates its tab without replacing its identity or view state");
    require(second->identity() == secondInitialIdentity &&
                second->view_state().centerX == -8.0f && second->view_state().centerY == 3.0f &&
                second->view_state().zoom == 0.75f &&
                second->expanded_hierarchy_ids() == std::vector<int>{20} &&
                second->selection().current().selectedObjectIds == std::vector<int>{20},
            "inactive tabs retain independent view, expansion and selection state");

    second->history().create_object(second->workspace().document_for_edit(), std::nullopt,
                                     "Local second draft");
    const auto secondRoot = second->workspace().document().root;
    const auto secondHistory = second->history().history_status();
    {
        std::ofstream malformed(secondPath, std::ios::binary | std::ios::trunc);
        malformed << "{";
    }
    require(second->workspace().source_file_status().state == SourceFileState::Changed &&
                !second->workspace().reload_source_file() && second->workspace().dirty() &&
                second->workspace().document().root == secondRoot &&
                second->history().history_status().currentToken == secondHistory.currentToken,
            "a failed reload must not destroy the selected tab's draft or history");
    const auto saveAsPath = outside / "second saved elsewhere.scene";
    const auto savedElsewhere = second->workspace().save_as(saveAsPath);
    require(savedElsewhere && !second->workspace().dirty() &&
                second->workspace().document().sourcePath == std::filesystem::canonical(saveAsPath) &&
                second->workspace().document().root == secondRoot,
            "Save As must preserve the tab's local content and move its tracked source identity");
    second->history().create_object(second->workspace().document_for_edit(), std::nullopt,
                                     "Draft before close");
    const auto closeCancelled = documents.close_tab(*secondTabId, UnsavedDecision::Cancel);
    require(closeCancelled.status == WorkspaceActionStatus::Cancelled &&
                documents.find(*secondTabId) == second && documents.tab_count() == 3,
            "closing a dirty tab must remain cancellable while another tab stays usable");

    const auto oldLaunchIdentity = first->identity();
    require(static_cast<bool>(documents.close_tab(*firstTabId, UnsavedDecision::Discard)) &&
                documents.open_scene(firstPath).has_value(),
            "a clean scene tab can close and reopen from its canonical source path");
    const auto reopenedId = documents.active_tab_id();
    const auto *reopened = documents.find(reopenedId);
    require(reopened != nullptr && reopenedId != *firstTabId &&
                reopened->identity() != oldLaunchIdentity,
            "reopening the same path cannot reuse the immutable launch TabId/document identity");
    {
        AsyncRequestQueue requests(78);
        auto pending = requests.begin(AsyncRequestKind::SaveScene, oldLaunchIdentity);
        pending.complete_selected(saveAsPath.string());
        require(requests.take_ready(reopened->identity()).empty(),
                "a late dialog result for a closed tab cannot target a newly opened tab at the same path");
    }
}

void test_save_all_preserves_each_tab_on_partial_failure()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    const auto scenes = temporary.path() / "scenes";
    std::filesystem::create_directories(assets);
    std::filesystem::create_directories(scenes);
    std::ofstream(project / "CMakeLists.txt") << "project(save-all-test)\n";
    const auto firstPath = scenes / "first.scene";
    const auto secondPath = scenes / "second.scene";
    write_json(firstPath, Json::array({object(1, "First original")}));
    write_json(secondPath, Json::array({object(2, "Second original")}));

    DocumentManager documents(project, assets);
    const auto firstId = documents.open_scene(firstPath);
    const auto secondId = documents.open_scene(secondPath);
    require(firstId && secondId, "Save All fixtures must open as independent tabs");
    auto *first = documents.find(*firstId);
    auto *second = documents.find(*secondId);
    const auto firstAddress = address_for_document(first->workspace().document(), 1, std::nullopt,
                                                    std::nullopt, {std::string("name")});
    const auto secondAddress = address_for_document(second->workspace().document(), 2, std::nullopt,
                                                     std::nullopt, {std::string("name")});
    require(first->history().set_property(first->workspace().document_for_edit(), firstAddress,
                                          "First edited") &&
                second->history().set_property(second->workspace().document_for_edit(), secondAddress,
                                               "Second edited"),
            "both tabs must retain independent drafts before Save All");
    write_json(secondPath, Json::array({object(2, "Second external version")}));

    const auto partial = documents.save_all();
    require(partial.action.status == WorkspaceActionStatus::Failed &&
                partial.failedTab == secondId && partial.savedTabs == std::vector<TabId>{*firstId} &&
                !first->workspace().dirty() && second->workspace().dirty() &&
                read_json(firstPath)[0]["name"] == "First edited" &&
                second->workspace().document().root[0]["name"] == "Second edited",
            "Save All reports a partial conflict without losing the unsaved tab's work or history");

    require(static_cast<bool>(second->workspace().reload_source_file()),
            "the conflicted tab can explicitly reload before testing an unsaved scene");
    const auto unsavedId = documents.create_scene_tab();
    auto *unsaved = documents.find(unsavedId);
    require(!documents.capture_active_scene_launch_snapshot(),
            "an untitled scene tab cannot be captured as a runnable saved-scene target");
    require(static_cast<bool>(unsaved->history().create_object(
                unsaved->workspace().document_for_edit(), std::nullopt, "Untitled draft")),
            "the new tab must have a retained dirty draft");
    const auto unsavedResult = documents.save_all();
    require(unsavedResult.action.status == WorkspaceActionStatus::Failed &&
                unsavedResult.failedTab == unsavedId && unsaved->workspace().dirty(),
            "Save All leaves an untitled draft intact and identifies the tab that needs Save As");

    const auto closeSavePath = scenes / "close-save.scene";
    const auto closeDiscardPath = scenes / "close-discard.scene";
    write_json(closeSavePath, Json::array({object(30, "Close Save Original")}));
    write_json(closeDiscardPath, Json::array({object(40, "Close Discard Original")}));
    const auto closeSaveId = documents.open_scene(closeSavePath);
    const auto closeDiscardId = documents.open_scene(closeDiscardPath);
    require(closeSaveId && closeDiscardId,
            "dirty close Save/Discard fixtures must open in independent scene tabs");
    auto *closeSave = documents.find(*closeSaveId);
    auto *closeDiscard = documents.find(*closeDiscardId);
    const auto saveAddress = address_for_document(closeSave->workspace().document(), 30,
        std::nullopt, std::nullopt, {std::string("name")});
    const auto discardAddress = address_for_document(closeDiscard->workspace().document(), 40,
        std::nullopt, std::nullopt, {std::string("name")});
    require(closeSave->history().set_property(closeSave->workspace().document_for_edit(),
                    saveAddress, "Close Save Draft") &&
                closeDiscard->history().set_property(closeDiscard->workspace().document_for_edit(),
                    discardAddress, "Close Discard Draft"),
            "both close-choice tabs must have retained dirty drafts");
    require(static_cast<bool>(documents.close_tab(*closeSaveId, UnsavedDecision::Save)) &&
                documents.find(*closeSaveId) == nullptr &&
                read_json(closeSavePath)[0]["name"] == "Close Save Draft",
            "closing with Save must durably write the active tab draft before removing it");
    require(static_cast<bool>(documents.close_tab(*closeDiscardId, UnsavedDecision::Discard)) &&
                documents.find(*closeDiscardId) == nullptr &&
                read_json(closeDiscardPath)[0]["name"] == "Close Discard Original",
            "closing with Discard must remove only that tab's draft without writing it");
}

void test_animation_tabs_are_protected_and_conflict_safe()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    std::ofstream(project / "CMakeLists.txt") << "project(animation-tabs-test)\n";
    const auto firstPath = assets / "walk.animset";
    const auto secondPath = assets / "run.animset";
    const auto uppercasePath = assets / "UPPER.ANIMSET";
    write_json(firstPath, Json{{"frames", Json::array({{{"duration", 0.2},
                 {"textureWrapperFilePath", "assets/walk.png"}, {"hasSourceRect", false},
                 {"sourceRect", {{"x", 0.0}, {"y", 0.0}, {"w", 0.0}, {"h", 0.0}}}}})},
                 {"framesPerSecond", 5.0}, {"loop", true}, {"width", 16.0}, {"height", 16.0},
                 {"name", "walk"}, {"filePath", firstPath.string()}, {"vendor", 11}});
    write_json(secondPath, Json{{"frames", Json::array({{{"duration", 0.1},
                 {"textureWrapperFilePath", "assets/run.png"}, {"hasSourceRect", false},
                 {"sourceRect", {{"x", 0.0}, {"y", 0.0}, {"w", 0.0}, {"h", 0.0}}}}})},
                 {"framesPerSecond", 10.0}, {"loop", true}, {"width", 16.0}, {"height", 16.0},
                 {"name", "run"}, {"filePath", secondPath.string()}});
    write_json(uppercasePath, Json{{"frames", Json::array()},
                                   {"framesPerSecond", 1.0}, {"loop", true},
                                   {"width", 16.0}, {"height", 16.0},
                                   {"name", "uppercase"}, {"filePath", uppercasePath.string()}});

    DocumentManager documents(project, assets);
    const auto uppercaseId = documents.open_animation(uppercasePath);
    require(uppercaseId && documents.animation_tab_count() == 1 &&
                static_cast<bool>(documents.close_animation_tab(*uppercaseId, UnsavedDecision::Discard)),
            "shared animation format classification must preserve case-insensitive tab opening");
    const auto firstId = documents.open_animation(firstPath);
    require(firstId && documents.active_animation_tab_id() == *firstId &&
                documents.find_animation_tab(*firstId) != nullptr,
            "opening an animation asset must create an independent addressable tab");
    const auto duplicateId = documents.open_animation(assets / "." / "walk.animset");
    require(duplicateId == firstId && documents.animation_tab_count() == 1,
            "opening the same canonical animation path must activate its existing tab");
    auto *first = documents.find_animation_tab(*firstId);
    const Json original = first->document().root();
    require(first->document().set("/loop", false).changed && first->document().dirty(),
            "animation edits must remain in a tab-local dirty draft");
    require(documents.close_animation_tab(*firstId, UnsavedDecision::Cancel).status ==
                WorkspaceActionStatus::Cancelled && documents.find_animation_tab(*firstId) == first,
            "Cancel must preserve a dirty animation tab");

    write_json(firstPath, Json{{"frames", Json::array()}, {"framesPerSecond", 1.0},
                               {"loop", true}, {"width", 16.0}, {"height", 16.0},
                               {"name", "walk"}, {"filePath", firstPath.string()},
                               {"external", "newer disk version"}});
    const auto conflictedSave = documents.save_animation_tab(*firstId);
    require(conflictedSave.status == WorkspaceActionStatus::Failed &&
                first->document().root() != original && first->document().dirty() &&
                first->document().root()["loop"] == false,
            "a stale asset identity must block save without replacing the external file or draft");
    require(documents.close_animation_tab(*firstId, UnsavedDecision::Discard) &&
                documents.find_animation_tab(*firstId) == nullptr,
            "Discard must close only the selected animation draft");

    const auto reopenedId = documents.open_animation(firstPath);
    require(reopenedId && *reopenedId != *firstId,
            "reopening an asset must use a fresh stable tab identity");
    auto *reopened = documents.find_animation_tab(*reopenedId);
    require(reopened->document().set("/loop", false).changed,
            "the reopened asset can be edited independently");
    const auto saveAll = documents.save_all();
    require(saveAll.action.status == WorkspaceActionStatus::Completed &&
                saveAll.savedTabs == std::vector<TabId>{*reopenedId} &&
                !reopened->document().dirty() && read_json(firstPath)["loop"] == false,
            "Save All must save dirty animation tabs and report their stable identities");

    const auto secondId = documents.open_animation(secondPath);
    require(secondId && documents.activate_animation_tab(*reopenedId) &&
                documents.active_animation_tab_id() == *reopenedId,
            "animation tabs must activate independently from scene tabs");
    auto *second = documents.find_animation_tab(*secondId);
    require(second->document().set("/loop", false).changed &&
                documents.close_animation_tab(*secondId, UnsavedDecision::Save) &&
                documents.find_animation_tab(*secondId) == nullptr &&
                read_json(secondPath)["loop"] == false,
            "closing with Save must persist the asset draft before removing its tab");

    std::string error;
    const auto outside = temporary.path() / "outside.animset";
    write_json(outside, Json{{"frames", Json::array()}, {"framesPerSecond", 1.0},
                             {"loop", true}, {"width", 16.0}, {"height", 16.0},
                             {"name", "outside"}, {"filePath", outside.string()}});
    require(!documents.open_animation(outside, &error) && !error.empty(),
            "animation tabs must not adopt references outside the current asset root");
}

void test_animation_external_changes_keep_observed_versions_and_identities()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    std::ofstream(project / "CMakeLists.txt") << "project(animation-conflict-test)\n";
    const auto firstPath = assets / "walk.animset";
    const auto secondPath = assets / "run.animset";
    const auto clip = [](std::string owner, bool loop) {
        return Json{{"frames", Json::array({{{"duration", 0.2},
                    {"textureWrapperFilePath", ""}, {"hasSourceRect", false},
                    {"sourceRect", {{"x", 0.0}, {"y", 0.0}, {"w", 0.0}, {"h", 0.0}}}}})},
                    {"framesPerSecond", 5.0}, {"loop", loop}, {"width", 16.0},
                    {"height", 16.0}, {"name", owner}, {"filePath", ""},
                    {"owner", std::move(owner)}};
    };
    write_json(firstPath, clip("first-original", true));
    write_json(secondPath, clip("second-original", true));

    DocumentManager documents(project, assets, ProjectGeneration{12});
    const auto firstId = documents.open_animation(firstPath);
    const auto secondId = documents.open_animation(secondPath);
    require(firstId && secondId, "both animation conflict fixtures must open");
    auto *first = documents.find_animation_tab(*firstId);
    auto *second = documents.find_animation_tab(*secondId);
    require(first != nullptr && second != nullptr &&
                first->document().set("/loop", false).changed,
            "the first asset must retain a dirty local draft independently from the clean second tab");
    const auto firstIdentity = first->identity();
    const auto secondIdentity = second->identity();

    write_json(firstPath, clip("external-first-version-with-a-different-size", true));
    write_json(secondPath, clip("external-second-version-with-a-different-size", false));
    const auto statuses = documents.source_file_statuses();
    const auto firstStatus = std::find_if(statuses.begin(), statuses.end(), [&](const auto &status) {
        return status.kind == DocumentTabKind::Animation && status.tabId == *firstId;
    });
    const auto secondStatus = std::find_if(statuses.begin(), statuses.end(), [&](const auto &status) {
        return status.kind == DocumentTabKind::Animation && status.tabId == *secondId;
    });
    require(statuses.size() == documents.tab_count() + documents.animation_tab_count() &&
                firstStatus != statuses.end() && firstStatus->identity == firstIdentity &&
                firstStatus->file.state == SourceFileState::Changed &&
                secondStatus != statuses.end() && secondStatus->identity == secondIdentity &&
                secondStatus->file.state == SourceFileState::Changed,
            "source polling must report clean and dirty animation tabs, including inactive assets, with tab and document identity");
    const Json firstDraft = first->document().root();
    const auto firstHistory = first->document().history_token();
    const auto firstSavedHistory = first->document().saved_history_token();
    const auto closeSaveConflict = documents.close_animation_tab(*firstId, UnsavedDecision::Save);
    require(closeSaveConflict.status == WorkspaceActionStatus::Failed &&
                closeSaveConflict.conflict && documents.find_animation_tab(*firstId) == first &&
                first->document().dirty(),
            "a conflicted Close-Save decision must keep the animation tab open with its draft");
    const auto conflicted = documents.save_animation_tab(firstIdentity);
    require(conflicted.status == WorkspaceActionStatus::Failed && conflicted.conflict &&
                first->document().root() == firstDraft && first->document().dirty() &&
                first->document().history_token() == firstHistory &&
                first->document().saved_history_token() == firstSavedHistory &&
                read_json(firstPath)["owner"] == "external-first-version-with-a-different-size",
            "a normal animation save conflict must preserve the draft, independent history and recoverable disk version");

    const auto observedFirst = first->document().source_file_status();
    write_json(firstPath, clip("external-version-after-the-overwrite-prompt-was-opened", true));
    const auto staleOverwrite = documents.save_animation_tab(
        firstIdentity, AssetOverwriteObservation{observedFirst.diskIdentity});
    require(staleOverwrite.conflict && first->document().dirty() &&
                read_json(firstPath)["owner"] == "external-version-after-the-overwrite-prompt-was-opened",
            "overwrite must compare against the exact observed version instead of rereading and silently rebasing it");

    const auto latestFirst = first->document().source_file_status();
    first->acknowledge_external_change(latestFirst);
    require(first->external_change_acknowledged(latestFirst),
            "Keep Work must acknowledge only the currently observed animation file identity");
    write_json(firstPath, clip("third-external-version-invalidates-keep-work", true));
    const auto laterFirst = first->document().source_file_status();
    require(laterFirst.state == SourceFileState::Changed &&
                !first->external_change_acknowledged(laterFirst),
            "a later external edit must invalidate the previous Keep Work acknowledgement");
    const auto overwritten = documents.save_animation_tab(
        firstIdentity, AssetOverwriteObservation{laterFirst.diskIdentity});
    require(overwritten && !first->document().dirty() &&
                first->document().history_token() == firstHistory &&
                first->document().saved_history_token() == firstHistory &&
                read_json(firstPath)["loop"] == false &&
                first->document().source_file_status().state == SourceFileState::Current,
            "an explicit observed-version overwrite must save local content, update only saved state, and clear stale status");

    const auto cleanSecondRoot = second->document().root();
    const auto cleanSecondHistory = second->document().history_token();
    const auto latestSecond = second->document().source_file_status();
    const auto cleanOverwrite = documents.save_animation_tab(
        secondIdentity, AssetOverwriteObservation{latestSecond.diskIdentity});
    require(cleanOverwrite && !second->document().dirty() &&
                second->document().history_token() == cleanSecondHistory &&
                second->document().root() == cleanSecondRoot &&
                read_json(secondPath) == cleanSecondRoot &&
                second->document().source_file_status().state == SourceFileState::Current,
            "a clean inactive asset can explicitly resolve its external version without changing its independent history");

    write_json(firstPath, clip("external-version-for-reload-with-a-different-size", true));
    const auto beforeReloadIdentity = first->identity();
    const auto beforeReloadStatus = first->document().source_file_status();
    require(beforeReloadStatus.state == SourceFileState::Changed,
            "a self-write must not suppress a subsequent external animation change");
    AsyncRequestQueue requests(83);
    auto pendingSaveAs = requests.begin(AsyncRequestKind::SaveAnimation, beforeReloadIdentity);
    pendingSaveAs.complete_selected((assets / "late.animset").string());
    const auto reloaded = documents.reload_animation_tab(beforeReloadIdentity);
    require(reloaded && first->identity() != beforeReloadIdentity &&
                first->document().root()["owner"] == "external-version-for-reload-with-a-different-size" &&
                !first->document().dirty() && !first->document().can_undo() &&
                first->document().source_file_status().state == SourceFileState::Current,
            "Reload must adopt only the selected tab, refresh its source identity and reset its history generation");
    const auto staleResults = requests.take_ready([&](const DocumentIdentity &identity) {
        const auto *tab = documents.find_animation_tab(identity.tabId);
        return tab != nullptr && tab->identity() == identity;
    });
    require(staleResults.empty(),
            "a Save As completion from before Reload must not continue against the new document generation");

    const auto currentIdentity = first->identity();
    const Json afterReload = first->document().root();
    const auto afterReloadHistory = first->document().history_token();
    require(first->document().set("/loop", false).changed,
            "the reloaded animation must accept new local edits");
    const Json dirtyAfterReload = first->document().root();
    const auto dirtyHistory = first->document().history_token();
    {
        std::ofstream malformed(firstPath, std::ios::binary | std::ios::trunc);
        malformed << "{";
    }
    const auto failedReload = documents.reload_animation_tab(currentIdentity);
    require(!failedReload,
            "a malformed external animation must fail explicit Reload");
    require(first->identity() == currentIdentity,
            "a failed external reload must not advance the document generation");
    require(first->document().dirty() && first->document().root() == dirtyAfterReload &&
                first->document().history_token() == dirtyHistory,
            "a failed external reload must preserve dirty content and undo history");
    require(afterReload["loop"] != dirtyAfterReload["loop"] &&
                afterReloadHistory != dirtyHistory,
            "the failed reload fixture must contain distinct saved and edited history states");

    write_json(firstPath, clip("recoverable-before-injected-write-failure", true));
    require(static_cast<bool>(documents.reload_animation_tab(currentIdentity)),
            "the animation can recover after a malformed external version is corrected");
    const auto writableIdentity = first->identity();
    require(first->document().set("/loop", false).changed,
            "the recovered animation must keep accepting a new draft");
    const Json beforeFailedWrite = read_json(firstPath);
    const Json failedWriteDraft = first->document().root();
    const auto failedWriteHistory = first->document().history_token();
    set_native_file_test_hooks(NativeFileTestHooks{.failTemporaryCreate = true});
    const auto failedWrite = documents.save_animation_tab(writableIdentity);
    clear_native_file_test_hooks();
    require(failedWrite.status == WorkspaceActionStatus::Failed && !failedWrite.conflict &&
                first->document().dirty() && first->document().root() == failedWriteDraft &&
                first->document().history_token() == failedWriteHistory &&
                read_json(firstPath) == beforeFailedWrite,
            "a failed animation write must retain the dirty draft, history and recoverable disk content");

    const auto destination = assets / "chosen.animset";
    write_json(destination, clip("destination-original-version", true));
    const auto createOnly = documents.save_animation_tab_as(writableIdentity, destination);
    require(createOnly.conflict && first->document().dirty() &&
                first->document().source_path() == firstPath,
            "Save As must never silently replace an existing destination");
    std::string destinationIdentityError;
    const auto observedDestination = native_file_identity(destination, destinationIdentityError);
    require(observedDestination && destinationIdentityError.empty(),
            "the existing Save As destination must have an observable identity");
    write_json(destination, clip("destination-changed-after-observation", true));
    const auto staleDestinationOverwrite = documents.save_animation_tab_as(
        writableIdentity, destination, AssetOverwriteObservation{observedDestination});
    require(staleDestinationOverwrite.conflict && first->document().dirty() &&
                read_json(destination)["owner"] == "destination-changed-after-observation" &&
                first->document().source_path() == firstPath,
            "Save As overwrite must reject a destination changed after the user observed it");
    const auto currentDestination = native_file_identity(destination, destinationIdentityError);
    const auto saveAs = documents.save_animation_tab_as(
        writableIdentity, destination, AssetOverwriteObservation{currentDestination});
    require(saveAs && !first->document().dirty() &&
                first->document().source_path() == destination &&
                read_json(destination) == failedWriteDraft &&
                first->document().source_file_status().state == SourceFileState::Current,
            "an explicit Save As overwrite must adopt the new source only after the observed write succeeds");

    require(first->document().set("/loop", true).changed,
            "the Save As result must leave the new source editable");
    const auto cancelledIdentity = first->identity();
    const auto cancelledDraft = first->document().root();
    const auto cancelledHistory = first->document().history_token();
    AsyncRequestQueue cancelledRequests(84);
    auto cancelled = cancelledRequests.begin(AsyncRequestKind::SaveAnimation, cancelledIdentity);
    cancelled.complete_cancelled();
    const auto cancelledResult = cancelledRequests.take_ready(cancelledIdentity);
    require(cancelledResult.size() == 1 &&
                cancelledResult.front().outcome == AsyncRequestOutcome::Cancelled &&
                first->document().dirty() && first->document().root() == cancelledDraft &&
                first->document().history_token() == cancelledHistory &&
                read_json(destination)["loop"] == false,
            "cancelling animation Save As must leave the tab, history and prior disk version recoverable");
}

void test_animation_tabs_migrate_roots_without_adopting_another_project()
{
    TemporaryDirectory temporary;
    const auto firstProject = temporary.path() / "first-project";
    const auto firstAssets = temporary.path() / "first-content";
    const auto secondProject = temporary.path() / "second-project";
    const auto secondAssets = temporary.path() / "second-content";
    std::filesystem::create_directories(firstProject);
    std::filesystem::create_directories(firstAssets);
    std::filesystem::create_directories(secondProject);
    std::filesystem::create_directories(secondAssets);
    std::ofstream(firstProject / "CMakeLists.txt") << "project(first-animation-project)\n";
    std::ofstream(secondProject / "CMakeLists.txt") << "project(second-animation-project)\n";
    std::ofstream(firstAssets / "sprite.png", std::ios::binary) << "first texture";
    std::ofstream(secondAssets / "sprite.png", std::ios::binary) << "second texture";
    const auto firstAnimation = firstAssets / "walk.animset";
    write_json(firstAnimation, Json{{"frames", Json::array({{{"duration", 0.25},
        {"textureWrapperFilePath", "assets/sprite.png"}, {"hasSourceRect", false},
        {"sourceRect", Json{{"x", 0.0}, {"y", 0.0}, {"w", 0.0}, {"h", 0.0}}}}})},
        {"framesPerSecond", 4.0}, {"loop", true}, {"width", 16.0}, {"height", 16.0},
        {"filePath", firstAnimation.string()}, {"owner", "first project"}});
    write_json(secondAssets / "walk.animset", Json{{"frames", Json::array()},
        {"framesPerSecond", 12.0}, {"loop", false}, {"width", 16.0}, {"height", 16.0},
        {"filePath", (secondAssets / "walk.animset").string()}, {"owner", "second project"}});

    DocumentManager documents(firstProject, firstAssets, ProjectGeneration{5});
    const auto animationId = documents.open_animation(firstAnimation);
    require(animationId.has_value(), "the first project's animation must open");
    auto *tab = documents.find_animation_tab(*animationId);
    require(tab != nullptr && tab->asset_root() == firstAssets &&
                tab->document().set("/loop", false).changed,
            "an open animation must retain its configured disjoint asset root and dirty draft");
    const auto savedSourcePath = tab->document().source_path();
    const auto savedSourceIdentity = tab->document().source_identity();
    const auto editedHistoryToken = tab->document().history_token();
    const Json firstDraft = tab->document().root();

    const auto refused = documents.set_project_roots(
        secondProject, secondAssets, ProjectGeneration{6});
    require(refused.status == WorkspaceActionStatus::Failed &&
                documents.find_animation_tab(*animationId) == tab && tab->project_root() == firstProject &&
                tab->asset_root() == firstAssets && tab->document().root() == firstDraft &&
                tab->document().dirty() && tab->document().history_token() == editedHistoryToken &&
                tab->document().source_path() == savedSourcePath &&
                tab->document().source_identity() == savedSourceIdentity &&
                tab->document().root().value("owner", std::string{}) == "first project",
            "project replacement must refuse a dirty animation without changing its source or history");

    const auto saved = documents.save_animation_tab(*animationId);
    require(static_cast<bool>(saved) && !tab->document().dirty() &&
                read_json(firstAnimation).value("loop", true) == false,
            "explicitly saving the animation must make project replacement safe: " + saved.message);
    const TabId oldSceneTab = documents.active_tab_id();
    const auto replaced = documents.set_project_roots(
        secondProject, secondAssets, ProjectGeneration{6});
    require(static_cast<bool>(replaced) && documents.find_animation_tab(*animationId) == nullptr &&
                documents.animation_tab_count() == 0 && documents.tab_count() == 1 &&
                documents.active_tab_id() != oldSceneTab &&
                documents.active_tab()->workspace().document().projectRoot == std::filesystem::canonical(secondProject),
            "successful replacement must retire outgoing animation and scene identities and open a clean incoming scene");

    const auto failed = documents.set_project_roots(
        temporary.path() / "missing-project", secondAssets, ProjectGeneration{7});
    require(failed.status == WorkspaceActionStatus::Failed &&
                documents.find_animation_tab(*animationId) == nullptr &&
                documents.active_tab()->workspace().document().projectRoot == std::filesystem::canonical(secondProject),
            "failed later replacement must preserve the current project without reviving retired animation history");
}

void test_save_as_rejects_open_canonical_sources_without_rebasing_drafts()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    const auto scenes = temporary.path() / "scenes";
    std::filesystem::create_directories(assets);
    std::filesystem::create_directories(scenes);
    std::ofstream(project / "CMakeLists.txt") << "project(save-as-ownership-test)\n";
    const auto sceneA = scenes / "a.scene";
    const auto sceneB = scenes / "b.scene";
    write_json(sceneA, Json::array({object(10, "Scene A") }));
    write_json(sceneB, Json::array({object(20, "Scene B") }));
    const auto clip = [](const std::string &name, const std::filesystem::path &path) {
        return Json{{"frames", Json::array()}, {"framesPerSecond", 8.0}, {"loop", true},
                    {"width", 16.0}, {"height", 16.0}, {"name", name},
                    {"filePath", path.string()}};
    };
    const auto animationA = assets / "a.animset";
    const auto animationB = assets / "b.animset";
    write_json(animationA, clip("animation A", animationA));
    write_json(animationB, clip("animation B", animationB));

    DocumentManager documents(project, assets);
    const auto sceneAId = documents.open_scene(sceneA);
    const auto sceneBId = documents.open_scene(sceneB);
    const auto animationAId = documents.open_animation(animationA);
    const auto animationBId = documents.open_animation(animationB);
    require(sceneAId && sceneBId && animationAId && animationBId,
            "both Scene and animation sources must open before Save As ownership checks");
    auto *sceneATab = documents.find(*sceneAId);
    auto *sceneBTab = documents.find(*sceneBId);
    auto *animationATab = documents.find_animation_tab(*animationAId);
    auto *animationBTab = documents.find_animation_tab(*animationBId);
    require(sceneATab && sceneBTab && animationATab && animationBTab,
            "all source owners must remain addressable while their drafts are dirty");
    require(sceneATab->history().create_object(sceneATab->workspace().document_for_edit(),
                    std::nullopt, "Scene A draft") &&
                sceneBTab->history().create_object(sceneBTab->workspace().document_for_edit(),
                    std::nullopt, "Scene B draft") &&
                animationATab->document().set("/loop", false).changed &&
                animationBTab->document().set("/loop", false).changed,
            "both candidate owners must retain independent dirty drafts before conflict checks");

    const auto sceneADraft = sceneATab->workspace().document().root;
    const auto sceneBDraft = sceneBTab->workspace().document().root;
    const auto sceneAHistory = sceneATab->history().history_status().currentToken;
    const auto sceneBHistory = sceneBTab->history().history_status().currentToken;
    const auto sceneBDisk = read_json(sceneB);
    std::error_code symlinkError;
    const auto sceneAlias = scenes / "b-alias.scene";
    std::filesystem::create_symlink(sceneB, sceneAlias, symlinkError);
    require(!symlinkError, "the Linux fixture must create a canonical source alias");
    std::string ownerError;
    const auto sceneOwner = documents.source_owner_for_destination(
        sceneAlias, *sceneAId, &ownerError);
    require(sceneOwner && sceneOwner->tabId == *sceneBId &&
                sceneOwner->kind == DocumentTabKind::Scene && ownerError.empty(),
            "Save As ownership must resolve a symlink to the open canonical Scene source");
    const auto sceneOverwrite = documents.save_scene_tab_as(
        sceneATab->identity(), sceneAlias, true);
    require(sceneOverwrite.status == WorkspaceActionStatus::Failed &&
                sceneOverwrite.message.find("Scene tab") != std::string::npos &&
                sceneATab->workspace().document().root == sceneADraft &&
                sceneATab->history().history_status().currentToken == sceneAHistory &&
                sceneBTab->workspace().document().root == sceneBDraft &&
                sceneBTab->history().history_status().currentToken == sceneBHistory &&
                read_json(sceneB) == sceneBDisk,
            "Scene Save As must reject an already-owned alias before writing or changing either history");
    require(static_cast<bool>(documents.save_scene_tab_as(
                sceneATab->identity(), sceneA, true)) &&
                sceneATab->workspace().document().sourcePath == std::filesystem::canonical(sceneA) &&
                sceneATab->workspace().document().root == sceneADraft,
            "a Scene tab must remain permitted to Save As over its own canonical source");

    const auto lateScene = scenes / "late.scene";
    write_json(lateScene, Json::array({object(30, "Late owner") }));
    require(!documents.source_owner_for_destination(lateScene, *sceneAId),
            "an unowned destination may be selected before another tab opens it");
    const auto lateSceneId = documents.open_scene(lateScene);
    require(lateSceneId.has_value(), "the late Scene destination owner must open");
    const auto lateSceneDisk = read_json(lateScene);
    const auto lateSceneSave = documents.save_scene_tab_as(sceneATab->identity(), lateScene, true);
    require(lateSceneSave.status == WorkspaceActionStatus::Failed &&
                read_json(lateScene) == lateSceneDisk &&
                sceneATab->workspace().document().root == sceneADraft,
            "a late Scene owner must be rechecked when the overwrite continuation is committed");

    const Json animationADraft = animationATab->document().root();
    const Json animationBDraft = animationBTab->document().root();
    const auto animationAHistory = animationATab->document().history_token();
    const auto animationBHistory = animationBTab->document().history_token();
    const Json animationBDisk = read_json(animationB);
    const auto animationAlias = assets / "b-alias.animset";
    std::filesystem::create_symlink(animationB, animationAlias, symlinkError);
    require(!symlinkError, "the animation fixture must create a contained source alias");
    const auto animationOwner = documents.source_owner_for_destination(
        animationAlias, *animationAId, &ownerError);
    require(animationOwner && animationOwner->tabId == *animationBId &&
                animationOwner->kind == DocumentTabKind::Animation,
            "animation ownership must resolve contained symlink aliases across open tabs");
    const auto animationOverwrite = documents.save_animation_tab_as(
        animationATab->identity(), animationAlias,
        AssetOverwriteObservation{native_file_identity(animationB, ownerError)});
    require(animationOverwrite.status == WorkspaceActionStatus::Failed &&
                animationOverwrite.message.find("Animation tab") != std::string::npos &&
                animationATab->document().root() == animationADraft &&
                animationATab->document().history_token() == animationAHistory &&
                animationBTab->document().root() == animationBDraft &&
                animationBTab->document().history_token() == animationBHistory &&
                read_json(animationB) == animationBDisk,
            "animation Save As must reject an already-owned alias before overwrite or source adoption");
    require(static_cast<bool>(documents.save_animation_tab_as(
                animationATab->identity(), animationA)) &&
                animationATab->document().source_path() == std::filesystem::canonical(animationA) &&
                animationATab->document().root() == animationADraft,
            "an animation tab must remain permitted to Save As over its own canonical source");

    const auto lateAnimation = assets / "late.animset";
    write_json(lateAnimation, clip("late", lateAnimation));
    require(!documents.source_owner_for_destination(lateAnimation, *animationAId),
            "a previously unowned animation destination may become owned before confirmation");
    const auto lateAnimationId = documents.open_animation(lateAnimation);
    require(lateAnimationId.has_value(), "the late animation destination owner must open");
    const Json lateAnimationDisk = read_json(lateAnimation);
    const auto lateAnimationSave = documents.save_animation_tab_as(
        animationATab->identity(), lateAnimation, AssetOverwriteObservation{std::nullopt});
    require(lateAnimationSave.status == WorkspaceActionStatus::Failed &&
                read_json(lateAnimation) == lateAnimationDisk &&
                animationATab->document().root() == animationADraft,
            "a late animation owner must be rechecked when an observed overwrite is committed");

    const auto staleSceneIdentity = sceneATab->identity();
    require(static_cast<bool>(documents.close_tab(*sceneAId, UnsavedDecision::Discard)),
            "the requesting Scene tab must close for the stale callback check");
    const auto reopenedSceneId = documents.open_scene(sceneA);
    require(reopenedSceneId && *reopenedSceneId != *sceneAId,
            "reopening a Scene source must allocate a new tab identity");
    const auto staleSceneResult = documents.save_scene_tab_as(staleSceneIdentity, sceneB, true);
    require(staleSceneResult.status == WorkspaceActionStatus::Failed && read_json(sceneB) == sceneBDisk,
            "a late Scene Save As callback must reject its closed tab identity without writing");
    const auto staleAnimationIdentity = animationATab->identity();
    require(static_cast<bool>(documents.close_animation_tab(*animationAId, UnsavedDecision::Discard)),
            "the requesting animation tab must close for the stale callback check");
    const auto reopenedAnimationId = documents.open_animation(animationA);
    require(reopenedAnimationId && *reopenedAnimationId != *animationAId,
            "reopening an animation source must allocate a new tab identity");
    const auto staleAnimationResult = documents.save_animation_tab_as(
        staleAnimationIdentity, lateAnimation, AssetOverwriteObservation{std::nullopt});
    require(staleAnimationResult.status == WorkspaceActionStatus::Failed &&
                read_json(lateAnimation) == lateAnimationDisk,
            "a late animation Save As callback must reject its closed tab identity without writing");
}
} // namespace

void test_focused_animation_undo_redo_is_terminal()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    std::ofstream(project / "CMakeLists.txt") << "project(tabs-focused-undo-test)\n";

    const auto animPath = assets / "walk.animset";
    write_json(animPath, Json{{"frames", Json::array({{{"duration", 0.2},
                                                        {"textureWrapperFilePath", ""}}})},
                              {"loop", true},
                              {"filePath", "assets/walk.animset"}});

    DocumentManager documents(project, assets);
    auto *sceneTab = documents.active_tab();
    require(sceneTab != nullptr, "default scene tab must exist");
    const auto sceneId = sceneTab->id();
    const auto nameAddress = address_for_document(sceneTab->workspace().document(), 0,
                                                  std::nullopt, std::nullopt, {std::string("name")});
    require(sceneTab->history().set_property(sceneTab->workspace().document_for_edit(),
                                             nameAddress, "Renamed") &&
                sceneTab->history().history_status().canUndo,
            "scene history must contain an undoable edit");

    const auto animId = documents.open_animation(animPath);
    require(animId.has_value(), "animation tab must open");
    auto *animTab = documents.find_animation_tab(*animId);
    require(animTab != nullptr && !animTab->document().can_undo() && !animTab->document().can_redo(),
            "fresh animation tab must have empty undo/redo stacks");

    std::string status;
    auto &editCommands = sceneTab->history();
    auto &selection = sceneTab->selection();
    auto &sceneDocument = sceneTab->workspace().document_for_edit();
    require(!can_execute_focused_undo(true, documents, editCommands) &&
                !can_execute_focused_redo(true, documents, editCommands),
            "focused Animation Assets with empty history must report undo/redo unavailable");
    require(!execute_focused_undo(true, documents, editCommands, sceneDocument, selection, status) &&
                editCommands.history_status().canUndo &&
                sceneDocument.root[0]["name"] == "Renamed",
            "focused Animation Assets undo must never fall through into Scene history");
    require(!execute_focused_redo(true, documents, editCommands, sceneDocument, selection, status) &&
                editCommands.history_status().canUndo,
            "focused Animation Assets redo must never fall through into Scene history");

    require(animTab->document().set("/loop", false).changed && animTab->document().can_undo(),
            "animation edit must record one animation undo step");
    require(can_execute_focused_undo(true, documents, editCommands) &&
                execute_focused_undo(true, documents, editCommands, sceneDocument, selection, status) &&
                animTab->document().root()["loop"] == true &&
                !animTab->document().can_undo() && animTab->document().can_redo() &&
                sceneDocument.root[0]["name"] == "Renamed",
            "focused animation undo must undo only the animation and leave Scene history untouched");
    require(!execute_focused_undo(true, documents, editCommands, sceneDocument, selection, status) &&
                sceneDocument.root[0]["name"] == "Renamed",
            "exhausted animation undo must remain terminal and not touch Scene history");
    require(execute_focused_redo(true, documents, editCommands, sceneDocument, selection, status) &&
                animTab->document().root()["loop"] == false &&
                !animTab->document().can_redo() &&
                sceneDocument.root[0]["name"] == "Renamed",
            "focused animation redo must redo only the animation");
    require(!execute_focused_redo(true, documents, editCommands, sceneDocument, selection, status) &&
                sceneDocument.root[0]["name"] == "Renamed",
            "exhausted animation redo must remain terminal and not touch Scene history");
    (void)sceneId;
}

void test_save_all_syncs_saved_animations_before_later_failure()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    std::ofstream(project / "CMakeLists.txt") << "project(tabs-save-all-sync-test)\n";

    const auto animPath = assets / "hero.animset";
    write_json(animPath, Json{{"frames", Json::array({{{"duration", 0.25},
                                                        {"sourceRect", {{"x", 0.0}, {"y", 0.0}, {"w", 0.0}, {"h", 0.0}}},
                                                        {"hasSourceRect", false},
                                                        {"textureWrapperFilePath", ""}}})},
                              {"loop", true},
                              {"filePath", "assets/hero.animset"}});
    const auto failingAnimPath = assets / "later.animset";
    write_json(failingAnimPath, Json{{"frames", Json::array({{{"duration", 0.25},
                                                               {"sourceRect", {{"x", 0.0}, {"y", 0.0}, {"w", 0.0}, {"h", 0.0}}},
                                                               {"hasSourceRect", false},
                                                               {"textureWrapperFilePath", ""}}})},
                                     {"loop", true},
                                     {"filePath", "assets/later.animset"}});

    DocumentManager documents(project, assets);
    auto *sceneTab = documents.active_tab();
    const auto animId = documents.open_animation(animPath);
    const auto failingAnimId = documents.open_animation(failingAnimPath);
    require(sceneTab != nullptr && animId.has_value() && failingAnimId.has_value(),
            "animation tabs and scene tab must open");

    auto *animTab = documents.find_animation_tab(*animId);
    auto *failingAnimTab = documents.find_animation_tab(*failingAnimId);
    require(animTab != nullptr && failingAnimTab != nullptr, "opened animation tabs must be addressable");
    require(animTab->document().set("/loop", false).changed, "first animation must become dirty");
    require(failingAnimTab->document().set("/loop", false).changed, "later animation must become dirty");

    // Cause the later animation save to fail by replacing its target file with a directory.
    std::filesystem::remove(failingAnimPath);
    std::filesystem::create_directory(failingAnimPath);

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{1}, assets);
    resources.register_consumer("assets/hero.animset", ResourceKind::Animation,
                                ResourceConsumer{sceneTab->identity(), 10, std::nullopt, "animator.clip"});

    RuntimeOptions runtimeOptions;
    runtimeOptions.assetRoot = assets;
    runtimeOptions.windowSettings.headless = true;
    platformator::Runtime runtime(runtimeOptions);
    EditorPreview preview(runtime);

    const auto result = documents.save_all();
    require(result.failedTab.has_value() && *result.failedTab == *failingAnimId &&
                std::find(result.savedTabs.begin(), result.savedTabs.end(), *animId) != result.savedTabs.end(),
            "save_all must report the saved animation tab alongside the later failed animation tab");
    const bool synced = sync_saved_animation_tabs(documents, result.savedTabs, resources,
                                                   preview, sceneTab->workspace().document());
    require(synced, "partial save_all must immediately sync saved animation tabs");
}

int main()
{
    try
    {
        test_external_scene_changes_are_tab_scoped_and_recoverable();
        test_save_all_preserves_each_tab_on_partial_failure();
        test_animation_tabs_are_protected_and_conflict_safe();
        test_animation_external_changes_keep_observed_versions_and_identities();
        test_animation_tabs_migrate_roots_without_adopting_another_project();
        test_save_as_rejects_open_canonical_sources_without_rebasing_drafts();
        test_focused_animation_undo_redo_is_terminal();
        test_save_all_syncs_saved_animations_before_later_failure();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "tabs_test: " << error.what() << '\n';
        return 1;
    }
}
