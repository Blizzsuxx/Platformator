#pragma once

#include "sessionids.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <memory>

struct ImGuiContext;

namespace platformator_editor
{
struct WindowHostRecord
{
    HostLayoutId layoutId;
    WindowHostGeneration generation;
    RendererGeneration rendererGeneration;
    bool isMain = false;

    bool operator==(const WindowHostRecord &) const = default;
};

// Tracks persistent layout identity separately from native window and renderer
// incarnations. SDL/ImGui ownership is layered on this registry by the app.
class WindowHostRegistry
{
  public:
    static constexpr std::size_t MaximumHosts = 8;

    WindowHostRegistry();
    WindowHostRegistry(const WindowHostRegistry &) = delete;
    WindowHostRegistry &operator=(const WindowHostRegistry &) = delete;

    HostLayoutId main_host() const noexcept;
    std::size_t size() const noexcept;
    std::optional<WindowHostRecord> find(HostLayoutId layoutId) const noexcept;

    std::optional<WindowHostRecord> create_host();
    std::optional<WindowHostRecord> restore_host(HostLayoutId layoutId);
    bool move_panel(std::string_view panelId, HostLayoutId destination,
                    std::map<std::string, HostLayoutId> &panelAssignments) const;
    bool close_host(HostLayoutId layoutId,
                    std::map<std::string, HostLayoutId> &panelAssignments);
    bool recreate_renderer(HostLayoutId layoutId);
    bool matches(HostLayoutId layoutId, WindowHostGeneration generation,
                 RendererGeneration rendererGeneration) const noexcept;

  private:
    std::optional<HostLayoutId> allocate_layout_id() noexcept;
    void observe_restored_layout_id(HostLayoutId layoutId) noexcept;
    std::optional<WindowHostRecord> make_record(HostLayoutId layoutId, bool isMain) noexcept;

    const HostLayoutId mainHost_ = main_host_layout_id;
    std::uint64_t nextLayoutId_ = 2;
    bool layoutIdsExhausted_ = false;
    MonotonicIdentitySource<WindowHostGeneration> generations_{1};
    MonotonicIdentitySource<RendererGeneration> rendererGenerations_{1};
    std::map<HostLayoutId, WindowHostRecord> hosts_;
};

struct NativeHostGeometry
{
    int x = SDL_WINDOWPOS_CENTERED;
    int y = SDL_WINDOWPOS_CENTERED;
    int width = 960;
    int height = 640;
    bool maximized = false;
};

NativeHostGeometry recover_host_geometry(NativeHostGeometry requested,
                                          std::span<const SDL_Rect> usableDisplays) noexcept;

// Owns one detached SDL window, renderer, ImGui context, and their backend
// resources. The main Runtime window remains owned by Runtime.
class SdlWindowHost
{
  public:
    SdlWindowHost(WindowHostRecord identity, std::string title,
                  NativeHostGeometry geometry = {}, std::string layoutSettings = {});
    SdlWindowHost(const SdlWindowHost &) = delete;
    SdlWindowHost &operator=(const SdlWindowHost &) = delete;
    SdlWindowHost(SdlWindowHost &&) noexcept;
    SdlWindowHost &operator=(SdlWindowHost &&) noexcept;
    ~SdlWindowHost();

    const WindowHostRecord &identity() const noexcept;
    SDL_Window *window() const noexcept;
    SDL_Renderer *renderer() const noexcept;
    ::ImGuiContext *context() const noexcept;
    std::uint32_t window_id() const noexcept;
    bool process_event(const SDL_Event &event);
    void begin_frame();
    void present();
    void show();
    NativeHostGeometry geometry() const noexcept;
    std::string save_layout_settings();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platformator_editor
