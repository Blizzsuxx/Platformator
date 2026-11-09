#include "windowhost.h"
#include "windowhost_testhooks.h"
#include "uitextures.h"
#include "test_support.h"
#include "backends/imgui_impl_sdl3_editor.h"

#include <imgui.h>

#include <array>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <stdexcept>

using namespace platformator_editor;
using namespace platformator_editor_test;

int main()
{
    try
    {
        WindowHostRegistry hosts;
        const auto mainHostId = hosts.main_host();
        const auto main = hosts.find(mainHostId);
        require(main && main->isMain && main->layoutId == main_host_layout_id &&
                    main->generation == WindowHostGeneration{1} &&
                    main->rendererGeneration == RendererGeneration{1},
                "host registry must begin with one stable main layout and a fresh native incarnation");

        std::map<std::string, HostLayoutId> panels{
            {"panel.hierarchy", mainHostId},
            {"panel.scene", mainHostId},
            {"panel.project", mainHostId}};
        const auto detached = hosts.create_host();
        require(detached && !detached->isMain && detached->layoutId == HostLayoutId{2} &&
                    detached->generation > main->generation &&
                    detached->rendererGeneration > main->rendererGeneration,
                "detaching a panel host must allocate a persistent layout ID and independent native generations");
        require(hosts.move_panel("panel.project", detached->layoutId, panels) &&
                    panels.at("panel.project") == detached->layoutId &&
                    hosts.move_panel("panel.hierarchy", detached->layoutId, panels) &&
                    panels.at("panel.hierarchy") == detached->layoutId &&
                    panels.at("panel.scene") == mainHostId,
                "panel moves must update only the requested persistent host assignments");
        require(!hosts.move_panel("panel.missing", detached->layoutId, panels) &&
                    !hosts.move_panel("panel.scene", HostLayoutId{999}, panels) &&
                    !hosts.move_panel("", mainHostId, panels),
                "unknown panels, unavailable destinations, and empty panel IDs must fail without changing assignments");

        require(hosts.matches(detached->layoutId, detached->generation,
                              detached->rendererGeneration),
                "the active host incarnation must match its current window and renderer generations");
        require(hosts.close_host(detached->layoutId, panels) &&
                    panels.at("panel.project") == mainHostId &&
                    panels.at("panel.hierarchy") == mainHostId &&
                    !hosts.find(detached->layoutId) &&
                    !hosts.matches(detached->layoutId, detached->generation,
                                  detached->rendererGeneration),
                "closing or losing a detached host must redock its panels and retire its native incarnation");
        require(!hosts.close_host(mainHostId, panels),
                "the main authoring host cannot be removed from the registry");

        const std::array<SDL_Rect, 1> currentDisplay{SDL_Rect{0, 0, 1920, 1080}};
        const auto offscreenHost = recover_host_geometry(
            NativeHostGeometry{5400, 3300, 960, 640, true}, currentDisplay);
        require(offscreenHost.x == SDL_WINDOWPOS_CENTERED &&
                    offscreenHost.y == SDL_WINDOWPOS_CENTERED && !offscreenHost.maximized &&
                    offscreenHost.width == 960 && offscreenHost.height == 640,
                "a saved host position on a removed display must recover to a centered usable window");
        const auto visibleHost = recover_host_geometry(
            NativeHostGeometry{50, 60, 960, 640, false}, currentDisplay);
        require(visibleHost.x == 50 && visibleHost.y == 60,
                "host recovery must preserve saved geometry that still overlaps a display");

        const auto restored = hosts.restore_host(detached->layoutId);
        require(restored && restored->layoutId == detached->layoutId &&
                    restored->generation > detached->generation &&
                    restored->rendererGeneration > detached->rendererGeneration &&
                    !hosts.restore_host(detached->layoutId),
                "restoring a persistent host layout must allocate new native generations without duplicate hosts");
        require(hosts.recreate_renderer(restored->layoutId),
                "a renderer recovery must create a new context and renderer incarnation");
        const auto recovered = hosts.find(restored->layoutId);
        require(recovered && recovered->layoutId == restored->layoutId &&
                    recovered->generation == restored->generation &&
                    recovered->rendererGeneration > restored->rendererGeneration &&
                    !hosts.matches(restored->layoutId, restored->generation,
                                  restored->rendererGeneration),
                "renderer recovery must preserve the window incarnation and reject stale renderer work");

        require(hosts.close_host(recovered->layoutId, panels),
                "a recovered detached host must remain removable");
        std::uint64_t largestLayoutId = 0;
        std::optional<WindowHostRecord> lastDetached;
        while (hosts.size() < WindowHostRegistry::MaximumHosts)
        {
            const auto created = hosts.create_host();
            require(created.has_value(), "host creation must succeed below the configured host limit");
            largestLayoutId = created->layoutId.value;
            lastDetached = created;
        }
        require(hosts.size() == WindowHostRegistry::MaximumHosts && !hosts.create_host(),
                "the initial resource limit must cap the registry at eight native hosts");
        require(lastDetached.has_value(), "the full registry must contain at least one detachable host");
        const auto toClose = lastDetached->layoutId;
        require(toClose != mainHostId && hosts.close_host(toClose, panels),
                "one detached host can close when the registry is at capacity");
        const auto afterClose = hosts.create_host();
        require(afterClose && hosts.size() == WindowHostRegistry::MaximumHosts &&
                    afterClose->layoutId.value > largestLayoutId,
                "closing a host must free capacity without reusing a persistent layout ID");

        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS),
                std::string("SDL video initialization failed: ") + SDL_GetError());
        ImGuiContext *baseContext = ImGui::CreateContext();
        std::uint32_t firstWindowId = 0;
        std::uint32_t secondWindowId = 0;
        {
            WindowHostRegistry surfaceRegistry;
            const auto firstIdentity = surfaceRegistry.create_host();
            const auto secondIdentity = surfaceRegistry.create_host();
            require(firstIdentity && secondIdentity,
                    "two independent detached surface identities must be available");
            SdlWindowHost first(*firstIdentity, "First detached host",
                                NativeHostGeometry{40, 50, 720, 480, false});
            require(ImGui::GetCurrentContext() == baseContext,
                    "constructing a detached host must restore the previously active ImGui context");
            SdlWindowHost second(*secondIdentity, "Second detached host",
                                 NativeHostGeometry{80, 90, 640, 420, false});
            firstWindowId = first.window_id();
            secondWindowId = second.window_id();
            require(ImGui::GetCurrentContext() == baseContext && first.window_id() != second.window_id() &&
                        first.renderer() != second.renderer() && first.context() != second.context() &&
                        ImGui_ImplSDL3_EditorContextForWindow(first.window()) == first.context() &&
                        ImGui_ImplSDL3_EditorContextForWindow(second.window()) == second.context(),
                    "each detached host must own an independent SDL window and renderer");

            SDL_Texture *sceneSource = SDL_CreateTexture(first.renderer(), SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_TARGET, 8, 8);
            require(sceneSource != nullptr,
                    std::string("could not create source Scene target: ") + SDL_GetError());
            SceneTextureReplicas sceneReplicas;
            const auto paint_source = [&](Uint8 r, Uint8 g, Uint8 b) {
                require(SDL_SetRenderTarget(first.renderer(), sceneSource) &&
                            SDL_SetRenderDrawColor(first.renderer(), r, g, b, 255) &&
                            SDL_RenderClear(first.renderer()) &&
                            SDL_SetRenderTarget(first.renderer(), nullptr),
                        std::string("could not paint source Scene target: ") + SDL_GetError());
            };
            const auto read_replica_pixel = [&](SDL_Texture *replica) {
                require(replica != nullptr && replica != sceneSource,
                        "detached Scene must use a renderer-local texture replica");
                const SDL_FRect destination{0.0f, 0.0f, 8.0f, 8.0f};
                require(SDL_SetRenderTarget(second.renderer(), nullptr) &&
                            SDL_SetRenderDrawColor(second.renderer(), 0, 0, 0, 255) &&
                            SDL_RenderClear(second.renderer()) &&
                            SDL_RenderTexture(second.renderer(), replica, nullptr, &destination),
                        std::string("could not draw detached Scene replica: ") + SDL_GetError());
                const SDL_Rect bounds{0, 0, 1, 1};
                SDL_Surface *pixel = SDL_RenderReadPixels(second.renderer(), &bounds);
                require(pixel != nullptr, std::string("could not read detached Scene pixel: ") + SDL_GetError());
                Uint8 rgba[4]{};
                const bool converted = SDL_ConvertPixels(1, 1, pixel->format, pixel->pixels,
                    pixel->pitch, SDL_PIXELFORMAT_RGBA32, rgba, sizeof(rgba));
                SDL_DestroySurface(pixel);
                require(converted, std::string("could not convert detached Scene pixel: ") + SDL_GetError());
                return std::array<Uint8, 4>{rgba[0], rgba[1], rgba[2], rgba[3]};
            };
            paint_source(240, 20, 10);
            SDL_Texture *redReplica = sceneReplicas.texture_for(first.renderer(), sceneSource, 1,
                second.renderer(), 8, 8);
            require(redReplica != nullptr && sceneReplicas.allocated_bytes() <=
                        SceneTextureReplicas::MaxTransferBytes,
                    "Scene readback, staging, and renderer-local replica must stay within 64 MiB");
            const auto redPixel = read_replica_pixel(redReplica);
            require(redPixel[0] > 180 && redPixel[1] < 80 && redPixel[2] < 80,
                    "detached Scene replica must contain the rendered source pixels");
            paint_source(10, 30, 230);
            SDL_Texture *blueReplica = sceneReplicas.texture_for(first.renderer(), sceneSource, 2,
                second.renderer(), 8, 8);
            const auto bluePixel = read_replica_pixel(blueReplica);
            require(blueReplica == redReplica && bluePixel[0] < 80 && bluePixel[1] < 100 &&
                        bluePixel[2] > 180,
                    "a changed Scene revision must update the cached renderer-local replica");
            sceneReplicas.release_renderer(second.renderer());
            require(sceneReplicas.allocated_bytes() == 0,
                    "closing a detached renderer must release its transfer and replica allocations");
            SDL_DestroyTexture(sceneSource);

            const auto failedIdentity = surfaceRegistry.create_host();
            require(failedIdentity.has_value(), "failure injection must have a detached host identity");
            std::map<std::string, HostLayoutId> failedHostPanels{
                {"panel.console", failedIdentity->layoutId}};
            const auto nativeWindowCount = [] {
                int count = 0;
                SDL_Window **windows = SDL_GetWindows(&count);
                SDL_free(windows);
                return count;
            };
            const int windowsBeforeFailure = nativeWindowCount();
            set_window_host_failure_point_for_testing(WindowHostFailurePoint::RendererCreation);
            bool rendererFailureReported = false;
            try
            {
                SdlWindowHost failed(*failedIdentity, "Injected renderer failure");
            }
            catch (const std::runtime_error &)
            {
                rendererFailureReported = true;
            }
            set_window_host_failure_point_for_testing(WindowHostFailurePoint::None);
            require(rendererFailureReported && nativeWindowCount() == windowsBeforeFailure,
                    "renderer creation failure must destroy the partially-created SDL window");
            require(surfaceRegistry.close_host(failedIdentity->layoutId, failedHostPanels) &&
                        failedHostPanels.at("panel.console") == surfaceRegistry.main_host(),
                    "renderer creation failure must permit registry recovery and redock assigned panels");

            first.begin_frame();
            ImGui::Begin("First host panel");
            ImGui::TextUnformatted("First context contents");
            ImGui::End();
            require(ImGui::GetCurrentContext() != baseContext,
                    "begin_frame must activate the selected host's independent ImGui context");
            first.present();
            require(ImGui::GetCurrentContext() == baseContext,
                    "present must restore the context that was active before the host frame");

            second.begin_frame();
            ImGui::Begin("Second host panel");
            ImGui::TextUnformatted("Second context contents");
            ImGui::End();
            second.present();
            const auto firstLayout = first.save_layout_settings();
            const auto secondLayout = second.save_layout_settings();
            require(firstLayout.find("First host panel") != std::string::npos &&
                        firstLayout.find("Second host panel") == std::string::npos &&
                        secondLayout.find("Second host panel") != std::string::npos &&
                        secondLayout.find("First host panel") == std::string::npos,
                    "each host must serialize an isolated per-context docking/settings blob");

            SDL_Event routed{};
            routed.type = SDL_EVENT_MOUSE_MOTION;
            routed.motion.windowID = first.window_id();
            routed.motion.x = 12.0f;
            routed.motion.y = 14.0f;
            require(first.process_event(routed) && !second.process_event(routed) &&
                        ImGui::GetCurrentContext() == baseContext,
                    "SDL input must route only to the owning host context and restore the active context");
            routed.motion.windowID = second.window_id();
            require(!first.process_event(routed) && second.process_event(routed),
                    "input addressed to another host must not leak into the first host context");

            SDL_Event key{};
            key.type = SDL_EVENT_KEY_DOWN;
            key.key.windowID = first.window_id();
            key.key.key = SDLK_F4;
            key.key.scancode = SDL_SCANCODE_F4;
            key.key.mod = SDL_KMOD_NONE;
            key.key.repeat = false;
            require(first.process_event(key), "keyboard events must route to their owning host");
            first.begin_frame();
            const bool firstReceivedKey = ImGui::IsKeyPressed(ImGuiKey_F4);
            first.present();
            second.begin_frame();
            const bool secondReceivedKey = ImGui::IsKeyPressed(ImGuiKey_F4);
            second.present();
            require(firstReceivedKey && !secondReceivedKey,
                    "focused-host keyboard input must stay inside its owning ImGui context");

            ImGuiPlatformImeData imeData{};
            imeData.WantTextInput = true;
            ImGui::SetCurrentContext(first.context());
            auto setImeData = ImGui::GetPlatformIO().Platform_SetImeDataFn;
            require(setImeData != nullptr, "each host context must register its IME backend callback");
            setImeData(first.context(), ImGui::GetMainViewport(), &imeData);
            ImGui_ImplSDL3_EditorTestUpdateImeForWindow(first.window());
            require(ImGui_ImplSDL3_EditorTestImeOwner() == first.window(),
                    "the focused host must acquire the shared SDL text-input owner");
            ImGui::SetCurrentContext(second.context());
            setImeData(second.context(), ImGui::GetMainViewport(), &imeData);
            ImGui_ImplSDL3_EditorTestUpdateImeForWindow(second.window());
            require(ImGui_ImplSDL3_EditorTestImeOwner() == second.window(),
                    "IME ownership must transfer to the newly focused host");
            imeData.WantTextInput = false;
            setImeData(second.context(), ImGui::GetMainViewport(), &imeData);
            ImGui_ImplSDL3_EditorTestUpdateImeForWindow(second.window());
            require(ImGui_ImplSDL3_EditorTestImeOwner() == nullptr,
                    "the current IME owner must release shared text input when editing ends");
            ImGui::SetCurrentContext(baseContext);
            SDL_Event display{};
            display.type = SDL_EVENT_DISPLAY_ADDED;
            display.display.displayID = 1;
            require(first.process_event(display) && second.process_event(display),
                    "display lifecycle events must reach every host context");
        }
        require(SDL_GetWindowFromID(firstWindowId) == nullptr &&
                    SDL_GetWindowFromID(secondWindowId) == nullptr &&
                    ImGui_ImplSDL3_EditorContextForWindow(SDL_GetWindowFromID(firstWindowId)) == nullptr,
                "host teardown must unregister backend window ownership and destroy both native windows");
        ImGui::DestroyContext(baseContext);
        SDL_Quit();

        std::cout << "window host registry checks passed\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
