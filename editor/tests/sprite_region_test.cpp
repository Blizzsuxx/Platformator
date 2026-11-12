#include "panels/spriteeditor.h"
#include "panels/inspector.h"

#include "document.h"
#include "resourceindex.h"
#include "selection.h"
#include "test_support.h"
#include "thumbnailcache.h"
#include "assetdocument.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <imgui.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace platformator_editor;
using namespace platformator_editor_test;

int main()
{
    try
    {
        SpriteSourceRegion region{4.0, 2.0, 8.0, 6.0};
        std::string error;
        require(validate_sprite_source_region(region, 32, 16, error),
                "finite positive regions inside original source dimensions must validate");
        require(!validate_sprite_source_region({-1.0, 0.0, 2.0, 2.0}, 32, 16, error) &&
                    !validate_sprite_source_region({0.0, 0.0, 0.0, 2.0}, 32, 16, error) &&
                    !validate_sprite_source_region({28.0, 0.0, 8.0, 2.0}, 32, 16, error) &&
                    !validate_sprite_source_region({0.0, 15.0, 2.0, 2.0}, 32, 16, error) &&
                    !validate_sprite_source_region({0.0, 0.0, std::numeric_limits<double>::infinity(), 1.0},
                                                   32, 16, error) &&
                    !validate_sprite_source_region({0.0, 0.0, 1.0, 1.0}, 0, 16, error),
                "negative, empty, nonfinite, out-of-image, or unavailable bounds must reject");

        DocumentSession document = make_default_document("/project", "/project/assets", 7);
        auto &components = document.root[0]["components"];
        components.push_back(Json{{"id", 21}, {"type", 6},
                                  {"textureFilePath", "assets/old.png"},
                                  {"sourceRectEnabled", false},
                                  {"sourceRect", Json{{"x", 0.0}, {"y", 0.0},
                                                       {"w", 4.0}, {"h", 4.0},
                                                       {"vendorMetadata", "retain"}}}});
        ++document.structureRevision;
        rebuild_document_indexes(document);
        const Json before = document.root;
        EditCommandService commands;
        auto rejected = commit_sprite_source_region(commands, document, 0, 21,
            "assets/sprites/hero.png", {28.0, 0.0, 8.0, 2.0}, 32, 16);
        require(rejected.status == CommandStatus::Rejected && document.root == before &&
                    commands.history_status().undoCommands == 0,
                "invalid source regions must not dirty the document or create history");
        rejected = commit_sprite_source_region(commands, document, 0, 21,
            "assets/../outside.png", region, 32, 16);
        require(rejected.status == CommandStatus::Rejected && document.root == before,
                "a source region cannot assign a texture outside the project asset root");

        const auto committed = commit_sprite_source_region(commands, document, 0, 21,
            "assets/sprites/hero.png", region, 32, 16);
        require(committed.status == CommandStatus::Changed &&
                    commands.history_status().undoCommands == 1 &&
                    document.root[0]["components"][1]["textureFilePath"] == "assets/sprites/hero.png" &&
                    document.root[0]["components"][1]["sourceRectEnabled"] == true &&
                    document.root[0]["components"][1]["sourceRect"] ==
                        Json{{"x", 4.0}, {"y", 2.0}, {"w", 8.0}, {"h", 6.0},
                             {"vendorMetadata", "retain"}},
                "texture assignment and source pixels must commit together without losing unknown rectangle fields");

        SelectionModel selection;
        const auto undone = commands.undo(document, selection);
        require(undone && document.root == before && commands.history_status().canRedo,
                "one Undo must restore the exact prior texture and source rectangle");
        const auto redone = commands.redo(document, selection);
        require(redone && document.root[0]["components"][1]["sourceRect"]["w"] == 8.0 &&
                    document.root[0]["components"][1]["textureFilePath"] == "assets/sprites/hero.png",
                "Redo must restore the committed image-pixel region exactly");

        const auto projectRoot = std::filesystem::temp_directory_path() /
                                 "platformator-animation-region-test";
        std::filesystem::remove_all(projectRoot);
        const auto assetRoot = projectRoot / "assets";
        std::filesystem::create_directories(assetRoot);
        {
            std::ofstream image(assetRoot / "sheet.png", std::ios::binary);
            image << "fixture";
        }
        const Json animationBefore = {
            {"frames", Json::array({{{"duration", 0.2},
                                     {"textureWrapperFilePath", "assets/old.png"},
                                     {"sourceRect", {{"x", 0}, {"y", 0}, {"w", 2}, {"h", 2},
                                                      {"vendor", "retain"}}}}})},
            {"loop", true}, {"vendorClip", 9}};
        AssetDocument animation(animationBefore);
        const auto invalidRegion = commit_animation_frame_source_region(
            {animation, 0}, "assets/sheet.png", assetRoot, {28, 0, 8, 2}, 32, 16);
        require(!invalidRegion.error.empty() && animation.root() == animationBefore &&
                    !animation.can_undo(),
                "invalid frame regions must preserve the asset and its history");
        const auto invalidTexture = commit_animation_frame_source_region(
            {animation, 0}, "assets/../outside.png", assetRoot, region, 32, 16);
        require(!invalidTexture.error.empty() && animation.root() == animationBefore,
                "frame regions must reject paths outside the project asset root");
        const auto frameCommit = commit_animation_frame_source_region(
            {animation, 0}, "assets/sheet.png", assetRoot, region, 32, 16);
        require(frameCommit.changed && animation.can_undo() &&
                    animation.root()["frames"][0]["textureWrapperFilePath"] == "assets/sheet.png" &&
                    animation.root()["frames"][0]["hasSourceRect"] == true &&
                    animation.root()["frames"][0]["sourceRect"] ==
                        Json{{"x", 4.0}, {"y", 2.0}, {"w", 8.0}, {"h", 6.0},
                             {"vendor", "retain"}},
                "animation frame texture and image-pixel bounds must commit atomically and retain unknown fields");
        require(animation.undo().changed && animation.root() == animationBefore &&
                    animation.redo().changed && animation.root()["frames"][0]["sourceRect"]["w"] == 8.0,
                "one asset Undo/Redo must restore the exact prior and committed frame regions");
        std::filesystem::remove_all(projectRoot);

        // T060: Inspector Sprite source-region prepared PNG/BMP thumbnails, caching, drag/cancel, and real bounds
        {
            TemporaryDirectory temporary;
            const auto proj = temporary.path() / "project";
            const auto assets = proj / "assets";
            std::filesystem::create_directories(assets);
            const auto pngPath = assets / "sprite_png.png";
            const auto bmpPath = assets / "sprite_bmp.bmp";
            {
                SDL_Surface *pngSurf = SDL_CreateSurface(24, 12, SDL_PIXELFORMAT_RGBA32);
                require(pngSurf != nullptr &&
                            SDL_FillSurfaceRect(pngSurf, nullptr, SDL_MapSurfaceRGBA(pngSurf, 255, 0, 0, 255)) &&
                            IMG_SavePNG(pngSurf, pngPath.string().c_str()),
                        "PNG fixture must save");
                SDL_DestroySurface(pngSurf);
            }
            {
                SDL_Surface *bmpSurf = SDL_CreateSurface(18, 10, SDL_PIXELFORMAT_RGBA32);
                require(bmpSurf != nullptr &&
                            SDL_FillSurfaceRect(bmpSurf, nullptr, SDL_MapSurfaceRGBA(bmpSurf, 0, 255, 0, 255)) &&
                            SDL_SaveBMP(bmpSurf, bmpPath.string().c_str()),
                        "BMP fixture must save");
                SDL_DestroySurface(bmpSurf);
            }

            SDL_Window *window = nullptr;
            SDL_Renderer *renderer = nullptr;
            require(SDL_Init(SDL_INIT_VIDEO) &&
                        SDL_CreateWindowAndRenderer("sprite-region-test", 320, 240, SDL_WINDOW_HIDDEN,
                                                    &window, &renderer),
                    "headless SDL renderer must initialize for thumbnail preparation");
            SDL_Texture *pngTex = IMG_LoadTexture(renderer, pngPath.string().c_str());
            SDL_Texture *bmpTex = IMG_LoadTexture(renderer, bmpPath.string().c_str());
            require(pngTex != nullptr && bmpTex != nullptr, "test textures must load");

            ImGui::CreateContext();
            ImGuiIO &io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {800.0f, 900.0f};
            io.DeltaTime = 1.0f / 60.0f;
            io.Fonts->AddFontDefault();
            io.Fonts->Build();

            DocumentSession sceneDoc = make_default_document(proj, assets, 11);
            sceneDoc.root[0]["components"].push_back(Json{
                {"id", 21}, {"type", 6}, {"enabled", true},
                {"textureFilePath", "assets/sprite_png.png"},
                {"sourceRectEnabled", true},
                {"sourceRect", Json{{"x", 0.0}, {"y", 0.0}, {"w", 8.0}, {"h", 6.0}}}});
            ++sceneDoc.structureRevision;
            rebuild_document_indexes(sceneDoc);

            SelectionModel sceneSel;
            sceneSel.set(sceneDoc, Selection{.objectId = 0, .selectedObjectIds = {0}});
            EditCommandService sceneCmds;
            BehaviorCatalog emptyCatalog;
            ResourceIndex resources;
            resources.set_project(ProjectGeneration{11}, assets);

            int prepareCalls = 0;
            InspectorPanel inspector;
            inspector.set_texture_preparer([&](const std::filesystem::path &path) {
                ++prepareCalls;
                if (path == pngPath)
                    return InspectorPanel::PreparedTextureInfo{pngTex, 24, 12};
                if (path == bmpPath)
                    return InspectorPanel::PreparedTextureInfo{bmpTex, 18, 10};
                return InspectorPanel::PreparedTextureInfo{};
            });

            auto drawFrame = [&](ImVec2 mouse = {-10.0f, -10.0f}, bool down = false, bool escape = false) {
                io.AddMousePosEvent(mouse.x, mouse.y);
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
                if (escape)
                    io.AddKeyEvent(ImGuiKey_Escape, true);
                io.DeltaTime = 1.0f / 60.0f;
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({600.0f, 850.0f}, ImGuiCond_Always);
                ImGui::Begin("inspector-region-test", nullptr,
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
                inspector.draw(sceneDoc, sceneSel, sceneCmds, emptyCatalog, {}, &resources);
                ImGui::End();
                ImGui::Render();
                if (escape)
                    io.AddKeyEvent(ImGuiKey_Escape, false);
                return ImGui::GetHoveredID();
            };

            // T073: Inspector queries the texture preparer each visible frame so
            // ThumbnailCache pins the active renderer's replica until rendering completes.
            for (int frame = 0; frame < 5; ++frame)
                drawFrame();
            require(prepareCalls == 5,
                    "Inspector must query texture preparation each visible frame to pin the active texture");

            // Switch to BMP and verify preparation works for BMP too.
            const auto texAddr = address_for_document(sceneDoc, 0, 21, std::nullopt, {std::string("textureFilePath")});
            require(static_cast<bool>(sceneCmds.set_property(sceneDoc, texAddr, "assets/sprite_bmp.bmp")),
                    "switching Sprite texture to BMP must succeed");
            drawFrame();
            require(prepareCalls == 6, "switching to BMP must prepare the BMP thumbnail");

            // Locate ##sprite-source-canvas and test bounded mouse drag + release (1 Undo) and Escape cancel.
            drawFrame();
            const ImVec2 canvasMin = inspector.sprite_region_widget().last_canvas_min();
            const ImVec2 canvasMax = inspector.sprite_region_widget().last_canvas_max();
            require(canvasMax.x > canvasMin.x && canvasMax.y > canvasMin.y,
                    "Inspector Sprite source canvas must be rendered with non-empty bounds");
            const std::size_t undoBeforeDrag = sceneCmds.history_status().undoCommands;
            drawFrame({canvasMin.x + 10.0f, canvasMin.y + 10.0f}, true);
            drawFrame({canvasMin.x + 80.0f, canvasMin.y + 50.0f}, true);
            drawFrame({canvasMin.x + 80.0f, canvasMin.y + 50.0f}, false);
            require(sceneCmds.history_status().undoCommands == undoBeforeDrag + 1,
                    "releasing a bounded Inspector source-region drag must commit one Undo step");
            const Json committedRect = sceneDoc.root[0]["components"][1]["sourceRect"];
            require(committedRect["x"].get<double>() >= 0.0 &&
                        committedRect["w"].get<double>() > 0.0 &&
                        committedRect["x"].get<double>() + committedRect["w"].get<double>() <= 18.0 &&
                        committedRect["y"].get<double>() + committedRect["h"].get<double>() <= 10.0,
                    "dragged region must stay within the true 18x10 BMP bounds");

            // Drag + Escape must cancel without adding Undo history or changing sourceRect.
            drawFrame({canvasMin.x + 10.0f, canvasMin.y + 10.0f}, true);
            drawFrame({canvasMin.x + 40.0f, canvasMin.y + 30.0f}, true, true);
            drawFrame({canvasMin.x + 40.0f, canvasMin.y + 30.0f}, false);
            require(sceneCmds.history_status().undoCommands == undoBeforeDrag + 1 &&
                        sceneDoc.root[0]["components"][1]["sourceRect"] == committedRect,
                    "Escape during Inspector source-region drag must cancel without modifying document or history");

            // Undo must restore the prior rectangle cleanly.
            require(static_cast<bool>(sceneCmds.undo(sceneDoc, sceneSel)) &&
                        sceneCmds.history_status().undoCommands == undoBeforeDrag,
                    "one Undo must revert the Inspector source-region drag");
            sceneSel.set(sceneDoc, Selection{.objectId = 0, .selectedObjectIds = {0}});

            // Two-renderer & cache invalidation check via ThumbnailCache.
            SDL_Window *windowB = nullptr;
            SDL_Renderer *rendererB = nullptr;
            require(SDL_CreateWindowAndRenderer("sprite-region-test-b", 320, 240, SDL_WINDOW_HIDDEN,
                                                &windowB, &rendererB),
                    "second headless SDL renderer must initialize for two-renderer test");
            {
                ThumbnailCache cache;
                SDL_Renderer *activeRenderer = renderer;
                SDL_Texture *lastObservedTex = nullptr;
                int cachePreparerCalls = 0;
                inspector.set_texture_preparer([&](const std::filesystem::path &path) {
                    ++cachePreparerCalls;
                    SDL_Texture *tex = cache.request(path, activeRenderer);
                    cache.poll_uploads(activeRenderer);
                    tex = cache.find(path, activeRenderer);
                    const auto [w, h] = cache.source_dimensions(path);
                    lastObservedTex = tex;
                    return InspectorPanel::PreparedTextureInfo{tex, w, h};
                });

                SDL_Texture *texA = nullptr;
                for (int i = 0; i < 60 && texA == nullptr; ++i)
                {
                    drawFrame();
                    cache.end_frame();
                    texA = lastObservedTex;
                    if (texA == nullptr)
                        SDL_Delay(5);
                }
                require(texA != nullptr,
                        "ThumbnailCache must prepare replica for renderer A (calls=" +
                            std::to_string(cachePreparerCalls) + ", msg=" +
                            cache.failure_message(bmpPath) + ")");

                activeRenderer = rendererB;
                SDL_Texture *texB = nullptr;
                for (int i = 0; i < 60 && texB == nullptr; ++i)
                {
                    drawFrame();
                    cache.end_frame();
                    texB = lastObservedTex;
                    if (texB == nullptr)
                        SDL_Delay(5);
                }
                require(texB != nullptr && texB != texA,
                        "detached/second renderer must obtain its own ThumbnailCache replica");

                cache.release_renderer(renderer);
                require(cache.find(bmpPath, renderer) == nullptr &&
                            cache.find(bmpPath, rendererB) == texB,
                        "releasing renderer A must preserve renderer B's replica");

                cache.clear(rendererB);
                drawFrame();
                cache.end_frame();
                require(lastObservedTex == nullptr,
                        "clearing/evicting ThumbnailCache entry must immediately stop Inspector from seeing stale texture");
                cache.release_renderer(rendererB);
            }

            ImGui::DestroyContext();
            SDL_DestroyTexture(pngTex);
            SDL_DestroyTexture(bmpTex);
            SDL_DestroyRenderer(rendererB);
            SDL_DestroyWindow(windowB);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
        }

        std::cout << "sprite region tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "sprite region tests failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
