#include "test_support.h"
#include "document.h"
#include "editcommands.h"
#include "preview.h"
#include "panelwindow.h"
#include "selection.h"
#include "viewport.h"
#include "style.h"
#include "preferences.h"
#include "toolbar.h"
#include "filedialogs.h"

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
        scenePanel.draw(document, preview, selection, commands, window, renderer_);
        drawPanel("Inspector###panel.inspector");
        drawPanel("Behaviors###panel.behaviors");
        drawPanel("Project and Assets###panel.project");
        drawPanel("Console###panel.console");
        drawPanel("Build and Run###panel.run");
        drawPanel("Validation###panel.validation");
        drawPanel("Editor Settings###panel.settings");
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
        state.canUndo = true;
        state.sceneLabel = "long-example-scene-name.scene";
        state.scenePath = "/tmp/example project/long-example-scene-name.scene";
        state.status = std::string(400, 'd');
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
} // namespace

int main()
{
    try
    {
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
        const std::string seed{std::istreambuf_iterator<char>(seedStream),
                               std::istreambuf_iterator<char>()};
        require(!seed.empty(), "actual docking acceptance must load the checked-in seed");
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
                    dockId("Behaviors###panel.behaviors") == 0x00003001 &&
                    dockId("Project and Assets###panel.project") == 0x00004000 &&
                    dockId("Console###panel.console") == 0x00004001,
                "first actual frames must attach each required window to its intended seeded dock node");
        // Reset Layout reloads the same seed into the active context; verify it at 150% too.
        ImGui::LoadIniSettingsFromMemory(seed.data(), seed.size());
        apply_editor_style(1.5f);
        imgui.draw_one_frame(document, preview, selection, commands, scenePanel, window);
        imgui.draw_one_frame(document, preview, selection, commands, scenePanel, window);
        require(dockId("Scene###panel.scene") == 0x00003000 &&
                    dockId("Hierarchy###panel.hierarchy") == 0x00002000 &&
                    dockId("Project and Assets###panel.project") == 0x00004000,
                "reset seeded docking must remain attached after real frames at 150% UI scale");
        imgui.draw_one_frame(document, preview, selection, commands, scenePanel, window, false);
        const auto isActive = [](const char *name) {
            const auto *found = ImGui::FindWindowByName(name);
            return found != nullptr && found->Active;
        };
        require(!isActive("Hierarchy###panel.hierarchy") && !isActive("Scene###panel.scene") &&
                    !isActive("Inspector###panel.inspector") && !isActive("Behaviors###panel.behaviors") &&
                    !isActive("Project and Assets###panel.project") && !isActive("Console###panel.console") &&
                    !isActive("Build and Run###panel.run") && !isActive("Validation###panel.validation") &&
                    !isActive("Editor Settings###panel.settings"),
                "a real hidden-panel frame must not submit the windows to ImGui");
        imgui.draw_one_frame(document, preview, selection, commands, scenePanel, window, true);
        require(isActive("Hierarchy###panel.hierarchy") && isActive("Scene###panel.scene") &&
                    isActive("Inspector###panel.inspector") && isActive("Behaviors###panel.behaviors") &&
                    isActive("Project and Assets###panel.project") && isActive("Console###panel.console") &&
                    isActive("Build and Run###panel.run") && isActive("Validation###panel.validation") &&
                    isActive("Editor Settings###panel.settings"),
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
        int openSceneActivations = 0;
        EditorToolbarActions toolbarActions;
        toolbarActions.openScene = [&] { ++openSceneActivations; };
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
                                                   std::tuple{480, 320, 1.5f}})
        {
            const auto toolbar = imgui.draw_toolbar_frame(window, width, height, scale, toolbarActions);
            const auto *viewport = ImGui::GetMainViewport();
            const auto hasItem = [&](std::string_view name) {
                return std::find_if(toolbar.items.begin(), toolbar.items.end(), [&](const auto &item) {
                    return item.name == name;
                }) != toolbar.items.end();
            };
            require(hasItem("Open Scene") && hasItem("Save") && hasItem("Build") &&
                        hasItem("Build and Run") && hasItem("Stop") && hasItem("More"),
                    "toolbar must keep primary scene/build actions and a reachable overflow at supported sizes");
            for (const auto &item : toolbar.items)
                require(item.width >= 0.0f && item.height >= 0.0f && item.x >= viewport->Pos.x - 1.0f &&
                            item.x + item.width <= viewport->Pos.x + viewport->Size.x + 1.0f,
                        "toolbar controls and status must remain inside the actual viewport bounds");
            const auto openSceneItem = std::find_if(toolbar.items.begin(), toolbar.items.end(),
                [](const auto &item) { return item.name == "Open Scene"; });
            require(openSceneItem != toolbar.items.end(),
                    "Open Scene toolbar action must have an actual clickable item");
            pushOpenSceneClick(*openSceneItem, width, height, scale);
        }
        require(openSceneActivations == 3,
                "clicking Open Scene at each supported toolbar size/scale must invoke its action callback");
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
    }
    catch (const std::exception &error)
    {
        std::cerr << "platformator_editor_startup_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
