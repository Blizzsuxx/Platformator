#include "windowhost.h"
#include "windowhost_testhooks.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <imgui.h>
#include "backends/imgui_impl_sdl3_editor.h"
#include <backends/imgui_impl_sdlrenderer3.h>

namespace platformator_editor
{
NativeHostGeometry recover_host_geometry(NativeHostGeometry requested,
                                          std::span<const SDL_Rect> usableDisplays) noexcept
{
    for (const auto &display : usableDisplays)
    {
        if (display.w <= 0 || display.h <= 0)
            continue;
        const auto right = static_cast<long long>(requested.x) + requested.width;
        const auto bottom = static_cast<long long>(requested.y) + requested.height;
        const auto displayRight = static_cast<long long>(display.x) + display.w;
        const auto displayBottom = static_cast<long long>(display.y) + display.h;
        if (right > display.x && bottom > display.y && requested.x < displayRight &&
            requested.y < displayBottom)
            return requested;
    }
    if (!usableDisplays.empty())
    {
        requested.x = SDL_WINDOWPOS_CENTERED;
        requested.y = SDL_WINDOWPOS_CENTERED;
        requested.maximized = false;
    }
    return requested;
}

WindowHostRegistry::WindowHostRegistry()
{
    const auto main = make_record(mainHost_, true);
    if (!main)
        throw std::overflow_error("cannot allocate the main editor host identity");
    hosts_.emplace(mainHost_, *main);
}

HostLayoutId WindowHostRegistry::main_host() const noexcept
{
    return mainHost_;
}

std::size_t WindowHostRegistry::size() const noexcept
{
    return hosts_.size();
}

std::optional<WindowHostRecord> WindowHostRegistry::find(HostLayoutId layoutId) const noexcept
{
    const auto found = hosts_.find(layoutId);
    if (found == hosts_.end())
        return std::nullopt;
    return found->second;
}

std::optional<HostLayoutId> WindowHostRegistry::allocate_layout_id() noexcept
{
    if (layoutIdsExhausted_ || nextLayoutId_ == 0)
        return std::nullopt;
    const HostLayoutId layoutId{nextLayoutId_};
    if (nextLayoutId_ == std::numeric_limits<std::uint64_t>::max())
        layoutIdsExhausted_ = true;
    else
        ++nextLayoutId_;
    return layoutId;
}

void WindowHostRegistry::observe_restored_layout_id(HostLayoutId layoutId) noexcept
{
    if (layoutIdsExhausted_ || layoutId.value < nextLayoutId_)
        return;
    if (layoutId.value == std::numeric_limits<std::uint64_t>::max())
    {
        layoutIdsExhausted_ = true;
        return;
    }
    nextLayoutId_ = layoutId.value + 1;
}

std::optional<WindowHostRecord> WindowHostRegistry::make_record(HostLayoutId layoutId,
                                                                bool isMain) noexcept
{
    try
    {
        return WindowHostRecord{layoutId, generations_.next(), rendererGenerations_.next(), isMain};
    }
    catch (const std::overflow_error &)
    {
        return std::nullopt;
    }
}

std::optional<WindowHostRecord> WindowHostRegistry::create_host()
{
    if (hosts_.size() >= MaximumHosts)
        return std::nullopt;
    const auto layoutId = allocate_layout_id();
    if (!layoutId)
        return std::nullopt;
    const auto record = make_record(*layoutId, false);
    if (!record)
        return std::nullopt;
    hosts_.emplace(*layoutId, *record);
    return record;
}

std::optional<WindowHostRecord> WindowHostRegistry::restore_host(HostLayoutId layoutId)
{
    if (layoutId.value <= mainHost_.value || hosts_.size() >= MaximumHosts ||
        hosts_.contains(layoutId))
        return std::nullopt;
    const auto record = make_record(layoutId, false);
    if (!record)
        return std::nullopt;
    hosts_.emplace(layoutId, *record);
    observe_restored_layout_id(layoutId);
    return record;
}

bool WindowHostRegistry::move_panel(std::string_view panelId, HostLayoutId destination,
                                    std::map<std::string, HostLayoutId> &panelAssignments) const
{
    if (panelId.empty() || !hosts_.contains(destination))
        return false;
    const auto panel = panelAssignments.find(std::string(panelId));
    if (panel == panelAssignments.end())
        return false;
    panel->second = destination;
    return true;
}

bool WindowHostRegistry::close_host(HostLayoutId layoutId,
                                    std::map<std::string, HostLayoutId> &panelAssignments)
{
    if (layoutId == mainHost_)
        return false;
    const auto host = hosts_.find(layoutId);
    if (host == hosts_.end())
        return false;
    for (auto &[panelId, assignment] : panelAssignments)
    {
        (void)panelId;
        if (assignment == layoutId)
            assignment = mainHost_;
    }
    hosts_.erase(host);
    return true;
}

bool WindowHostRegistry::recreate_renderer(HostLayoutId layoutId)
{
    const auto host = hosts_.find(layoutId);
    if (host == hosts_.end())
        return false;
    RendererGeneration nextRenderer;
    try
    {
        nextRenderer = rendererGenerations_.next();
    }
    catch (const std::overflow_error &)
    {
        return false;
    }
    host->second.rendererGeneration = nextRenderer;
    return true;
}

bool WindowHostRegistry::matches(HostLayoutId layoutId, WindowHostGeneration generation,
                                 RendererGeneration rendererGeneration) const noexcept
{
    const auto host = hosts_.find(layoutId);
    return host != hosts_.end() && host->second.generation == generation &&
           host->second.rendererGeneration == rendererGeneration;
}

namespace
{
std::mutex windowHostFailureMutex;
WindowHostFailurePoint windowHostFailurePoint = WindowHostFailurePoint::None;

bool consume_window_host_failure(WindowHostFailurePoint point) noexcept
{
    std::lock_guard lock(windowHostFailureMutex);
    if (windowHostFailurePoint != point)
        return false;
    windowHostFailurePoint = WindowHostFailurePoint::None;
    return true;
}

class CurrentContextScope
{
  public:
    explicit CurrentContextScope(ImGuiContext *context) noexcept
        : previous_(ImGui::GetCurrentContext())
    {
        ImGui::SetCurrentContext(context);
    }

    ~CurrentContextScope()
    {
        ImGui::SetCurrentContext(previous_);
    }

  private:
    ImGuiContext *previous_;
};

std::string sdl_error(const char *operation)
{
    return std::string(operation) + ": " + SDL_GetError();
}

bool is_display_event(std::uint32_t type) noexcept
{
    switch (type)
    {
    case SDL_EVENT_DISPLAY_ORIENTATION:
    case SDL_EVENT_DISPLAY_ADDED:
    case SDL_EVENT_DISPLAY_REMOVED:
    case SDL_EVENT_DISPLAY_MOVED:
    case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
#if SDL_HAS_EVENT_DISPLAY_USABLE_BOUNDS_CHANGED
    case SDL_EVENT_DISPLAY_USABLE_BOUNDS_CHANGED:
#endif
        return true;
    default:
        return false;
    }
}
} // namespace

void set_window_host_failure_point_for_testing(WindowHostFailurePoint point) noexcept
{
    std::lock_guard lock(windowHostFailureMutex);
    windowHostFailurePoint = point;
}

struct SdlWindowHost::Impl
{
    Impl(WindowHostRecord hostIdentity, std::string hostTitle,
         NativeHostGeometry hostGeometry, std::string settings)
        : identity(hostIdentity), title(std::move(hostTitle)), requestedGeometry(hostGeometry)
    {
        if (identity.isMain || !identity.layoutId || !identity.generation ||
            !identity.rendererGeneration)
            throw std::invalid_argument("a detached SDL host requires valid non-main host identities");
        if (title.empty() || requestedGeometry.width <= 0 || requestedGeometry.height <= 0 ||
            requestedGeometry.width > 16384 || requestedGeometry.height > 16384)
            throw std::invalid_argument("detached SDL host title and dimensions must be valid");

        try
        {
            window = SDL_CreateWindow(title.c_str(), requestedGeometry.width,
                                      requestedGeometry.height,
                                      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                          SDL_WINDOW_HIDDEN);
            if (window == nullptr)
                throw std::runtime_error(sdl_error("SDL window creation failed"));
            SDL_SetWindowPosition(window, requestedGeometry.x, requestedGeometry.y);
            if (requestedGeometry.maximized)
                SDL_MaximizeWindow(window);

            if (consume_window_host_failure(WindowHostFailurePoint::RendererCreation))
                throw std::runtime_error("injected SDL renderer creation failure");
            renderer = SDL_CreateRenderer(window, nullptr);
            if (renderer == nullptr)
                throw std::runtime_error(sdl_error("SDL renderer creation failed"));

            ImGuiContext *previousContext = ImGui::GetCurrentContext();
            fallbackContext = previousContext;
            IMGUI_CHECKVERSION();
            context = ImGui::CreateContext();
            if (context == nullptr)
                throw std::runtime_error("Dear ImGui context creation failed");
            {
                CurrentContextScope current(context);
                ImGuiIO &io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
                io.IniFilename = nullptr;
                if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer))
                    throw std::runtime_error(sdl_error("ImGui SDL3 initialization failed"));
                platformInitialized = true;
                if (!ImGui_ImplSDLRenderer3_Init(renderer))
                    throw std::runtime_error(sdl_error("ImGui SDL renderer initialization failed"));
                rendererInitialized = true;
                ImGui_ImplSDL3_EditorSetSharedGamepadOwner(false);
                if (!settings.empty())
                    ImGui::LoadIniSettingsFromMemory(settings.data(), settings.size());
            }
            ImGui::SetCurrentContext(previousContext);
        }
        catch (...)
        {
            cleanup();
            throw;
        }
    }

    ~Impl() { cleanup(); }

    void cleanup() noexcept
    {
        ImGuiContext *previous = ImGui::GetCurrentContext();
        if (context != nullptr)
        {
            ImGuiContext *ownedContext = context;
            ImGui::SetCurrentContext(context);
            if (frameActive)
            {
                ImGui::EndFrame();
                frameActive = false;
            }
            if (rendererInitialized)
            {
                ImGui_ImplSDLRenderer3_Shutdown();
                rendererInitialized = false;
            }
            if (platformInitialized)
            {
                ImGui_ImplSDL3_Shutdown();
                platformInitialized = false;
            }
            ImGui::DestroyContext(context);
            context = nullptr;
            if (previous == ownedContext)
                previous = fallbackContext;
        }
        ImGui::SetCurrentContext(previous);
        if (renderer != nullptr)
        {
            SDL_DestroyRenderer(renderer);
            renderer = nullptr;
        }
        if (window != nullptr)
        {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
    }

    WindowHostRecord identity;
    std::string title;
    NativeHostGeometry requestedGeometry;
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    ImGuiContext *context = nullptr;
    ImGuiContext *fallbackContext = nullptr;
    ImGuiContext *previousFrameContext = nullptr;
    bool platformInitialized = false;
    bool rendererInitialized = false;
    bool frameActive = false;
};

SdlWindowHost::SdlWindowHost(WindowHostRecord identity, std::string title,
                             NativeHostGeometry geometry, std::string layoutSettings)
    : impl_(std::make_unique<Impl>(identity, std::move(title), geometry,
                                   std::move(layoutSettings)))
{
}

SdlWindowHost::SdlWindowHost(SdlWindowHost &&) noexcept = default;
SdlWindowHost &SdlWindowHost::operator=(SdlWindowHost &&) noexcept = default;
SdlWindowHost::~SdlWindowHost() = default;

const WindowHostRecord &SdlWindowHost::identity() const noexcept
{
    static const WindowHostRecord empty{};
    return impl_ ? impl_->identity : empty;
}

SDL_Window *SdlWindowHost::window() const noexcept
{
    return impl_ ? impl_->window : nullptr;
}

SDL_Renderer *SdlWindowHost::renderer() const noexcept
{
    return impl_ ? impl_->renderer : nullptr;
}

ImGuiContext *SdlWindowHost::context() const noexcept
{
    return impl_ ? impl_->context : nullptr;
}

std::uint32_t SdlWindowHost::window_id() const noexcept
{
    return window() != nullptr ? SDL_GetWindowID(window()) : 0;
}

bool SdlWindowHost::process_event(const SDL_Event &event)
{
    if (!impl_ || impl_->context == nullptr)
        return false;
    const SDL_Window *target = SDL_GetWindowFromEvent(&event);
    if ((target == nullptr && !is_display_event(event.type)) ||
        (target != nullptr && target != impl_->window))
        return false;
    CurrentContextScope current(impl_->context);
    return ImGui_ImplSDL3_ProcessEvent(&event);
}

void SdlWindowHost::begin_frame()
{
    if (!impl_ || impl_->context == nullptr || impl_->frameActive)
        throw std::logic_error("detached SDL host frame is unavailable or already active");
    impl_->previousFrameContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(impl_->context);
    impl_->frameActive = true;
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void SdlWindowHost::present()
{
    if (!impl_ || !impl_->frameActive)
        throw std::logic_error("detached SDL host present requires its active ImGui frame");
    ImGuiContext *previousContext = impl_->previousFrameContext;
    ImGui::SetCurrentContext(impl_->context);
    ImGui::Render();
    if (!SDL_SetRenderDrawColor(impl_->renderer, 36, 38, 42, 255) ||
        !SDL_RenderClear(impl_->renderer))
    {
        const std::string error = sdl_error("detached SDL host frame clear failed");
        impl_->frameActive = false;
        ImGui::SetCurrentContext(previousContext);
        impl_->previousFrameContext = nullptr;
        throw std::runtime_error(error);
    }
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), impl_->renderer);
    SDL_RenderPresent(impl_->renderer);
    impl_->frameActive = false;
    ImGui::SetCurrentContext(previousContext);
    impl_->previousFrameContext = nullptr;
}

void SdlWindowHost::show()
{
    if (impl_ && impl_->window != nullptr)
        SDL_ShowWindow(impl_->window);
}

NativeHostGeometry SdlWindowHost::geometry() const noexcept
{
    if (!impl_ || impl_->window == nullptr)
        return {};
    NativeHostGeometry result;
    SDL_GetWindowPosition(impl_->window, &result.x, &result.y);
    SDL_GetWindowSize(impl_->window, &result.width, &result.height);
    result.maximized = (SDL_GetWindowFlags(impl_->window) & SDL_WINDOW_MAXIMIZED) != 0;
    return result;
}

std::string SdlWindowHost::save_layout_settings()
{
    if (!impl_ || impl_->context == nullptr)
        return {};
    CurrentContextScope current(impl_->context);
    std::size_t size = 0;
    const char *settings = ImGui::SaveIniSettingsToMemory(&size);
    if (settings == nullptr || size == 0)
        return {};
    return std::string(settings, size);
}
} // namespace platformator_editor
