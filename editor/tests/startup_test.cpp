#include "test_support.h"
#include "defaultlayout.h"
#include "document.h"
#include "editcommands.h"
#include "preview.h"
#include "panelwindow.h"
#include "panels/game.h"
#include "selection.h"
#include "viewport.h"
#include "style.h"
#include "preferences.h"
#include "toolbar.h"
#include "filedialogs.h"
#include "editorapp.h"
#include "editorresources.h"
#include "projectsession.h"
#include "statustoast.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <platformator/runtime.h>

#include <iostream>
#include <fstream>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

using namespace platformator_editor_test;
using namespace platformator_editor;
using platformator_editor::DocumentSession;
using platformator_editor::EditCommandService;
using platformator_editor::EditorPreview;
using platformator_editor::SceneViewportPanel;
using platformator_editor::SelectionModel;
using platformator_editor::make_default_document;
using platformator_editor::rebuild_document_indexes;

namespace
{
class ImGuiSession
{
  public:
    ImGuiSession(SDL_Window *window, SDL_Renderer *renderer) : renderer_(renderer)
    {
        try
        {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            contextCreated_ = true;
            if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer_))
                throw std::runtime_error(std::string("SDL3 ImGui backend initialization failed: ") + SDL_GetError());
            platformInitialized_ = true;
            if (!ImGui_ImplSDLRenderer3_Init(renderer_))
                throw std::runtime_error(std::string("SDL renderer ImGui backend initialization failed: ") + SDL_GetError());
            rendererInitialized_ = true;
        }
        catch (...)
        {
            cleanup();
            throw;
        }
    }

    ~ImGuiSession()
    {
        cleanup();
    }

    void draw_one_frame(DocumentSession &document, EditorPreview &preview,
                        SelectionModel &selection, EditCommandService &commands,
                        SceneViewportPanel &scenePanel, SDL_Window *window, bool showPanels = true)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            ImGui_ImplSDL3_ProcessEvent(&event);
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        submit_editor_dockspace(0x6E706EF3, 40.0f);
        const auto drawPanel = [showPanels](const char *name) {
            bool open = showPanels;
            const auto frame = begin_panel_window(showPanels, name, &open);
            if (frame.contentsVisible)
                ImGui::TextUnformatted("docking acceptance frame");
            end_panel_window(frame);
        };
        drawPanel("Hierarchy###panel.hierarchy");
        ImGui::SetNextWindowSize(ImVec2(480.0f, 360.0f), ImGuiCond_Always);
        scenePanel.set_visible(showPanels);
        SnapPreferences snap;
        scenePanel.draw(document, preview, selection, commands, window, renderer_, snap);
        drawPanel("Inspector###panel.inspector");
        drawPanel("Project and Assets###panel.project");
        drawPanel("Console###panel.console");
        drawPanel("Project Settings###panel.project_settings");
        drawPanel("Validation###panel.validation");
        ImGui::Render();
        if (!SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255) || !SDL_RenderClear(renderer_))
            throw std::runtime_error(std::string("SDL renderer setup failed: ") + SDL_GetError());
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
        SDL_RenderPresent(renderer_);
    }

    EditorToolbarFrame draw_toolbar_frame(SDL_Window *window, int width, int height, float scale,
                                          const EditorToolbarActions &actions = {})
    {
        SDL_SetWindowSize(window, width, height);
        apply_editor_style(scale);
        SDL_Event event;
        while (SDL_PollEvent(&event))
            ImGui_ImplSDL3_ProcessEvent(&event);
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        EditorToolbarState state;
        state.operationActive = true;
        const auto frame = draw_editor_toolbar(state, actions);
        submit_editor_dockspace(0x6E706EF3, frame.height);
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
        SDL_RenderPresent(renderer_);
        return frame;
    }

    FileDialogPathAction draw_path_entry_frame(bool &open, std::string_view error,
                                               char *path, std::size_t pathCapacity)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            ImGui_ImplSDL3_ProcessEvent(&event);
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const auto action = draw_file_dialog_path_entry(open, error, path, pathCapacity);
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
        SDL_RenderPresent(renderer_);
        return action;
    }

  private:
    void cleanup() noexcept
    {
        if (rendererInitialized_)
        {
            ImGui_ImplSDLRenderer3_Shutdown();
            rendererInitialized_ = false;
        }
        if (platformInitialized_)
        {
            ImGui_ImplSDL3_Shutdown();
            platformInitialized_ = false;
        }
        if (contextCreated_)
        {
            ImGui::DestroyContext();
            contextCreated_ = false;
        }
    }

    SDL_Renderer *renderer_ = nullptr;
    bool contextCreated_ = false;
    bool platformInitialized_ = false;
    bool rendererInitialized_ = false;
};

void projectless_adoption_cases()
{
    TemporaryDirectory temporary;
    const auto bootstrap = temporary.path() / "bootstrap";
    const auto bootstrapAssets = bootstrap / "assets";
    const auto project = temporary.path() / "project";
    const auto projectAssets = project / "assets";
    const auto savedProject = temporary.path() / "last-project";
    const auto lastWorkspacePath = temporary.path() / "preferences" / "last-workspace.json";
    std::filesystem::create_directories(projectAssets);
    std::ofstream(project / "CMakeLists.txt") << "project(startup-fixture)\n";
    std::filesystem::create_directories(savedProject / "assets");
    std::ofstream(savedProject / "CMakeLists.txt") << "project(removed-startup-fixture)\n";
    std::string persistenceError;
    require(save_last_workspace(lastWorkspacePath, savedProject, persistenceError),
            "a valid workspace choice must be saved before missing-root restoration coverage: " +
                persistenceError);
    std::filesystem::remove_all(savedProject);
    const auto missingRestoration = load_last_workspace(lastWorkspacePath);
    bool missingRootRejected = false;
    try
    {
        if (missingRestoration.projectRoot)
            validate_project_folder(*missingRestoration.projectRoot);
    }
    catch (const std::invalid_argument &)
    {
        missingRootRejected = true;
    }
    require(missingRestoration.projectRoot && missingRootRejected,
            "a removed last project must be rejected while allowing startup to fall back to a usable bootstrap workspace");
    std::ofstream(lastWorkspacePath, std::ios::binary | std::ios::trunc) << "{";
    const auto invalidRestoration = load_last_workspace(lastWorkspacePath);
    require(!invalidRestoration.projectRoot && !invalidRestoration.message.empty(),
            "a corrupt last-workspace preference must be ignored with an actionable message");
    require(!editor_project_root_has_manifest(bootstrap) && editor_project_root_has_manifest(project),
            "projectless startup must distinguish an empty bootstrap root from an asset project");

    std::filesystem::create_directories(bootstrapAssets);
    ProjectSession session(bootstrap, bootstrapAssets);
    auto *tab = session.documents().active_tab();
    require(tab != nullptr, "projectless bootstrap must provide an untitled document for the welcome flow");
    const TabId outgoingTabId = tab->id();
    auto &workspace = tab->workspace();
    const auto nameAddress = address_for_document(workspace.document(), 0, std::nullopt,
                                                  std::nullopt, {std::string("name")});
    const auto edited = tab->history().set_property(workspace.document_for_edit(), nameAddress,
                                                     "Draft before choosing project");
    require(static_cast<bool>(edited) && workspace.dirty(),
            "a projectless untitled scene must retain edits while the project chooser is used");
    const auto historyBefore = tab->history().history_status();

    const auto rejected = session.set_project_roots(project, project / "missing-assets");
    require(!rejected && workspace.dirty() &&
                workspace.document().projectRoot == std::filesystem::canonical(bootstrap) &&
                tab->history().history_status().currentToken == historyBefore.currentToken,
            "an invalid project selection must leave the current draft and history usable");

    const auto refusedDirtyAdoption = session.set_project_roots(project, projectAssets);
    require(!refusedDirtyAdoption && workspace.dirty() &&
                workspace.document().projectRoot == std::filesystem::canonical(bootstrap) &&
                tab->history().history_status().currentToken == historyBefore.currentToken,
            "adopting a real project must stop while the projectless draft is unsaved");

    const auto adopted = session.set_project_roots(project, projectAssets, true);
    require(static_cast<bool>(adopted) && session.documents().tab_count() == 1 &&
                session.documents().find(outgoingTabId) == nullptr,
            "explicitly discarding the projectless draft must retire its tab and history");
    const auto *incoming = session.documents().active_tab();
    require(incoming != nullptr && !incoming->workspace().dirty() &&
                incoming->workspace().document().projectRoot == std::filesystem::canonical(project) &&
                incoming->workspace().document().assetRoot == std::filesystem::canonical(projectAssets),
            "adopting a real project must start a clean document under the incoming project roots");
}

void executable_relative_resource_cases()
{
    TemporaryDirectory temporary;
    const auto prefix = temporary.path() / "install prefix";
    const auto executableDirectory = prefix / "bin";
    const auto resourceDirectory = prefix / "share" / "platformator" / "editor";
    std::filesystem::create_directories(executableDirectory);
    std::filesystem::create_directories(resourceDirectory);

    const auto layout = resourceDirectory / "default-layout.ini";
    constexpr std::string_view optionalContents = "[Window][Optional install layout]\nPos=1,2\n";
    {
        std::ofstream output(layout, std::ios::binary);
        output.write(optionalContents.data(), static_cast<std::streamsize>(optionalContents.size()));
    }
    const auto loaded = read_optional_editor_resource(
        executableDirectory, "../share/platformator/editor/default-layout.ini", 1024);
    require(loaded && *loaded == optionalContents,
            "the optional editor layout must resolve relative to the installed executable");
    require(read_editor_layout_or_fallback(
                executableDirectory, "../share/platformator/editor/default-layout.ini",
                "[Window][Embedded fallback]\n", 1024) == optionalContents,
            "the installed optional layout must override the embedded fallback");

    require(!read_optional_editor_resource(executableDirectory,
                                           "../share/platformator/editor/missing.ini", 1024),
            "a missing optional resource must leave the caller free to use its embedded fallback");
    require(read_editor_layout_or_fallback(
                executableDirectory, "../share/platformator/editor/missing.ini",
                "[Window][Embedded fallback]\n", 1024) == "[Window][Embedded fallback]\n",
            "a missing optional layout must retain the embedded default");
    require(!read_optional_editor_resource(executableDirectory,
                                           "../../outside.ini", 1024),
            "optional resource lookup must reject paths outside the installation prefix");
    require(!read_optional_editor_resource(executableDirectory,
                                           "../share/platformator/editor/default-layout.ini",
                                           optionalContents.size() - 1),
            "optional resource lookup must reject files above its caller-provided size limit");
    require(!read_optional_editor_resource(executableDirectory,
                                           layout, 1024),
            "optional resource lookup must reject absolute paths");
}

void pending_run_validation_cases()
{
    const DocumentIdentity sceneA{ProjectGeneration{4}, TabId{21}, DocumentGeneration{3}};
    const DocumentIdentity sceneB{ProjectGeneration{4}, TabId{22}, DocumentGeneration{3}};
    const DocumentIdentity reopenedA{ProjectGeneration{4}, TabId{23}, DocumentGeneration{1}};
    const DocumentIdentity replacedProject{ProjectGeneration{5}, TabId{21}, DocumentGeneration{3}};
    PendingRunValidation pending;
    const auto switchedTicket = pending.begin(sceneA);
    require(switchedTicket && pending.matches(*switchedTicket, sceneA) &&
                !pending.matches(*switchedTicket, sceneB),
            "a validation wait must remain bound to its originating Scene tab");
    require(!pending.complete(*switchedTicket, sceneB) && pending.cancel(),
            "switching tabs must reject and retire an old Run continuation");

    const auto closedTicket = pending.begin(sceneA);
    require(closedTicket && !pending.matches(*closedTicket, reopenedA),
            "closing and reopening the same file must not inherit an earlier Run wait");
    pending.cancel();
    const auto projectTicket = pending.begin(sceneA);
    require(projectTicket && !pending.matches(*projectTicket, replacedProject),
            "project replacement must invalidate a pending Run identity");
    pending.cancel();

    int launches = 0;
    std::optional<DocumentIdentity> launchedDocument;
    const auto stoppedTicket = pending.begin(sceneA);
    require(stoppedTicket && pending.cancel(), "Stop must retire a waiting Run request");
    if (stoppedTicket && pending.complete(*stoppedTicket, sceneA))
    {
        ++launches;
        launchedDocument = stoppedTicket->document;
    }
    require(launches == 0,
            "Stop after validation is ready but before continuation must prevent any launch");

    const auto olderTicket = pending.begin(sceneA);
    const auto newerTicket = pending.begin(sceneA);
    require(olderTicket && newerTicket && newerTicket->requestId > olderTicket->requestId &&
                !pending.complete(*olderTicket, sceneA) &&
                pending.complete(*newerTicket, sceneA),
            "a newer Run request must supersede an older waiting continuation");
    ++launches;
    launchedDocument = newerTicket->document;
    require(launches == 1 && launchedDocument == sceneA && !pending.active(),
            "the current Run intent must launch its exact Scene identity once after validation is accepted");
}
} // namespace

int main()
{
    try
    {
        std::string toastMessage = "startup ready";
        std::string previousToast;
        StatusToastClock::time_point toastDeadline{};
        const StatusToastClock::time_point toastNow{};
        update_status_toast(toastMessage, previousToast, toastDeadline, toastNow);
        require(toastMessage == "startup ready" &&
                    status_toast_visible(toastMessage, toastDeadline, toastNow),
                "a new toast must remain visible on its first frame when the old deadline is zero");
        update_status_toast(toastMessage, previousToast, toastDeadline,
                            toastNow + std::chrono::seconds(5));
        require(toastMessage.empty() &&
                    !status_toast_visible(toastMessage, toastDeadline,
                                          toastNow + std::chrono::seconds(5)),
                "a toast must clear when its refreshed deadline expires");
        toastMessage = "next action";
        update_status_toast(toastMessage, previousToast, toastDeadline,
                            toastNow + std::chrono::seconds(6));
        require(toastMessage == "next action" &&
                    status_toast_visible(toastMessage, toastDeadline,
                                         toastNow + std::chrono::seconds(6)),
                "a later status message must receive a fresh visible interval");
        require(runtime_inspection_list_height(300.0f, true, 20.0f, 4.0f) == 176.0f &&
                    runtime_inspection_list_height(100.0f, true, 20.0f, 4.0f) == 1.0f &&
                    runtime_inspection_list_height(300.0f, false, 20.0f, 4.0f) == 300.0f,
                "runtime inspection must reserve enough list height for selected object details");

        pending_run_validation_cases();
        require(resolve_editor_ui_scale(1.0f, 1280, 800, 1280, 800) == 1.0f &&
                    resolve_editor_ui_scale(1.5f, 1280, 800, 1920, 1200) == 2.25f &&
                    resolve_editor_ui_scale(1.0f, 1280, 800, 2560, 1600) == 2.0f,
                "the active UI scale must combine persisted user scale with the current host DPI");
        require(resolve_editor_ui_scale(1.5f, 1280, 800, 1280, 800) == 1.5f &&
                    resolve_editor_ui_scale(1.5f, 1280, 800, 2560, 1600) == 3.0f &&
                    resolve_editor_ui_scale(1.5f, 1280, 800, 0, 0) == 1.5f,
                "the active UI scale must update when a host moves between monitor pixel densities");
        require(resolve_editor_ui_scale(4.0f, 1280, 800, 1280, 800) == 1.0f &&
                    resolve_editor_ui_scale(3.0f, 1280, 800, 10000, 8000) == 6.0f,
                "invalid user scales must fall back safely and high-DPI totals must stay bounded");
        projectless_adoption_cases();
        executable_relative_resource_cases();
        TemporaryDirectory temporary;
        const auto assets = temporary.path() / "assets";
        std::filesystem::create_directories(assets);
        RuntimeOptions options;
        options.assetRoot = assets.string();
        options.windowSettings.width = 320;
        options.windowSettings.height = 240;

        // This scope mirrors run_editor's ownership order: ImGui is released
        // before Runtime destroys the SDL window and renderer.
        platformator::Runtime runtime(options);
        auto *window = runtime.getWindowHandle();
        auto *renderer = runtime.getRenderer();
        require(window != nullptr && renderer != nullptr, "Runtime must create the editor window and renderer");
        DocumentSession document = make_default_document(temporary.path(), assets, 2);
        document.root.push_back(Json{{"id", 10}, {"name", "Scene panel fixture"}, {"tag", ""},
                                     {"active", true}, {"position", {{"x", 30.0}, {"y", 40.0}}},
                                     {"scale", {{"x", 1.0}, {"y", 1.0}}}, {"rotation", 0.0},
                                     {"children", Json::array()},
                                     {"components", Json::array({Json{{"id", 11}, {"type", 6},
                                         {"textureFilePath", "assets/missing.png"}, {"flip", 0},
                                         {"width", 20.0}, {"height", 20.0},
                                         {"sourceRectEnabled", false}}})}});
        document.structureRevision++;
        rebuild_document_indexes(document);
        EditorPreview preview(runtime);
        preview.reconcile(document, true);
        SelectionModel selection;
        EditCommandService commands;
        SceneViewportPanel scenePanel;
        ImGuiSession imgui(window, renderer);
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        const auto seedPath = std::filesystem::path(__FILE__).parent_path().parent_path() /
                              "resources" / "default-layout.ini";
        std::ifstream seedStream(seedPath, std::ios::binary);
        const std::string checkedInSeed{std::istreambuf_iterator<char>(seedStream),
                                        std::istreambuf_iterator<char>()};
        const std::string seed(kEmbeddedDefaultLayout);
        require(!seed.empty() && seed == checkedInSeed,
                "the editor must compile the checked-in default docking layout into its binary");
        ImGui::LoadIniSettingsFromMemory(seed.data(), seed.size());
        apply_editor_style(1.0f);
        imgui.draw_one_frame(document, preview, selection, commands, scenePanel, window);
        imgui.draw_one_frame(document, preview, selection, commands, scenePanel, window);
        const auto dockId = [](const char *name) {
            const auto *found = ImGui::FindWindowByName(name);
            return found == nullptr ? ImGuiID{} : found->DockId;
        };
        require(dockId("Hierarchy###panel.hierarchy") == 0x00002000 &&
                    dockId("Scene###panel.scene") == 0x00003000 &&
                    dockId("Inspector###panel.inspector") == 0x00003001 &&
                    dockId("Project and Assets###panel.project") == 0x00001001 &&
                    dockId("Console###panel.console") == 0x00001001,
                "first actual frames must attach each required window to its intended seeded dock node");
        // Reset Layout reloads the same seed into the active context; verify it at 150% too.
        ImGui::LoadIniSettingsFromMemory(seed.data(), seed.size());
        apply_editor_style(1.5f);
        imgui.draw_one_frame(document, preview, selection, commands, scenePanel, window);
        imgui.draw_one_frame(document, preview, selection, commands, scenePanel, window);
        require(dockId("Scene###panel.scene") == 0x00003000 &&
                    dockId("Hierarchy###panel.hierarchy") == 0x00002000 &&
                    dockId("Project and Assets###panel.project") == 0x00001001,
                "reset seeded docking must remain attached after real frames at 150% UI scale");
        imgui.draw_one_frame(document, preview, selection, commands, scenePanel, window, false);
        const auto isActive = [](const char *name) {
            const auto *found = ImGui::FindWindowByName(name);
            return found != nullptr && found->Active;
        };
        require(!isActive("Hierarchy###panel.hierarchy") && !isActive("Scene###panel.scene") &&
                    !isActive("Inspector###panel.inspector") &&
                    !isActive("Project and Assets###panel.project") && !isActive("Console###panel.console") &&
                    !isActive("Project Settings###panel.project_settings") && !isActive("Validation###panel.validation"),
                "a real hidden-panel frame must not submit the windows to ImGui");
        imgui.draw_one_frame(document, preview, selection, commands, scenePanel, window, true);
        require(isActive("Hierarchy###panel.hierarchy") && isActive("Scene###panel.scene") &&
                    isActive("Inspector###panel.inspector") &&
                    isActive("Project and Assets###panel.project") && isActive("Console###panel.console") &&
                    isActive("Project Settings###panel.project_settings") && isActive("Validation###panel.validation"),
                "View-style reopening must submit the panel windows again on the next frame");
        auto legacyLayout = seed;
        const auto stableSceneHeader = legacyLayout.find("[Window][panel.scene]");
        require(stableSceneHeader != std::string::npos,
                "startup layout migration fixture must include the stable Scene entry");
        legacyLayout.replace(stableSceneHeader, std::string("[Window][panel.scene]").size(),
                             "[Window][Scene]");
        const auto migratedLayout = migrate_scene_window_identity(legacyLayout);
        const auto authoredSceneBeforeLayoutReload = document.root;
        ImGui::LoadIniSettingsFromMemory(migratedLayout.data(), migratedLayout.size());
        apply_editor_style(1.0f);
        imgui.draw_one_frame(document, preview, selection, commands, scenePanel, window);
        imgui.draw_one_frame(document, preview, selection, commands, scenePanel, window);
        require(dockId("Scene###panel.scene") == 0x00003000 &&
                    document.root == authoredSceneBeforeLayoutReload,
                "migrated saved Scene docking must bind the real window without changing scene data");

        const std::string legacyBottomSplitLayout =
            "[Docking][Data]\n"
            "DockSpace ID=0x6E706EF3 Pos=0,0 Size=1280,800 Split=Y\n"
            "  DockNode ID=0x00001000 Parent=0x6E706EF3 SizeRef=1280,500 Split=X\n"
            "  DockNode ID=0x00001001 Parent=0x6E706EF3 SizeRef=1280,300 Split=X\n"
            "    DockNode ID=0x00002000 Parent=0x00001000 SizeRef=260,500\n"
            "    DockNode ID=0x00003000 Parent=0x00001000 SizeRef=1020,500 CentralNode=1\n"
            "    DockNode ID=0x00004000 Parent=0x00001001 SizeRef=700,300\n"
            "    DockNode ID=0x00004001 Parent=0x00001001 SizeRef=580,300\n"
            "[Window][panel.scene]\nPos=260,0\nSize=720,570\nDockId=0x00003000,0\n"
            "[Window][panel.game]\nPos=260,0\nSize=720,570\nDockId=0x00003000,1\n"
            "[Window][panel.hierarchy]\nPos=0,0\nSize=260,570\nDockId=0x00002000,0\n"
            "[Window][panel.project]\nPos=0,500\nSize=700,300\nDockId=0x00004000,0\n"
            "[Window][panel.console]\nPos=700,500\nSize=580,300\nDockId=0x00004001,0\n"
            "[Window][panel.run]\nPos=700,500\nSize=580,300\nDockId=0x00004001,1\n"
            "[Window][panel.validation]\nPos=700,500\nSize=580,300\nDockId=0x00004001,2\n";
        const auto migratedBottomSplit = migrate_scene_window_identity(legacyBottomSplitLayout);
        require(migratedBottomSplit.find("0x00004000") == std::string::npos &&
                migratedBottomSplit.find("0x00004001") == std::string::npos &&
                migratedBottomSplit.find("DockId=0x00001001,0") != std::string::npos &&
                migratedBottomSplit.find("DockId=0x00001001,1") != std::string::npos &&
                migratedBottomSplit.find("DockId=0x00001001,2") != std::string::npos &&
                migratedBottomSplit.find("DockId=0x00001001,3") != std::string::npos &&
                migratedBottomSplit.find("DockId=0x00001001,4") != std::string::npos &&
                migratedBottomSplit.find("[Window][animation-editor]") != std::string::npos,
                "legacy bottom split layout must migrate to unified 0x00001001 tab dock node with all bottom panels");
        require(migrate_scene_window_identity(migratedBottomSplit) == migratedBottomSplit,
                "migrated bottom split layout must be idempotent");
        int openSceneActivations = 0;
        EditorToolbarActions toolbarActions;
        toolbarActions.buildAndRun = [&] { ++openSceneActivations; };
        const auto pushOpenSceneClick = [&](const EditorToolbarItemBounds &item, int width, int height,
                                            float scale) {
            const float x = item.x + item.width * 0.5f;
            const float y = item.y + item.height * 0.5f;
            const auto pushMouse = [&](Uint32 type) {
                SDL_Event event{};
                event.type = type;
                if (type == SDL_EVENT_MOUSE_MOTION)
                {
                    event.motion.windowID = SDL_GetWindowID(window);
                    event.motion.x = x;
                    event.motion.y = y;
                }
                else
                {
                    event.button.windowID = SDL_GetWindowID(window);
                    event.button.button = SDL_BUTTON_LEFT;
                    event.button.x = x;
                    event.button.y = y;
                }
                require(SDL_PushEvent(&event), "SDL must queue toolbar activation input");
            };
            pushMouse(SDL_EVENT_MOUSE_MOTION);
            pushMouse(SDL_EVENT_MOUSE_BUTTON_DOWN);
            imgui.draw_toolbar_frame(window, width, height, scale, toolbarActions);
            pushMouse(SDL_EVENT_MOUSE_BUTTON_UP);
            imgui.draw_toolbar_frame(window, width, height, scale, toolbarActions);
        };
        for (const auto &[width, height, scale] : {std::tuple{1280, 800, 1.0f},
                                                   std::tuple{720, 500, 1.0f},
                                                   std::tuple{480, 320, 1.5f},
                                                   std::tuple{1280, 800, 2.0f},
                                                   std::tuple{320, 280, 2.0f}})
        {
            const auto toolbar = imgui.draw_toolbar_frame(window, width, height, scale, toolbarActions);
            const auto toolbarSemantics = editor_toolbar_semantic_nodes(toolbar);
            require(toolbarSemantics.size() == 1 &&
                        toolbarSemantics.front().id == "main/toolbar",
                    "toolbar semantics must omit the removed identity and status phantom nodes");
            const auto *viewport = ImGui::GetMainViewport();
            const auto hasItem = [&](std::string_view name) {
                return std::find_if(toolbar.items.begin(), toolbar.items.end(), [&](const auto &item) {
                    return item.name == name;
                }) != toolbar.items.end();
            };
            require(!hasItem("Open Scene") && !hasItem("Save") && !hasItem("Build") &&
                        hasItem("Build and Run") && hasItem("Pause") && hasItem("Step") && hasItem("Stop") &&
                        toolbar.items.size() == 4,
                    "toolbar must keep only primary scene/build actions without duplicate overflow commands");
            for (const auto &item : toolbar.items)
            {
                const bool fitsViewport = item.width >= 0.0f && item.height >= 0.0f &&
                    item.x >= viewport->Pos.x - 1.0f &&
                    item.x + item.width <= viewport->Pos.x + viewport->Size.x + 1.0f &&
                    item.y >= viewport->Pos.y - 1.0f &&
                    item.y + item.height <= viewport->Pos.y + toolbar.height + 1.0f;
                if (!fitsViewport)
                    throw std::runtime_error("toolbar item " + item.name + " at " +
                        std::to_string(width) + "x" + std::to_string(height) + " scale " +
                        std::to_string(scale) + " has bounds " + std::to_string(item.x) + "," +
                        std::to_string(item.y) + " + " + std::to_string(item.width) + "x" +
                        std::to_string(item.height) + " outside viewport or toolbar height " +
                        std::to_string(toolbar.height));
            }
            for (std::size_t idx = 1; idx < toolbar.items.size(); ++idx)
            {
                const auto &prev = toolbar.items[idx - 1];
                const auto &curr = toolbar.items[idx];
                require(prev.x + prev.width <= curr.x + 1.0f,
                        "toolbar items must not overlap horizontally: " + prev.name + " vs " + curr.name);
            }
            const auto runItem = std::find_if(toolbar.items.begin(), toolbar.items.end(),
                [](const auto &item) { return item.name == "Build and Run"; });
            require(runItem != toolbar.items.end(), "playback toolbar must expose Build and Run");
            pushOpenSceneClick(*runItem, width, height, scale);
        }
        require(openSceneActivations == 5,
                "clicking Build and Run at each supported toolbar size/scale must invoke its action callback");
        require(preview.target() != nullptr && preview.object_info(10) != nullptr,
                "Scene panel must compose its preview texture in the editor frame");
        require(document.root[1]["position"]["x"] == 30.0,
                "opening and drawing Scene navigation must not mutate saved object/camera data");

        SDL_SetWindowSize(window, 1280, 800);
        bool pathEntryOpen = true;
        std::array<char, 128> manualPath{};
        require(imgui.draw_path_entry_frame(pathEntryOpen, "native provider failed",
                                            manualPath.data(), manualPath.size()) == FileDialogPathAction::None &&
                    pathEntryOpen && ImGui::GetCurrentContext()->OpenPopupStack.Size == 1,
                "native dialog failure must show the path entry popup");
        SDL_Event escapeEvent{};
        escapeEvent.type = SDL_EVENT_KEY_DOWN;
        escapeEvent.key.windowID = SDL_GetWindowID(window);
        escapeEvent.key.scancode = SDL_SCANCODE_ESCAPE;
        escapeEvent.key.key = SDLK_ESCAPE;
        escapeEvent.key.mod = SDL_KMOD_NONE;
        escapeEvent.key.down = true;
        require(SDL_PushEvent(&escapeEvent), "SDL must queue Escape for the path-entry popup");
        require(imgui.draw_path_entry_frame(pathEntryOpen, "native provider failed",
                                            manualPath.data(), manualPath.size()) == FileDialogPathAction::Cancel &&
                    !pathEntryOpen,
                "Escape must cancel path entry and clear its open state");
        SDL_Event escapeRelease{};
        escapeRelease.type = SDL_EVENT_KEY_UP;
        escapeRelease.key.windowID = SDL_GetWindowID(window);
        escapeRelease.key.scancode = SDL_SCANCODE_ESCAPE;
        escapeRelease.key.key = SDLK_ESCAPE;
        escapeRelease.key.mod = SDL_KMOD_NONE;
        require(SDL_PushEvent(&escapeRelease), "SDL must queue Escape release");
        imgui.draw_path_entry_frame(pathEntryOpen, "native provider failed",
                                    manualPath.data(), manualPath.size());
        require(ImGui::GetCurrentContext()->OpenPopupStack.Size == 0,
                "canceling path entry must close the actual ImGui popup instead of reopening a stuck modal");

        // T069: Verify GamePanel::draw(nullptr, true) renders compact Aspect/Scale bar in idle state.
        GamePanel gamePanel;
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(640.0f, 400.0f));
        ImGui::Begin("Game");
        gamePanel.draw(nullptr, true);
        ImGuiWindow *gameWindow = ImGui::GetCurrentWindow();
        require(gameWindow != nullptr && gameWindow->DC.CursorMaxPos.y > gameWindow->Pos.y,
                "GamePanel::draw in idle state must render the compact Aspect/Scale controls");
        ImGui::End();
        ImGui::Render();
    }
    catch (const std::exception &error)
    {
        std::cerr << "platformator_editor_startup_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
