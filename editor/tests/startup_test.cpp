#include "test_support.h"
#include "document.h"
#include "editcommands.h"
#include "preview.h"
#include "selection.h"
#include "viewport.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <platformator/runtime.h>

#include <iostream>
#include <stdexcept>
#include <string>

using namespace platformator_editor_test;
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
                        SceneViewportPanel &scenePanel, SDL_Window *window)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            ImGui_ImplSDL3_ProcessEvent(&event);
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("startup smoke");
        ImGui::TextUnformatted("startup and orderly shutdown");
        ImGui::End();
        ImGui::SetNextWindowSize(ImVec2(480.0f, 360.0f), ImGuiCond_Always);
        scenePanel.draw(document, preview, selection, commands, window, renderer_);
        ImGui::Render();
        if (!SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255) || !SDL_RenderClear(renderer_))
            throw std::runtime_error(std::string("SDL renderer setup failed: ") + SDL_GetError());
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
        SDL_RenderPresent(renderer_);
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
        imgui.draw_one_frame(document, preview, selection, commands, scenePanel, window);
        require(preview.target() != nullptr && preview.object_info(10) != nullptr,
                "Scene panel must compose its preview texture in the editor frame");
        require(document.root[1]["position"]["x"] == 30.0,
                "opening and drawing Scene navigation must not mutate saved object/camera data");
    }
    catch (const std::exception &error)
    {
        std::cerr << "platformator_editor_startup_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
