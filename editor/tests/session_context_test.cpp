#include "documentmanager.h"
#include "layersettings.h"
#include "projectsession.h"
#include "sessionids.h"
#include "test_support.h"

#include <platformator/runtime.h>

#include <stdexcept>
#include <type_traits>
#include <limits>
#include <iostream>

using namespace platformator_editor;
using namespace platformator_editor_test;

static_assert(!std::is_same_v<ProjectGeneration, TabId>);
static_assert(!std::is_same_v<TabId, DocumentGeneration>);
static_assert(!std::is_same_v<HostLayoutId, WindowHostGeneration>);
static_assert(!std::is_same_v<WindowHostGeneration, RendererGeneration>);

namespace
{
void test_identity_scopes_and_never_reuse()
{
    MonotonicIdentitySource<TabId> tabs;
    const auto first = tabs.next();
    const auto second = tabs.next();
    require(first.value == 1 && second.value == 2 && first != second,
            "tab identity allocation must be monotonic and never reuse an open-tab ID");

    MonotonicIdentitySource<TabId> last(std::numeric_limits<std::uint64_t>::max());
    require(last.next().value == std::numeric_limits<std::uint64_t>::max(),
            "the final representable identity is valid");
    bool exhausted = false;
    try
    {
        (void)last.next();
    }
    catch (const std::overflow_error &)
    {
        exhausted = true;
    }
    require(exhausted, "identity exhaustion must fail instead of wrapping or reusing an ID");

    const DocumentIdentity firstTab{ProjectGeneration{7}, first, DocumentGeneration{1}};
    const DocumentIdentity secondTab{ProjectGeneration{7}, second, DocumentGeneration{1}};
    require(firstTab != secondTab && matches(firstTab, ProjectGeneration{7}, first,
                                              DocumentGeneration{1}) &&
                !matches(firstTab, ProjectGeneration{7}, second, DocumentGeneration{1}),
            "identical document generations in different tabs must remain distinct");
    require(!matches(firstTab, ProjectGeneration{8}, first, DocumentGeneration{1}) &&
                !matches(firstTab, ProjectGeneration{7}, first, DocumentGeneration{2}),
            "old project or document incarnations must be rejected");
}

void test_tab_history_selection_and_canonical_activation()
{
    TestEnvironment environment;
    const auto project = environment.filesystem_root();
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    std::ofstream(project / "CMakeLists.txt") << "project(session-context-test)\n";

    DocumentManager manager(project, assets, ProjectGeneration{1});
    const TabId firstId = manager.active_tab_id();
    const TabId secondId = manager.create_scene_tab();
    auto *first = manager.find(firstId);
    auto *second = manager.find(secondId);
    require(first != nullptr && second != nullptr && firstId != secondId,
            "each open scene tab must have a distinct stable TabId");
    require(first->workspace().document().documentGeneration != 0 &&
                !first->workspace().document().indexes.orderedObjectIds.empty(),
            "a new tab must start with a nonzero document generation and current indexes");
    require(first->workspace().document().documentGeneration ==
                second->workspace().document().documentGeneration,
            "independent tabs may begin with the same document generation");
    require(first->identity() != second->identity(),
            "tab identity must complete the document-generation identity tuple");

    auto created = first->history().create_object(first->workspace().document_for_edit(), std::nullopt,
                                                   "First tab object");
    require(created && first->history().dirty() && first->workspace().dirty() &&
                !second->history().dirty() && !second->workspace().dirty(),
            "history binding must remain alive and dirty state must be tab-local");
    auto secondCreated = second->history().create_object(second->workspace().document_for_edit(), std::nullopt,
                                                          "Second tab object");
    require(secondCreated && secondCreated.command->payload["id"] == created.command->payload["id"],
            "separate documents may contain the same object ID without sharing identity");
    const auto firstAddress = address_for_document(first->workspace().document(),
        created.command->payload["id"].get<int>(), std::nullopt, std::nullopt,
        {std::string("name")});
    const auto crossTabEdit = second->history().set_property(second->workspace().document_for_edit(),
                                                              firstAddress, "retargeted");
    require(crossTabEdit.status == CommandStatus::StaleAddress &&
                second->workspace().document().root[1]["name"] == "Second tab object",
            "a same-generation result from another tab must be rejected as stale");

    DocumentIdentity notifiedIdentity;
    first->selection().subscribe([&](const SelectionNotification &notification) {
        notifiedIdentity = notification.identity;
    });
    first->selection().set(first->workspace().document(), Selection{.objectId = created.command->payload["id"].get<int>()});
    require(first->selection().current().objectId.has_value() &&
                !second->selection().current().objectId.has_value() && notifiedIdentity == first->identity(),
            "selection state and notifications must remain scoped to the owning tab");

    const auto scenePath = project / "shared.scene";
    write_json(scenePath, scene({object(90, "Shared Scene")}));
    const auto opened = manager.open_scene(scenePath);
    require(opened && manager.active_tab_id() == *opened,
            "opening a scene must activate its writable tab");
    const auto reopened = manager.open_scene(scenePath);
    require(reopened == opened && manager.active_tab_id() == *opened && manager.tab_count() == 3,
            "opening the same canonical path must activate, not duplicate, the document");
}

void test_layer_sync_does_not_dirty_clean_documents_or_add_history()
{
    TestEnvironment environment;
    const auto project = environment.filesystem_root();
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    std::ofstream(project / "CMakeLists.txt") << "project(layer-sync-test)\n";

    const auto scenePath = assets / "Collider.scene";
    const Json collider = component(7, 3, Json{{"collisionGroup", layer_to_collision_group(0)},
                                               {"collisionMask", 0}});
    write_json(scenePath, scene({object(1, "Collider", Json::array({collider}))}));

    DocumentManager manager(project, assets, ProjectGeneration{1});
    const auto tabId = manager.open_scene(scenePath);
    require(tabId.has_value(), "layer-sync fixture scene must open");
    auto *tab = manager.find(*tabId);
    require(tab != nullptr && !tab->workspace().dirty(),
            "opened layer-sync fixture must begin clean");
    const auto beforeHistory = tab->history().history_status();
    const auto beforeRevision = tab->workspace().document().valueRevision;

    auto settings = default_layer_settings();
    settings.collisionMatrix[0] = 1u;
    const auto changed = manager.sync_layer_settings(settings, false);
    require(!changed.empty() &&
                tab->workspace().document().root[0]["components"][0]["collisionMask"] == 1u,
            "history-free synchronization must apply the current slot-0 collision mask");
    const auto afterHistory = tab->history().history_status();
    require(!tab->workspace().dirty() &&
                afterHistory.currentToken == beforeHistory.currentToken &&
                afterHistory.undoCommands == beforeHistory.undoCommands &&
                tab->workspace().document().valueRevision > beforeRevision,
            "derived layer synchronization must keep clean-file and undo state clean while revising values");
}

void test_only_one_runtime_can_be_active()
{
    TestEnvironment environment;
    RuntimeOptions options;
    options.assetRoot = environment.filesystem_root().string();
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    platformator::Runtime first(options);
    require(platformator::Runtime::tryGetCurrent() == &first,
            "the project session owns the one active authoring Runtime");

    bool rejected = false;
    try
    {
        platformator::Runtime second(options);
    }
    catch (const std::logic_error &)
    {
        rejected = true;
    }
    require(rejected && platformator::Runtime::tryGetCurrent() == &first,
            "a second active Runtime must be rejected without replacing the owner");
}

void test_project_replacement_is_transactional_and_retires_outgoing_work()
{
    TestEnvironment environment;
    const auto firstProject = environment.root() / "first";
    const auto firstAssets = firstProject / "assets";
    const auto secondProject = environment.root() / "second";
    const auto secondAssets = secondProject / "assets";
    std::filesystem::create_directories(firstAssets);
    std::filesystem::create_directories(secondAssets);
    std::ofstream(firstProject / "CMakeLists.txt") << "project(first)\n";
    std::ofstream(secondProject / "CMakeLists.txt") << "project(second)\n";

    ProjectSession session(firstProject, firstAssets);
    const TabId tabId = session.documents().active_tab_id();
    auto *tab = session.documents().find(tabId);
    require(tab != nullptr, "project session must expose its initial document tab");
    auto edit = tab->history().create_object(tab->workspace().document_for_edit(), std::nullopt,
                                             "Unsaved object");
    require(edit && tab->workspace().dirty(), "root replacement fixture must contain unsaved work");
    const TabId secondTabId = session.documents().create_scene_tab();
    auto *secondTab = session.documents().find(secondTabId);
    require(secondTab != nullptr, "root replacement must include every open document tab");
    require(secondTab->history().create_object(secondTab->workspace().document_for_edit(), std::nullopt,
                                               "Second unsaved object") &&
                secondTab->workspace().dirty(),
            "the second open tab must also carry independent unsaved work");
    const Json beforeRoot = tab->workspace().document().root;
    const auto beforeHistory = tab->history().history_status();
    const auto beforeIdentity = tab->identity();

    const auto failed = session.set_project_roots(environment.root() / "missing",
                                                  environment.root() / "missing-assets");
    require(failed.status == WorkspaceActionStatus::Failed && session.generation() == beforeIdentity.projectGeneration &&
                tab->identity() == beforeIdentity && tab->workspace().document().root == beforeRoot &&
                tab->workspace().dirty() &&
                tab->history().history_status().currentToken == beforeHistory.currentToken,
            "failed project replacement must preserve generation, document, dirty state and history");

    const auto refused = session.set_project_roots(secondProject, secondAssets);
    require(refused.status == WorkspaceActionStatus::Failed &&
                session.generation() == beforeIdentity.projectGeneration &&
                session.documents().find(tabId) == tab && session.documents().find(secondTabId) == secondTab &&
                tab->workspace().document().root == beforeRoot && tab->workspace().dirty() &&
                tab->history().history_status().currentToken == beforeHistory.currentToken,
            "dirty project replacement must refuse without changing either open tab or its history");

    const auto replaced = session.set_project_roots(secondProject, secondAssets, true);
    require(replaced && session.generation() == ProjectGeneration{2} &&
                session.documents().tab_count() == 1 && session.documents().animation_tab_count() == 0 &&
                session.documents().find(tabId) == nullptr && session.documents().find(secondTabId) == nullptr,
            "explicit discard must retire all outgoing scene tabs and their histories");
    const TabId incomingTab = session.documents().active_tab_id();
    const auto *incoming = session.documents().find(incomingTab);
    require(incoming != nullptr && incomingTab != tabId && incomingTab != secondTabId &&
                !incoming->workspace().dirty() &&
                incoming->workspace().document().projectRoot == std::filesystem::canonical(secondProject),
            "successful replacement must provide a fresh clean document for the incoming project");

    const auto returned = session.set_project_roots(firstProject, firstAssets);
    require(returned && session.generation() == ProjectGeneration{3} &&
                session.documents().tab_count() == 1 && session.documents().active_tab_id() != incomingTab &&
                session.documents().active_tab()->workspace().document().projectRoot ==
                    std::filesystem::canonical(firstProject),
            "A-to-B-to-A project restoration must create fresh project-owned document identities");
}
} // namespace

int main()
{
    try
    {
        test_identity_scopes_and_never_reuse();
        test_tab_history_selection_and_canonical_activation();
        test_layer_sync_does_not_dirty_clean_documents_or_add_history();
        test_project_replacement_is_transactional_and_retires_outgoing_work();
        test_only_one_runtime_can_be_active();
    }
    catch (const std::exception &error)
    {
        std::cerr << "session context test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
