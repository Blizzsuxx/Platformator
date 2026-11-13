#include "editorapp.h"
#include "defaultlayout.h"
#include "editorresources.h"
#include "actions.h"
#include "accessibility.h"
#if defined(__linux__)
#include "platform/accessibility_linux.h"
#elif defined(__APPLE__)
#include "platform/accessibility_macos.h"
#elif defined(_WIN32)
#include "platform/accessibility_windows.h"
#endif
#include "filedialogs.h"
#include "panels/console.h"
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
#include "panels/game.h"
#include "playsession.h"
#endif
#include "panels/hierarchy.h"
#include "panels/inspector.h"
#include "panels/project.h"
#include "panels/runsettings.h"
#include "preferences.h"
#include "process.h"
#include "project.h"
#include "projectsession.h"
#include "platform/nativefile.h"
#include "preview.h"
#include "panelwindow.h"
#include "style.h"
#include "toolbar.h"
#include "uitextures.h"
#include "validation.h"
#include "viewport.h"
#include "workspace.h"
#include "windowhost.h"
#include "componenttypes.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include "backends/imgui_impl_sdl3_editor.h"
#include <backends/imgui_impl_sdlrenderer3.h>

#include <platformator/runtime.h>

namespace platformator_editor
{
std::chrono::milliseconds editor_wait_duration(
    EditorWaitClock::time_point now,
    std::span<const EditorWaitClock::time_point> deadlines,
    EditorWaitClock::duration maximumWait,
    std::optional<EditorWaitClock::duration> pollingInterval) noexcept
{
    auto deadline = now + std::max(maximumWait, EditorWaitClock::duration::zero());
    if (pollingInterval)
        deadline = std::min(deadline, now + std::max(*pollingInterval, EditorWaitClock::duration::zero()));
    for (const auto candidate : deadlines)
        deadline = std::min(deadline, candidate);
    if (deadline <= now)
        return std::chrono::milliseconds::zero();
    return std::chrono::ceil<std::chrono::milliseconds>(deadline - now);
}

EditorAppTickDriver::EditorAppTickDriver(std::optional<EditorAppProbeOptions> probe)
    : probe_(std::move(probe))
{
}

bool EditorAppTickDriver::tick(const std::function<bool()> &runFrame,
                               const std::function<void()> &idleWait)
{
    if (probe_ && probe_->frameLimit != 0 && frameCount_ >= probe_->frameLimit)
        return false;

    const auto cycleStarted = std::chrono::steady_clock::now();
    const bool keepRunning = runFrame();
    const auto activeEnded = std::chrono::steady_clock::now();
    if (!keepRunning)
        return false;
    const auto idleCpuStarted = std::clock();
    idleWait();
    const auto idleCpuEnded = std::clock();
    const auto cycleEnded = std::chrono::steady_clock::now();

    if (probe_ && frameCount_ >= probe_->warmupFrames)
    {
        const auto sample = frameCount_ - probe_->warmupFrames;
        const auto active = std::chrono::duration_cast<std::chrono::microseconds>(
            activeEnded - cycleStarted).count();
        const auto cycle = std::chrono::duration_cast<std::chrono::microseconds>(
            cycleEnded - cycleStarted).count();
        const auto idleCpu = idleCpuEnded >= idleCpuStarted
            ? static_cast<std::int64_t>((static_cast<long double>(idleCpuEnded - idleCpuStarted) * 1'000'000.0L) /
                                        CLOCKS_PER_SEC)
            : 0;
        std::cout << "GUI_FRAME_US," << sample << ',' << active << '\n'
                  << "GUI_CYCLE_US," << sample << ',' << cycle << '\n'
                  << "GUI_ACTIVE_FRAME_US," << sample << ',' << active << '\n'
                  << "GUI_WAIT_INCLUSIVE_US," << sample << ',' << cycle << '\n'
                  << "GUI_IDLE_CPU_US," << sample << ',' << idleCpu << '\n';
    }
    ++frameCount_;
    return keepRunning && (!probe_ || probe_->frameLimit == 0 || frameCount_ < probe_->frameLimit);
}

bool EditorAppTickDriver::should_inject_hover_input() const noexcept
{
    if (!probe_ || !probe_->hoverInputWorkload || frameCount_ < probe_->warmupFrames)
        return false;
    return (frameCount_ - probe_->warmupFrames) % 10 == 0;
}

void EditorAppTickDriver::note_input_event(EditorWaitClock::time_point when) noexcept
{
    if (probe_)
        inputEventStarted_ = when;
}

void EditorAppTickDriver::note_present(EditorWaitClock::time_point when,
                                      EditorWaitClock::time_point applicationStart)
{
    if (!probe_)
        return;
    if (!coldPreparationReported_)
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            when - applicationStart).count();
        std::cout << "GUI_COLD_PREP_US," << elapsed << '\n';
        coldPreparationReported_ = true;
    }
    if (inputEventStarted_ && frameCount_ >= probe_->warmupFrames)
    {
        const auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            when - *inputEventStarted_).count();
        std::cout << "GUI_EVENT_PRESENT_US," << frameCount_ - probe_->warmupFrames << ','
                  << std::max<std::int64_t>(0, latency) << '\n';
        inputEventStarted_.reset();
    }
}

std::size_t EditorAppTickDriver::frame_count() const noexcept
{
    return frameCount_;
}

void apply_project_resource_change(ResourceIndex &resources, EditorPreview &preview,
                                   const DocumentSession &document,
                                   const std::filesystem::path &path, bool directory)
{
    if (path.empty())
        return;
    const auto consumers = resources.affected_consumers(path, directory);
    resources.invalidate(path);
    if (directory)
    {
        const auto current = resources.uncertainty_epoch();
        const auto next = current == std::numeric_limits<std::uint64_t>::max()
                              ? current : current + 1;
        resources.invalidate_subtree(path, next);
    }
    if (!consumers.empty())
        preview.refresh_asset(document, path, directory, consumers);
}

bool can_execute_focused_undo(bool animationFocused, const DocumentManager &documents,
                              const EditCommandService &editCommands)
{
    if (animationFocused)
    {
        if (const auto *animTab = documents.active_animation_tab())
            return animTab->document().can_undo();
    }
    return editCommands.history_status().canUndo;
}

bool can_execute_focused_redo(bool animationFocused, const DocumentManager &documents,
                              const EditCommandService &editCommands)
{
    if (animationFocused)
    {
        if (const auto *animTab = documents.active_animation_tab())
            return animTab->document().can_redo();
    }
    return editCommands.history_status().canRedo;
}

bool execute_focused_undo(bool animationFocused, DocumentManager &documents,
                          EditCommandService &editCommands, DocumentSession &sceneDocument,
                          SelectionModel &selection, std::string &statusMessage)
{
    if (animationFocused)
    {
        if (auto *animTab = documents.active_animation_tab())
        {
            if (animTab->document().can_undo())
            {
                animTab->document().undo();
                statusMessage = "Animation undo completed.";
                return true;
            }
            statusMessage = "Nothing to undo in animation.";
            return false;
        }
    }
    const auto result = editCommands.undo(sceneDocument, selection);
    statusMessage = result ? "Undo completed." : result.message;
    return static_cast<bool>(result);
}

bool execute_focused_redo(bool animationFocused, DocumentManager &documents,
                          EditCommandService &editCommands, DocumentSession &sceneDocument,
                          SelectionModel &selection, std::string &statusMessage)
{
    if (animationFocused)
    {
        if (auto *animTab = documents.active_animation_tab())
        {
            if (animTab->document().can_redo())
            {
                animTab->document().redo();
                statusMessage = "Animation redo completed.";
                return true;
            }
            statusMessage = "Nothing to redo in animation.";
            return false;
        }
    }
    const auto result = editCommands.redo(sceneDocument, selection);
    statusMessage = result ? "Redo completed." : result.message;
    return static_cast<bool>(result);
}

bool sync_saved_animation_tabs(const DocumentManager &documents, const std::vector<TabId> &savedTabs,
                               ResourceIndex &resources, EditorPreview &preview,
                               const DocumentSession &sceneDocument)
{
    bool refreshedProjectPreviews = false;
    for (const TabId id : savedTabs)
    {
        if (const auto *animTab = documents.find_animation_tab(id))
        {
            if (const auto &source = animTab->document().source_path())
                apply_project_resource_change(resources, preview, sceneDocument, *source, false);
            refreshedProjectPreviews = true;
        }
    }
    return refreshedProjectPreviews;
}

struct RegisteredValidationDependency
{
    std::filesystem::path path;
    ResourceConsumer consumer;
};

namespace
{
constexpr ImGuiID kEditorDockspaceId = 0x6E706EF3;

std::filesystem::path native_path_from_utf8(std::string_view value)
{
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value)
        encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(std::move(encoded));
}

std::string native_path_to_utf8(const std::filesystem::path &path)
{
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char *>(encoded.data()), encoded.size()};
}

std::string read_default_layout()
{
    const char *basePath = SDL_GetBasePath();
    return read_editor_layout_or_fallback(
        basePath == nullptr ? std::filesystem::path{} : native_path_from_utf8(basePath),
        native_path_from_utf8(PLATFORMATOR_EDITOR_RESOURCE_RELATIVE_PATH),
        kEmbeddedDefaultLayout, 1024U * 1024U);
}

std::filesystem::path canonical_directory(const std::filesystem::path &path, const char *label)
{
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error || !std::filesystem::is_directory(canonical))
        throw std::invalid_argument(std::string(label) + " is not an existing directory: " + path.string());
    return canonical;
}

std::filesystem::path resolve_path(const std::filesystem::path &path,
                                   const std::filesystem::path &invocationDirectory)
{
    return path.is_absolute() ? path : invocationDirectory / path;
}

float parse_scale(std::string_view value)
{
    if (value.empty())
        throw std::invalid_argument("--ui-scale requires a positive finite number");
    std::string copy(value);
    char *end = nullptr;
    errno = 0;
    const float scale = std::strtof(copy.c_str(), &end);
    if (errno == ERANGE || end == copy.c_str() || *end != '\0' || !std::isfinite(scale) || scale <= 0.0f)
        throw std::invalid_argument("--ui-scale requires a positive finite number");
    return scale;
}

class ImGuiSdlSession
{
  public:
    ImGuiSdlSession(SDL_Window *window, SDL_Renderer *renderer)
        : renderer_(renderer)
    {
        IMGUI_CHECKVERSION();
        context_ = ImGui::CreateContext();
        contextCreated_ = context_ != nullptr;
        if (!contextCreated_)
            throw std::runtime_error("Dear ImGui context creation failed");
        ImGui::SetCurrentContext(context_);
        setup_editor_fonts(ImGui::GetIO());
        if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer_))
        {
            cleanup();
            throw std::runtime_error(std::string("ImGui SDL3 initialization failed: ") + SDL_GetError());
        }
        platformInitialized_ = true;
        if (!ImGui_ImplSDLRenderer3_Init(renderer_))
        {
            cleanup();
            throw std::runtime_error(std::string("ImGui SDL_Renderer initialization failed: ") + SDL_GetError());
        }
        rendererInitialized_ = true;
        ImGui_ImplSDL3_EditorSetSharedGamepadOwner(true);
    }

    ImGuiSdlSession(const ImGuiSdlSession &) = delete;
    ImGuiSdlSession &operator=(const ImGuiSdlSession &) = delete;

    ~ImGuiSdlSession()
    {
        cleanup();
    }

    void begin_frame()
    {
        ImGui::SetCurrentContext(context_);
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }

    void present(SDL_Renderer *renderer)
    {
        ImGui::SetCurrentContext(context_);
        ImGui::Render();
        if (!SDL_SetRenderDrawColor(renderer, 36, 38, 42, 255) || !SDL_RenderClear(renderer))
            throw std::runtime_error(std::string("SDL renderer clear failed: ") + SDL_GetError());
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

  private:
    void cleanup() noexcept
    {
        ImGuiContext *previous = ImGui::GetCurrentContext();
        if (contextCreated_)
            ImGui::SetCurrentContext(context_);
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
            ImGui::DestroyContext(context_);
            if (previous == context_)
                previous = nullptr;
            context_ = nullptr;
            contextCreated_ = false;
        }
        ImGui::SetCurrentContext(previous);
    }

    SDL_Renderer *renderer_ = nullptr;
    ImGuiContext *context_ = nullptr;
    bool contextCreated_ = false;
    bool platformInitialized_ = false;
    bool rendererInitialized_ = false;
};

struct ProjectBuildCommands
{
    std::vector<std::string> configure;
    std::vector<std::string> build;
    std::string error;
};

std::vector<std::string> selected_target_cmake_options(const std::string &target)
{
    return {"-DPLATFORMATOR_BUILD_RUNNER=" + std::string(target == "main" ? "ON" : "OFF"),
            "-DPLATFORMATOR_BUILD_EXAMPLES=" + std::string(target == "mario_example" ? "ON" : "OFF"),
            "-DPLATFORMATOR_BUILD_EDITOR=" + std::string(target == "platformator_editor" ? "ON" : "OFF")};
}

std::vector<std::string> production_cmake_options(const std::string &target,
                                                 bool enableEditorSession)
{
    std::vector<std::string> options{"-DCMAKE_EXPORT_COMPILE_COMMANDS=OFF",
                                     "-DPLATFORMATOR_SANITIZER=none",
                                     "-DPLATFORMATOR_ENABLE_DEBUG_TOOLS=OFF",
                                     std::string("-DPLATFORMATOR_ENABLE_EDITOR_SESSION=") +
                                         (enableEditorSession ? "ON" : "OFF"),
                                     "-DPLATFORMATOR_ENABLE_BENCHMARKS=OFF",
                                     "-DPLATFORMATOR_ENABLE_WORK_PROBES=OFF",
                                     "-DPLATFORMATOR_BUILD_TESTS=OFF"};
    const auto targetOptions = selected_target_cmake_options(target);
    options.insert(options.end(), targetOptions.begin(), targetOptions.end());
    return options;
}

std::vector<std::string> development_cmake_options(const std::string &target)
{
    std::vector<std::string> options{"-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                                     "-DPLATFORMATOR_SANITIZER=none",
                                     "-DPLATFORMATOR_ENABLE_DEBUG_TOOLS=ON",
                                     "-DPLATFORMATOR_ENABLE_EDITOR_SESSION=ON",
                                     "-DPLATFORMATOR_ENABLE_BENCHMARKS=OFF",
                                     "-DPLATFORMATOR_ENABLE_WORK_PROBES=OFF",
                                     "-DPLATFORMATOR_BUILD_TESTS=ON"};
    const auto targetOptions = selected_target_cmake_options(target);
    options.insert(options.end(), targetOptions.begin(), targetOptions.end());
    return options;
}

ProjectBuildCommands make_build_commands(const std::filesystem::path &projectRoot,
                                        const RunPreferences &preferences)
{
    if (preferences.target.empty() || preferences.executable.empty())
        return {{}, {}, "choose a target and executable before building"};
    if (preferences.configurePreset.empty() != preferences.buildPreset.empty())
        return {{}, {}, "configure and build presets must both be set or both be cleared"};
    const std::string parallelJobs = std::to_string(std::max(1u, std::thread::hardware_concurrency()));
    if (!preferences.configurePreset.empty())
    {
        std::vector<std::string> configure{"cmake", "--preset", preferences.configurePreset};
        std::vector<std::string> build{"cmake", "--build", "--preset", preferences.buildPreset,
                                       "--parallel", parallelJobs};
        if (preferences.production)
        {
            configure.emplace_back("-DCMAKE_BUILD_TYPE=Release");
            const auto options = production_cmake_options(preferences.target,
                                                         preferences.embeddedGameView);
            configure.insert(configure.end(), options.begin(), options.end());
            build.insert(build.end(), {"--config", "Release"});
        }
        else
            configure.emplace_back("-DPLATFORMATOR_ENABLE_EDITOR_SESSION=ON");
        build.insert(build.end(), {"--target", preferences.target});
        return {std::move(configure), std::move(build), {}};
    }

    const auto executable = native_path_from_utf8(preferences.executable);
    const auto buildDirectory = executable.is_absolute()
                                    ? executable.parent_path()
                                    : projectRoot / executable.parent_path();
    if (buildDirectory.empty())
        return {{}, {}, "the executable path does not identify a build directory"};
    const auto buildType = preferences.production ? "Release" : "Debug";
    std::vector<std::string> configure{"cmake", "-S", projectRoot.string(), "-B", buildDirectory.string(),
                                       "-DCMAKE_BUILD_TYPE=" + std::string(buildType)};
    if (preferences.production)
    {
        const auto options = production_cmake_options(preferences.target,
                                                     preferences.embeddedGameView);
        configure.insert(configure.end(), options.begin(), options.end());
    }
    else
    {
        const auto options = development_cmake_options(preferences.target);
        configure.insert(configure.end(), options.begin(), options.end());
    }
    std::vector<std::string> build{"cmake", "--build", buildDirectory.string(),
                                   "--parallel", parallelJobs,
                                   "--target", preferences.target};
    return {std::move(configure), std::move(build), {}};
}

std::vector<std::string> make_run_arguments(const RunPreferences &preferences,
                                           bool embeddedGameView = false)
{
    std::vector<std::string> arguments{"--window-width",
                                       std::to_string(embeddedGameView ? 640 : preferences.width),
                                       "--window-height",
                                       std::to_string(embeddedGameView ? 360 : preferences.height)};
    if (!embeddedGameView && preferences.fullscreen)
        arguments.emplace_back("--fullscreen");
    if (!embeddedGameView && preferences.maximized)
        arguments.emplace_back("--maximized");
    if (!embeddedGameView && preferences.preserveAspect)
        arguments.emplace_back("--keep-aspect-ratio");
    std::string debugDraw;
    if (!preferences.production)
    {
        for (const auto &overlay : preferences.overlays)
        {
            if (!debugDraw.empty())
                debugDraw.push_back(',');
            debugDraw += overlay;
        }
    }
    if (!debugDraw.empty())
    {
        arguments.emplace_back("--debug-draw");
        arguments.push_back(std::move(debugDraw));
    }
    if (preferences.startPaused && !preferences.production)
        arguments.emplace_back("--start-paused");
    return arguments;
}

std::string executable_identity(const std::filesystem::path &path)
{
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error || !std::filesystem::is_regular_file(canonical))
        return {};
    const auto size = std::filesystem::file_size(canonical, error);
    if (error)
        return {};
    const auto modified = std::filesystem::last_write_time(canonical, error);
    if (error)
        return {};
    return canonical.string() + ":" + std::to_string(size) + ":" +
           std::to_string(modified.time_since_epoch().count());
}

bool same_pipeline_inputs(const PipelineIdentity &left, const PipelineIdentity &right)
{
    return left.canonicalProjectPath == right.canonicalProjectPath && left.profile == right.profile &&
           left.target == right.target && left.executablePath == right.executablePath &&
           left.sourceInputIdentity == right.sourceInputIdentity;
}

struct PendingAssetAssignment
{
    std::vector<PropertyAddress> addresses;
    Selection selection;
    std::string expectedAssetType;
    bool nullable = false;
};

} // namespace

LaunchOptions parse_launch_options(int argc, const char *const argv[],
                                  const std::filesystem::path &invocationDirectory)
{
    if (argc < 0 || (argc > 0 && argv == nullptr))
        throw std::invalid_argument("invalid command-line arguments");

    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] != nullptr && std::string_view(argv[i]) == "--help")
            return LaunchOptions{.help = true};
    }

    std::error_code error;
    const auto invocationRoot = std::filesystem::canonical(invocationDirectory, error);
    if (error)
        throw std::invalid_argument("cannot resolve invocation directory: " + invocationDirectory.string());

    std::optional<std::filesystem::path> projectArgument;
    std::optional<std::filesystem::path> assetArgument;
    std::optional<std::filesystem::path> sceneArgument;
    LaunchOptions options;
    auto require_value = [&](int &index, const char *flag) -> std::string_view {
        if (index + 1 >= argc || argv[index + 1] == nullptr)
            throw std::invalid_argument(std::string(flag) + " requires a value");
        ++index;
        return argv[index];
    };

    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] == nullptr)
            throw std::invalid_argument("command-line argument is null");
        const std::string_view argument(argv[i]);
        if (argument == "--project-root")
            projectArgument = std::filesystem::path(require_value(i, "--project-root"));
        else if (argument == "--asset-root")
            assetArgument = std::filesystem::path(require_value(i, "--asset-root"));
        else if (argument == "--scene")
            sceneArgument = std::filesystem::path(require_value(i, "--scene"));
        else if (argument == "--ui-scale")
        {
            options.uiScale = parse_scale(require_value(i, "--ui-scale"));
            options.uiScaleProvided = true;
        }
        else if (argument == "--no-restore")
            options.noRestore = true;
        else
            throw std::invalid_argument("unknown editor option: " + std::string(argument));
    }

    options.projectRootProvided = projectArgument.has_value();
    options.assetRootProvided = assetArgument.has_value();
    options.projectRoot = canonical_directory(
        projectArgument ? resolve_path(*projectArgument, invocationRoot) : invocationRoot, "project root");
    if (options.projectRootProvided && !std::filesystem::is_regular_file(options.projectRoot / "CMakeLists.txt"))
        throw std::invalid_argument("project root must contain CMakeLists.txt: " + options.projectRoot.string());

    if (assetArgument)
        options.assetRoot = canonical_directory(resolve_path(*assetArgument, invocationRoot), "asset root");
    else if (options.projectRootProvided)
        options.assetRoot = canonical_directory(options.projectRoot / "assets", "asset root");

    if (sceneArgument)
    {
        const auto sceneCandidate = resolve_path(*sceneArgument, invocationRoot);
        const auto canonicalScene = std::filesystem::canonical(sceneCandidate, error);
        if (error || !std::filesystem::is_regular_file(canonicalScene))
            throw std::invalid_argument("scene is not an existing file: " + sceneCandidate.string());
        options.scenePath = canonicalScene;
    }
    return options;
}

std::string launch_help()
{
    return "Usage: platformator_editor [--project-root PATH] [--scene FILE] [--asset-root PATH] "
           "[--ui-scale NUMBER] [--no-restore] [--help]\n"
           "Without --project-root, the last opened workspace is restored; its last scene is restored too.\n"
           "Relative paths are resolved against the invocation directory.\n"
           "--ui-scale must be a positive finite style scale.\n";
}

bool editor_project_root_has_manifest(const std::filesystem::path &projectRoot) noexcept
{
    std::error_code error;
    return std::filesystem::is_directory(projectRoot, error) && !error &&
           std::filesystem::is_regular_file(projectRoot / "CMakeLists.txt", error) && !error;
}

namespace
{
class BootstrapWorkspace
{
  public:
    explicit BootstrapWorkspace(const std::filesystem::path &preferredParent)
    {
        std::error_code error;
        std::filesystem::path parent = preferredParent;
        if (parent.empty() || !std::filesystem::is_directory(parent, error) || error)
        {
            error.clear();
            parent = std::filesystem::temp_directory_path(error);
            if (error)
                throw std::runtime_error("could not find a temporary directory for the projectless workspace");
        }
        parent /= "PlatformatorEditorBootstrap";
        std::filesystem::create_directories(parent, error);
        if (error)
            throw std::runtime_error("could not prepare the projectless workspace directory: " + error.message());

        static std::atomic<std::uint64_t> nextId{1};
        const auto timePart = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned int attempt = 0; attempt < 64; ++attempt)
        {
            root_ = parent / ("session-" + std::to_string(timePart) + "-" +
                              std::to_string(nextId.fetch_add(1, std::memory_order_relaxed)));
            error.clear();
            if (!std::filesystem::create_directory(root_, error))
            {
                if (!error || error == std::errc::file_exists)
                    continue;
                throw std::runtime_error("could not create the projectless workspace: " + error.message());
            }
            assetRoot_ = root_ / "assets";
            std::filesystem::create_directory(assetRoot_, error);
            if (error)
            {
                std::error_code cleanupError;
                std::filesystem::remove_all(root_, cleanupError);
                root_.clear();
                throw std::runtime_error("could not create the projectless asset directory: " + error.message());
            }
            return;
        }
        throw std::runtime_error("could not allocate a unique projectless workspace directory");
    }

    ~BootstrapWorkspace()
    {
        if (!root_.empty())
        {
            std::error_code error;
            bool containsUserData = false;
            for (std::filesystem::directory_iterator entry(root_, error), end;
                 !error && entry != end; entry.increment(error))
            {
                if (entry->path() != assetRoot_ ||
                    !std::filesystem::is_empty(assetRoot_, error) || error)
                {
                    containsUserData = true;
                    break;
                }
            }
            if (!error && !containsUserData)
            {
                std::error_code ignored;
                std::filesystem::remove_all(root_, ignored);
            }
        }
    }

    BootstrapWorkspace(const BootstrapWorkspace &) = delete;
    BootstrapWorkspace &operator=(const BootstrapWorkspace &) = delete;
    const std::filesystem::path &project_root() const noexcept { return root_; }
    const std::filesystem::path &asset_root() const noexcept { return assetRoot_; }

  private:
    std::filesystem::path root_;
    std::filesystem::path assetRoot_;
};

int run_editor_impl(int argc, char *argv[], std::optional<EditorAppProbeOptions> probeOptions)
{
    const auto applicationStart = std::chrono::steady_clock::now();
    try
    {
        const auto invocationDirectory = std::filesystem::current_path();
        auto options = parse_launch_options(argc, argv, invocationDirectory);
        if (options.help)
        {
            std::cout << launch_help();
            return 0;
        }

        std::string startupMessage;
        std::filesystem::path preferenceRootPath;
        std::optional<std::filesystem::path> lastWorkspacePath;
        if (char *preferenceRoot = SDL_GetPrefPath("Platformator", "Editor"))
        {
            preferenceRootPath = preferenceRoot;
            lastWorkspacePath = preferenceRootPath / "last-workspace.json";
            SDL_free(preferenceRoot);
        }
        else
        {
            const char *error = SDL_GetError();
            startupMessage = std::string("Preferences are unavailable: ") +
                             (error == nullptr ? "unknown SDL error" : error);
        }

        bool projectOpened = false;
        auto has_asset_directory = [](const std::filesystem::path &path) {
            std::error_code error;
            return std::filesystem::is_directory(path, error) && !error;
        };
        if (!options.projectRootProvided && lastWorkspacePath)
        {
            const auto lastWorkspace = load_last_workspace(*lastWorkspacePath);
            if (!lastWorkspace.message.empty() && startupMessage.empty())
                startupMessage = lastWorkspace.message;
            if (lastWorkspace.projectRoot)
            {
                try
                {
                    const auto restoredRoot = canonical_directory(*lastWorkspace.projectRoot, "last workspace");
                    if (!editor_project_root_has_manifest(restoredRoot))
                        throw std::invalid_argument("last workspace no longer contains CMakeLists.txt");
                    const auto restoredAssets = options.assetRootProvided
                        ? options.assetRoot : canonical_directory(restoredRoot / "assets", "asset root");
                    if (!has_asset_directory(restoredAssets))
                        throw std::invalid_argument("last workspace asset root is unavailable");
                    options.projectRoot = restoredRoot;
                    if (!options.assetRootProvided)
                        options.assetRoot = restoredAssets;
                    projectOpened = true;
                }
                catch (const std::exception &error)
                {
                    if (startupMessage.empty())
                        startupMessage = std::string("Last workspace could not be restored: ") + error.what();
                }
            }
        }
        if (!projectOpened && options.projectRootProvided)
        {
            if (!editor_project_root_has_manifest(options.projectRoot))
                throw std::invalid_argument("project root must contain CMakeLists.txt: " + options.projectRoot.string());
            if (!options.assetRootProvided)
                options.assetRoot = canonical_directory(options.projectRoot / "assets", "asset root");
            projectOpened = true;
        }
        if (!projectOpened && editor_project_root_has_manifest(invocationDirectory))
        {
            const auto currentAssets = options.assetRootProvided
                ? options.assetRoot : invocationDirectory / "assets";
            if (has_asset_directory(currentAssets))
            {
                options.projectRoot = invocationDirectory;
                if (!options.assetRootProvided)
                    options.assetRoot = canonical_directory(currentAssets, "asset root");
                projectOpened = true;
            }
        }

        std::unique_ptr<BootstrapWorkspace> bootstrapWorkspace;
        if (!projectOpened)
        {
            bootstrapWorkspace = std::make_unique<BootstrapWorkspace>(preferenceRootPath);
            options.projectRoot = bootstrapWorkspace->project_root();
            if (!options.assetRootProvided)
                options.assetRoot = bootstrapWorkspace->asset_root();
            if (startupMessage.empty())
                startupMessage = "No project is open. Choose a project folder to get started.";
        }

        WorkspacePreferences preferences;
        if (projectOpened)
            preferences.projectKey = options.projectRoot.string();
        std::string savedLayout;
        std::optional<ProjectPreferencePaths> preferencePaths;
        if (!preferenceRootPath.empty() && projectOpened)
        {
            try
            {
                preferencePaths = make_project_preference_paths(preferenceRootPath, options.projectRoot);
                auto loaded = load_workspace_preferences(preferencePaths->settingsPath,
                                                         preferencePaths->layoutPath, options.projectRoot);
                preferences = std::move(loaded.preferences);
                savedLayout = migrate_scene_window_identity(loaded.layout);
                if (!loaded.settingsPresent)
                    apply_known_bundled_run_defaults(preferences, options.projectRoot);
                if (loaded.recovered)
                    startupMessage = loaded.message;
            }
            catch (const std::exception &error)
            {
                startupMessage = std::string("Preferences were not loaded: ") + error.what();
            }
            if (lastWorkspacePath && projectOpened)
            {
                std::string error;
                if (!save_last_workspace(*lastWorkspacePath, options.projectRoot, error) && startupMessage.empty())
                    startupMessage = std::move(error);
            }
        }

        RuntimeOptions runtimeOptions;
        runtimeOptions.assetRoot = options.assetRoot.string();
        runtimeOptions.windowSettings.width = preferences.editorWindow.width;
        runtimeOptions.windowSettings.height = preferences.editorWindow.height;
        runtimeOptions.windowSettings.fullscreen = preferences.editorWindow.fullscreen;
        runtimeOptions.windowSettings.maximizeOnStartup = preferences.editorWindow.maximized;
        runtimeOptions.windowSettings.highPixelDensity = true;
        auto set_runtime_window_preferences = [&] {
            runtimeOptions.windowSettings.width = preferences.editorWindow.width;
            runtimeOptions.windowSettings.height = preferences.editorWindow.height;
            runtimeOptions.windowSettings.fullscreen = preferences.editorWindow.fullscreen;
            runtimeOptions.windowSettings.maximizeOnStartup = preferences.editorWindow.maximized;
        };

        // Runtime owns SDL and the renderer. The ImGui session is declared after
        // Runtime so its backends/context shut down while those SDL objects live.
        auto runtime = std::make_unique<platformator::Runtime>(runtimeOptions);
        SDL_Window *window = runtime->getWindowHandle();
        SDL_Renderer *renderer = runtime->getRenderer();
        if (window == nullptr || renderer == nullptr)
            throw std::runtime_error("Runtime did not provide the editor window and renderer");
        if (probeOptions)
        {
            const char *video = SDL_GetCurrentVideoDriver();
            const char *rendererName = SDL_GetRendererName(renderer);
            std::cout << "GUI_RENDERER," << (video == nullptr ? "unknown" : video) << ','
                      << (rendererName == nullptr ? "unknown" : rendererName) << '\n';
        }

        const Uint32 projectWakeEventType = SDL_RegisterEvents(1);
        if (projectWakeEventType == static_cast<Uint32>(-1))
            throw std::runtime_error("Could not register the project-monitor wake event.");
        auto wakeGate = std::make_shared<CoalescedWakeGate>();
        auto make_wake = [&](const char *source) {
            (void)source;
            return [projectWakeEventType, wakeGate] {
                if (!wakeGate->try_queue())
                    return;
                SDL_Event wake{};
                wake.type = projectWakeEventType;
                if (!SDL_PushEvent(&wake))
                    wakeGate->queue_failed();
            };
        };
        const auto wake_editor = make_wake("generic");
        ProjectSession projectSession(options.projectRoot, options.assetRoot);
        projectSession.set_wake_callback(make_wake("projectSession"));
        ProjectContext &projectContext = projectSession.project_context();
        DocumentManager &documentManager = projectSession.documents();
        DocumentTab *activeDocumentTab = documentManager.active_tab();
        DocumentWorkspace *workspace = &activeDocumentTab->workspace();
        std::filesystem::path activeProjectRoot = options.projectRoot;
        std::filesystem::path activeAssetRoot = options.assetRoot;
        ProjectScanService sourceScans(2);
        sourceScans.set_wake_callback(make_wake("sourceScans"));
        ProjectScanToken sourceScan;
        std::uint64_t sourceScanRequest = 0;
        std::uint64_t sourceScanRevision = 0;
        std::uint64_t handledSourceRevision = projectSession.source_change_revision();
        std::string sourceInputIdentity;
        bool sourceInputIdentityInitialized = false;
        std::vector<std::string> lastConfiguredArgv;
        std::filesystem::path lastConfiguredProjectRoot;
        auto request_source_scan = [&] {
            const auto requestedRevision = projectSession.source_change_revision();
            sourceScan.cancel();
            sourceScan = {};
            sourceScan = sourceScans.request_scan(projectContext.snapshot(), activeProjectRoot,
                ProjectScanOptions{.recursive = true, .maxFiles = 50000, .maxEntries = 100000,
                                   .sourceInputsOnly = true,
                                   .maxSourceBytes = 256U * 1024U * 1024U});
            if (!sourceScan)
                throw std::runtime_error("source scan service reached its outstanding-scan limit");
            sourceScanRequest = sourceScan.request_id();
            sourceScanRevision = requestedRevision;
        };
        request_source_scan();
        const auto source_scan_interval = [&] {
            return projectSession.source_notifications_available()
                ? std::chrono::seconds(60) : std::chrono::seconds(5);
        };
        auto nextSourceScan = std::chrono::steady_clock::now() + source_scan_interval();
        ProjectScanService assetScans(2, 2);
        assetScans.set_wake_callback(make_wake("assetScans"));
        std::map<HostLayoutId, std::unique_ptr<SdlWindowHost>> detachedHosts;
        ProjectPanel projectPanel(projectContext, projectSession.project_changes(), assetScans, renderer);
        projectPanel.set_wake_callback(make_wake("projectPanel"));
        ProjectScanService assetWatchScans(1, 1);
        assetWatchScans.set_wake_callback(make_wake("assetWatchScans"));
        ProjectScanToken assetWatchScan;
        ProjectSnapshotToken assetWatchSnapshot;
        RecoveryRetrySchedule assetWatchSchedule;
        std::uint64_t appliedResourceRootUncertainty = std::numeric_limits<std::uint64_t>::max();
        bool assetRootRecoveryActive = false;
        const auto assetWatchInterval = std::chrono::seconds(60);
        auto nextReferenceFallbackCheck = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        auto reset_asset_watcher = [&] {
            assetWatchScan.cancel();
            assetWatchScan = {};
            assetWatchSnapshot = {};
            assetWatchScans.cancel_pending_and_wait();
            assetWatchSchedule.request_now(std::chrono::steady_clock::now());
            appliedResourceRootUncertainty = std::numeric_limits<std::uint64_t>::max();
            assetRootRecoveryActive = false;
            nextReferenceFallbackCheck = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        };
        auto request_asset_watch_scan = [&] {
            assetWatchSnapshot = projectSession.project_changes().begin_root_snapshot();
            assetWatchScan = assetWatchScans.request_scan(
                projectContext.snapshot(), activeAssetRoot,
                ProjectScanOptions{.recursive = true, .maxFiles = 50000, .maxEntries = 100000,
                                   .trackFileMetadata = true});
            if (!assetWatchScan)
                throw std::runtime_error("asset watcher reached its outstanding-scan limit");
        };
        {
            auto panelVisibility = default_panel_visibility();
            for (const auto &[stableId, visible] : preferences.panelVisibility)
                if (panelVisibility.contains(stableId))
                    panelVisibility[stableId] = visible;
            preferences.panelVisibility = std::move(panelVisibility);
        }

        const auto startup = resolve_startup_scene(options.scenePath, preferences.recentScene, options.noRestore);
        if (startup.missingRestoredScene)
        {
            preferences.recentScene.reset();
            startupMessage = startup.message;
        }
        if (startup.scenePath)
        {
            const auto opened = workspace->open_document(startup.scenePath, UnsavedDecision::Discard);
            if (opened)
                record_adopted_scene(preferences, *workspace->document().sourcePath);
            else
                startupMessage = "Scene could not be opened: " + opened.message;
        }

        auto dialogs = std::make_unique<FileDialogService>(projectContext.snapshot().generation);
        SelectionModel *selection = &activeDocumentTab->selection();
        EditCommandService *editCommands = &activeDocumentTab->history();
        auto scenePreview = std::make_unique<EditorPreview>(*runtime);
        SceneTextureReplicas sceneTextures;
        scenePreview->set_resource_index(&projectSession.resource_index());
        scenePreview->set_wake_callback(make_wake("scenePreview"));
        scenePreview->reconcile(workspace->document(), true);
        SceneViewportPanel scenePanel;
        HierarchyPanel hierarchyPanel;
        hierarchyPanel.set_on_frame_selected([&scenePanel]() {
            scenePanel.request_frame_selected();
        });
        std::unordered_map<std::uint64_t, std::pair<std::uint64_t, std::string>>
            visibilityDocumentIncarnations;
        bool sceneViewPreferencesDirty = false;
        bool activeTabStateInitialized = false;
        std::vector<int> previewChangedObjectIds;
        std::vector<int> validationChangedObjectIds;
        bool validationForceFull = false;
        ValidationRecoveryGate validationRecoveryGate;
        bool previewStructureChanged = false;
        bool previewInheritedTranslationChanged = false;
        ValidationService validationService;
        validationService.set_wake_callback(make_wake("validationService"));
        std::unordered_set<std::uint64_t> subscribedTabIds;
        auto subscribe_document_tab = [&](DocumentTab &tab) {
            if (!subscribedTabIds.insert(tab.id().value).second)
                return;
            const TabId tabId = tab.id();
            tab.history().subscribe([&, tabId](const DocumentChange &change,
                                                const std::vector<int> &objectIds) {
                if (activeDocumentTab == nullptr || activeDocumentTab->id() != tabId)
                    return;
                const auto *owner = documentManager.find(tabId);
                if (owner == nullptr)
                    return;
                hierarchyPanel.apply_change(owner->workspace().document(), change);
                previewStructureChanged = previewStructureChanged || change.structural;
                validationForceFull = validationForceFull || change.structural;
                validationChangedObjectIds.insert(validationChangedObjectIds.end(),
                    change.affectedObjectIds.begin(), change.affectedObjectIds.end());
                validationChangedObjectIds.insert(validationChangedObjectIds.end(),
                    objectIds.begin(), objectIds.end());
                const bool poseChange = std::find(change.categories.begin(), change.categories.end(),
                                                   DocumentChangeCategory::Pose) != change.categories.end();
                previewInheritedTranslationChanged = previewInheritedTranslationChanged ||
                    std::any_of(change.changedPaths.begin(), change.changedPaths.end(),
                        [](const std::string &path) {
                            return path == "position" || path.starts_with("position.") ||
                                   path.starts_with("position[");
                        }) || (poseChange && change.changedPaths.empty());
                previewChangedObjectIds.insert(previewChangedObjectIds.end(),
                                               objectIds.begin(), objectIds.end());
            });
        };
        subscribe_document_tab(*activeDocumentTab);
        std::uint64_t previewDocumentGeneration = workspace->document().documentGeneration;
        TabId previewTabId = activeDocumentTab->id();
        InspectorPanel inspectorPanel;
        inspectorPanel.set_host_generation(1);
        RunSettingsPanel runSettingsPanel;
        runSettingsPanel.set_project_root(activeProjectRoot);
        ConsolePanel consolePanel;
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
        GamePanel gamePanel;
        gamePanel.set_renderer(renderer);
#endif
        GamePipeline gamePipeline;
        std::optional<TabId> gameSessionSourceTab;
        std::optional<std::filesystem::path> gameSessionSourcePath;
        PipelineIdentity activePipelineIdentity;
        bool hasActivePipelineIdentity = false;
        std::uint64_t pipelineGeneration = 1;
        std::optional<bool> pendingPipelineBuildOnly;
        BehaviorCatalog behaviorCatalog;
        BehaviorCatalogQuery catalogQuery;
        CatalogIdentity catalogIdentity;
        catalogIdentity.canonicalProjectPath = activeProjectRoot.string();
        catalogIdentity.profile = preferences.run.buildPreset;
        catalogIdentity.target = preferences.run.target;
        catalogIdentity.executablePath = preferences.run.executable;
        behaviorCatalog.configure(preferences.run.catalogProtocol == "common-runner-v1"
                                      ? CatalogProtocol::CommonRunnerV1
                                      : CatalogProtocol::Unavailable,
                                  catalogIdentity);
        std::uint64_t catalogQueryGeneration = 1;
        bool initialCatalogRefreshPending = behaviorCatalog.can_query();
        std::unique_ptr<ImGuiSdlSession> imgui;
        ImGuiIO *io = nullptr;
        const std::string defaultLayout = read_default_layout();
        int logicalWidth = 0;
        int logicalHeight = 0;
        SDL_GetWindowSize(window, &logicalWidth, &logicalHeight);
        if (layout_needs_recovery(savedLayout, logicalWidth, logicalHeight))
        {
            savedLayout = defaultLayout;
        }
        float activeScale = options.uiScaleProvided ? options.uiScale : preferences.userScale;
        if (activeScale < 0.5f || activeScale > 3.0f || !std::isfinite(activeScale))
            activeScale = 1.0f;
        float lastAppliedScale = -1.0f;

        auto initialize_imgui_context = [&] {
            io = &ImGui::GetIO();
            io->ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
            io->IniFilename = nullptr;
            if (!savedLayout.empty())
                ImGui::LoadIniSettingsFromMemory(savedLayout.data(), savedLayout.size());
        };
        imgui = std::make_unique<ImGuiSdlSession>(window, renderer);
        initialize_imgui_context();
        auto rebuild_runtime_context = [&](const std::filesystem::path &assetRoot) {
            sceneTextures.release_all();
            scenePreview.reset();
            imgui.reset();
            projectPanel.set_renderer(nullptr);
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
            gamePanel.release_renderer(renderer);
            gamePanel.set_renderer(nullptr);
#endif
            runtime.reset();
            window = nullptr;
            renderer = nullptr;
            try
            {
                RuntimeOptions replacementOptions = runtimeOptions;
                replacementOptions.assetRoot = assetRoot.string();
                runtime = std::make_unique<platformator::Runtime>(replacementOptions);
                window = runtime->getWindowHandle();
                renderer = runtime->getRenderer();
                if (window == nullptr || renderer == nullptr)
                    throw std::runtime_error("Runtime did not provide the editor window and renderer");
                projectPanel.set_renderer(renderer);
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
                gamePanel.set_renderer(renderer);
#endif
                scenePreview = std::make_unique<EditorPreview>(*runtime);
                scenePreview->set_resource_index(&projectSession.resource_index());
                scenePreview->set_wake_callback(make_wake("scenePreview_restart"));
                scenePreview->reconcile(workspace->document(), true);
                imgui = std::make_unique<ImGuiSdlSession>(window, renderer);
                initialize_imgui_context();
            }
            catch (...)
            {
                sceneTextures.release_all();
                scenePreview.reset();
                imgui.reset();
                projectPanel.set_renderer(nullptr);
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
                gamePanel.release_renderer(renderer);
                gamePanel.set_renderer(nullptr);
#endif
                runtime.reset();
                window = nullptr;
                renderer = nullptr;
                io = nullptr;
                throw;
            }
        };

        bool running = true;
        bool closeRequested = false;
        bool showManualPath = false;
        bool openSceneDialogPending = false;
        AsyncRequestKind manualPathKind = AsyncRequestKind::OpenScene;
        std::optional<DocumentIdentity> manualPathIdentity;
        std::array<char, 4096> manualPath{};
        std::string dialogError;
        std::optional<PendingAssetAssignment> pendingAssetAssignment;
        std::optional<TabId> closeAnimationPromptTab;
        struct AnimationExternalConflict
        {
            DocumentIdentity identity;
            SourceFileStatus observed;
        };
        std::optional<AnimationExternalConflict> externalAnimationConflict;
        bool externalAnimationSaveAsPending = false;
        std::optional<DocumentIdentity> pendingAnimationSaveAsIdentity;
        struct AnimationDestinationOverwrite
        {
            DocumentIdentity identity;
            std::filesystem::path path;
            AssetOverwriteObservation observation;
            bool returnToExternalConflict = false;
        };
        std::optional<AnimationDestinationOverwrite> animationDestinationOverwrite;
        bool showAnimationEditorWindow = true;
        bool animationWindowFocused = false;
        bool focusAnimationEditorWindow = false;
        bool animationWindowPlaybackVisible = false;
        bool runPanelVisibleLastFrame = true;
        bool validationPanelVisibleLastFrame = true;
        std::string statusMessage = startupMessage;
        PendingRunValidation pendingRunValidation;
        AccessibilitySnapshots accessibilitySnapshots;
        accessibilitySnapshots.set_wake_callback(make_wake("accessibilitySnapshots"));
        WindowHostRegistry windowHosts;
        const WindowHostRecord mainHost = *windowHosts.find(windowHosts.main_host());
        const HostLayoutId mainAccessibilityHost = mainHost.layoutId;
        const WindowHostGeneration mainAccessibilityGeneration = mainHost.generation;
#if defined(__APPLE__)
        MacAccessibilityProvider mainMacAccessibility;
        std::map<HostLayoutId, std::unique_ptr<MacAccessibilityProvider>> detachedMacAccessibility;
#elif defined(_WIN32)
        WindowsAccessibilityProvider mainWindowsAccessibility;
        std::map<HostLayoutId, std::unique_ptr<WindowsAccessibilityProvider>> detachedWindowsAccessibility;
#endif
        const auto panel_can_detach = [](std::string_view panelId) {
            const bool rendererIndependent =
                panelId == "panel.hierarchy" || panelId == "panel.scene" ||
                panelId == "panel.inspector" ||
                panelId == "panel.project" || panelId == "panel.console" ||
                panelId == "panel.run" || panelId == "panel.validation";
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
            return rendererIndependent || panelId == "panel.game";
#else
            return rendererIndependent;
#endif
        };
        const auto panel_title = [](std::string_view panelId) -> std::string {
            if (panelId == "panel.hierarchy") return "Hierarchy";
            if (panelId == "panel.scene") return "Scene";
            if (panelId == "panel.inspector") return "Inspector";
            if (panelId == "panel.project") return "Project";
            if (panelId == "panel.console") return "Console";
            if (panelId == "panel.run") return "Build and Run";
            if (panelId == "panel.validation") return "Validation";
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
            if (panelId == "panel.game") return "Game";
#endif
            return "Editor Panel";
        };
        const auto restore_detached_hosts = [&] {
            std::vector<HostLayoutId> requestedHosts;
            for (auto &[panelId, hostId] : preferences.panelHostAssignments)
            {
                if (!panel_can_detach(panelId))
                {
                    hostId = main_host_layout_id;
                    continue;
                }
                if (hostId != main_host_layout_id &&
                    std::find(requestedHosts.begin(), requestedHosts.end(), hostId) == requestedHosts.end())
                    requestedHosts.push_back(hostId);
            }
            for (const HostLayoutId hostId : requestedHosts)
            {
                const auto identity = windowHosts.restore_host(hostId);
                if (!identity)
                {
                    for (auto &[panelId, assignedHost] : preferences.panelHostAssignments)
                    {
                        (void)panelId;
                        if (assignedHost == hostId)
                            assignedHost = main_host_layout_id;
                    }
                    statusMessage = "Some saved detached panels were returned to the main window because the host limit or identity was invalid.";
                    continue;
                }
                auto geometry = preferences.hostGeometry[hostId];
                int displayCount = 0;
                SDL_DisplayID *displays = SDL_GetDisplays(&displayCount);
                std::vector<SDL_Rect> usableDisplays;
                usableDisplays.reserve(std::max(displayCount, 0));
                for (int i = 0; displays != nullptr && i < displayCount; ++i)
                {
                    SDL_Rect bounds{};
                    if (SDL_GetDisplayUsableBounds(displays[i], &bounds))
                        usableDisplays.push_back(bounds);
                }
                SDL_free(displays);
                const auto recovered = recover_host_geometry(
                    NativeHostGeometry{geometry.x, geometry.y, geometry.width,
                                       geometry.height, geometry.maximized}, usableDisplays);
                geometry = HostGeometryPreferences{recovered.x, recovered.y, recovered.width,
                                                    recovered.height, recovered.maximized};
                const auto settings = preferences.hostLayoutSettings.find(hostId);
                const std::string layout = settings == preferences.hostLayoutSettings.end()
                    ? std::string{} : settings->second;
                std::string title = "Platformator Editor — ";
                for (const auto &[panelId, assignedHost] : preferences.panelHostAssignments)
                    if (assignedHost == hostId)
                    {
                        title += panel_title(panelId);
                        break;
                    }
                try
                {
                    auto host = std::make_unique<SdlWindowHost>(
                        *identity, std::move(title),
                        NativeHostGeometry{geometry.x, geometry.y, geometry.width,
                                           geometry.height, geometry.maximized}, layout);
                    host->show();
                    detachedHosts.emplace(hostId, std::move(host));
                }
                catch (const std::exception &error)
                {
                    windowHosts.close_host(hostId, preferences.panelHostAssignments);
                    preferences.hostGeometry.erase(hostId);
                    preferences.hostLayoutSettings.erase(hostId);
                    statusMessage = std::string("A saved detached host could not be restored and its panels were redocked: ") + error.what();
                }
            }
        };
        restore_detached_hosts();
        constexpr std::size_t maximumHostLayoutBytes = 1U << 20;
#if defined(__linux__)
        LinuxAccessibilityProvider linuxAccessibility;
#elif defined(__APPLE__)
        const auto start_detached_accessibility = [&](HostLayoutId hostId, SdlWindowHost &host) {
            auto provider = std::make_unique<MacAccessibilityProvider>();
            std::string providerError;
            if (provider->start(accessibilitySnapshots, host.window(), host.identity().layoutId,
                                host.identity().generation, providerError))
                detachedMacAccessibility.insert_or_assign(hostId, std::move(provider));
            else if (!providerError.empty())
                statusMessage = "macOS accessibility provider unavailable: " + providerError;
        };
#elif defined(_WIN32)
        const auto start_detached_accessibility = [&](HostLayoutId hostId, SdlWindowHost &host) {
            auto provider = std::make_unique<WindowsAccessibilityProvider>();
            std::string providerError;
            if (provider->start(accessibilitySnapshots, host.window(), host.identity().layoutId,
                                host.identity().generation, providerError))
                detachedWindowsAccessibility.insert_or_assign(hostId, std::move(provider));
            else if (!providerError.empty())
                statusMessage = "Windows UI Automation provider unavailable: " + providerError;
        };
#endif
        std::uint64_t accessibilitySnapshotRevision = 0;
        const auto capture_detached_host_state = [&] {
            for (auto &[hostId, host] : detachedHosts)
            {
                const auto geometry = host->geometry();
                preferences.hostGeometry[hostId] = HostGeometryPreferences{
                    geometry.x, geometry.y, geometry.width, geometry.height, geometry.maximized};
                std::string settings = host->save_layout_settings();
                if (settings.size() <= maximumHostLayoutBytes)
                    preferences.hostLayoutSettings[hostId] = std::move(settings);
                else if (statusMessage.empty())
                    statusMessage = "A detached host layout exceeded the 1 MiB save limit and was not updated.";
            }
        };
#if defined(__linux__)
        std::string accessibilityProviderError;
        if (!linuxAccessibility.start(accessibilitySnapshots, mainAccessibilityHost,
                                      mainAccessibilityGeneration, accessibilityProviderError) &&
            !accessibilityProviderError.empty())
            statusMessage = "Linux accessibility provider unavailable: " + accessibilityProviderError;
#elif defined(__APPLE__)
        std::string accessibilityProviderError;
        if (!mainMacAccessibility.start(accessibilitySnapshots, window, mainAccessibilityHost,
                                        mainAccessibilityGeneration, accessibilityProviderError) &&
            !accessibilityProviderError.empty())
            statusMessage = "macOS accessibility provider unavailable: " + accessibilityProviderError;
        for (auto &[hostId, host] : detachedHosts)
            start_detached_accessibility(hostId, *host);
#elif defined(_WIN32)
        std::string accessibilityProviderError;
        if (!mainWindowsAccessibility.start(accessibilitySnapshots, window, mainAccessibilityHost,
                                            mainAccessibilityGeneration, accessibilityProviderError) &&
            !accessibilityProviderError.empty())
            statusMessage = "Windows UI Automation provider unavailable: " + accessibilityProviderError;
        for (auto &[hostId, host] : detachedHosts)
            start_detached_accessibility(hostId, *host);
#endif
        const auto open_animation_file = [&](const std::filesystem::path &path) {
            std::string error;
            if (documentManager.open_animation(path, &error))
            {
                showAnimationEditorWindow = true;
                focusAnimationEditorWindow = true;
                statusMessage = "Opened animation asset " + path.filename().string() + ".";
            }
            else
                statusMessage = "Animation asset could not be opened: " + error;
        };
        projectPanel.set_open_animation_callback(open_animation_file);
        inspectorPanel.set_open_animation_callback(open_animation_file);
        inspectorPanel.set_texture_preparer([&](const std::filesystem::path &path) {
            const auto prepared = projectPanel.prepared_animation_texture(path);
            return InspectorPanel::PreparedTextureInfo{
                prepared.texture, prepared.sourceWidth, prepared.sourceHeight};
        });
        ValidationResult validation;
        std::unordered_map<int, std::vector<RegisteredValidationDependency>>
            registeredValidationDependencies;
        auto unregister_validation_object = [&](int objectId) {
            const auto found = registeredValidationDependencies.find(objectId);
            if (found == registeredValidationDependencies.end())
                return;
            for (const auto &dependency : found->second)
                projectSession.resource_index().unregister_consumer(
                    dependency.path, dependency.consumer);
            registeredValidationDependencies.erase(found);
        };
        auto clear_validation_dependencies = [&] {
            while (!registeredValidationDependencies.empty())
                unregister_validation_object(registeredValidationDependencies.begin()->first);
        };
        auto update_validation_dependencies = [&](const ValidationCompletion &completion) {
            std::unordered_set<int> refreshed;
            if (completion.incremental)
            {
                refreshed.insert(completion.refreshedObjectIds.begin(),
                                 completion.refreshedObjectIds.end());
                for (const int objectId : completion.refreshedObjectIds)
                    unregister_validation_object(objectId);
            }
            else
                clear_validation_dependencies();

            for (const auto &dependency : completion.result.assetDependencies)
            {
                if (completion.incremental && !refreshed.contains(dependency.objectId))
                    continue;
                ResourceConsumer consumer{completion.document, dependency.objectId,
                    dependency.componentId, "validation.asset"};
                projectSession.resource_index().register_consumer(
                    dependency.logicalPath, dependency.kind, consumer);
                registeredValidationDependencies[dependency.objectId].push_back(
                    {dependency.logicalPath, std::move(consumer)});
            }
        };
        validationService.request(workspace->document());
        auto request_validation = [&] {
            validationChangedObjectIds.clear();
            validationForceFull = false;
            validationService.cancel();
            validationService.request(workspace->document());
            statusMessage = "Validating the current scene...";
        };
        auto request_current_validation = [&](const DocumentSession &document) {
            if (validationForceFull || validationChangedObjectIds.empty())
                validationService.request(document);
            else
                validationService.request_changed(document, validationChangedObjectIds);
            validationChangedObjectIds.clear();
            validationForceFull = false;
        };
        auto bind_active_document = [&] {
            DocumentTab *nextTab = documentManager.active_tab();
            if (nextTab == nullptr || nextTab == activeDocumentTab)
                return false;

            if (pendingRunValidation.cancel())
                statusMessage = "Pending Build and Run cancelled because the active Scene tab changed.";

            if (activeDocumentTab != nullptr && activeTabStateInitialized)
            {
                inspectorPanel.cancel_active_interactions(
                workspace != nullptr ? &workspace->document_for_edit() : nullptr);
            scenePanel.commit_gesture();
                const auto currentView = scenePanel.view_state();
                activeDocumentTab->view_state() = SceneViewState{
                    currentView.center.x, currentView.center.y, currentView.zoom};
                activeDocumentTab->set_expanded_hierarchy_ids(hierarchyPanel.expanded_object_ids());
            }

            activeDocumentTab = nextTab;
            workspace = &activeDocumentTab->workspace();
            selection = &activeDocumentTab->selection();
            editCommands = &activeDocumentTab->history();
            subscribe_document_tab(*activeDocumentTab);
            selection->repair(workspace->document());
            scenePanel.restore_view_state(workspace->document(),
                activeDocumentTab->view_state().centerX,
                activeDocumentTab->view_state().centerY,
                activeDocumentTab->view_state().zoom);
            if (activeDocumentTab->has_expansion_state())
                hierarchyPanel.restore_expanded_object_ids(
                    workspace->document(), activeDocumentTab->expanded_hierarchy_ids());
            else
                hierarchyPanel.reset_expansion_for_document();
            activeTabStateInitialized = true;
            scenePreview->reconcile(workspace->document(), true);
            clear_validation_dependencies();
            validationService.cancel();
            validationService.request(workspace->document());
            validation = {};
            previewStructureChanged = false;
            previewInheritedTranslationChanged = false;
            previewChangedObjectIds.clear();
            validationChangedObjectIds.clear();
            validationForceFull = false;
            previewDocumentGeneration = workspace->document().documentGeneration;
            previewTabId = activeDocumentTab->id();
            if (workspace->document().sourcePath)
            {
                preferences.activeTabPath = *workspace->document().sourcePath;
                preferences.recentScene = *workspace->document().sourcePath;
            }
            else
                preferences.activeTabPath.reset();
            return true;
        };
        auto activate_document_tab = [&](TabId id) {
            if (activeDocumentTab != nullptr && activeDocumentTab->id() == id)
                return true;
            inspectorPanel.cancel_active_interactions(workspace != nullptr ? &workspace->document_for_edit() : nullptr);
            if (!documentManager.activate_tab(id))
                return false;
            bind_active_document();
            return true;
        };
        auto create_scene_tab = [&] {
            inspectorPanel.cancel_active_interactions(workspace != nullptr ? &workspace->document_for_edit() : nullptr);
            documentManager.create_scene_tab();
            bind_active_document();
        };
        enum class PendingAction
        {
            None,
            NewDocument,
            OpenDocument,
            ChangeProjectRoots,
            BuildAndRun,
            Exit
        };
        PendingAction pendingAction = PendingAction::None;
        std::optional<std::filesystem::path> pendingOpenPath;
        std::optional<ProjectRootCandidate> pendingProjectRoots;
        bool applyProjectRootsAfterFrame = false;
        std::filesystem::path pendingSaveAsPath;
        bool showUnsavedPrompt = false;
        bool showOverwritePrompt = false;
        bool showExternalConflictPrompt = false;
        bool showPreferences = false;
        bool showShortcutReference = false;
        bool externalSaveAsPending = false;
        std::optional<TabId> closePromptTab;
        std::optional<TabId> closeAfterSaveTab;
        std::optional<TabId> pendingGameSessionCloseTab;
        UnsavedDecision pendingGameSessionCloseDecision = UnsavedDecision::Discard;
        bool showGameSessionClosePrompt = false;
        bool waitingForGameSessionStopBeforeClose = false;
        bool pendingGameRestartAfterStop = false;
        bool gameSessionRestartAvailable = false;
        std::string gameSessionStaleMessage;
        std::optional<std::filesystem::path> deferredOpenScenePath;
        auto nextExternalFileCheck = std::chrono::steady_clock::now();
        std::vector<DocumentSourceStatus> cachedSourceStatuses;
        DeferredPreferenceSave deferredPreferenceSave;
        std::string lastWindowTitle;
        WorkspacePreferencePersistence preferencePersistence(preferences, savedLayout);
        std::optional<std::filesystem::path> persistedLastWorkspacePath;
        std::optional<std::filesystem::path> persistedLastWorkspaceRoot;

        auto persist_preferences = [&] {
            deferredPreferenceSave.clear();
            capture_detached_host_state();
            if (ImGui::GetCurrentContext() != nullptr)
            {
                std::size_t layoutSize = 0;
                if (const char *layout = ImGui::SaveIniSettingsToMemory(&layoutSize))
                    savedLayout.assign(layout, layoutSize);
                ImGui::GetIO().WantSaveIniSettings = false;
            }
            if (window != nullptr)
            {
                const auto flags = SDL_GetWindowFlags(window);
                preferences.editorWindow.fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
                preferences.editorWindow.maximized = !preferences.editorWindow.fullscreen &&
                    (flags & SDL_WINDOW_MAXIMIZED) != 0;
                if (!preferences.editorWindow.fullscreen && !preferences.editorWindow.maximized)
                {
                    int width = 0;
                    int height = 0;
                    if (SDL_GetWindowSize(window, &width, &height) && width >= 320 && height >= 320)
                    {
                        preferences.editorWindow.width = width;
                        preferences.editorWindow.height = height;
                    }
                }
                set_runtime_window_preferences();
            }
            if (lastWorkspacePath && projectOpened)
            {
                if (persistedLastWorkspacePath != lastWorkspacePath ||
                    persistedLastWorkspaceRoot != activeProjectRoot)
                {
                    std::string error;
                    if (!save_last_workspace(*lastWorkspacePath, activeProjectRoot, error))
                        statusMessage = error;
                    else
                    {
                        persistedLastWorkspacePath = lastWorkspacePath;
                        persistedLastWorkspaceRoot = activeProjectRoot;
                    }
                }
            }
            if (!preferencePaths)
                return;
            const auto stored = preferencePersistence.flush(preferences, savedLayout,
                [&](bool writeSettings, bool writeLayout) {
                    return save_workspace_preferences(*preferencePaths, preferences, savedLayout,
                                                      writeSettings, writeLayout);
                });
            if (!stored.succeeded())
                statusMessage = stored.message;
            else if (!stored.message.empty())
                statusMessage = stored.message;
        };
        auto schedule_preference_save = [&] {
            deferredPreferenceSave.schedule(std::chrono::steady_clock::now());
        };
        auto close_detached_host = [&](HostLayoutId hostId) {
            auto host = detachedHosts.find(hostId);
            if (host == detachedHosts.end())
                return false;
            inspectorPanel.cancel_active_interactions(
                workspace != nullptr ? &workspace->document_for_edit() : nullptr);
            capture_detached_host_state();
            if (!windowHosts.close_host(hostId, preferences.panelHostAssignments))
                return false;
            const auto identity = host->second->identity();
#if defined(__APPLE__)
            if (const auto provider = detachedMacAccessibility.find(hostId);
                provider != detachedMacAccessibility.end())
            {
                provider->second->stop();
                detachedMacAccessibility.erase(provider);
            }
#elif defined(_WIN32)
            if (const auto provider = detachedWindowsAccessibility.find(hostId);
                provider != detachedWindowsAccessibility.end())
            {
                provider->second->stop();
                detachedWindowsAccessibility.erase(provider);
            }
#endif
            accessibilitySnapshots.retire(identity.layoutId, identity.generation);
            sceneTextures.release_renderer(host->second->renderer());
            projectPanel.release_renderer(host->second->renderer());
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
            gamePanel.release_renderer(host->second->renderer());
            gamePanel.set_renderer(renderer);
#endif
            detachedHosts.erase(host);
            preferences.hostGeometry.erase(hostId);
            preferences.hostLayoutSettings.erase(hostId);
            schedule_preference_save();
            return true;
        };
        auto close_all_detached_hosts = [&] {
            std::vector<HostLayoutId> hostIds;
            hostIds.reserve(detachedHosts.size());
            for (const auto &[hostId, host] : detachedHosts)
            {
                (void)host;
                hostIds.push_back(hostId);
            }
            for (const HostLayoutId hostId : hostIds)
                close_detached_host(hostId);
        };
        auto move_panel_to_host = [&](std::string_view panelId, HostLayoutId destination) {
            if (!panel_can_detach(panelId) && destination != main_host_layout_id)
                return false;
            const auto assignment = preferences.panelHostAssignments.find(std::string(panelId));
            if (assignment == preferences.panelHostAssignments.end())
                return false;
            const HostLayoutId previous = assignment->second;
            if (previous == destination)
                return true;
            if (panelId == "panel.inspector")
                inspectorPanel.cancel_active_interactions(
                    workspace != nullptr ? &workspace->document_for_edit() : nullptr);
            if (!windowHosts.move_panel(panelId, destination, preferences.panelHostAssignments))
                return false;
            if (previous != main_host_layout_id)
            {
                const bool stillAssigned = std::any_of(
                    preferences.panelHostAssignments.begin(), preferences.panelHostAssignments.end(),
                    [&](const auto &entry) { return entry.second == previous; });
                if (!stillAssigned)
                    close_detached_host(previous);
            }
            schedule_preference_save();
            return true;
        };
        auto detach_panel = [&](std::string_view panelId) {
            if (!panel_can_detach(panelId))
            {
                statusMessage = "This panel uses renderer-owned images and remains in the main window.";
                return;
            }
            const auto assignment = preferences.panelHostAssignments.find(std::string(panelId));
            if (assignment == preferences.panelHostAssignments.end() ||
                assignment->second != main_host_layout_id)
                return;
            const auto identity = windowHosts.create_host();
            if (!identity)
            {
                statusMessage = "The editor has reached its limit of eight native host windows.";
                return;
            }
            int x = 0;
            int y = 0;
            SDL_GetWindowPosition(window, &x, &y);
            NativeHostGeometry geometry{x + 48, y + 48, 960, 640, false};
            try
            {
                std::string title = "Platformator Editor — " + panel_title(panelId);
                auto host = std::make_unique<SdlWindowHost>(*identity, std::move(title), geometry);
                host->show();
                detachedHosts.emplace(identity->layoutId, std::move(host));
#if defined(__APPLE__) || defined(_WIN32)
                start_detached_accessibility(identity->layoutId, *detachedHosts.at(identity->layoutId));
#endif
                preferences.hostGeometry[identity->layoutId] = HostGeometryPreferences{
                    geometry.x, geometry.y, geometry.width, geometry.height, geometry.maximized};
                if (!move_panel_to_host(panelId, identity->layoutId))
                    close_detached_host(identity->layoutId);
            }
            catch (const std::exception &error)
            {
                std::map<std::string, HostLayoutId> unusedAssignments;
                windowHosts.close_host(identity->layoutId, unusedAssignments);
                statusMessage = std::string("Could not detach the panel: ") + error.what();
            }
        };
        auto commit_scene_gesture = [&] { scenePanel.commit_gesture(); };
        auto cancel_scene_gesture = [&] {
            scenePanel.cancel_gesture(*editCommands, workspace->document_for_edit());
        };
        auto current_scene_allows_run = [&] {
            const auto sourceStatus = workspace->source_file_status();
            if (activeDocumentTab->source_file_allows_run(sourceStatus))
                return true;
            if (sourceStatus.state == SourceFileState::Changed ||
                sourceStatus.state == SourceFileState::Missing)
                showExternalConflictPrompt = true;
            statusMessage = sourceStatus.message.empty()
                ? "Resolve the saved scene file identity before Run."
                : sourceStatus.message + ". Resolve it before Run.";
            return false;
        };
        auto start_pipeline_now = [&](bool buildOnly) {
            commit_scene_gesture();
            if (!buildOnly && (showExternalConflictPrompt || externalSaveAsPending))
            {
                statusMessage = "Resolve the changed scene file before Run.";
                return false;
            }
            if (!buildOnly && !current_scene_allows_run())
                return false;
            if (gamePipeline.busy())
            {
                statusMessage = "A project operation is already running.";
                return false;
            }
            if (sourceInputIdentity.empty())
            {
                statusMessage = "Waiting for the bounded source identity scan to finish.";
                return false;
            }
#if !PLATFORMATOR_ENABLE_EDITOR_SESSION
            if (!buildOnly && preferences.run.embeddedGameView)
            {
                statusMessage = "This editor build does not include the opt-in editor-session feature. Rebuild with PLATFORMATOR_ENABLE_EDITOR_SESSION=ON or choose an external game window.";
                return false;
            }
#endif
            const auto buildCommands = make_build_commands(activeProjectRoot, preferences.run);
            if (!buildCommands.error.empty())
            {
                statusMessage = buildCommands.error;
                return false;
            }
            if (!buildOnly)
            {
                if (preferences.run.width < 1 || preferences.run.width > 16384 ||
                    preferences.run.height < 1 || preferences.run.height > 16384)
                {
                    statusMessage = "Run window dimensions must be between 1 and 16384 pixels.";
                    return false;
                }
                if (!validationService.is_current(workspace->document(), validation))
                {
                    request_current_validation(workspace->document());
                    if (!pendingRunValidation.begin(activeDocumentTab->identity()))
                    {
                        statusMessage = "Run was cancelled because validation request identities are exhausted.";
                        return false;
                    }
                    statusMessage = "Waiting for current scene validation before Run...";
                    return false;
                }
                pendingRunValidation.cancel();
                if (!validation_allows_run(validationService, workspace->document(), validation))
                {
                    statusMessage = "Run is blocked by current scene validation errors.";
                    preferences.panelVisibility["panel.validation"] = true;
                    return false;
                }
                if (workspace->dirty() || !workspace->document().sourcePath)
                {
                    statusMessage = "Save the scene before Build and Run.";
                    return false;
                }
            }

            const auto executable = native_path_from_utf8(preferences.run.executable);
            const auto executablePath = executable.is_absolute()
                                           ? executable.lexically_normal()
                                           : (activeProjectRoot / executable).lexically_normal();
            PipelineIdentity identity;
            identity.canonicalProjectPath = activeProjectRoot.string();
            identity.profile = preferences.run.buildPreset.empty()
                                   ? (preferences.run.production ? "production" : "debug")
                                   : preferences.run.buildPreset;
            identity.target = preferences.run.target;
            identity.executablePath = executablePath.string();
            identity.sourceInputIdentity = sourceInputIdentity;

            const bool skipConfigure = !buildCommands.configure.empty() &&
                                       lastConfiguredProjectRoot == activeProjectRoot &&
                                       lastConfiguredArgv == buildCommands.configure;
            PipelineRequest request;
            request.projectRoot = activeProjectRoot;
            request.assetRoot = activeAssetRoot;
            request.executablePath = executablePath;
            request.configureArgv = skipConfigure ? std::vector<std::string>{} : buildCommands.configure;
            request.buildArgv = buildCommands.build;
            request.runArguments = make_run_arguments(preferences.run,
                                                       preferences.run.embeddedGameView);
            request.identity = identity;
            request.catalogProtocol = preferences.run.catalogProtocol;
            request.embeddedGameView = !buildOnly && preferences.run.embeddedGameView;
            request.buildOnly = buildOnly;
            if (!buildOnly)
            {
                const auto launchScene = documentManager.capture_active_scene_launch_snapshot();
                if (!launchScene || launchScene->identity.tabId != activeDocumentTab->id())
                {
                    statusMessage = "The active scene tab no longer has a saved launch snapshot.";
                    return false;
                }
                std::error_code sceneError;
                const auto savedScene = std::filesystem::canonical(launchScene->sourcePath, sceneError);
                if (sceneError || !std::filesystem::is_regular_file(savedScene))
                {
                    statusMessage = "The saved scene file is not available for Run.";
                    return false;
                }
                request.savedScenePath = savedScene;
                request.scene = launchScene->scene;
                request.sceneDocumentIdentity = launchScene->identity;
                request.sceneSavedHistoryToken = launchScene->savedHistoryToken;
                request.sceneSavedFileIdentity = launchScene->savedFileIdentity;
            }
            catalogIdentity.canonicalProjectPath = identity.canonicalProjectPath;
            catalogIdentity.profile = identity.profile;
            catalogIdentity.target = identity.target;
            catalogIdentity.executablePath = identity.executablePath;
            catalogIdentity.binaryIdentity.clear();
            catalogIdentity.sourceInputIdentity = identity.sourceInputIdentity;
            behaviorCatalog.configure(preferences.run.catalogProtocol == "common-runner-v1"
                                          ? CatalogProtocol::CommonRunnerV1
                                          : CatalogProtocol::Unavailable,
                                      catalogIdentity);
            const auto launchDocumentTab = request.sceneDocumentIdentity
                ? std::optional<TabId>{request.sceneDocumentIdentity->tabId} : std::nullopt;
            const auto launchScenePath = request.savedScenePath.empty()
                ? std::optional<std::filesystem::path>{}
                : std::optional<std::filesystem::path>{request.savedScenePath};
            const auto generation = pipelineGeneration++;
            if (!gamePipeline.start(std::move(request), behaviorCatalog, generation))
            {
                statusMessage = gamePipeline.status().message;
                return false;
            }
            gameSessionSourceTab = launchDocumentTab;
            gameSessionSourcePath = launchScenePath;
            gameSessionStaleMessage.clear();
            gameSessionRestartAvailable = false;
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
            gamePanel.set_pipeline_status({}, false);
#endif
            activePipelineIdentity = std::move(identity);
            hasActivePipelineIdentity = true;
            if (!skipConfigure)
            {
                lastConfiguredArgv = buildCommands.configure;
                lastConfiguredProjectRoot = activeProjectRoot;
            }
            statusMessage = buildOnly ? "Build started." : "Build and Run started.";
            return true;
        };
        auto queue_pipeline = [&](bool buildOnly) {
            if (!projectOpened)
            {
                statusMessage = "Open a project before building or running a scene.";
                return;
            }
            if (applyProjectRootsAfterFrame || pendingProjectRoots)
            {
                statusMessage = "Wait for the selected project to finish replacing the current roots.";
                return;
            }
            if (gamePipeline.busy())
            {
                statusMessage = "A project operation is already running.";
                return;
            }
            if (catalogQuery.running())
            {
                statusMessage = "Wait for the active catalog refresh or stop it before starting a build.";
                return;
            }
            pendingPipelineBuildOnly = buildOnly;
            try
            {
                request_source_scan();
                nextSourceScan = std::chrono::steady_clock::now() + source_scan_interval();
                statusMessage = buildOnly ? "Refreshing source identity before Build..."
                                          : "Refreshing source identity before Build and Run...";
            }
            catch (const std::exception &error)
            {
                pendingPipelineBuildOnly.reset();
                statusMessage = std::string("Could not refresh source identity: ") + error.what();
            }
        };
        auto undo_edit = [&] {
            if (!(animationWindowFocused && showAnimationEditorWindow &&
                  documentManager.active_animation_tab() != nullptr))
                commit_scene_gesture();
            execute_focused_undo(animationWindowFocused && showAnimationEditorWindow,
                                 documentManager, *editCommands, workspace->document_for_edit(),
                                 *selection, statusMessage);
        };
        auto redo_edit = [&] {
            if (!(animationWindowFocused && showAnimationEditorWindow &&
                  documentManager.active_animation_tab() != nullptr))
                commit_scene_gesture();
            execute_focused_redo(animationWindowFocused && showAnimationEditorWindow,
                                 documentManager, *editCommands, workspace->document_for_edit(),
                                 *selection, statusMessage);
        };
        auto finish_successful_adoption = [&] {
            request_validation();
            if (workspace->document().sourcePath)
                record_adopted_scene(preferences, *workspace->document().sourcePath);
            persist_preferences();
        };
        auto request_save_as = [&] {
            dialogs->save_scene(window, activeDocumentTab->identity(),
                                workspace->document().sourcePath.value_or(activeProjectRoot));
        };
        auto request_open_scene = [&] {
            openSceneDialogPending = true;
            statusMessage = "Choose a scene file to open...";
            dialogs->open_scene(window, activeDocumentTab->identity(), activeProjectRoot);
        };
        auto open_scene_in_tab = [&](const std::filesystem::path &path) {
            inspectorPanel.cancel_active_interactions(
                workspace != nullptr ? &workspace->document_for_edit() : nullptr);
            std::string error;
            const auto openedTab = documentManager.open_scene(path, &error);
            if (!openedTab)
            {
                statusMessage = "Scene could not be opened: " + error;
                return false;
            }
            bind_active_document();
            if (workspace->document().sourcePath)
            {
                preferences.activeTabPath = *workspace->document().sourcePath;
                record_adopted_scene(preferences, *workspace->document().sourcePath);
            }
            statusMessage = "Scene opened in its own tab.";
            return true;
        };
        auto refresh_behavior_catalog = [&] {
            if (!behaviorCatalog.can_query())
            {
                statusMessage = "Catalog refresh is disabled because this target has not declared common-runner-v1.";
                return false;
            }
            const auto executable = preferences.run.executable.empty()
                ? std::filesystem::path{} : native_path_from_utf8(preferences.run.executable);
            const auto selectedExecutable = executable.is_absolute()
                ? executable : activeProjectRoot / executable;
            std::error_code identityError;
            const auto canonicalExecutable = std::filesystem::canonical(selectedExecutable, identityError);
            if (identityError || !std::filesystem::is_regular_file(canonicalExecutable))
            {
                const std::string message = "The configured executable does not exist; build the selected target first.";
                behaviorCatalog.invalidate(message);
                statusMessage = message;
                return false;
            }
            const std::string binaryIdentity = executable_identity(canonicalExecutable);
            if (binaryIdentity.empty())
            {
                statusMessage = "Could not identify the selected executable.";
                return false;
            }
            catalogIdentity.executablePath = selectedExecutable.lexically_normal().string();
            catalogIdentity.binaryIdentity = binaryIdentity;
            catalogIdentity.sourceInputIdentity = sourceInputIdentity;
            catalogIdentity.profile = preferences.run.buildPreset.empty()
                ? (preferences.run.production ? "production" : "debug") : preferences.run.buildPreset;
            catalogIdentity.target = preferences.run.target;
            catalogIdentity.canonicalProjectPath = activeProjectRoot.string();
            behaviorCatalog.configure(CatalogProtocol::CommonRunnerV1, catalogIdentity);
            ProcessRequest request;
            request.argv = {canonicalExecutable.string(), "--editor-catalog"};
            request.workingDirectory = activeProjectRoot;
            request.outputMode = ProcessOutputMode::Separate;
            if (catalogQuery.start(behaviorCatalog, std::move(request), catalogQueryGeneration++))
            {
                statusMessage = "Refreshing behavior catalog...";
                return true;
            }
            statusMessage = behaviorCatalog.last_error();
            return false;
        };
        AssetBrowseCallback browse_asset = [&](PropertyAddress address, std::string expectedAssetType,
                                                bool nullable) {
            pendingAssetAssignment = PendingAssetAssignment{
                {std::move(address)}, selection->current(), expectedAssetType, nullable};
            dialogs->open_asset(window, activeDocumentTab->identity(), activeAssetRoot,
                                expectedAssetType);
        };
        inspectorPanel.set_multi_asset_browse_callback(
            [&](std::vector<PropertyAddress> addresses, Selection originSelection,
                std::string expectedAssetType, bool nullable) {
                pendingAssetAssignment = PendingAssetAssignment{
                    std::move(addresses), std::move(originSelection), expectedAssetType, nullable};
                dialogs->open_asset(window, activeDocumentTab->identity(), activeAssetRoot,
                                    expectedAssetType);
            });
        auto execute_pending = [&](UnsavedDecision decision) {
            if (decision == UnsavedDecision::Save)
                commit_scene_gesture();
            else if (decision == UnsavedDecision::Discard)
                cancel_scene_gesture();
            WorkspaceActionResult result{WorkspaceActionStatus::Failed, "no pending workspace action"};
            if (pendingAction == PendingAction::NewDocument)
                result = workspace->new_document(decision);
            else if (pendingAction == PendingAction::OpenDocument)
                result = workspace->open_document(pendingOpenPath, decision);
            else if (pendingAction == PendingAction::ChangeProjectRoots)
            {
                if (decision == UnsavedDecision::Cancel)
                    result = {WorkspaceActionStatus::Cancelled, {}};
                else if (decision == UnsavedDecision::Save && !workspace->document().sourcePath)
                {
                    request_save_as();
                    showUnsavedPrompt = false;
                    return;
                }
                else if (decision == UnsavedDecision::Save)
                    result = workspace->save();
                else
                    result = {WorkspaceActionStatus::Completed, {}};
            }
            else if (pendingAction == PendingAction::BuildAndRun)
            {
                if (decision == UnsavedDecision::Cancel)
                    result = {WorkspaceActionStatus::Cancelled, {}};
                else if (decision == UnsavedDecision::Discard && workspace->document().sourcePath)
                {
                    if (workspace->dirty())
                    {
                        const auto reloaded = workspace->reload_source_file();
                        if (reloaded)
                        {
                            selection->repair(workspace->document());
                            scenePreview->reconcile(workspace->document(), true);
                            validationService.cancel();
                            validationService.request(workspace->document());
                            clear_validation_dependencies();
                            validation = ValidationResult{};
                            result = {WorkspaceActionStatus::Completed, "Discarded unsaved changes; reloaded scene from disk."};
                        }
                        else
                            result = {WorkspaceActionStatus::Failed, reloaded.message};
                    }
                    else
                        result = {WorkspaceActionStatus::Completed, {}};
                }
                else if (decision == UnsavedDecision::Save && !workspace->document().sourcePath)
                {
                    request_save_as();
                    showUnsavedPrompt = false;
                    return;
                }
                else if (decision == UnsavedDecision::Save)
                    result = workspace->save();
                else
                    result = {WorkspaceActionStatus::Failed, "Build and Run requires a saved scene."};
            }
            else if (pendingAction == PendingAction::Exit)
            {
                if (decision == UnsavedDecision::Cancel)
                    result = {WorkspaceActionStatus::Cancelled, {}};
                else if (decision == UnsavedDecision::Discard)
                    result = {WorkspaceActionStatus::Completed, {}};
                else if (workspace->dirty() && !workspace->document().sourcePath)
                {
                    request_save_as();
                    showUnsavedPrompt = false;
                    return;
                }
                else
                {
                    inspectorPanel.cancel_active_interactions(
                workspace != nullptr ? &workspace->document_for_edit() : nullptr);
            scenePanel.commit_gesture();
                    const auto savedAll = documentManager.save_all();
                    result = savedAll.action;
                    if (savedAll.failedTab && savedAll.action.conflict)
                    {
                        if (const auto *animation =
                                documentManager.find_animation_tab(*savedAll.failedTab))
                        {
                            externalAnimationConflict = AnimationExternalConflict{
                                animation->identity(), animation->document().source_file_status()};
                            showAnimationEditorWindow = true;
                            showUnsavedPrompt = false;
                        }
                    }
                }
            }

            if (result.status == WorkspaceActionStatus::Completed)
            {
                const bool runAfterSave = pendingAction == PendingAction::BuildAndRun;
                const bool replaceProjectRoots = pendingAction == PendingAction::ChangeProjectRoots;
                if (pendingAction == PendingAction::Exit)
                    running = false;
                else if (pendingAction == PendingAction::OpenDocument)
                    finish_successful_adoption();
                else if (replaceProjectRoots)
                    applyProjectRootsAfterFrame = true;
                pendingAction = PendingAction::None;
                pendingOpenPath.reset();
                showUnsavedPrompt = false;
                statusMessage = result.message;
                if (decision == UnsavedDecision::Save && running)
                {
                    if (workspace->document().sourcePath)
                        record_adopted_scene(preferences, *workspace->document().sourcePath);
                    persist_preferences();
                }
                if (runAfterSave && running)
                    queue_pipeline(false);
            }
            else if (result.status == WorkspaceActionStatus::Cancelled)
            {
                pendingAction = PendingAction::None;
                pendingOpenPath.reset();
                pendingProjectRoots.reset();
                showUnsavedPrompt = false;
            }
            else
            {
                statusMessage = result.message;
                if (result.message.find("changed on disk") != std::string::npos)
                    showExternalConflictPrompt = true;
                else if (decision == UnsavedDecision::Discard ||
                         (pendingAction == PendingAction::BuildAndRun &&
                          result.message.find("changed on disk") == std::string::npos))
                {
                    pendingAction = PendingAction::None;
                    pendingOpenPath.reset();
                    pendingProjectRoots.reset();
                    showUnsavedPrompt = false;
                }
                else
                    showUnsavedPrompt = true;
            }
        };
        auto begin_replacement = [&](PendingAction action, std::optional<std::filesystem::path> path = {}) {
            commit_scene_gesture();
            if (action != PendingAction::ChangeProjectRoots)
                pendingProjectRoots.reset();
            pendingAction = action;
            pendingOpenPath = std::move(path);
            if (workspace->dirty())
                showUnsavedPrompt = true;
            else
                execute_pending(UnsavedDecision::Discard);
        };
        auto request_project_folder = [&] {
            if (gamePipeline.busy() || catalogQuery.running() || pendingPipelineBuildOnly ||
                pendingAction != PendingAction::None || applyProjectRootsAfterFrame)
            {
                statusMessage = "Stop the active project operation before changing project roots.";
                return;
            }
            dialogs->select_project_folder(window, activeDocumentTab->identity(),
                                           activeProjectRoot);
        };
        auto perform_close_document_tab = [&](TabId id, UnsavedDecision decision) {
            auto *tab = documentManager.find(id);
            if (tab == nullptr)
                return WorkspaceActionResult{WorkspaceActionStatus::Failed,
                                             "the requested scene tab is no longer open"};
            const bool wasActive = activeDocumentTab == tab;
            const auto scenePath = tab->workspace().document().sourcePath;
            const DocumentIdentity closingIdentity{
                ProjectGeneration{tab->workspace().document().projectGeneration}, tab->id(),
                DocumentGeneration{tab->workspace().document().documentGeneration}};
            inspectorPanel.cancel_active_interactions(&tab->workspace().document_for_edit());
            if (wasActive)
            {
                if (decision == UnsavedDecision::Discard)
                    scenePanel.cancel_gesture(*editCommands, workspace->document_for_edit());
                else
                    inspectorPanel.cancel_active_interactions(
                workspace != nullptr ? &workspace->document_for_edit() : nullptr);
            scenePanel.commit_gesture();
                const auto currentView = scenePanel.view_state();
                tab->view_state() = SceneViewState{currentView.center.x,
                                                   currentView.center.y,
                                                   currentView.zoom};
                tab->set_expanded_hierarchy_ids(hierarchyPanel.expanded_object_ids());
                activeDocumentTab = nullptr;
                workspace = nullptr;
                selection = nullptr;
                editCommands = nullptr;
            }
            auto result = documentManager.close_tab(id, decision);
            if (!result)
            {
                if (wasActive)
                    bind_active_document();
                return result;
            }
            const bool cancelledPendingRun = pendingRunValidation.ticket() &&
                pendingRunValidation.ticket()->document == closingIdentity &&
                pendingRunValidation.cancel();
            projectSession.resource_index().unregister_document(closingIdentity);
            if (scenePath)
            {
                const auto openTabIds = documentManager.tab_ids();
                const bool stillOpen = std::any_of(openTabIds.begin(),
                    openTabIds.end(), [&](TabId openId) {
                        const auto *openTab = documentManager.find(openId);
                        return openTab != nullptr && openTab->workspace().document().sourcePath == scenePath;
                    });
                if (!stillOpen)
                {
                    std::erase_if(preferences.openTabs, [&](const SavedTabPreferences &saved) {
                        return saved.scenePath.lexically_normal() == scenePath->lexically_normal();
                    });
                    if (preferences.activeTabPath &&
                        preferences.activeTabPath->lexically_normal() == scenePath->lexically_normal())
                        preferences.activeTabPath.reset();
                }
            }
            closePromptTab.reset();
            closeAfterSaveTab.reset();
            if (wasActive)
                bind_active_document();
            deferredPreferenceSave.schedule(std::chrono::steady_clock::now());
            statusMessage = result.message.empty() ? "Scene tab closed." : result.message;
            if (cancelledPendingRun)
                statusMessage = "Pending Build and Run cancelled because its Scene tab was closed. " +
                                statusMessage;
            return result;
        };
        auto close_document_tab = [&](TabId id, UnsavedDecision decision) {
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
            auto *tab = documentManager.find(id);
            auto *playSession = gamePipeline.play_session();
            if (tab != nullptr && playSession != nullptr && playSession->launch_identity() &&
                playSession->state() != PlaySessionState::Idle)
            {
                const DocumentIdentity closingIdentity{
                    ProjectGeneration{tab->workspace().document().projectGeneration}, tab->id(),
                    DocumentGeneration{tab->workspace().document().documentGeneration}};
                if (playSession->launch_identity()->document == closingIdentity)
                {
                    pendingGameSessionCloseTab = id;
                    pendingGameSessionCloseDecision = decision;
                    showGameSessionClosePrompt = true;
                    statusMessage = "Choose whether the embedded Game session should stop with its source tab.";
                    return WorkspaceActionResult{WorkspaceActionStatus::Cancelled,
                        "choose whether to stop the embedded Game session or keep it running"};
                }
            }
#endif
            return perform_close_document_tab(id, decision);
        };
        auto show_animation_conflict = [&](const DocumentIdentity &identity) {
            const auto *tab = documentManager.find_animation_tab(identity.tabId);
            if (tab == nullptr || tab->identity() != identity)
                return;
            if (!externalAnimationConflict || externalAnimationConflict->identity != identity)
                externalAnimationConflict = AnimationExternalConflict{
                    identity, tab->document().source_file_status()};
            externalAnimationSaveAsPending = false;
            pendingAnimationSaveAsIdentity.reset();
            showAnimationEditorWindow = true;
            if (pendingAction == PendingAction::Exit)
                showUnsavedPrompt = false;
        };
        auto request_animation_save_as = [&](const DocumentIdentity &identity) {
            const auto *tab = documentManager.find_animation_tab(identity.tabId);
            if (tab == nullptr || tab->identity() != identity)
            {
                statusMessage = "Animation Save As was cancelled because its tab changed.";
                return;
            }
            pendingAnimationSaveAsIdentity = identity;
            dialogs->save_animation(window, identity,
                tab->document().source_path().value_or(tab->asset_root()));
        };
        auto continue_after_animation_save = [&](const DocumentIdentity &identity) {
            if (const auto *animTab = documentManager.find_animation_tab(identity.tabId))
            {
                if (const auto &source = animTab->document().source_path())
                    apply_project_resource_change(projectSession.resource_index(), *scenePreview,
                                                  workspace->document(), *source, false);
            }
            projectPanel.refresh();
            if (closeAnimationPromptTab == identity.tabId)
            {
                const auto closed = documentManager.close_animation_tab(
                    identity.tabId, UnsavedDecision::Save);
                if (closed)
                {
                    closeAnimationPromptTab.reset();
                    statusMessage = "Animation tab saved and closed.";
                }
                else
                    statusMessage = "Animation tab remains open: " + closed.message;
            }
            if (pendingAction == PendingAction::Exit && !closeAnimationPromptTab)
                execute_pending(UnsavedDecision::Save);
        };
        auto save_animation_as_to = [&](const DocumentIdentity &identity,
                                        const std::filesystem::path &path,
                                        std::optional<AssetOverwriteObservation> overwrite) {
            const bool hadOverwriteObservation = overwrite.has_value();
            const auto saved = documentManager.save_animation_tab_as(identity, path,
                                                                       std::move(overwrite));
            if (saved)
            {
                externalAnimationConflict.reset();
                externalAnimationSaveAsPending = false;
                pendingAnimationSaveAsIdentity.reset();
                animationDestinationOverwrite.reset();
                statusMessage = saved.message;
                continue_after_animation_save(identity);
                return;
            }
            statusMessage = saved.message;
            if (saved.conflict && !hadOverwriteObservation)
            {
                std::string identityError;
                const auto observed = native_file_identity(path, identityError);
                if (identityError.empty())
                {
                    const bool returningToConflict = externalAnimationSaveAsPending ||
                        (externalAnimationConflict &&
                         externalAnimationConflict->identity.tabId == identity.tabId);
                    animationDestinationOverwrite = AnimationDestinationOverwrite{
                        identity, path, AssetOverwriteObservation{observed}, returningToConflict};
                    externalAnimationConflict.reset();
                    return;
                }
                statusMessage += " Could not inspect the destination: " + identityError;
            }
            else if (saved.conflict && animationDestinationOverwrite)
                statusMessage += " The observed destination version is still required; recheck it explicitly before replacing it.";
            if (externalAnimationSaveAsPending && !animationDestinationOverwrite)
            {
                externalAnimationSaveAsPending = false;
                pendingAnimationSaveAsIdentity.reset();
                if (const auto *tab = documentManager.find_animation_tab(identity.tabId);
                    tab != nullptr && tab->identity() == identity)
                    show_animation_conflict(identity);
            }
        };
        auto save_as_to = [&](const std::filesystem::path &path, bool overwrite) {
            inspectorPanel.cancel_active_interactions(
                workspace != nullptr ? &workspace->document_for_edit() : nullptr);
            commit_scene_gesture();
            const auto saved = documentManager.save_scene_tab_as(
                activeDocumentTab->identity(), path, overwrite);
            if (saved)
            {
                statusMessage = saved.message;
                showExternalConflictPrompt = false;
                externalSaveAsPending = false;
                activeDocumentTab->clear_external_change_acknowledgement();
                if (workspace->document().sourcePath)
                    record_adopted_scene(preferences, *workspace->document().sourcePath);
                persist_preferences();
                showOverwritePrompt = false;
                pendingSaveAsPath.clear();
                if (pendingAction != PendingAction::None)
                    execute_pending(pendingAction == PendingAction::Exit
                        ? UnsavedDecision::Save : UnsavedDecision::Discard);
                if (closeAfterSaveTab)
                    close_document_tab(*closeAfterSaveTab, UnsavedDecision::Discard);
            }
            else
            {
                statusMessage = saved.message;
                if (!overwrite && saved.message.find("already exists") != std::string::npos)
                {
                    pendingSaveAsPath = path;
                    showOverwritePrompt = true;
                }
                else if (externalSaveAsPending)
                {
                    externalSaveAsPending = false;
                    showExternalConflictPrompt = true;
                }
                else if (pendingAction != PendingAction::None)
                    showUnsavedPrompt = true;
            }
        };
        auto save_active_animation_tab = [&] {
            auto *animationTab = documentManager.active_animation_tab();
            if (animationTab == nullptr)
                return;
            const auto identity = animationTab->identity();
            const auto saved = documentManager.save_animation_tab(identity);
            statusMessage = saved ? saved.message :
                "Animation save was not committed: " + saved.message;
            if (saved)
            {
                if (const auto &source = animationTab->document().source_path())
                    apply_project_resource_change(projectSession.resource_index(),
                                                  *scenePreview, workspace->document(),
                                                  *source, false);
                projectPanel.refresh();
            }
            if (saved.conflict)
                show_animation_conflict(identity);
        };
        auto save_current = [&] {
            if (animationWindowFocused && showAnimationEditorWindow &&
                documentManager.active_animation_tab() != nullptr)
            {
                save_active_animation_tab();
                return;
            }
            commit_scene_gesture();
            if (!workspace->document().sourcePath)
            {
                request_save_as();
                return;
            }
            const auto saved = workspace->save();
            if (saved)
            {
                statusMessage = saved.message;
                showExternalConflictPrompt = false;
                activeDocumentTab->clear_external_change_acknowledgement();
                record_adopted_scene(preferences, *workspace->document().sourcePath);
                persist_preferences();
            }
            else
            {
                statusMessage = saved.message;
                if (saved.message.find("changed on disk") != std::string::npos)
                    showExternalConflictPrompt = true;
            }
        };
        auto save_all_documents = [&] {
            if (gamePipeline.busy() || catalogQuery.running() || pendingPipelineBuildOnly ||
                pendingAction != PendingAction::None || applyProjectRootsAfterFrame)
            {
                statusMessage = "Stop the active project operation before saving open documents.";
                return;
            }
            inspectorPanel.cancel_active_interactions(
                workspace != nullptr ? &workspace->document_for_edit() : nullptr);
            scenePanel.commit_gesture();
            const auto result = documentManager.save_all();
            if (sync_saved_animation_tabs(documentManager, result.savedTabs,
                                          projectSession.resource_index(), *scenePreview,
                                          workspace->document()))
                projectPanel.refresh();
            if (!result.savedTabs.empty())
                persist_preferences();
            if (result.failedTab)
            {
                if (documentManager.find_animation_tab(*result.failedTab))
                {
                    documentManager.activate_animation_tab(*result.failedTab);
                    showAnimationEditorWindow = true;
                    if (const auto *tab = documentManager.find_animation_tab(*result.failedTab);
                        tab != nullptr && result.action.conflict)
                        show_animation_conflict(tab->identity());
                }
                else
                    activate_document_tab(*result.failedTab);
                const auto savedCount = result.savedTabs.size();
                statusMessage = savedCount == 0
                    ? "Save All stopped: " + result.action.message
                    : "Save All saved " + std::to_string(savedCount) +
                          " document tab(s), then stopped: " + result.action.message;
                if (!documentManager.find_animation_tab(*result.failedTab) &&
                    (workspace->source_file_status().state == SourceFileState::Changed ||
                    workspace->source_file_status().state == SourceFileState::Missing)
                    )
                    showExternalConflictPrompt = true;
                return;
            }
            statusMessage = result.action.message;
        };
        auto request_build_and_run = [&] {
            commit_scene_gesture();
            if (!projectOpened)
            {
                statusMessage = "Open a project before building or running a scene.";
                return;
            }
            if (!current_scene_allows_run())
                return;
            if (showExternalConflictPrompt || externalSaveAsPending)
            {
                statusMessage = "Resolve the changed scene file before Run.";
                return;
            }
            if (applyProjectRootsAfterFrame || pendingProjectRoots)
            {
                statusMessage = "Wait for the selected project to finish replacing the current roots.";
                return;
            }
            if (gamePipeline.busy())
            {
                statusMessage = "A project operation is already running.";
                return;
            }
            if (!validationService.is_current(workspace->document(), validation))
            {
                request_current_validation(workspace->document());
                if (!pendingRunValidation.begin(activeDocumentTab->identity()))
                {
                    statusMessage = "Run was cancelled because validation request identities are exhausted.";
                    return;
                }
                statusMessage = "Waiting for current scene validation before Run...";
                return;
            }
            pendingRunValidation.cancel();
            if (!validation_allows_run(validationService, workspace->document(), validation))
            {
                statusMessage = "Run is blocked by current scene validation errors.";
                preferences.panelVisibility["panel.validation"] = true;
                return;
            }
            if (workspace->dirty())
            {
                pendingAction = PendingAction::BuildAndRun;
                showUnsavedPrompt = true;
                return;
            }
            if (!workspace->document().sourcePath)
            {
                pendingAction = PendingAction::BuildAndRun;
                request_save_as();
                return;
            }
            queue_pipeline(false);
        };
        auto restart_embedded_game = [&] {
            if (gamePipeline.busy())
            {
                pendingGameRestartAfterStop = true;
                gamePipeline.stop();
                statusMessage = "Stopping the embedded Game before Build & Restart.";
                return;
            }
            if (gameSessionSourceTab && documentManager.find(*gameSessionSourceTab) != nullptr)
            {
                if (!activate_document_tab(*gameSessionSourceTab))
                {
                    statusMessage = "Could not activate the saved scene tab used by the embedded Game.";
                    return;
                }
            }
            else if (gameSessionSourcePath)
            {
                std::string error;
                const auto reopened = documentManager.open_scene(*gameSessionSourcePath, &error);
                if (!reopened || !activate_document_tab(*reopened))
                {
                    statusMessage = "Could not reopen the saved scene used by the embedded Game: " + error;
                    return;
                }
                gameSessionSourceTab = *reopened;
            }
            request_build_and_run();
        };
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
        gamePanel.set_restart_callback(restart_embedded_game);
#endif
        auto stop_owned_operation = [&] {
            if (pendingRunValidation.cancel())
                statusMessage = "Build and Run cancelled while waiting for current scene validation.";
            else if (pendingPipelineBuildOnly)
            {
                pendingPipelineBuildOnly.reset();
                statusMessage = "Build request cancelled before the pipeline started.";
            }
            else if (pendingAction == PendingAction::BuildAndRun)
            {
                pendingAction = PendingAction::None;
                showUnsavedPrompt = false;
                statusMessage = "Build and Run cancelled before the pipeline started.";
            }
            else if (catalogQuery.running())
            {
                catalogQuery.stop();
                behaviorCatalog.invalidate("catalog refresh stopped by the user");
                statusMessage = "Catalog refresh stopped.";
            }
            else
            {
                gamePipeline.stop();
                statusMessage = "Stopping the owned game process.";
            }
        };
        auto apply_project_root_change = [&] {
            if (!pendingProjectRoots)
            {
                applyProjectRootsAfterFrame = false;
                statusMessage = "Project folder selection was lost; the current project remains active.";
                return;
            }
            if (gamePipeline.busy() || catalogQuery.running() || pendingPipelineBuildOnly)
            {
                pendingPipelineBuildOnly.reset();
                gamePipeline.stop();
                catalogQuery.stop();
                behaviorCatalog.invalidate("project replacement cancelled the active operation");
                statusMessage = "Stopping project work before replacing roots.";
                return;
            }

            const ProjectRootCandidate candidate = *pendingProjectRoots;
            const auto oldSnapshot = projectContext.snapshot();
            if (candidate.projectRoot == oldSnapshot.projectRoot && candidate.assetRoot == oldSnapshot.assetRoot)
            {
                pendingProjectRoots.reset();
                applyProjectRootsAfterFrame = false;
                statusMessage = "The selected project folder is already active.";
                return;
            }

            const bool cancelledPendingRun = pendingRunValidation.cancel();
            const std::string runCancellationMessage = cancelledPendingRun
                ? "Pending Build and Run cancelled because project roots changed. " : "";

            const auto oldPreferencePaths = preferencePaths;
            const bool oldProjectOpened = projectOpened;
            const std::string oldLayout = savedLayout;
            const float oldActiveScale = activeScale;
            std::size_t currentLayoutSize = 0;
            if (const char *currentLayout = ImGui::SaveIniSettingsToMemory(&currentLayoutSize))
                savedLayout.assign(currentLayout, currentLayoutSize);
            persist_preferences();
            WorkspacePreferences oldPreferences = preferences;

            WorkspacePreferences nextPreferences;
            nextPreferences.projectKey = candidate.projectRoot.string();
            std::string nextLayout;
            std::optional<ProjectPreferencePaths> nextPreferencePaths;
            std::string preferenceMessage;
            try
            {
                if (!preferenceRootPath.empty())
                {
                    nextPreferencePaths = make_project_preference_paths(preferenceRootPath,
                                                                         candidate.projectRoot);
                    auto loaded = load_workspace_preferences(nextPreferencePaths->settingsPath,
                                                              nextPreferencePaths->layoutPath,
                                                              candidate.projectRoot);
                    nextPreferences = std::move(loaded.preferences);
                    nextLayout = migrate_scene_window_identity(loaded.layout);
                    if (!loaded.settingsPresent)
                        apply_known_bundled_run_defaults(nextPreferences, candidate.projectRoot);
                    if (loaded.recovered)
                        preferenceMessage = loaded.message;
                }
                auto panelVisibility = default_panel_visibility();
                for (const auto &[stableId, visible] : nextPreferences.panelVisibility)
                    if (panelVisibility.contains(stableId))
                        panelVisibility[stableId] = visible;
                nextPreferences.panelVisibility = std::move(panelVisibility);
            }
            catch (const std::exception &error)
            {
                nextPreferences = WorkspacePreferences{};
                nextPreferences.projectKey = candidate.projectRoot.string();
                nextPreferences.panelVisibility = default_panel_visibility();
                nextLayout.clear();
                nextPreferencePaths.reset();
                preferenceMessage = std::string("New project preferences were reset: ") + error.what();
            }

            sourceScan.cancel();
            sourceScan = {};
            sourceScans.cancel_pending_and_wait();
            assetScans.cancel_pending_and_wait();
            reset_asset_watcher();
            projectPanel.refresh();
            dialogs->shutdown();
            pendingAssetAssignment.reset();
            cancel_scene_gesture();
            behaviorCatalog.invalidate("project context changed");
            sourceInputIdentity.clear();
            sourceInputIdentityInitialized = false;
            lastConfiguredArgv.clear();
            lastConfiguredProjectRoot.clear();
            sourceScanRequest = 0;

            try
            {
                const auto changed = projectSession.set_project_roots(candidate.projectRoot,
                                                                       candidate.assetRoot);
                if (!changed)
                    throw std::runtime_error(changed.message);
                activeProjectRoot = projectContext.snapshot().projectRoot;
                activeAssetRoot = projectContext.snapshot().assetRoot;
                runSettingsPanel.set_project_root(activeProjectRoot);
                close_all_detached_hosts();
                preferences = std::move(nextPreferences);
                restore_detached_hosts();
                set_runtime_window_preferences();
                preferencePaths = std::move(nextPreferencePaths);
                savedLayout = std::move(nextLayout);
                int displayWidth = 0;
                int displayHeight = 0;
                SDL_GetWindowSize(window, &displayWidth, &displayHeight);
                if (layout_needs_recovery(savedLayout, displayWidth, displayHeight))
                    savedLayout = defaultLayout;
                activeScale = options.uiScaleProvided ? options.uiScale : preferences.userScale;
                if (activeScale < 0.5f || activeScale > 3.0f || !std::isfinite(activeScale))
                    activeScale = 1.0f;
                lastAppliedScale = -1.0f;

                dialogs = std::make_unique<FileDialogService>(projectContext.snapshot().generation);
                rebuild_runtime_context(activeAssetRoot);
                catalogIdentity = CatalogIdentity{};
                catalogIdentity.canonicalProjectPath = activeProjectRoot.string();
                catalogIdentity.profile = preferences.run.buildPreset;
                catalogIdentity.target = preferences.run.target;
                catalogIdentity.executablePath = preferences.run.executable;
                behaviorCatalog.configure(preferences.run.catalogProtocol == "common-runner-v1"
                                              ? CatalogProtocol::CommonRunnerV1
                                              : CatalogProtocol::Unavailable,
                                          catalogIdentity);
                initialCatalogRefreshPending = behaviorCatalog.can_query();
                catalogQueryGeneration++;
                nextSourceScan = std::chrono::steady_clock::now() + source_scan_interval();
                handledSourceRevision = projectSession.source_change_revision();
                request_source_scan();
                if (workspace->document().sourcePath)
                    record_adopted_scene(preferences, *workspace->document().sourcePath);
                projectOpened = true;
                pendingProjectRoots.reset();
                applyProjectRootsAfterFrame = false;
                statusMessage = runCancellationMessage + (preferenceMessage.empty()
                    ? "Project and asset roots changed; document and undo history were preserved."
                    : preferenceMessage);
                persist_preferences();
            }
            catch (const std::exception &error)
            {
                const std::string replacementError = error.what();
                try
                {
                    if (projectContext.snapshot().projectRoot != oldSnapshot.projectRoot ||
                        projectContext.snapshot().assetRoot != oldSnapshot.assetRoot)
                    {
                        const auto restored = projectSession.set_project_roots(
                            oldSnapshot.projectRoot, oldSnapshot.assetRoot);
                        if (!restored)
                            throw std::runtime_error(restored.message);
                    }
                    activeProjectRoot = oldSnapshot.projectRoot;
                    activeAssetRoot = oldSnapshot.assetRoot;
                    runSettingsPanel.set_project_root(activeProjectRoot);
                    projectOpened = oldProjectOpened;
                    close_all_detached_hosts();
                    preferences = std::move(oldPreferences);
                    set_runtime_window_preferences();
                    preferencePaths = oldPreferencePaths;
                    savedLayout = oldLayout;
                    activeScale = oldActiveScale;
                    lastAppliedScale = -1.0f;
                    dialogs = std::make_unique<FileDialogService>(projectContext.snapshot().generation);
                    rebuild_runtime_context(activeAssetRoot);
                    restore_detached_hosts();
                    catalogIdentity.canonicalProjectPath = activeProjectRoot.string();
                    catalogIdentity.profile = preferences.run.buildPreset;
                    catalogIdentity.target = preferences.run.target;
                    catalogIdentity.executablePath = preferences.run.executable;
                    behaviorCatalog.configure(preferences.run.catalogProtocol == "common-runner-v1"
                                                  ? CatalogProtocol::CommonRunnerV1
                                                  : CatalogProtocol::Unavailable,
                                              catalogIdentity);
                    initialCatalogRefreshPending = behaviorCatalog.can_query();
                    nextSourceScan = std::chrono::steady_clock::now() + source_scan_interval();
                    handledSourceRevision = projectSession.source_change_revision();
                    request_source_scan();
                    statusMessage = runCancellationMessage +
                        "Project change failed; the previous project was restored: " + replacementError;
                }
                catch (const std::exception &restoreError)
                {
                    throw std::runtime_error("project replacement failed (" + replacementError +
                                             ") and the previous Runtime could not be restored (" +
                                             restoreError.what() + ")");
                }
                pendingProjectRoots.reset();
                applyProjectRootsAfterFrame = false;
            }
        };

        if (!options.noRestore)
        {
            if (workspace->document().sourcePath)
            {
                const auto currentPath = workspace->document().sourcePath->lexically_normal();
                const auto saved = std::find_if(preferences.openTabs.begin(), preferences.openTabs.end(),
                    [&](const SavedTabPreferences &entry) {
                        return entry.scenePath.lexically_normal() == currentPath;
                    });
                if (saved != preferences.openTabs.end())
                {
                    activeDocumentTab->view_state() = SceneViewState{
                        saved->viewCenterX, saved->viewCenterY, saved->viewZoom};
                    if (saved->hasExpansionState)
                        activeDocumentTab->set_expanded_hierarchy_ids(saved->expandedHierarchyIds);
                }
            }
            scenePanel.restore_view_state(workspace->document(),
                activeDocumentTab->view_state().centerX,
                activeDocumentTab->view_state().centerY,
                activeDocumentTab->view_state().zoom);
            if (activeDocumentTab->has_expansion_state())
                hierarchyPanel.restore_expanded_object_ids(
                    workspace->document(), activeDocumentTab->expanded_hierarchy_ids());
            else
                hierarchyPanel.reset_expansion_for_document();
            activeTabStateInitialized = true;
            for (const auto &savedTab : preferences.openTabs)
            {
                std::string error;
                const auto restoredId = documentManager.open_scene(savedTab.scenePath, &error);
                if (!restoredId)
                {
                    if (statusMessage.empty())
                        statusMessage = "A saved scene tab could not be restored: " + error;
                    continue;
                }
                if (auto *tab = documentManager.find(*restoredId))
                {
                    tab->view_state() = SceneViewState{savedTab.viewCenterX,
                                                       savedTab.viewCenterY,
                                                       savedTab.viewZoom};
                    if (savedTab.hasExpansionState)
                        tab->set_expanded_hierarchy_ids(savedTab.expandedHierarchyIds);
                }
            }
            if (preferences.activeTabPath)
            {
                std::string error;
                if (!documentManager.open_scene(*preferences.activeTabPath, &error) &&
                    statusMessage.empty())
                    statusMessage = "The previously active scene tab could not be restored: " + error;
            }
            bind_active_document();
        }

        std::optional<SDL_Event> pendingWakeEvent;
        int inputSettleFrames = 0;
        EditorAppTickDriver tickDriver(std::move(probeOptions));
        while (tickDriver.tick([&]() {
            if (!running)
                return false;
            std::optional<RunValidationTicket> runValidationReadyThisFrame;
        {
            bool deleteKeyPressedThisFrame = false;
            bool mouseButtonEventThisFrame = false;
            if (deferredPreferenceSave.take_if_ready(std::chrono::steady_clock::now()))
                persist_preferences();
            const auto currentSourceRevision = projectSession.source_change_revision();
            if (currentSourceRevision != handledSourceRevision)
            {
                handledSourceRevision = currentSourceRevision;
                sourceInputIdentity.clear();
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
                if (auto *playSession = gamePipeline.play_session(); playSession != nullptr &&
                    playSession->state() != PlaySessionState::Idle)
                {
                    gameSessionStaleMessage = "Source inputs changed. The embedded session is stopping because its build is stale. Build & Restart to run the latest saved scene and code.";
                    gameSessionRestartAvailable = true;
                }
#endif
                catalogIdentity.sourceInputIdentity.clear();
                behaviorCatalog.invalidate("configured source inputs changed; refresh before Run");
                if (gamePipeline.busy() && hasActivePipelineIdentity)
                {
                    auto invalidIdentity = activePipelineIdentity;
                    invalidIdentity.sourceInputIdentity.clear();
                    gamePipeline.update_identity(invalidIdentity);
                    activePipelineIdentity = std::move(invalidIdentity);
                }
                if (!sourceScan)
                {
                    try
                    {
                        request_source_scan();
                        nextSourceScan = std::chrono::steady_clock::now() + source_scan_interval();
                        statusMessage = "Configured source inputs changed; refreshing before the next Build or Run.";
                    }
                    catch (const std::exception &error)
                    {
                        statusMessage = std::string("Could not refresh source identity after a notification: ") +
                                        error.what();
                    }
                }
            }
            catalogQuery.poll();
            gamePipeline.poll();
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
            if (auto *playSession = gamePipeline.play_session())
                while (auto event = playSession->take_event())
                    gamePanel.consume_event(*playSession, *event);
            if (!gameSessionStaleMessage.empty())
                gamePanel.set_pipeline_status(gameSessionStaleMessage, gameSessionRestartAvailable);
            else if (!gamePipeline.busy() && gamePipeline.status().outcome != PipelineOutcome::None)
                gamePanel.set_pipeline_status(gamePipeline.status().message, false);
            else
                gamePanel.set_pipeline_status({}, false);
#endif
            if (waitingForGameSessionStopBeforeClose && pendingGameSessionCloseTab &&
                !gamePipeline.busy())
            {
                const TabId tabId = *pendingGameSessionCloseTab;
                const auto decision = pendingGameSessionCloseDecision;
                pendingGameSessionCloseTab.reset();
                waitingForGameSessionStopBeforeClose = false;
                const auto result = perform_close_document_tab(tabId, decision);
                if (!result)
                    statusMessage = result.message;
            }
            if (pendingGameRestartAfterStop && !gamePipeline.busy())
            {
                pendingGameRestartAfterStop = false;
                restart_embedded_game();
            }
            if (!gamePipeline.busy())
            {
                hasActivePipelineIdentity = false;
                const auto &pipelineStatus = gamePipeline.status();
                const bool reachedBuilding = std::find(pipelineStatus.transitions.begin(),
                                                       pipelineStatus.transitions.end(),
                                                       PipelineStage::Building) !=
                                             pipelineStatus.transitions.end();
                if (pipelineStatus.outcome == PipelineOutcome::Failed ||
                    (pipelineStatus.outcome == PipelineOutcome::Cancelled && !reachedBuilding))
                {
                    lastConfiguredArgv.clear();
                    lastConfiguredProjectRoot.clear();
                }
            }

            for (auto &scanResult : sourceScans.take_ready(projectContext.snapshot().generation))
            {
                if (scanResult.requestId != sourceScanRequest)
                    continue;
                sourceScan = {};
                if (sourceScanRevision != projectSession.source_change_revision())
                {
                    try
                    {
                        request_source_scan();
                        nextSourceScan = std::chrono::steady_clock::now() + source_scan_interval();
                        statusMessage = "Source inputs changed during identity preparation; checking the latest revision.";
                    }
                    catch (const std::exception &error)
                    {
                        statusMessage = std::string("Could not refresh source identity after a concurrent change: ") +
                                        error.what();
                        pendingPipelineBuildOnly.reset();
                    }
                    continue;
                }
                if (scanResult.cancelled || scanResult.truncated || !scanResult.error.empty() ||
                    scanResult.sourceInputIdentity.empty())
                {
                    statusMessage = scanResult.error.empty()
                                        ? "Source identity scan was cancelled, truncated, or found no CMake/source inputs."
                                        : "Source identity scan failed: " + scanResult.error;
                    sourceInputIdentity.clear();
                    pendingPipelineBuildOnly.reset();
                    if (gamePipeline.busy() && hasActivePipelineIdentity)
                    {
                        auto invalidIdentity = activePipelineIdentity;
                        invalidIdentity.sourceInputIdentity.clear();
                        gamePipeline.update_identity(invalidIdentity);
                        activePipelineIdentity = std::move(invalidIdentity);
                    }
                    else
                    {
                        catalogIdentity.sourceInputIdentity.clear();
                        behaviorCatalog.invalidate("source identity scan failed or exceeded its bound");
                    }
                }
                else if (sourceInputIdentity != scanResult.sourceInputIdentity)
                {
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
                    if (auto *playSession = gamePipeline.play_session(); playSession != nullptr &&
                        playSession->state() != PlaySessionState::Idle)
                    {
                        gameSessionStaleMessage = "Source inputs changed. The embedded session is stopping because its build is stale. Build & Restart to run the latest saved scene and code.";
                        gameSessionRestartAvailable = true;
                    }
#endif
                    sourceInputIdentity = std::move(scanResult.sourceInputIdentity);
                    if (gamePipeline.busy() && hasActivePipelineIdentity)
                    {
                        auto changedIdentity = activePipelineIdentity;
                        changedIdentity.sourceInputIdentity = sourceInputIdentity;
                        if (!same_pipeline_inputs(activePipelineIdentity, changedIdentity))
                        {
                            gamePipeline.update_identity(changedIdentity);
                            activePipelineIdentity = std::move(changedIdentity);
                        }
                    }
                    else
                        catalogIdentity.sourceInputIdentity = sourceInputIdentity;
                    sourceInputIdentityInitialized = true;
                }
                else
                    sourceInputIdentityInitialized = true;
            }
            if (initialCatalogRefreshPending && !sourceScan && !sourceInputIdentity.empty() &&
                !catalogQuery.running() && !gamePipeline.busy())
            {
                initialCatalogRefreshPending = false;
                refresh_behavior_catalog();
            }
            if (pendingPipelineBuildOnly && !sourceScan)
            {
                const bool buildOnly = *pendingPipelineBuildOnly;
                pendingPipelineBuildOnly.reset();
                start_pipeline_now(buildOnly);
            }
            const auto now = std::chrono::steady_clock::now();
            ProjectChangeBatch changeBatch = projectSession.take_change_batch();
            auto &resources = projectSession.resource_index();
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
            if (!changeBatch.events.empty() || changeBatch.rootDirty)
            {
                const auto *playSession = gamePipeline.play_session();
                if (gamePipeline.busy() && playSession != nullptr &&
                    playSession->state() != PlaySessionState::Idle)
                {
                    gameSessionStaleMessage = GamePanel::asset_change_status_message(
                        playSession->capabilities().liveAssetReload);
                    gameSessionRestartAvailable = true;
                }
            }
#endif
            bool validationDependencyCoverageMissing = false;
            projectPanel.apply_changes(changeBatch, [&](const ProjectChangeEvent &event,
                                                        const std::filesystem::path &path) {
                const auto consumers = resources.affected_consumers(path, event.directory);
                const DocumentIdentity currentIdentity{
                    ProjectGeneration{workspace->document().projectGeneration},
                    workspace->document().tabId,
                    DocumentGeneration{workspace->document().documentGeneration}};
                const auto affectedValidationObjects =
                    validation_object_ids_for_resource_consumers(consumers, currentIdentity);
                if (affectedValidationObjects.empty())
                    validationDependencyCoverageMissing = true;
                validationChangedObjectIds.insert(validationChangedObjectIds.end(),
                    affectedValidationObjects.begin(), affectedValidationObjects.end());
                apply_project_resource_change(resources, *scenePreview, workspace->document(),
                                              path, event.directory);
            });
            if (changeBatch.rootDirty &&
                changeBatch.uncertaintyEpoch != appliedResourceRootUncertainty)
            {
                const auto currentEpoch = projectSession.resource_index().uncertainty_epoch();
                const auto nextEpoch = currentEpoch == std::numeric_limits<std::uint64_t>::max()
                    ? currentEpoch : currentEpoch + 1;
                projectSession.resource_index().invalidate_subtree(activeAssetRoot,
                    std::max(changeBatch.uncertaintyEpoch, nextEpoch));
                appliedResourceRootUncertainty = changeBatch.uncertaintyEpoch;
            }
            if (changeBatch.rootDirty && !assetRootRecoveryActive)
                assetWatchSchedule.request_now(now);
            assetRootRecoveryActive = changeBatch.rootDirty;
            if (projectSession.project_changes().coverage() != ProjectCoverageState::Current &&
                now >= nextReferenceFallbackCheck)
            {
                auto fallbackBatch = ProjectChangeBatch{};
                fallbackBatch.projectGeneration = projectSession.generation();
                fallbackBatch.coverage = projectSession.project_changes().coverage();
                for (const auto &fallback : projectSession.resource_index().inspect_fallback_paths(512))
                {
                    ProjectChangeEvent event;
                    event.kind = ProjectChangeKind::Modified;
                    event.path = fallback.logicalPath;
                    event.projectGeneration = fallbackBatch.projectGeneration;
                    fallbackBatch.events.push_back(event);
                    const DocumentIdentity currentIdentity{
                        ProjectGeneration{workspace->document().projectGeneration},
                        workspace->document().tabId,
                        DocumentGeneration{workspace->document().documentGeneration}};
                    const auto affectedValidationObjects =
                        validation_object_ids_for_resource_consumers(
                            fallback.consumers, currentIdentity);
                    if (affectedValidationObjects.empty())
                        validationDependencyCoverageMissing = true;
                    validationChangedObjectIds.insert(validationChangedObjectIds.end(),
                        affectedValidationObjects.begin(), affectedValidationObjects.end());
                    scenePreview->refresh_asset(workspace->document(), fallback.logicalPath,
                                                false, fallback.consumers);
                }
                projectPanel.apply_changes(fallbackBatch);
                nextReferenceFallbackCheck = now + std::chrono::seconds(2);
            }
            if (validationRecoveryGate.should_request_full(
                    changeBatch.rootDirty, changeBatch.projectGeneration,
                    changeBatch.uncertaintyEpoch))
            {
                validationService.request(workspace->document(), true);
                validationChangedObjectIds.clear();
                validationForceFull = false;
            }
            else if (validationDependencyCoverageMissing &&
                     !validationService.is_current(workspace->document(), validation))
            {
                validationService.request(workspace->document(), true);
                validationChangedObjectIds.clear();
                validationForceFull = false;
            }
            else if (!validationChangedObjectIds.empty())
            {
                validationService.request_changed(workspace->document(), validationChangedObjectIds);
                validationChangedObjectIds.clear();
                validationForceFull = false;
            }
            if (!projectSession.monitor_error().empty())
                statusMessage = projectSession.monitor_error();
            if (!sourceScan && now >= nextSourceScan)
            {
                try
                {
                    request_source_scan();
                    nextSourceScan = now + source_scan_interval();
                }
                catch (const std::exception &error)
                {
                    sourceScan = {};
                    statusMessage = std::string("Could not refresh source identity: ") + error.what();
                }
            }

            for (auto &assetScanResult : assetWatchScans.take_ready(projectContext.snapshot().generation))
            {
                if (assetScanResult.requestId != assetWatchScan.request_id())
                    continue;
                assetWatchScan = {};
                const bool snapshotComplete = !assetScanResult.cancelled && !assetScanResult.truncated &&
                                              assetScanResult.error.empty() &&
                                              !assetScanResult.fileIdentity.empty() &&
                                              assetScanResult.fileMetadata.size() == assetScanResult.files.size();
                const bool supersededByNewUncertainty =
                    assetWatchSnapshot.uncertaintyEpoch != projectSession.project_changes().uncertainty_epoch();
                std::error_code rootError;
                const bool rootUnavailable = !std::filesystem::is_directory(activeAssetRoot, rootError) ||
                                             static_cast<bool>(rootError);
                std::vector<std::pair<std::filesystem::path, std::string>> inventory;
                if (snapshotComplete)
                    inventory = std::move(assetScanResult.inventoryEntries);
                const bool adopted = projectSession.project_changes().complete_root_snapshot(
                    assetWatchSnapshot, inventory, snapshotComplete, rootUnavailable);
                assetWatchSnapshot = {};
                if (!snapshotComplete || !adopted)
                {
                    if (!assetScanResult.cancelled)
                        statusMessage = assetScanResult.error.empty()
                            ? "Asset scan was incomplete; coverage remains degraded until a complete retry."
                            : "Asset scan failed: " + assetScanResult.error;
                    assetRootRecoveryActive = true;
                    if (supersededByNewUncertainty)
                        assetWatchSchedule.request_now(now);
                    else
                        assetWatchSchedule.retry_after_failure(now);
                }
                else
                {
                    assetRootRecoveryActive = false;
                    assetWatchSchedule.schedule_success(now, assetWatchInterval);
                }
            }
            if (!assetWatchScan && assetWatchSchedule.due(now))
            {
                try
                {
                    request_asset_watch_scan();
                    assetWatchSchedule.schedule_success(now, assetWatchInterval);
                }
                catch (const std::exception &error)
                {
                    assetWatchScan = {};
                    if (assetWatchSnapshot.requestId != 0)
                        projectSession.project_changes().complete_root_snapshot(assetWatchSnapshot, false);
                    assetWatchSnapshot = {};
                    assetRootRecoveryActive = true;
                    assetWatchSchedule.retry_after_failure(now);
                    statusMessage = std::string("Could not watch assets: ") + error.what();
                }
            }

            if (!gamePipeline.busy())
            {
                const auto executable = native_path_from_utf8(preferences.run.executable);
                const auto selectedExecutable = executable.empty()
                    ? std::filesystem::path{}
                    : (executable.is_absolute() ? executable : activeProjectRoot / executable);
                const std::string activeBuildPreset = preferences.run.buildPreset.empty()
                    ? (preferences.run.production ? "production" : "debug")
                    : preferences.run.buildPreset;

                static std::filesystem::path s_lastConfiguredExecutable;
                static std::string s_lastSourceInputIdentity;
                static std::string s_lastBuildPreset;
                static std::string s_lastTarget;
                static std::string s_lastBinaryIdentity;
                static std::chrono::steady_clock::time_point s_nextCatalogThrottle;

                const bool inputsChanged = (selectedExecutable != s_lastConfiguredExecutable) ||
                                           (sourceInputIdentity != s_lastSourceInputIdentity) ||
                                           (activeBuildPreset != s_lastBuildPreset) ||
                                           (preferences.run.target != s_lastTarget);
                const bool throttleExpired = (now >= s_nextCatalogThrottle);

                if (inputsChanged || throttleExpired || s_lastBinaryIdentity.empty())
                {
                    s_lastConfiguredExecutable = selectedExecutable;
                    s_lastSourceInputIdentity = sourceInputIdentity;
                    s_lastBuildPreset = activeBuildPreset;
                    s_lastTarget = preferences.run.target;
                    s_lastBinaryIdentity = executable_identity(selectedExecutable);
                    s_nextCatalogThrottle = now + std::chrono::seconds(1);

                    catalogIdentity.canonicalProjectPath = activeProjectRoot.string();
                    catalogIdentity.profile = activeBuildPreset;
                    catalogIdentity.target = preferences.run.target;
                    catalogIdentity.executablePath = selectedExecutable.lexically_normal().string();
                    catalogIdentity.binaryIdentity = s_lastBinaryIdentity;
                    catalogIdentity.sourceInputIdentity = sourceInputIdentity;
                    behaviorCatalog.configure(preferences.run.catalogProtocol == "common-runner-v1"
                                                  ? CatalogProtocol::CommonRunnerV1
                                                  : CatalogProtocol::Unavailable,
                                              catalogIdentity);
                }
            }
            if (tickDriver.should_inject_hover_input())
            {
                int width = 0;
                int height = 0;
                SDL_GetWindowSize(window, &width, &height);
                SDL_Event injected{};
                injected.type = SDL_EVENT_MOUSE_MOTION;
                injected.motion.timestamp = SDL_GetTicksNS();
                injected.motion.windowID = SDL_GetWindowID(window);
                injected.motion.x = static_cast<float>(width) * 0.5f;
                injected.motion.y = static_cast<float>(height) * 0.5f;
                SDL_PushEvent(&injected);
            }
            auto poll_next_event = [&](SDL_Event *outEvent) -> bool {
                if (pendingWakeEvent.has_value())
                {
                    *outEvent = *pendingWakeEvent;
                    pendingWakeEvent.reset();
                    return true;
                }
                return SDL_PollEvent(outEvent) != 0;
            };

            SDL_Event event;
            std::vector<HostLayoutId> closeDetachedAfterEvent;
            while (poll_next_event(&event))
            {
                if (event.type == projectWakeEventType)
                    wakeGate->event_consumed();
                if ((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) ||
                    event.type == SDL_EVENT_MOUSE_MOTION ||
                    event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                    event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                    event.type == SDL_EVENT_MOUSE_WHEEL ||
                    event.type == SDL_EVENT_KEY_DOWN ||
                    event.type == SDL_EVENT_KEY_UP ||
                    event.type == SDL_EVENT_TEXT_INPUT ||
                    event.type == SDL_EVENT_TEXT_EDITING)
                {
                    inputSettleFrames = 2;
                }
                if (event.type == SDL_EVENT_MOUSE_MOTION ||
                    event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                    event.type == SDL_EVENT_MOUSE_WHEEL ||
                    event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_TEXT_INPUT)
                    tickDriver.note_input_event(std::chrono::steady_clock::now());
                if (gamePanel.is_input_captured())
                {
                    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
                    {
                        auto *playSession = gamePipeline.play_session();
                        gamePanel.handle_sdl_event(playSession, event);
                    }
                }
                if (!gamePanel.is_input_captured() && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                    event.key.scancode == SDL_SCANCODE_DELETE &&
                    event.key.windowID == SDL_GetWindowID(window))
                    deleteKeyPressedThisFrame = true;
                if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                     event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                    event.button.windowID == SDL_GetWindowID(window))
                    mouseButtonEventThisFrame = true;
                for (auto &[hostId, host] : detachedHosts)
                {
                    host->process_event(event);
                    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                        event.window.windowID == host->window_id())
                        closeDetachedAfterEvent.push_back(hostId);
                }
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                     event.window.windowID == SDL_GetWindowID(window)))
                    closeRequested = true;
            }
            std::sort(closeDetachedAfterEvent.begin(), closeDetachedAfterEvent.end());
            closeDetachedAfterEvent.erase(
                std::unique(closeDetachedAfterEvent.begin(), closeDetachedAfterEvent.end()),
                closeDetachedAfterEvent.end());
            for (const HostLayoutId hostId : closeDetachedAfterEvent)
                close_detached_host(hostId);
            if (mouseButtonEventThisFrame)
                deleteKeyPressedThisFrame = false;

            if (closeRequested && gamePipeline.busy())
            {
                gamePipeline.stop();
                statusMessage = "Stopping the owned game process before closing.";
            }
            if (closeRequested && !gamePipeline.busy() && pendingAction == PendingAction::None &&
                !showExternalConflictPrompt && !showOverwritePrompt &&
                !externalAnimationConflict && !externalAnimationSaveAsPending &&
                !animationDestinationOverwrite)
            {
                const auto animationTabIds = documentManager.animation_tab_ids();
                const bool dirtyAnimationOpen = std::any_of(animationTabIds.begin(),
                    animationTabIds.end(), [&](TabId id) {
                        const auto *tab = documentManager.find_animation_tab(id);
                        return tab != nullptr && tab->document().dirty();
                    });
                if (workspace->dirty() || dirtyAnimationOpen)
                {
                    pendingAction = PendingAction::Exit;
                    showUnsavedPrompt = true;
                    closeRequested = false;
                }
                else
                    running = false;
            }
            if (!running)
                return false;

            const auto externalCheckNow = std::chrono::steady_clock::now();
            if (externalCheckNow >= nextExternalFileCheck)
            {
                nextExternalFileCheck = externalCheckNow + std::chrono::milliseconds(500);
                cachedSourceStatuses = documentManager.source_file_statuses();
                auto activeStatus = std::find_if(cachedSourceStatuses.begin(), cachedSourceStatuses.end(),
                    [&](const DocumentSourceStatus &status) {
                        return status.kind == DocumentTabKind::Scene &&
                               status.identity == activeDocumentTab->identity();
                    });
                const SourceFileStatus sourceStatus = activeStatus == cachedSourceStatuses.end()
                    ? workspace->source_file_status() : activeStatus->file;
                for (const auto &tabStatus : cachedSourceStatuses)
                {
                    if (tabStatus.kind == DocumentTabKind::Scene &&
                        tabStatus.identity == activeDocumentTab->identity())
                        continue;
                    if (tabStatus.kind == DocumentTabKind::Animation &&
                        documentManager.active_animation_tab() != nullptr &&
                        tabStatus.identity == documentManager.active_animation_tab()->identity())
                        continue;
                    if ((tabStatus.file.state == SourceFileState::Changed ||
                         tabStatus.file.state == SourceFileState::Missing) && statusMessage.empty())
                    {
                        statusMessage = tabStatus.kind == DocumentTabKind::Animation
                            ? "An animation asset tab changed on disk. Activate it to resolve the external version."
                            : "A non-active scene tab changed on disk. Activate it to resolve the external version.";
                        break;
                    }
                }
                if (sourceStatus.state == SourceFileState::Current ||
                    sourceStatus.state == SourceFileState::NoSource)
                {
                    activeDocumentTab->clear_external_change_acknowledgement();
                    if (sourceStatus.state == SourceFileState::Current)
                        showExternalConflictPrompt = false;
                }
                else if (sourceStatus.state == SourceFileState::Changed ||
                         sourceStatus.state == SourceFileState::Missing)
                {
                    const bool acknowledged =
                        activeDocumentTab->external_change_acknowledged(sourceStatus);
                    if (!acknowledged && !externalSaveAsPending)
                    {
                        if (!showExternalConflictPrompt)
                            commit_scene_gesture();
                        showExternalConflictPrompt = true;
                    }
                }
                else if (sourceStatus.state == SourceFileState::Unavailable && statusMessage.empty())
                    statusMessage = "Could not check the saved scene file: " + sourceStatus.message;

                if (auto *animationTab = documentManager.active_animation_tab())
                {
                    const auto animationStatus = animationTab->document().source_file_status();
                    if (animationStatus.state == SourceFileState::Current ||
                        animationStatus.state == SourceFileState::NoSource)
                    {
                        animationTab->clear_external_change_acknowledgement();
                        if (externalAnimationConflict &&
                            externalAnimationConflict->identity.tabId == animationTab->id())
                            externalAnimationConflict.reset();
                    }
                    else if (animationStatus.state == SourceFileState::Changed ||
                             animationStatus.state == SourceFileState::Missing)
                    {
                        if (!animationTab->external_change_acknowledged(animationStatus) &&
                            !externalAnimationSaveAsPending && !animationDestinationOverwrite)
                            show_animation_conflict(animationTab->identity());
                    }
                    else if (animationStatus.state == SourceFileState::Unavailable &&
                             statusMessage.empty())
                        statusMessage = "Could not check the saved animation file: " + animationStatus.message;
                }
                else if (externalAnimationConflict)
                    externalAnimationConflict.reset();
            }

            if (externalAnimationSaveAsPending && pendingAnimationSaveAsIdentity)
            {
                const auto pendingIdentity = *pendingAnimationSaveAsIdentity;
                const auto *tab = documentManager.find_animation_tab(pendingIdentity.tabId);
                if (tab == nullptr || tab->identity() != pendingIdentity)
                {
                    externalAnimationSaveAsPending = false;
                    pendingAnimationSaveAsIdentity.reset();
                    animationDestinationOverwrite.reset();
                    if (tab != nullptr)
                    {
                        const auto status = tab->document().source_file_status();
                        if (status.state == SourceFileState::Changed ||
                            status.state == SourceFileState::Missing)
                            show_animation_conflict(tab->identity());
                    }
                    if (pendingAction == PendingAction::Exit)
                        showUnsavedPrompt = true;
                    statusMessage = "Animation Save As expired because its tab or project generation changed; the draft remains open.";
                }
            }

            if (deferredOpenScenePath && !showExternalConflictPrompt && !externalSaveAsPending &&
                !showUnsavedPrompt && !showOverwritePrompt && pendingAction == PendingAction::None &&
                !closeAfterSaveTab && !showManualPath && !pendingAssetAssignment)
            {
                const auto path = std::move(*deferredOpenScenePath);
                deferredOpenScenePath.reset();
                open_scene_in_tab(path);
            }

            for (const auto &result : dialogs->take_ready(activeDocumentTab->identity()))
            {
                if (result.identity.kind == AsyncRequestKind::OpenScene)
                {
                    openSceneDialogPending = false;
                    if (result.outcome == AsyncRequestOutcome::Cancelled)
                        statusMessage = "Open Scene was cancelled; the current scene is unchanged.";
                }
                if (result.identity.kind == AsyncRequestKind::SelectFolder &&
                    result.outcome == AsyncRequestOutcome::Cancelled)
                    statusMessage = "Project folder selection was cancelled; current roots are unchanged.";
                if (result.identity.kind == AsyncRequestKind::OpenExecutable &&
                    result.outcome == AsyncRequestOutcome::Cancelled)
                    statusMessage = "Executable selection was cancelled; run settings are unchanged.";
                if (result.identity.kind == AsyncRequestKind::OpenAsset &&
                    result.outcome == AsyncRequestOutcome::Cancelled)
                    pendingAssetAssignment.reset();
                if (result.identity.kind == AsyncRequestKind::SaveScene &&
                    result.outcome == AsyncRequestOutcome::Cancelled &&
                    pendingAction == PendingAction::BuildAndRun)
                {
                    pendingAction = PendingAction::None;
                    showUnsavedPrompt = false;
                    statusMessage = "Build and Run cancelled while choosing a scene path.";
                }
                if (result.identity.kind == AsyncRequestKind::SaveScene &&
                    result.outcome == AsyncRequestOutcome::Cancelled && closeAfterSaveTab)
                {
                    closeAfterSaveTab.reset();
                    closePromptTab.reset();
                    statusMessage = "Save As was cancelled; the scene tab remains open.";
                }
                else if (result.identity.kind == AsyncRequestKind::SaveScene &&
                         result.outcome == AsyncRequestOutcome::Cancelled &&
                         pendingAction != PendingAction::None && !externalSaveAsPending)
                {
                    showUnsavedPrompt = true;
                    statusMessage = "Save As was cancelled; the pending workspace action is still waiting for a decision.";
                }
                if (result.identity.kind == AsyncRequestKind::SaveScene && externalSaveAsPending &&
                    result.outcome == AsyncRequestOutcome::Cancelled)
                {
                    externalSaveAsPending = false;
                    showExternalConflictPrompt = true;
                }
                if (result.outcome == AsyncRequestOutcome::Failed)
                {
                    dialogError = result.error;
                    manualPathKind = result.identity.kind;
                    manualPathIdentity = result.identity.documentIdentity;
                    showManualPath = true;
                    continue;
                }
                if (result.outcome != AsyncRequestOutcome::Selected)
                    continue;
                if (result.identity.kind == AsyncRequestKind::OpenScene)
                {
                    if (showExternalConflictPrompt || externalSaveAsPending || showUnsavedPrompt ||
                        pendingAction != PendingAction::None || showOverwritePrompt)
                    {
                        deferredOpenScenePath = result.selectedPath;
                        statusMessage = "The selected scene is waiting for the current tab decision.";
                    }
                    else
                        open_scene_in_tab(result.selectedPath);
                }
                else if (result.identity.kind == AsyncRequestKind::SelectFolder)
                {
                    try
                    {
                        pendingProjectRoots = validate_project_folder(result.selectedPath);
                        begin_replacement(PendingAction::ChangeProjectRoots);
                    }
                    catch (const std::exception &error)
                    {
                        pendingProjectRoots.reset();
                        statusMessage = std::string("Project folder was not adopted: ") + error.what();
                    }
                }
                else if (result.identity.kind == AsyncRequestKind::SaveScene)
                {
                    externalSaveAsPending = false;
                    save_as_to(result.selectedPath, false);
                }
                else if (result.identity.kind == AsyncRequestKind::OpenExecutable)
                {
                    std::error_code executableError;
                    const auto absoluteExecutable = std::filesystem::absolute(
                        result.selectedPath, executableError).lexically_normal();
                    if (executableError ||
                        !std::filesystem::is_regular_file(absoluteExecutable, executableError) ||
                        executableError)
                    {
                        statusMessage = "The selected executable path is unavailable or is not a regular file.";
                    }
                    else
                    {
                        preferences.run.executable = native_path_to_utf8(absoluteExecutable);
                        statusMessage = "Run executable path updated. Choose its target and catalog protocol if needed.";
                    }
                }
                else if (result.identity.kind == AsyncRequestKind::OpenAsset && pendingAssetAssignment)
                {
                    auto assignment = std::move(*pendingAssetAssignment);
                    pendingAssetAssignment.reset();
                    const auto completion = apply_asset_browse_completion(
                        workspace->document_for_edit(), *editCommands,
                        assignment.addresses, assignment.selection,
                        result.identity.documentIdentity, activeAssetRoot,
                        result.selectedPath, assignment.expectedAssetType, assignment.nullable);
                    statusMessage = completion.message;
                }
            }

            const auto animationDialogResults = dialogs->take_ready(
                [&](const DocumentIdentity &identity) {
                    const auto *tab = documentManager.find_animation_tab(identity.tabId);
                    return tab != nullptr && tab->identity() == identity;
                });
            for (const auto &result : animationDialogResults)
            {
                if (result.identity.kind != AsyncRequestKind::SaveAnimation)
                    continue;
                const auto identity = result.identity.documentIdentity;
                if (result.outcome == AsyncRequestOutcome::Cancelled)
                {
                    if (externalAnimationSaveAsPending)
                    {
                        externalAnimationSaveAsPending = false;
                        pendingAnimationSaveAsIdentity.reset();
                        if (const auto *tab = documentManager.find_animation_tab(identity.tabId);
                            tab != nullptr && tab->identity() == identity)
                            show_animation_conflict(identity);
                    }
                    statusMessage = "Animation Save As was cancelled; the animation tab remains open.";
                    continue;
                }
                if (result.outcome == AsyncRequestOutcome::Failed)
                {
                    dialogError = result.error;
                    manualPathKind = AsyncRequestKind::SaveAnimation;
                    manualPathIdentity = identity;
                    showManualPath = true;
                    continue;
                }
                save_animation_as_to(identity, result.selectedPath, std::nullopt);
            }

            const auto &currentDocument = workspace->document();
            if (auto completedValidation = validationService.take_current(currentDocument))
            {
                update_validation_dependencies(*completedValidation);
                validation = std::move(completedValidation->result);
            }
            if (!validationService.is_current(currentDocument))
                request_current_validation(currentDocument);
            if (const auto &ticket = pendingRunValidation.ticket(); ticket)
            {
                if (ticket->document != activeDocumentTab->identity())
                {
                    pendingRunValidation.cancel();
                    statusMessage = "Pending Build and Run cancelled because its originating document is no longer active.";
                }
                else if (validationService.is_current(currentDocument, validation))
                    runValidationReadyThisFrame = *ticket;
            }
            scenePreview->poll_asset_reloads(currentDocument);
            if (currentDocument.tabId != previewTabId ||
                currentDocument.documentGeneration != previewDocumentGeneration)
            {
                selection->repair(currentDocument);
                previewStructureChanged = true;
                previewDocumentGeneration = currentDocument.documentGeneration;
                previewTabId = currentDocument.tabId;
            }
            scenePreview->reconcile(currentDocument, previewStructureChanged, previewChangedObjectIds,
                                    previewInheritedTranslationChanged);
            previewStructureChanged = false;
            previewInheritedTranslationChanged = false;
            previewChangedObjectIds.clear();

            int pixelWidth = 0;
            int pixelHeight = 0;
            SDL_GetWindowSize(window, &logicalWidth, &logicalHeight);
            if (!SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight))
                pixelWidth = logicalWidth, pixelHeight = logicalHeight;
            const float totalUiScale = resolve_editor_ui_scale(activeScale, logicalWidth, logicalHeight,
                                                               pixelWidth, pixelHeight);
            if (std::abs(totalUiScale - lastAppliedScale) > 0.001f)
            {
                apply_editor_style(totalUiScale);
                lastAppliedScale = totalUiScale;
            }

            imgui->begin_frame();
            ImGuiContext *mainImGuiContext = ImGui::GetCurrentContext();
            std::unordered_set<HostLayoutId> detachedFramesStarted;
            for (auto &[hostId, host] : detachedHosts)
            {
                host->begin_frame();
                detachedFramesStarted.insert(hostId);
                ImGui::SetCurrentContext(host->context());
                int hostWidth = 0;
                int hostHeight = 0;
                int hostPixelWidth = 0;
                int hostPixelHeight = 0;
                SDL_GetWindowSize(host->window(), &hostWidth, &hostHeight);
                if (!SDL_GetWindowSizeInPixels(host->window(), &hostPixelWidth, &hostPixelHeight))
                    hostPixelWidth = hostWidth, hostPixelHeight = hostHeight;
                apply_editor_style(resolve_editor_ui_scale(activeScale, hostWidth, hostHeight,
                                                           hostPixelWidth, hostPixelHeight));
                submit_editor_dockspace(kEditorDockspaceId, 0.0f);
                ImGui::SetCurrentContext(mainImGuiContext);
            }
            const auto activate_panel_context = [&](std::string_view panelId) -> ImGuiContext * {
                const auto assignment = preferences.panelHostAssignments.find(std::string(panelId));
                if (assignment == preferences.panelHostAssignments.end() ||
                    assignment->second == main_host_layout_id)
                    return nullptr;
                const HostLayoutId hostId = assignment->second;
                const auto host = detachedHosts.find(hostId);
                if (host == detachedHosts.end())
                {
                    assignment->second = main_host_layout_id;
                    schedule_preference_save();
                    return nullptr;
                }
                ImGuiContext *previous = ImGui::GetCurrentContext();
                if (detachedFramesStarted.insert(hostId).second)
                {
                    host->second->begin_frame();
                    int hostWidth = 0;
                    int hostHeight = 0;
                    int hostPixelWidth = 0;
                    int hostPixelHeight = 0;
                    SDL_GetWindowSize(host->second->window(), &hostWidth, &hostHeight);
                    if (!SDL_GetWindowSizeInPixels(host->second->window(),
                                                   &hostPixelWidth, &hostPixelHeight))
                        hostPixelWidth = hostWidth, hostPixelHeight = hostHeight;
                    apply_editor_style(resolve_editor_ui_scale(activeScale, hostWidth, hostHeight,
                                                               hostPixelWidth, hostPixelHeight));
                    submit_editor_dockspace(kEditorDockspaceId, 0.0f);
                }
                else
                    ImGui::SetCurrentContext(host->second->context());
                return previous;
            };
            const auto restore_panel_context = [](ImGuiContext *context) {
                if (context != nullptr)
                    ImGui::SetCurrentContext(context);
            };
            projectSession.resource_index().begin_visible_reference_frame();
            const bool hasModal = showUnsavedPrompt || showOverwritePrompt || showExternalConflictPrompt ||
                                  showManualPath || showPreferences || showShortcutReference ||
                                  externalAnimationConflict.has_value() ||
                                  externalAnimationSaveAsPending ||
                                  animationDestinationOverwrite.has_value() ||
                                  showGameSessionClosePrompt ||
                                  ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
            SDL_Window *keyboardFocus = SDL_GetKeyboardFocus();
            ImGuiContext *inputContext = ImGui_ImplSDL3_EditorContextForWindow(keyboardFocus);
            const bool appFocused = inputContext != nullptr;
            if (!appFocused)
                inputContext = mainImGuiContext;
            ImGui::SetCurrentContext(inputContext);
            ImGuiIO &inputIo = ImGui::GetIO();
            const bool widgetOwnsInput = inputIo.WantTextInput || ImGui::IsAnyItemActive();
            const EditorInputCapture shortcutCapture{
                appFocused, widgetOwnsInput,
                hasModal || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId),
                scenePanel.has_active_input(),
                selection->current().focusedPanel == FocusedPanel::Scene,
                selection->current().focusedPanel == FocusedPanel::Hierarchy,
                appFocused,
                gamePanel.is_input_captured()};
            const bool shortcutAllowed = editor_action_allowed(EditorAction::NewScene, shortcutCapture);
            if (shortcutAllowed && ImGui::IsKeyPressed(ImGuiKey_F6))
                request_validation();
            if (ImGui::IsKeyPressed(ImGuiKey_Comma))
            {
                const auto action = dispatch_editor_key(EditorKey::Comma, inputIo.KeyShift,
                                                        inputIo.KeyCtrl, shortcutCapture);
                if (action == EditorAction::Preferences)
                    showPreferences = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Q))
            {
                const auto action = dispatch_editor_key(EditorKey::Q, inputIo.KeyShift,
                                                        inputIo.KeyCtrl, shortcutCapture);
                if (action == EditorAction::SelectHandTool)
                    scenePanel.set_tool(SceneTool::Hand);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_W))
            {
                const auto action = dispatch_editor_key(EditorKey::W, inputIo.KeyShift,
                                                        inputIo.KeyCtrl, shortcutCapture);
                if (action == EditorAction::SelectMoveTool)
                    scenePanel.set_tool(SceneTool::Move);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E))
            {
                const auto action = dispatch_editor_key(EditorKey::E, inputIo.KeyShift,
                                                        inputIo.KeyCtrl, shortcutCapture);
                if (action == EditorAction::SelectRotateTool)
                    scenePanel.set_tool(SceneTool::Rotate);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R))
            {
                const auto action = dispatch_editor_key(EditorKey::R, inputIo.KeyShift,
                                                        inputIo.KeyCtrl, shortcutCapture);
                if (action == EditorAction::SelectScaleTool)
                    scenePanel.set_tool(SceneTool::Scale);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F))
            {
                const auto action = dispatch_editor_key(EditorKey::F, inputIo.KeyShift,
                                                        inputIo.KeyCtrl, shortcutCapture);
                if (action == EditorAction::FrameSelected)
                    scenePanel.request_frame_selected();
                else if (action == EditorAction::FrameAll)
                    scenePanel.request_frame_all();
            }
            const auto f5Action = ImGui::IsKeyPressed(ImGuiKey_F5)
                ? dispatch_editor_key(EditorKey::F5, inputIo.KeyShift, inputIo.KeyCtrl, shortcutCapture)
                : std::nullopt;
            const bool ownedOperationActive = gamePipeline.busy() || catalogQuery.running() ||
                                              pendingPipelineBuildOnly.has_value() ||
                                              pendingRunValidation.active() ||
                                              applyProjectRootsAfterFrame;
            if (f5Action == EditorAction::Stop && ownedOperationActive)
                stop_owned_operation();
            if (shortcutAllowed && !gamePipeline.busy() && !catalogQuery.running() &&
                !pendingPipelineBuildOnly && pendingAction != PendingAction::BuildAndRun &&
                pendingAction != PendingAction::ChangeProjectRoots && !applyProjectRootsAfterFrame)
            {
                if (inputIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N))
                    create_scene_tab();
                else if (inputIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
                    request_open_scene();
                else if (inputIo.KeyCtrl && inputIo.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S))
                    request_save_as();
                else if (inputIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
                    save_current();
                else if (inputIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
                {
                    if (inputIo.KeyShift)
                        redo_edit();
                    else
                        undo_edit();
                }
                else if (inputIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
                    redo_edit();
                else if (inputIo.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Equal) ||
                                        ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)))
                    scenePanel.adjust_zoom(1.1f);
                else if (inputIo.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Minus) ||
                                        ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)))
                    scenePanel.adjust_zoom(1.0f / 1.1f);
                else if (inputIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_B))
                    queue_pipeline(true);
                else if (f5Action == EditorAction::BuildAndRun)
                    request_build_and_run();
            }
            ImGui::SetCurrentContext(mainImGuiContext);

            bool projectLocked = gamePipeline.busy() || catalogQuery.running() ||
                                 pendingPipelineBuildOnly.has_value() ||
                                 pendingAction == PendingAction::BuildAndRun ||
                                 pendingAction == PendingAction::ChangeProjectRoots ||
                                 applyProjectRootsAfterFrame;
            const bool documentEditingLocked = gamePipeline.busy() ||
                                               pendingPipelineBuildOnly.has_value() ||
                                               pendingAction == PendingAction::BuildAndRun ||
                                               pendingAction == PendingAction::ChangeProjectRoots ||
                                               applyProjectRootsAfterFrame;
            bool hasDirtySceneTab = false;
            for (const TabId id : documentManager.tab_ids())
            {
                const auto *tab = documentManager.find(id);
                hasDirtySceneTab |= tab != nullptr && tab->workspace().dirty();
            }
            bool hasDirtyAnimationTab = false;
            for (const TabId id : documentManager.animation_tab_ids())
            {
                const auto *tab = documentManager.find_animation_tab(id);
                hasDirtyAnimationTab |= tab != nullptr && tab->document().dirty();
            }
            const auto panelStateBefore = preferences.panelVisibility;
            const bool mainMenuBarVisible = ImGui::BeginMainMenuBar();
            if (mainMenuBarVisible)
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("New Scene Tab", "Ctrl+N", false, !projectLocked))
                        create_scene_tab();
                    if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, !projectLocked))
                        request_open_scene();
                    if (ImGui::MenuItem("Open Project Folder...", nullptr, false, !projectLocked))
                        request_project_folder();
                    ImGui::Separator();
                    if (ImGui::MenuItem("Save", "Ctrl+S", false,
                                        !projectLocked &&
                                            (workspace->dirty() || !workspace->document().sourcePath)))
                        save_current();
                    if (ImGui::MenuItem("Save All", nullptr, false,
                                        !projectLocked && (hasDirtySceneTab || hasDirtyAnimationTab)))
                        save_all_documents();
                    if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, !projectLocked))
                        request_save_as();
                    ImGui::Separator();
                    if (ImGui::MenuItem("Exit"))
                        closeRequested = true;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Edit"))
                {
                    const auto history = editCommands->history_status();
                    const bool animActive = animationWindowFocused && showAnimationEditorWindow &&
                        documentManager.active_animation_tab() != nullptr;
                    const bool canUndo = animActive
                        ? documentManager.active_animation_tab()->document().can_undo()
                        : history.canUndo;
                    const bool canRedo = animActive
                        ? documentManager.active_animation_tab()->document().can_redo()
                        : history.canRedo;
                    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo && !projectLocked)) undo_edit();
                    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo && !projectLocked)) redo_edit();
                    ImGui::Separator();
                    if (ImGui::BeginMenu("Duplicate References"))
                    {
                        const bool remap = preferences.duplicationReferencePolicy ==
                            "remap-internal-targets";
                        if (ImGui::MenuItem("Preserve Existing Targets", nullptr, !remap))
                        {
                            preferences.duplicationReferencePolicy = "preserve-existing-targets";
                            schedule_preference_save();
                        }
                        if (ImGui::MenuItem("Remap Internal Targets", nullptr, remap))
                        {
                            preferences.duplicationReferencePolicy = "remap-internal-targets";
                            schedule_preference_save();
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Validate Scene", "F6")) request_validation();
                    ImGui::Separator();
                    if (ImGui::MenuItem("Preferences...", "Ctrl+,"))
                        showPreferences = true;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Assets"))
                {
                    if (ImGui::BeginMenu("Create"))
                    {
                        const auto targetDir = projectPanel.selected_directory();
                        if (ImGui::MenuItem("Folder"))
                            projectPanel.execute_context_create("Folder", targetDir);
                        if (ImGui::MenuItem("Behavior Script"))
                            projectPanel.execute_context_create("Behavior Script", targetDir);
                        if (ImGui::MenuItem("Scene"))
                            projectPanel.execute_context_create("Scene", targetDir);
                        if (ImGui::MenuItem("Object Template"))
                            projectPanel.execute_context_create("Object Template", targetDir);
                        if (ImGui::MenuItem("Animation Clip"))
                            projectPanel.execute_context_create("Animation Clip", targetDir);
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                    const auto selectedEntry = projectPanel.selected_entry();
                    const bool hasSelectedAsset = selectedEntry.has_value();
                    if (ImGui::MenuItem("Delete Asset", "Delete", false, hasSelectedAsset))
                    {
                        projectPanel.execute_context_delete(*selectedEntry);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Refresh"))
                    {
                        projectPanel.refresh();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("GameObject"))
                {
                    const auto selectedObjId = (selection != nullptr && selection->current().objectId)
                        ? selection->current().objectId : std::nullopt;
                    if (ImGui::MenuItem("Create Empty", "Ctrl+Shift+N", false, !projectLocked))
                    {
                        auto res = editCommands->create_object(workspace->document_for_edit(), std::nullopt, "GameObject");
                        if (res.status == CommandStatus::Changed && res.command && selection != nullptr)
                        {
                            int newId = res.command->payload.value("id", 0);
                            selection->set(workspace->document(), Selection{newId, std::nullopt, std::nullopt, FocusedPanel::Hierarchy});
                        }
                    }
                    if (ImGui::MenuItem("Create Empty Child", "Alt+Shift+N", false, !projectLocked && selectedObjId.has_value()))
                    {
                        auto res = editCommands->create_object(workspace->document_for_edit(), *selectedObjId, "GameObject");
                        if (res.status == CommandStatus::Changed && res.command && selection != nullptr)
                        {
                            int newId = res.command->payload.value("id", 0);
                            selection->set(workspace->document(), Selection{newId, std::nullopt, std::nullopt, FocusedPanel::Hierarchy});
                        }
                    }
                    if (ImGui::BeginMenu("2D Object"))
                    {
                        if (ImGui::MenuItem("Sprite", nullptr, false, !projectLocked))
                        {
                            auto res = editCommands->create_object(workspace->document_for_edit(), std::nullopt, "Sprite",
                                                                   std::numeric_limits<std::size_t>::max(), {},
                                                                   Json::array({default_component_record(6)}));
                            if (res.status == CommandStatus::Changed && res.command && selection != nullptr)
                            {
                                int newId = res.command->payload.value("id", 0);
                                selection->set(workspace->document(), Selection{newId, std::nullopt, std::nullopt, FocusedPanel::Hierarchy});
                            }
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Camera", nullptr, false, !projectLocked))
                    {
                        auto res = editCommands->create_object(workspace->document_for_edit(), std::nullopt, "Camera",
                                                               std::numeric_limits<std::size_t>::max(), {},
                                                               Json::array({default_component_record(2)}));
                        if (res.status == CommandStatus::Changed && res.command && selection != nullptr)
                        {
                            int newId = res.command->payload.value("id", 0);
                            selection->set(workspace->document(), Selection{newId, std::nullopt, std::nullopt, FocusedPanel::Hierarchy});
                        }
                    }
                    if (ImGui::MenuItem("Audio Object", nullptr, false, !projectLocked))
                    {
                        auto res = editCommands->create_object(workspace->document_for_edit(), std::nullopt, "Audio",
                                                               std::numeric_limits<std::size_t>::max(), {},
                                                               Json::array({default_component_record(1)}));
                        if (res.status == CommandStatus::Changed && res.command && selection != nullptr)
                        {
                            int newId = res.command->payload.value("id", 0);
                            selection->set(workspace->document(), Selection{newId, std::nullopt, std::nullopt, FocusedPanel::Hierarchy});
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Frame Selected", "F", false, selectedObjId.has_value()))
                        scenePanel.request_frame_selected();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Component"))
                {
                    const auto selectedObjId = (selection != nullptr && selection->current().objectId)
                        ? selection->current().objectId : std::nullopt;
                    const bool canAdd = !projectLocked && selectedObjId.has_value();
                    if (ImGui::MenuItem("Audio Source", nullptr, false, canAdd))
                        editCommands->add_component(workspace->document_for_edit(), *selectedObjId, default_component_record(1));
                    if (ImGui::MenuItem("Camera", nullptr, false, canAdd))
                        editCommands->add_component(workspace->document_for_edit(), *selectedObjId, default_component_record(2));
                    if (ImGui::MenuItem("Script", nullptr, false, canAdd))
                        editCommands->add_component(workspace->document_for_edit(), *selectedObjId, default_component_record(5));
                    if (ImGui::MenuItem("Sprite Renderer", nullptr, false, canAdd))
                        editCommands->add_component(workspace->document_for_edit(), *selectedObjId, default_component_record(6));
                    if (ImGui::BeginMenu("Physics", canAdd))
                    {
                        if (ImGui::MenuItem("Box Collider"))
                            editCommands->add_component(workspace->document_for_edit(), *selectedObjId, default_component_record(3));
                        if (ImGui::MenuItem("Rigidbody"))
                            editCommands->add_component(workspace->document_for_edit(), *selectedObjId, default_component_record(4));
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Animation", canAdd))
                    {
                        if (ImGui::MenuItem("Animator"))
                            editCommands->add_component(workspace->document_for_edit(), *selectedObjId, default_component_record(0));
                        ImGui::EndMenu();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Window"))
                {
                    auto panel_item = [&](const char *label, const char *id,
                                          const char *shortcut = nullptr) {
                        bool visible = preferences.panelVisibility[id];
                        if (ImGui::MenuItem(label, shortcut, &visible))
                            preferences.panelVisibility[id] = visible;
                    };
                    if (ImGui::BeginMenu("Panels"))
                    {
                        panel_item("Hierarchy", "panel.hierarchy");
                        panel_item("Scene", "panel.scene");
                        panel_item("Inspector", "panel.inspector");
                        panel_item("Project and Assets", "panel.project");
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
                        panel_item("Game", "panel.game");
#endif
                        bool showAnimationEditor = showAnimationEditorWindow;
                        if (ImGui::MenuItem("Animation Assets", nullptr, &showAnimationEditor))
                            showAnimationEditorWindow = showAnimationEditor;
                        panel_item("Console", "panel.console");
                        panel_item("Build and Run Settings", "panel.run");
                        panel_item("Validation", "panel.validation");
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Panel Windows"))
                    {
                        std::vector<std::pair<const char *, const char *>> hostPanels{
                            {"Hierarchy", "panel.hierarchy"},
                            {"Scene", "panel.scene"},
                            {"Inspector", "panel.inspector"},
                            {"Project and Assets", "panel.project"},
                            {"Console", "panel.console"},
                            {"Build and Run Settings", "panel.run"},
                            {"Validation", "panel.validation"}};
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
                        hostPanels.emplace_back("Game", "panel.game");
#endif
                        for (const auto &[label, panelId] : hostPanels)
                        {
                            const std::string menuLabel = std::string(label) + "###host-panel." + panelId;
                            if (!ImGui::BeginMenu(menuLabel.c_str()))
                                continue;
                            const HostLayoutId currentHost =
                                preferences.panelHostAssignments[panelId];
                            if (currentHost == main_host_layout_id)
                            {
                                const bool canCreate = windowHosts.size() < WindowHostRegistry::MaximumHosts;
                                if (ImGui::MenuItem("Detach to New Window", nullptr, false, canCreate))
                                    detach_panel(panelId);
                            }
                            else if (ImGui::MenuItem("Dock in Main Window"))
                                move_panel_to_host(panelId, main_host_layout_id);

                            std::vector<std::pair<HostLayoutId, std::string>> availableHosts;
                            for (const auto &[hostId, surface] : detachedHosts)
                            {
                                (void)surface;
                                if (hostId == currentHost)
                                    continue;
                                std::string hostLabel = "Host " + std::to_string(hostId.value);
                                for (const auto &[otherPanel, assignedHost] : preferences.panelHostAssignments)
                                    if (assignedHost == hostId)
                                    {
                                        hostLabel += " (" + panel_title(otherPanel) + ")";
                                        break;
                                    }
                                availableHosts.emplace_back(hostId, std::move(hostLabel));
                            }
                            if (!availableHosts.empty())
                            {
                                ImGui::Separator();
                                for (const auto &[hostId, hostLabel] : availableHosts)
                                    if (ImGui::MenuItem(("Move to " + hostLabel).c_str()))
                                        move_panel_to_host(panelId, hostId);
                            }
                            ImGui::EndMenu();
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Layouts"))
                    {
                        if (ImGui::MenuItem("Reset Layout"))
                        {
                            std::vector<HostLayoutId> hostIds;
                            for (const auto &[hostId, host] : detachedHosts)
                            {
                                (void)host;
                                hostIds.push_back(hostId);
                            }
                            for (const HostLayoutId hostId : hostIds)
                                close_detached_host(hostId);
                            reset_workspace_layout(preferences);
                            savedLayout = defaultLayout;
                            if (!savedLayout.empty())
                                ImGui::LoadIniSettingsFromMemory(savedLayout.data(), savedLayout.size());
                            activeScale = options.uiScaleProvided ? options.uiScale : 1.0f;
                            lastAppliedScale = -1.0f;
                            statusMessage = "Workspace layout and panel visibility were reset.";
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                    bool editorFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
                    if (ImGui::MenuItem("Fullscreen", nullptr, &editorFullscreen))
                    {
                        if (!SDL_SetWindowFullscreen(window, editorFullscreen) || !SDL_SyncWindow(window))
                            statusMessage = std::string("Could not change editor fullscreen state: ") + SDL_GetError();
                        else
                            persist_preferences();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Help"))
                {
                    if (ImGui::MenuItem("Keyboard Shortcuts"))
                        showShortcutReference = true;
                    ImGui::Separator();
                    if (ImGui::MenuItem("About Platformator"))
                        statusMessage = "Platformator Editor v0.14";
                    ImGui::EndMenu();
                }

                ImGui::EndMainMenuBar();
            }

            bool welcomeContentsVisible = false;
            std::optional<SemanticBounds> welcomeOpenProjectBounds;
            bool welcomeOpenProjectFocused = false;
            if (!projectOpened)
            {
                const ImGuiViewport *welcomeViewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(
                    ImVec2(welcomeViewport->WorkPos.x + welcomeViewport->WorkSize.x * 0.5f,
                           welcomeViewport->WorkPos.y + welcomeViewport->WorkSize.y * 0.5f),
                    ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Always);
                welcomeContentsVisible = ImGui::Begin(
                    "Welcome to Platformator###projectless-welcome", nullptr,
                    ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                        ImGuiWindowFlags_AlwaysAutoResize);
                if (welcomeContentsVisible)
                {
                    ImGui::TextWrapped("Open a project folder containing CMakeLists.txt and assets to build and run scenes.");
                    ImGui::TextWrapped("Your current untitled scene stays available if you cancel or choose an invalid folder.");
                    ImGui::Spacing();
                    const bool openProjectClicked = ImGui::Button(
                        "Open Project Folder...", ImVec2(-1.0f, 0.0f));
                    const ImVec2 buttonMinimum = ImGui::GetItemRectMin();
                    const ImVec2 buttonMaximum = ImGui::GetItemRectMax();
                    welcomeOpenProjectBounds = SemanticBounds{
                        buttonMinimum.x, buttonMinimum.y,
                        buttonMaximum.x - buttonMinimum.x,
                        buttonMaximum.y - buttonMinimum.y};
                    welcomeOpenProjectFocused = ImGui::IsItemFocused();
                    if (openProjectClicked)
                        request_project_folder();
                    if (!statusMessage.empty())
                    {
                        ImGui::Spacing();
                        ImGui::TextWrapped("%s", statusMessage.c_str());
                    }
                }
                ImGui::End();
            }

            if (showPreferences)
                ImGui::OpenPopup("Editor Preferences");
            if (ImGui::BeginPopupModal("Editor Preferences", &showPreferences,
                                       ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("Interface scale");
                for (const float scale : {1.0f, 1.5f, 2.0f})
                {
                    const std::string label = std::to_string(static_cast<int>(scale * 100.0f)) + "%";
                    if (ImGui::RadioButton(label.c_str(), std::abs(activeScale - scale) < 0.001f))
                    {
                        activeScale = scale;
                        preferences.userScale = scale;
                        lastAppliedScale = -1.0f;
                        schedule_preference_save();
                    }
                    ImGui::SameLine();
                }
                ImGui::NewLine();
                ImGui::TextWrapped("Scale applies to interface fonts and controls. Display scaling is added automatically.");
                if (ImGui::Button("Close"))
                    showPreferences = false;
                ImGui::EndPopup();
            }

            if (showShortcutReference)
            {
                ImGui::SetNextWindowSize(ImVec2(460.0f, 420.0f), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("Keyboard Shortcuts", &showShortcutReference))
                {
                    ImGui::TextWrapped("Shortcuts pause while a text field, modal dialog, active gesture, or another window owns input.");
                    ImGui::Separator();
                    if (ImGui::BeginTable("###editor-shortcut-table", 3,
                                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn("Action");
                        ImGui::TableSetupColumn("Shortcut");
                        ImGui::TableSetupColumn("Focus");
                        ImGui::TableHeadersRow();
                        for (const auto &action : editor_action_descriptors())
                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(action.label.data(),
                                action.label.data() + action.label.size());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextUnformatted(action.shortcut.data(),
                                action.shortcut.data() + action.shortcut.size());
                            ImGui::TableSetColumnIndex(2);
                            const char *scope = action.sceneOnly ? "Scene" :
                                action.hierarchyOnly ? "Hierarchy" : "Any panel";
                            ImGui::TextUnformatted(scope);
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::End();
            }

            EditorToolbarState toolbarState;
            toolbarState.projectLocked = projectLocked;
            toolbarState.operationActive = gamePipeline.busy() || catalogQuery.running() ||
                                           pendingPipelineBuildOnly.has_value();
            toolbarState.saveEnabled = workspace->dirty() || !workspace->document().sourcePath;
            const auto path_to_utf8 = [](const std::filesystem::path &path) {
                const auto encoded = path.u8string();
                return std::string(reinterpret_cast<const char *>(encoded.data()), encoded.size());
            };
            const std::string sceneLabel = workspace->document().sourcePath
                ? path_to_utf8(workspace->document().sourcePath->filename()) : "Untitled Scene";
            const std::string projectLabel = path_to_utf8(activeProjectRoot.filename().empty()
                ? activeProjectRoot : activeProjectRoot.filename());
            std::string operationLabel;
            if (catalogQuery.running() || gamePipeline.status().stage == PipelineStage::QueryingCatalog)
                operationLabel = "Checking";
            else if (pendingPipelineBuildOnly)
                operationLabel = "Building";
            else
            {
                switch (gamePipeline.status().stage)
                {
                case PipelineStage::Configuring: operationLabel = "Configuring"; break;
                case PipelineStage::Building: operationLabel = "Building"; break;
                case PipelineStage::Launching: operationLabel = "Launching"; break;
                case PipelineStage::Running: operationLabel = "Running"; break;
                case PipelineStage::Idle:
                case PipelineStage::QueryingCatalog: break;
                }
                if (operationLabel.empty())
                {
                    switch (gamePipeline.status().outcome)
                    {
                    case PipelineOutcome::Succeeded: operationLabel = "Complete"; break;
                    case PipelineOutcome::Failed: operationLabel = "Failed"; break;
                    case PipelineOutcome::Cancelled: operationLabel = "Cancelled"; break;
                    case PipelineOutcome::None: break;
                    }
                }
            }
            toolbarState.projectLabel = projectOpened ? projectLabel : "No Project";
            toolbarState.projectPath = projectOpened ? path_to_utf8(activeProjectRoot) : std::string{};
            toolbarState.sceneLabel = sceneLabel;
            toolbarState.scenePath = workspace->document().sourcePath
                ? path_to_utf8(*workspace->document().sourcePath) : std::string{};
            toolbarState.status = workspace->dirty() ? "Modified" : "Saved";
            if (!operationLabel.empty())
                toolbarState.status += " | " + operationLabel;
            const auto *activePlaySession = gamePipeline.play_session();
            const bool playSessionActive = activePlaySession != nullptr &&
                (activePlaySession->state() == PlaySessionState::Running ||
                 activePlaySession->state() == PlaySessionState::Paused);
            toolbarState.isRunning = activePlaySession != nullptr && activePlaySession->state() == PlaySessionState::Running;
            toolbarState.isPaused = activePlaySession != nullptr && activePlaySession->state() == PlaySessionState::Paused;
            toolbarState.pauseResumeEnabled = playSessionActive && activePlaySession->capabilities().playControl;
            toolbarState.stepEnabled = toolbarState.isPaused && activePlaySession->capabilities().playControl;
            toolbarState.activeTool = scenePanel.tool();

            EditorToolbarActions toolbarActions;
            toolbarActions.openScene = request_open_scene;
            toolbarActions.save = save_current;
            toolbarActions.build = [&] { queue_pipeline(true); };
            toolbarActions.buildAndRun = request_build_and_run;
            toolbarActions.pause = [&] { if (auto *s = gamePipeline.play_session()) s->request_pause(); };
            toolbarActions.resume = [&] { if (auto *s = gamePipeline.play_session()) s->request_resume(); };
            toolbarActions.step = [&] { if (auto *s = gamePipeline.play_session()) s->request_step(); };
            toolbarActions.stop = stop_owned_operation;
            toolbarActions.selectTool = [&](SceneTool tool) { scenePanel.set_tool(tool); };
            const auto toolbarFrame = draw_editor_toolbar(toolbarState, toolbarActions);
            constexpr float sceneTabBarHeight = 32.0f;
            const ImGuiViewport *mainViewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x,
                                           mainViewport->WorkPos.y + toolbarFrame.height));
            ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x, sceneTabBarHeight));
            constexpr ImGuiWindowFlags sceneTabBarFlags = ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoDocking;
            const bool sceneTabBarVisible = ImGui::Begin(
                "###editor.scene-tabs", nullptr, sceneTabBarFlags);
            const bool tabActionsLocked = pendingAction != PendingAction::None || showUnsavedPrompt ||
                showExternalConflictPrompt || showOverwritePrompt || externalSaveAsPending ||
                externalAnimationConflict.has_value() || externalAnimationSaveAsPending ||
                animationDestinationOverwrite.has_value() ||
                showManualPath || pendingAssetAssignment.has_value() || closeAfterSaveTab.has_value() ||
                closePromptTab.has_value() || pendingGameSessionCloseTab.has_value() ||
                applyProjectRootsAfterFrame;
            const bool sceneTabActionsEnabled = !tabActionsLocked && !projectLocked;
            std::optional<SemanticBounds> newSceneBounds;
            std::unordered_map<std::uint64_t, SemanticBounds> sceneTabBounds;
            std::unordered_map<std::uint64_t, SemanticBounds> sceneTabCloseBounds;
            std::unordered_set<std::uint64_t> focusedSceneTabIds;
            ImGui::BeginDisabled(!sceneTabActionsEnabled);
            for (const TabId tabId : documentManager.tab_ids())
            {
                const auto *tab = documentManager.find(tabId);
                if (tab == nullptr)
                    continue;
                const std::string cleanName = tab->workspace().document().sourcePath
                    ? path_to_utf8(tab->workspace().document().sourcePath->filename())
                    : "Untitled";
                std::string tabName = cleanName;
                if (tab->workspace().dirty())
                    tabName += " *";
                const auto sourceStatus = std::find_if(cachedSourceStatuses.begin(), cachedSourceStatuses.end(),
                    [&](const DocumentSourceStatus &item) { return item.tabId == tabId; });
                if (sourceStatus != cachedSourceStatuses.end() &&
                    (sourceStatus->file.state == SourceFileState::Changed ||
                     sourceStatus->file.state == SourceFileState::Missing))
                    tabName += " !";
                const std::string hiddenId = "###scene-tab." + std::to_string(tabId.value);
                const bool isActive = tabId == activeDocumentTab->id();
                if (isActive)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                // Reserve the same marker and close button space for every tab so saving or an external change
                // cannot move the close button and all following tabs.
                const float markerWidth = ImGui::CalcTextSize(" * !").x;
                const float closeBtnWidth = ImGui::CalcTextSize("×").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                const float tabWidth = ImGui::CalcTextSize(cleanName.c_str()).x + markerWidth +
                                       ImGui::GetStyle().FramePadding.x * 2.0f + closeBtnWidth;
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
                ImGui::SetNextItemAllowOverlap();
                const bool tabClicked = ImGui::Button((tabName + hiddenId).c_str(),
                                                      ImVec2(tabWidth, 0.0f));
                ImGui::PopStyleVar();
                const ImVec2 tabMin = ImGui::GetItemRectMin();
                const ImVec2 tabMax = ImGui::GetItemRectMax();
                const bool tabHovered = ImGui::IsItemHovered();
                sceneTabBounds.emplace(tabId.value, SemanticBounds{
                    tabMin.x, tabMin.y, tabMax.x - tabMin.x,
                    tabMax.y - tabMin.y});
                if (ImGui::IsItemFocused())
                    focusedSceneTabIds.insert(tabId.value);

                // Embedded close button at the right edge of the tab
                const ImVec2 closeMin(tabMax.x - closeBtnWidth, tabMin.y);
                sceneTabCloseBounds.emplace(tabId.value, SemanticBounds{
                    closeMin.x, closeMin.y, closeBtnWidth,
                    tabMax.y - tabMin.y});

                const std::string tabPushId = std::to_string(tabId.value);
                ImGui::PushID(tabPushId.c_str());
                ImGui::SetCursorScreenPos(closeMin);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.30f));
                const bool closeTabClicked = ImGui::Button("×", ImVec2(closeBtnWidth, tabMax.y - tabMin.y));
                ImGui::PopStyleColor(3);
                ImGui::PopID();

                if (tabClicked && !closeTabClicked)
                    activate_document_tab(tabId);
                if (isActive)
                    ImGui::PopStyleColor();
                if (tabHovered && sourceStatus != cachedSourceStatuses.end() &&
                    sourceStatus->file.state != SourceFileState::Current &&
                    sourceStatus->file.state != SourceFileState::NoSource)
                    ImGui::SetTooltip("%s", sourceStatus->file.message.c_str());

                if (closeTabClicked)
                {
                    if (tab->workspace().dirty())
                        closePromptTab = tabId;
                    else
                        close_document_tab(tabId, UnsavedDecision::Discard);
                }
                ImGui::SetCursorScreenPos(ImVec2(tabMax.x + ImGui::GetStyle().ItemSpacing.x, tabMin.y));
            }
            const bool newSceneClicked = ImGui::Button("+ Scene");
            {
                const ImVec2 minimum = ImGui::GetItemRectMin();
                const ImVec2 maximum = ImGui::GetItemRectMax();
                newSceneBounds = SemanticBounds{minimum.x, minimum.y,
                                                maximum.x - minimum.x,
                                                maximum.y - minimum.y};
            }
            if (newSceneClicked)
                create_scene_tab();
            ImGui::EndDisabled();
            ImGui::End();
            submit_editor_dockspace(kEditorDockspaceId, toolbarFrame.height + sceneTabBarHeight);

            if (closePromptTab)
                ImGui::OpenPopup("Close Scene Tab");
            if (ImGui::BeginPopupModal("Close Scene Tab", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize))
            {
                auto *closingTab = closePromptTab ? documentManager.find(*closePromptTab) : nullptr;
                if (closingTab == nullptr)
                {
                    closePromptTab.reset();
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    const std::string sceneName = closingTab->workspace().document().sourcePath
                        ? closingTab->workspace().document().sourcePath->filename().string()
                        : "Untitled scene";
                    ImGui::TextWrapped("Save changes to %s before closing this tab?", sceneName.c_str());
                    if (ImGui::Button("Save"))
                    {
                        const TabId id = *closePromptTab;
                        if (!closingTab->workspace().document().sourcePath)
                        {
                            activate_document_tab(id);
                            closeAfterSaveTab = id;
                            closePromptTab.reset();
                            request_save_as();
                            ImGui::CloseCurrentPopup();
                        }
                        else
                        {
                            const auto result = close_document_tab(id, UnsavedDecision::Save);
                            closePromptTab.reset();
                            ImGui::CloseCurrentPopup();
                            if (!result)
                            {
                                if (pendingGameSessionCloseTab != id)
                                {
                                    activate_document_tab(id);
                                    statusMessage = result.message;
                                    if (result.message.find("changed on disk") != std::string::npos)
                                        showExternalConflictPrompt = true;
                                }
                            }
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Discard"))
                    {
                        const auto result = close_document_tab(*closePromptTab,
                                                               UnsavedDecision::Discard);
                        closePromptTab.reset();
                        ImGui::CloseCurrentPopup();
                        if (!result && pendingGameSessionCloseTab != closePromptTab)
                            statusMessage = result.message;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel"))
                    {
                        closePromptTab.reset();
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndPopup();
            }

            if (showGameSessionClosePrompt)
                ImGui::OpenPopup("Close Embedded Game Session");
            if (ImGui::BeginPopupModal("Close Embedded Game Session", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize))
            {
                auto *closingTab = pendingGameSessionCloseTab
                    ? documentManager.find(*pendingGameSessionCloseTab) : nullptr;
                if (closingTab == nullptr)
                {
                    pendingGameSessionCloseTab.reset();
                    showGameSessionClosePrompt = false;
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    const std::string sceneName = closingTab->workspace().document().sourcePath
                        ? closingTab->workspace().document().sourcePath->filename().string()
                        : "Untitled scene";
                    bool canKeepGameRunning = false;
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
                    const auto *playSession = gamePipeline.play_session();
                    canKeepGameRunning = playSession != nullptr &&
                        playSession->state() != PlaySessionState::Idle &&
                        playSession->state() != PlaySessionState::Stopping;
#endif
                    ImGui::TextWrapped("The embedded Game was launched from %s.", sceneName.c_str());
                    ImGui::TextWrapped("Stop the game before closing, or keep it running from its saved launch snapshot?");
                    ImGui::BeginDisabled(!canKeepGameRunning);
                    if (ImGui::Button("Keep Game Running and Close Scene"))
                    {
                        const TabId id = *pendingGameSessionCloseTab;
                        const auto decision = pendingGameSessionCloseDecision;
                        pendingGameSessionCloseTab.reset();
                        showGameSessionClosePrompt = false;
                        const auto result = perform_close_document_tab(id, decision);
                        if (!result)
                            statusMessage = result.message;
                        else
                            statusMessage = "Scene tab closed; the embedded Game continues from its saved launch snapshot.";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button("Stop Game and Close Scene"))
                    {
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
                        const auto *session = gamePipeline.play_session();
                        showGameSessionClosePrompt = false;
                        if (gamePipeline.busy() && session != nullptr &&
                            session->state() != PlaySessionState::Idle)
                        {
                            waitingForGameSessionStopBeforeClose = true;
                            gamePipeline.stop();
                            statusMessage = "Stopping the embedded Game before closing its source tab.";
                        }
                        else
                        {
                            const TabId id = *pendingGameSessionCloseTab;
                            const auto decision = pendingGameSessionCloseDecision;
                            pendingGameSessionCloseTab.reset();
                            const auto result = perform_close_document_tab(id, decision);
                            if (!result)
                                statusMessage = result.message;
                        }
#else
                        const TabId id = *pendingGameSessionCloseTab;
                        const auto decision = pendingGameSessionCloseDecision;
                        pendingGameSessionCloseTab.reset();
                        showGameSessionClosePrompt = false;
                        const auto result = perform_close_document_tab(id, decision);
                        if (!result)
                            statusMessage = result.message;
#endif
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel"))
                    {
                        pendingGameSessionCloseTab.reset();
                        closePromptTab.reset();
                        closeAfterSaveTab.reset();
                        showGameSessionClosePrompt = false;
                        statusMessage = "Scene tab remains open with its embedded Game session unchanged.";
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndPopup();
            }
            const auto show_panel = [&](const char *stableId) {
                return preferences.panelVisibility[stableId];
            };
            DocumentSession &visibilityDocument = workspace->document_for_edit();
            const std::string visibilitySceneKey = visibilityDocument.sourcePath
                ? visibilityDocument.sourcePath->lexically_normal().generic_string()
                : "tab:" + std::to_string(visibilityDocument.tabId.value);
            auto &hiddenObjectIds = preferences.hiddenObjectIdsByScene[visibilitySceneKey];
            auto &lockedObjectIds = preferences.lockedObjectIdsByScene[visibilitySceneKey];
            bool visibilityStatePruned = false;
            const auto [incarnation, firstSeen] = visibilityDocumentIncarnations.try_emplace(
                visibilityDocument.tabId.value,
                visibilityDocument.documentGeneration, visibilitySceneKey);
            if (!firstSeen && incarnation->second.first != visibilityDocument.documentGeneration)
            {
                if (incarnation->second.second == visibilitySceneKey)
                {
                    hiddenObjectIds.clear();
                    lockedObjectIds.clear();
                    visibilityStatePruned = true;
                }
                incarnation->second = {visibilityDocument.documentGeneration, visibilitySceneKey};
            }
            std::unordered_set<int> liveObjectIds;
            liveObjectIds.reserve(visibilityDocument.indexes.objectsById.size());
            for (const auto &[objectId, location] : visibilityDocument.indexes.objectsById)
            {
                (void)location;
                liveObjectIds.insert(objectId);
            }
            visibilityStatePruned |= prune_editor_object_ids(hiddenObjectIds, liveObjectIds);
            visibilityStatePruned |= prune_editor_object_ids(lockedObjectIds, liveObjectIds);
            if (visibilityStatePruned)
                schedule_preference_save();
            bool hierarchyContentsVisible = false;
            bool showHierarchy = show_panel("panel.hierarchy");
            ImGuiContext *previousHierarchyContext = activate_panel_context("panel.hierarchy");
            const auto hierarchyFrame = begin_panel_window(showHierarchy,
                "Hierarchy###panel.hierarchy", &showHierarchy);
            hierarchyContentsVisible = hierarchyFrame.contentsVisible;
            if (hierarchyFrame.contentsVisible)
            {
                const auto hiddenBeforeHierarchy = hiddenObjectIds;
                const auto lockedBeforeHierarchy = lockedObjectIds;
                const auto previousDuplicatePolicy = preferences.duplicationReferencePolicy;
                const auto duplicateCatalog = behaviorCatalog.snapshot();
                struct TemplateAuthorCacheKey
                {
                    std::uint64_t projectGeneration = 0;
                    TabId tabId{};
                    std::uint64_t documentGeneration = 0;
                    std::uint64_t catalogRevision = 0;

                    bool operator==(const TemplateAuthorCacheKey &) const = default;
                };

                static TemplateAuthorCacheKey s_lastAuthorCacheKey;
                static std::vector<Json> s_cachedAuthorBindings;
                static std::vector<TemplateAuthorSchemaDeclaration> s_cachedAuthorSchemas;

                const TemplateAuthorCacheKey authorKey{
                    projectSession.generation().value,
                    workspace->document().tabId,
                    workspace->document().documentGeneration,
                    behaviorCatalog.revision()
                };

                if (authorKey != s_lastAuthorCacheKey)
                {
                    s_cachedAuthorBindings = inspectorPanel.template_author_bindings(visibilityDocument);
                    s_cachedAuthorSchemas = inspectorPanel.template_author_schemas(visibilityDocument);
                    s_lastAuthorCacheKey = authorKey;
                }
                const auto &authorReferenceBindings = s_cachedAuthorBindings;
                const auto &authorSchemaDeclarations = s_cachedAuthorSchemas;
                ImGui::BeginDisabled(documentEditingLocked);
                hierarchyPanel.draw(visibilityDocument, *selection, *editCommands,
                                    !documentEditingLocked,
                                    &hiddenObjectIds, &lockedObjectIds,
                                    &preferences.duplicationReferencePolicy,
                                    duplicateCatalog ? &duplicateCatalog->document : nullptr,
                                    authorReferenceBindings, authorSchemaDeclarations,
                                    deleteKeyPressedThisFrame, mouseButtonEventThisFrame);
                ImGui::EndDisabled();
                if (hiddenObjectIds != hiddenBeforeHierarchy ||
                    lockedObjectIds != lockedBeforeHierarchy ||
                    preferences.duplicationReferencePolicy != previousDuplicatePolicy)
                    schedule_preference_save();
            }
            end_panel_window(hierarchyFrame);
            restore_panel_context(previousHierarchyContext);
            preferences.panelVisibility["panel.hierarchy"] = showHierarchy;
            activeTabStateInitialized = true;
            const auto currentExpandedIds = hierarchyPanel.expanded_object_ids();
            activeDocumentTab->set_expanded_hierarchy_ids(currentExpandedIds);
            if (workspace->document().sourcePath)
            {
                auto savedTab = std::find_if(preferences.openTabs.begin(), preferences.openTabs.end(),
                    [&](const SavedTabPreferences &candidate) {
                        return candidate.scenePath.lexically_normal() ==
                               workspace->document().sourcePath->lexically_normal();
                    });
                if (savedTab == preferences.openTabs.end())
                {
                    if (preferences.openTabs.size() >= 128)
                        preferences.openTabs.erase(preferences.openTabs.begin());
                    preferences.openTabs.push_back(SavedTabPreferences{*workspace->document().sourcePath});
                    savedTab = std::prev(preferences.openTabs.end());
                }
                if (savedTab->expandedHierarchyIds != currentExpandedIds ||
                    !savedTab->hasExpansionState)
                {
                    savedTab->expandedHierarchyIds = currentExpandedIds;
                    savedTab->hasExpansionState = true;
                    sceneViewPreferencesDirty = true;
                }
            }

            projectPanel.service_owner_work(renderer);
            bool projectContentsVisible = false;
            bool showProject = show_panel("panel.project");
            ImGuiContext *previousProjectContext = activate_panel_context("panel.project");
            SDL_Renderer *projectFrameRenderer = renderer;
            if (const auto assignment = preferences.panelHostAssignments.find("panel.project");
                assignment != preferences.panelHostAssignments.end() &&
                assignment->second != main_host_layout_id)
                if (const auto host = detachedHosts.find(assignment->second); host != detachedHosts.end())
                    projectFrameRenderer = host->second->renderer();
            projectPanel.set_frame_renderer(projectFrameRenderer);
            const auto projectFrame = begin_panel_window(showProject,
                "Project and Assets###panel.project", &showProject);
            projectContentsVisible = projectFrame.contentsVisible;
            if (projectFrame.contentsVisible)
            {
                auto *animationTab = documentManager.active_animation_tab();
                if (animationTab != nullptr && animationTab->document().source_path())
                {
                    auto &assetDocument = animationTab->document();
                    projectPanel.set_animation_draft_preview(
                        *assetDocument.source_path(), animationTab->asset_root(), assetDocument.root(),
                        assetDocument.history_token(), assetDocument.saved_history_token());

                    const Json &clip = assetDocument.root();
                    const Json *frames = clip.is_object() && clip.contains("frames") &&
                        clip["frames"].is_array() ? &clip["frames"] : nullptr;
                    if (showAnimationEditorWindow && frames != nullptr &&
                        animationTab->panel().selected_frame() < frames->size())
                    {
                        const Json &frame = (*frames)[animationTab->panel().selected_frame()];
                        if (frame.is_object())
                        {
                            const auto texture = frame.find("textureWrapperFilePath");
                            if (texture != frame.end() && texture->is_string() && !texture->get<std::string>().empty())
                            {
                                std::filesystem::path logical(texture->get<std::string>());
                                if (!logical.is_absolute() && logical.begin() != logical.end() &&
                                    *logical.begin() == "assets")
                                    logical = logical.lexically_relative("assets");
                                if (!logical.is_absolute())
                                    logical = (animationTab->asset_root() / logical).lexically_normal();
                                const auto relative = logical.lexically_normal().lexically_relative(
                                    animationTab->asset_root().lexically_normal());
                                const bool contained = !relative.is_absolute() &&
                                    std::none_of(relative.begin(), relative.end(), [](const auto &part) {
                                        return part == "..";
                                    });
                                if (contained)
                                {
                                    const ResourceConsumer consumer{
                                        {projectSession.generation(), animationTab->id(),
                                         DocumentGeneration{assetDocument.generation()}},
                                        static_cast<int>(animationTab->panel().selected_frame()),
                                        std::nullopt, "animation.preview.texture"};
                                    projectSession.resource_index().register_visible_animation_texture(
                                        logical, consumer);
                                }
                            }
                        }
                    }
                }
                else
                    projectPanel.clear_animation_draft_preview();
                projectPanel.draw(workspace->document_for_edit(), *editCommands, selection);
            }
            if (projectPanel.take_reference_retry_request())
            {
                const auto scheduled = scenePreview->retry_failed_assets(workspace->document());
                projectPanel.set_reference_retry_status(scheduled);
                if (scheduled != 0)
                    statusMessage = "Retrying failed referenced Scene asset previews.";
            }
            end_panel_window(projectFrame);
            projectPanel.set_frame_renderer(renderer);
            restore_panel_context(previousProjectContext);
            preferences.panelVisibility["panel.project"] = showProject;

            accessibilitySnapshots.dispatch_pending(32,
                [&](const SemanticAction &action, const SemanticNode &) {
                    if (action.action == "activate" &&
                        action.nodeId == "main/welcome/open-project")
                    {
                        if (!projectLocked)
                            request_project_folder();
                        return;
                    }
                    if (action.action == "activate" &&
                        action.nodeId == "main/scene-tabs/new")
                    {
                        if (!projectLocked && !tabActionsLocked)
                            create_scene_tab();
                        return;
                    }
                    if (action.action == "activate" &&
                        action.nodeId == "main/scene-tabs/save-all")
                    {
                        if (!projectLocked && !tabActionsLocked)
                            save_all_documents();
                        return;
                    }
                    constexpr std::string_view sceneTabActivatePrefix =
                        "main/scene-tabs/activate/";
                    constexpr std::string_view sceneTabClosePrefix =
                        "main/scene-tabs/close/";
                    const auto parseSceneTabId = [](std::string_view text) -> std::optional<TabId> {
                        std::uint64_t value = 0;
                        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
                        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0)
                            return std::nullopt;
                        return TabId{value};
                    };
                    if (action.action == "activate" &&
                        action.nodeId.starts_with(sceneTabActivatePrefix))
                    {
                        if (!projectLocked && !tabActionsLocked)
                        {
                            const auto id = parseSceneTabId(
                                std::string_view(action.nodeId).substr(sceneTabActivatePrefix.size()));
                            if (id && documentManager.find(*id) != nullptr)
                                activate_document_tab(*id);
                        }
                        return;
                    }
                    if (action.action == "activate" &&
                        action.nodeId.starts_with(sceneTabClosePrefix))
                    {
                        if (!projectLocked && !tabActionsLocked)
                        {
                            const auto id = parseSceneTabId(
                                std::string_view(action.nodeId).substr(sceneTabClosePrefix.size()));
                            if (id && documentManager.find(*id) != nullptr)
                                closePromptTab = *id;
                        }
                        return;
                    }
                    constexpr std::string_view menuPrefix = "main/menu/";
                    if (action.action == "activate" && action.nodeId.starts_with(menuPrefix))
                    {
                        const std::string_view command =
                            std::string_view(action.nodeId).substr(menuPrefix.size());
                        if (command == "file/new-scene-tab")
                        {
                            if (!projectLocked && !tabActionsLocked)
                                create_scene_tab();
                        }
                        else if (command == "file/open-scene")
                        {
                            if (!projectLocked)
                                request_open_scene();
                        }
                        else if (command == "file/open-project")
                        {
                            if (!projectLocked)
                                request_project_folder();
                        }
                        else if (command == "file/save")
                        {
                            if (!projectLocked)
                                save_current();
                        }
                        else if (command == "file/save-all")
                        {
                            if (!projectLocked && !tabActionsLocked)
                                save_all_documents();
                        }
                        else if (command == "file/save-as")
                        {
                            if (!projectLocked)
                                request_save_as();
                        }
                        else if (command == "file/exit")
                            closeRequested = true;
                        else if (command == "edit/undo")
                        {
                            if (!projectLocked &&
                                can_execute_focused_undo(animationWindowFocused && showAnimationEditorWindow,
                                                         documentManager, *editCommands))
                                undo_edit();
                        }
                        else if (command == "edit/redo")
                        {
                            if (!projectLocked &&
                                can_execute_focused_redo(animationWindowFocused && showAnimationEditorWindow,
                                                         documentManager, *editCommands))
                                redo_edit();
                        }
                        else if (command == "edit/validate")
                            request_validation();
                        else if (command == "edit/preferences")
                            showPreferences = true;
                        else if (command == "assets/create-folder")
                            projectPanel.execute_context_create("Folder", projectPanel.selected_directory());
                        else if (command == "assets/create-script")
                            projectPanel.execute_context_create("Behavior Script", projectPanel.selected_directory());
                        else if (command == "assets/create-scene")
                            projectPanel.execute_context_create("Scene", projectPanel.selected_directory());
                        else if (command == "assets/create-template")
                            projectPanel.execute_context_create("Object Template", projectPanel.selected_directory());
                        else if (command == "assets/create-animation")
                            projectPanel.execute_context_create("Animation Clip", projectPanel.selected_directory());
                        else if (command == "assets/delete")
                        {
                            if (const auto entry = projectPanel.selected_entry())
                                projectPanel.execute_context_delete(*entry);
                        }
                        else if (command == "assets/refresh")
                            projectPanel.refresh();
                        else if (command == "gameobject/create-empty")
                        {
                            if (!projectLocked)
                            {
                                auto res = editCommands->create_object(workspace->document_for_edit(), std::nullopt, "GameObject");
                                if (res.status == CommandStatus::Changed && res.command && selection != nullptr)
                                    selection->set(workspace->document(), Selection{res.command->payload.value("id", 0), std::nullopt, std::nullopt, FocusedPanel::Hierarchy});
                            }
                        }
                        else if (command == "gameobject/create-empty-child")
                        {
                            if (!projectLocked && selection != nullptr && selection->current().objectId)
                            {
                                auto res = editCommands->create_object(workspace->document_for_edit(), *selection->current().objectId, "GameObject");
                                if (res.status == CommandStatus::Changed && res.command)
                                    selection->set(workspace->document(), Selection{res.command->payload.value("id", 0), std::nullopt, std::nullopt, FocusedPanel::Hierarchy});
                            }
                        }
                        else if (command == "gameobject/create-sprite")
                        {
                            if (!projectLocked)
                            {
                                auto res = editCommands->create_object(workspace->document_for_edit(), std::nullopt, "Sprite",
                                                                       std::numeric_limits<std::size_t>::max(), {},
                                                                       Json::array({default_component_record(6)}));
                                if (res.status == CommandStatus::Changed && res.command && selection != nullptr)
                                    selection->set(workspace->document(), Selection{res.command->payload.value("id", 0), std::nullopt, std::nullopt, FocusedPanel::Hierarchy});
                            }
                        }
                        else if (command == "gameobject/create-camera")
                        {
                            if (!projectLocked)
                            {
                                auto res = editCommands->create_object(workspace->document_for_edit(), std::nullopt, "Camera",
                                                                       std::numeric_limits<std::size_t>::max(), {},
                                                                       Json::array({default_component_record(2)}));
                                if (res.status == CommandStatus::Changed && res.command && selection != nullptr)
                                    selection->set(workspace->document(), Selection{res.command->payload.value("id", 0), std::nullopt, std::nullopt, FocusedPanel::Hierarchy});
                            }
                        }
                        else if (command == "gameobject/create-audio")
                        {
                            if (!projectLocked)
                            {
                                auto res = editCommands->create_object(workspace->document_for_edit(), std::nullopt, "Audio",
                                                                       std::numeric_limits<std::size_t>::max(), {},
                                                                       Json::array({default_component_record(1)}));
                                if (res.status == CommandStatus::Changed && res.command && selection != nullptr)
                                    selection->set(workspace->document(), Selection{res.command->payload.value("id", 0), std::nullopt, std::nullopt, FocusedPanel::Hierarchy});
                            }
                        }
                        else if (command == "gameobject/frame-selected")
                            scenePanel.request_frame_selected();
                        else if (command == "component/audio-source")
                        {
                            if (!projectLocked && selection != nullptr && selection->current().objectId)
                                editCommands->add_component(workspace->document_for_edit(), *selection->current().objectId, default_component_record(1));
                        }
                        else if (command == "component/camera")
                        {
                            if (!projectLocked && selection != nullptr && selection->current().objectId)
                                editCommands->add_component(workspace->document_for_edit(), *selection->current().objectId, default_component_record(2));
                        }
                        else if (command == "component/script")
                        {
                            if (!projectLocked && selection != nullptr && selection->current().objectId)
                                editCommands->add_component(workspace->document_for_edit(), *selection->current().objectId, default_component_record(5));
                        }
                        else if (command == "component/sprite-renderer")
                        {
                            if (!projectLocked && selection != nullptr && selection->current().objectId)
                                editCommands->add_component(workspace->document_for_edit(), *selection->current().objectId, default_component_record(6));
                        }
                        else if (command == "component/box-collider")
                        {
                            if (!projectLocked && selection != nullptr && selection->current().objectId)
                                editCommands->add_component(workspace->document_for_edit(), *selection->current().objectId, default_component_record(3));
                        }
                        else if (command == "component/rigidbody")
                        {
                            if (!projectLocked && selection != nullptr && selection->current().objectId)
                                editCommands->add_component(workspace->document_for_edit(), *selection->current().objectId, default_component_record(4));
                        }
                        else if (command == "component/animator")
                        {
                            if (!projectLocked && selection != nullptr && selection->current().objectId)
                                editCommands->add_component(workspace->document_for_edit(), *selection->current().objectId, default_component_record(0));
                        }
                        else if (command == "window/hierarchy" || command == "view/hierarchy")
                            preferences.panelVisibility["panel.hierarchy"] =
                                !preferences.panelVisibility["panel.hierarchy"];
                        else if (command == "window/scene" || command == "view/scene")
                            preferences.panelVisibility["panel.scene"] =
                                !preferences.panelVisibility["panel.scene"];
                        else if (command == "window/inspector" || command == "view/inspector")
                            preferences.panelVisibility["panel.inspector"] =
                                !preferences.panelVisibility["panel.inspector"];
                        else if (command == "window/project" || command == "view/project")
                            preferences.panelVisibility["panel.project"] =
                                !preferences.panelVisibility["panel.project"];
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
                        else if (command == "window/game" || command == "view/game")
                            preferences.panelVisibility["panel.game"] =
                                !preferences.panelVisibility["panel.game"];
#endif
                        else if (command == "window/animation" || command == "view/animation")
                            showAnimationEditorWindow = !showAnimationEditorWindow;
                        else if (command == "window/console" || command == "view/console")
                            preferences.panelVisibility["panel.console"] =
                                !preferences.panelVisibility["panel.console"];
                        else if (command == "window/run-settings" || command == "view/run-settings")
                            preferences.panelVisibility["panel.run"] =
                                !preferences.panelVisibility["panel.run"];
                        else if (command == "window/validation" || command == "view/validation")
                            preferences.panelVisibility["panel.validation"] =
                                !preferences.panelVisibility["panel.validation"];
                        else if (command == "window/reset-layout" || command == "view/reset-layout")
                        {
                            reset_workspace_layout(preferences);
                            savedLayout = defaultLayout;
                            if (!savedLayout.empty())
                                ImGui::LoadIniSettingsFromMemory(savedLayout.data(), savedLayout.size());
                            activeScale = options.uiScaleProvided ? options.uiScale : 1.0f;
                            lastAppliedScale = -1.0f;
                            statusMessage = "Workspace layout and panel visibility were reset.";
                        }
                        else if (command == "window/fullscreen" || command == "view/fullscreen")
                        {
                            const bool fullscreen =
                                (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0;
                            if (!SDL_SetWindowFullscreen(window, fullscreen) || !SDL_SyncWindow(window))
                                statusMessage = std::string("Could not change editor fullscreen state: ") +
                                                SDL_GetError();
                            else
                                persist_preferences();
                        }
                        else if (command == "window/zoom-in" || command == "view/zoom-in")
                            scenePanel.adjust_zoom(1.1f);
                        else if (command == "window/zoom-out" || command == "view/zoom-out")
                            scenePanel.adjust_zoom(1.0f / 1.1f);
                        else if (command == "window/frame-all" || command == "view/frame-all")
                            scenePanel.request_frame_all();
                        else if (command == "window/frame-selected" || command == "view/frame-selected")
                            scenePanel.request_frame_selected();
                        else if (command == "tool/hand" || command == "view/tool-hand")
                            scenePanel.set_tool(SceneTool::Hand);
                        else if (command == "tool/move" || command == "view/tool-move")
                            scenePanel.set_tool(SceneTool::Move);
                        else if (command == "tool/rotate" || command == "view/tool-rotate")
                            scenePanel.set_tool(SceneTool::Rotate);
                        else if (command == "tool/scale" || command == "view/tool-scale")
                            scenePanel.set_tool(SceneTool::Scale);
                        else if (command == "window/build" || command == "run/build")
                        {
                            if (projectOpened && !projectLocked)
                                queue_pipeline(true);
                        }
                        else if (command == "window/build-and-run" || command == "run/build-and-run")
                        {
                            if (projectOpened && !projectLocked)
                                request_build_and_run();
                        }
                        else if (command == "window/stop" || command == "run/stop")
                            stop_owned_operation();
                        else if (command == "run/settings")
                            preferences.panelVisibility["panel.run"] = true;
                        else if (command == "help/shortcuts")
                            showShortcutReference = true;
                        else if (command == "help/preferences")
                            showPreferences = true;
                        return;
                    }
                    if (consolePanel.dispatch_semantic_action(
                            action, gamePipeline.status(), gamePipeline.console()))
                    {
                        return;
                    }
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
                    if (action.nodeId.starts_with("main/panel/game/") &&
                        action.action == "activate")
                    {
                        if (auto *playSession = gamePipeline.play_session())
                            gamePanel.dispatch_semantic_action(*playSession, action);
                        return;
                    }
#endif
                    if (action.nodeId.starts_with("main/panel/run/options/") &&
                        !projectLocked && !catalogQuery.running() &&
                        runSettingsPanel.dispatch_semantic_action(preferences.run, action))
                        return;
                    if (action.nodeId == "main/panel/run/stop" && action.action == "activate")
                    {
                        stop_owned_operation();
                        return;
                    }
                    if (projectPanel.dispatch_semantic_action(
                            workspace->document_for_edit(), *editCommands, *selection, action,
                            mainAccessibilityGeneration, !projectLocked && !documentEditingLocked))
                    {
                        preferences.panelVisibility["panel.project"] = true;
                        return;
                    }
                    if (projectLocked)
                        return;
                    if (action.nodeId == "main/panel/run/build" && action.action == "activate")
                    {
                        if (projectOpened)
                            queue_pipeline(true);
                        return;
                    }
                    if (action.nodeId == "main/panel/run/build-and-run" && action.action == "activate")
                    {
                        if (projectOpened)
                            request_build_and_run();
                        return;
                    }
                    if (action.nodeId == "main/panel/run/refresh-discovery" && action.action == "activate")
                    {
                        runSettingsPanel.refresh_discovery(true);
                        statusMessage = "Refreshed project presets, built targets, and executable choices.";
                        return;
                    }
                    if (action.nodeId == "main/panel/run/browse-executable" && action.action == "activate")
                    {
                        std::filesystem::path defaultLocation = activeProjectRoot;
                        if (!preferences.run.executable.empty())
                        {
                            auto configured = native_path_from_utf8(preferences.run.executable);
                            if (!configured.is_absolute())
                                configured = activeProjectRoot / configured;
                            std::error_code pathError;
                            if (std::filesystem::is_directory(configured, pathError) && !pathError)
                                defaultLocation = configured;
                            else if (!configured.parent_path().empty())
                                defaultLocation = configured.parent_path();
                        }
                        dialogs->open_executable(window, activeDocumentTab->identity(), defaultLocation);
                        statusMessage = "Choose the game executable for the current scene tab.";
                        return;
                    }
                    if (action.nodeId == "main/panel/run/refresh-sources" && action.action == "activate")
                    {
                        try
                        {
                            request_source_scan();
                            nextSourceScan = std::chrono::steady_clock::now() + source_scan_interval();
                            statusMessage = "Refreshing CMake and source input identity...";
                        }
                        catch (const std::exception &error)
                        {
                            statusMessage = std::string("Could not refresh source identity: ") + error.what();
                        }
                        return;
                    }
                    if (action.nodeId == "main/panel/validation/refresh" && action.action == "activate")
                    {
                        request_validation();
                        return;
                    }
                    if (scenePanel.dispatch_semantic_action(
                            action, workspace->document(), selection->current()))
                        return;
                    const std::string validationIssuePrefix =
                        "main/tab/" + std::to_string(activeDocumentTab->id().value) +
                        "/document/" + std::to_string(workspace->document().documentGeneration) +
                        "/validation/issue/";
                    if (action.nodeId.starts_with(validationIssuePrefix) && action.action == "activate" &&
                        validationService.is_current(workspace->document(), validation))
                    {
                        std::size_t issueIndex = 0;
                        const auto issueText = std::string_view(action.nodeId).substr(validationIssuePrefix.size());
                        const auto parsed = std::from_chars(issueText.data(), issueText.data() + issueText.size(),
                                                            issueIndex);
                        if (parsed.ec == std::errc{} && parsed.ptr == issueText.data() + issueText.size() &&
                            issueIndex < validation.issues.size())
                        {
                            const auto &issue = validation.issues[issueIndex];
                            if (issue.objectId &&
                                workspace->document().indexes.objectsById.contains(*issue.objectId))
                            {
                                if (issue.componentId &&
                                    !workspace->document().indexes.componentsById.contains(*issue.componentId))
                                    return;
                                selection->set(workspace->document(), Selection{
                                    issue.objectId, issue.componentId, std::nullopt,
                                    FocusedPanel::Inspector, mainAccessibilityGeneration});
                                inspectorPanel.reveal_field(*issue.objectId, issue.componentId,
                                                            issue.fieldPath);
                                preferences.panelVisibility["panel.inspector"] = true;
                            }
                        }
                        return;
                    }
                    const std::string hierarchyScope =
                        "main/tab/" + std::to_string(activeDocumentTab->id().value) +
                        "/document/" + std::to_string(workspace->document().documentGeneration);
                    if (hierarchyPanel.dispatch_semantic_action(
                            workspace->document_for_edit(), *selection, action,
                            hierarchyScope, mainAccessibilityGeneration))
                        return;
                    if (inspectorPanel.dispatch_semantic_action(
                            workspace->document_for_edit(), *selection, action,
                        "main/panel/inspector", &behaviorCatalog, editCommands))
                    {
                        preferences.panelVisibility["panel.inspector"] = true;
                        return;
                    }
                    constexpr std::string_view tabPrefix = "animation/tab/";
                    constexpr std::string_view closePrefix = "animation/close/";
                    const auto parse_tab_id = [](std::string_view text) -> std::optional<TabId> {
                        std::uint64_t value = 0;
                        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
                        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0)
                            return std::nullopt;
                        return TabId{value};
                    };
                    if (action.nodeId == "animation/save" && action.action == "activate")
                    {
                        if (auto *tab = documentManager.active_animation_tab())
                        {
                            const auto identity = tab->identity();
                            const auto saved = documentManager.save_animation_tab(identity);
                            statusMessage = saved ? saved.message :
                                "Animation save was not committed: " + saved.message;
                            if (saved)
                            {
                                if (const auto &source = tab->document().source_path())
                                    apply_project_resource_change(projectSession.resource_index(),
                                                                  *scenePreview, workspace->document(),
                                                                  *source, false);
                                projectPanel.refresh();
                            }
                            if (saved.conflict)
                                show_animation_conflict(identity);
                        }
                        return;
                    }
                    if (action.action == "activate" && action.nodeId.starts_with(tabPrefix))
                    {
                        const auto id = parse_tab_id(std::string_view(action.nodeId).substr(tabPrefix.size()));
                        if (id && documentManager.activate_animation_tab(*id))
                            showAnimationEditorWindow = true;
                        return;
                    }
                    if (action.action == "activate" && action.nodeId.starts_with(closePrefix))
                    {
                        const auto id = parse_tab_id(std::string_view(action.nodeId).substr(closePrefix.size()));
                        const auto *tab = id ? documentManager.find_animation_tab(*id) : nullptr;
                        if (tab != nullptr && tab->document().dirty())
                            closeAnimationPromptTab = *id;
                        else if (id && documentManager.close_animation_tab(*id, UnsavedDecision::Discard))
                            statusMessage = "Animation tab closed.";
                        return;
                    }
                    if (auto *tab = documentManager.active_animation_tab())
                    {
                        std::string error;
                        const std::string scope = "animation/editor/" + std::to_string(tab->id().value);
                        tab->panel().dispatch_semantic_action(tab->document(), tab->asset_root(),
                                                             action, scope, error);
                        if (!error.empty())
                            statusMessage = error;
                    }
                });
            bool animationContentsVisible = false;
            if (showAnimationEditorWindow && documentManager.animation_tab_count() != 0)
            {
                ImGui::SetNextWindowSize(ImVec2(620.0f, 520.0f), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowDockID(0x00001001, ImGuiCond_FirstUseEver);
                if (focusAnimationEditorWindow)
                {
                    ImGui::SetNextWindowFocus();
                    focusAnimationEditorWindow = false;
                }
                animationContentsVisible = ImGui::Begin("Animation Assets###animation-editor",
                                                        &showAnimationEditorWindow);
                animationWindowFocused = animationContentsVisible &&
                    ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                if (animationContentsVisible)
                {
                    std::optional<TabId> closeRequest;
                    for (const TabId id : documentManager.animation_tab_ids())
                    {
                        const auto *tab = documentManager.find_animation_tab(id);
                        if (tab == nullptr)
                            continue;
                        const std::string cleanLabel = tab->document().source_path()
                            ? tab->document().source_path()->filename().string() : "Animation";
                        std::string label = cleanLabel;
                        const bool changedOnDisk = std::any_of(
                            cachedSourceStatuses.begin(), cachedSourceStatuses.end(),
                            [&](const DocumentSourceStatus &source) {
                                return source.kind == DocumentTabKind::Animation &&
                                       source.identity == tab->identity() &&
                                       (source.file.state == SourceFileState::Changed ||
                                        source.file.state == SourceFileState::Missing);
                            });
                        if (changedOnDisk)
                            label += " !";
                        if (tab->document().dirty())
                            label += " *";
                        const bool active = documentManager.active_animation_tab_id() == id;
                        if (active)
                            ImGui::PushStyleColor(ImGuiCol_Button,
                                ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        const float markerWidth = ImGui::CalcTextSize(" * !").x;
                        const float closeBtnWidth = ImGui::CalcTextSize("×").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                        const float tabWidth = ImGui::CalcTextSize(cleanLabel.c_str()).x + markerWidth +
                                               ImGui::GetStyle().FramePadding.x * 2.0f + closeBtnWidth;
                        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
                        ImGui::SetNextItemAllowOverlap();
                        const bool tabClicked = ImGui::Button((label + "###animation-tab." +
                                           std::to_string(id.value)).c_str(),
                                          ImVec2(tabWidth, 0.0f));
                        ImGui::PopStyleVar();
                        const ImVec2 tabMin = ImGui::GetItemRectMin();
                        const ImVec2 tabMax = ImGui::GetItemRectMax();
                        const ImVec2 closeMin(tabMax.x - closeBtnWidth, tabMin.y);

                        ImGui::PushID(static_cast<int>(id.value & 0x7fffffffU));
                        ImGui::SetCursorScreenPos(closeMin);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.30f));
                        const bool closeClicked = ImGui::Button("×", ImVec2(closeBtnWidth, tabMax.y - tabMin.y));
                        ImGui::PopStyleColor(3);
                        ImGui::PopID();

                        if (closeClicked)
                            closeRequest = id;
                        else if (tabClicked)
                            documentManager.activate_animation_tab(id);
                        if (active)
                            ImGui::PopStyleColor();
                        ImGui::SetCursorScreenPos(ImVec2(tabMax.x + ImGui::GetStyle().ItemSpacing.x, tabMin.y));
                    }
                    ImGui::NewLine();
                    ImGui::Separator();

                    if (closeRequest)
                    {
                        const auto *closing = documentManager.find_animation_tab(*closeRequest);
                        if (closing != nullptr && closing->document().dirty())
                            closeAnimationPromptTab = *closeRequest;
                        else if (documentManager.close_animation_tab(*closeRequest,
                                                                      UnsavedDecision::Discard))
                            statusMessage = "Animation tab closed.";
                    }

                    if (auto *animationTab = documentManager.active_animation_tab())
                    {
                        ImGui::BeginDisabled(projectLocked);
                        animationTab->panel().draw(animationTab->document(),
                            animationTab->asset_root(), [&](const std::filesystem::path &path) {
                                return projectPanel.prepared_animation_texture(path);
                            });
                        ImGui::EndDisabled();
                    }
                    else
                        ImGui::TextDisabled("Open an .animset from the Project panel.");
                }
                ImGui::End();
            }
            if (!animationContentsVisible)
            {
                for (const TabId id : documentManager.animation_tab_ids())
                {
                    if (auto *tab = documentManager.find_animation_tab(id))
                    {
                        tab->panel().cancel_numeric_drafts();
                        tab->panel().set_playing(false);
                    }
                }
            }

            const auto accessibleText = [](std::string value, std::size_t maximum = 512) {
                if (value.size() > maximum)
                {
                    value.resize(maximum > 3 ? maximum - 3 : 0);
                    value += "...";
                }
                return value;
            };

            const std::string animationRootId = "animation/window";
            const bool animationSemanticsVisible = showAnimationEditorWindow &&
                animationContentsVisible && documentManager.animation_tab_count() != 0;
            animationWindowPlaybackVisible = animationSemanticsVisible;

            struct SemanticSnapshotCacheKey
            {
                std::uint64_t projectGeneration = 0;
                std::uint64_t documentGeneration = 0;
                Selection selection;
                std::uint64_t consoleRevision = 0;
                bool operationActive = false;

                bool operator==(const SemanticSnapshotCacheKey &) const = default;
            };

            static SemanticSnapshotCacheKey s_lastSemanticKey;
            static bool s_hasPublishedSemanticSnapshot = false;

            const SemanticSnapshotCacheKey currentSemanticKey{
                projectSession.generation().value,
                workspace->document().documentGeneration,
                selection->current(),
                gamePipeline.console().revision(),
                !operationLabel.empty()
            };

            const bool needSemanticRebuild = !s_hasPublishedSemanticSnapshot ||
                                             (accessibilitySnapshots.pending_actions() > 0) ||
                                             (currentSemanticKey != s_lastSemanticKey);

            SemanticSnapshot animationSnapshot;
            if (needSemanticRebuild)
            {
                animationSnapshot.hostLayoutId = mainAccessibilityHost;
                animationSnapshot.hostGeneration = mainAccessibilityGeneration;
                if (accessibilitySnapshotRevision != std::numeric_limits<std::uint64_t>::max())
                    ++accessibilitySnapshotRevision;
                animationSnapshot.revision = accessibilitySnapshotRevision;
                SemanticNode animationRoot;
                animationRoot.id = animationRootId;
                animationRoot.role = SemanticRole::Window;
                animationRoot.name = "Animation Assets";
                animationRoot.visible = animationSemanticsVisible;
                animationSnapshot.nodes.push_back(std::move(animationRoot));

            SemanticNode menuBarRoot;
            menuBarRoot.id = "main/menu-bar";
            menuBarRoot.role = SemanticRole::Group;
            menuBarRoot.name = "Editor menu bar";
            menuBarRoot.visible = mainMenuBarVisible;
            menuBarRoot.expanded = true;
            animationSnapshot.nodes.push_back(std::move(menuBarRoot));
            {
                auto toolbarNodes = editor_toolbar_semantic_nodes(toolbarState, toolbarFrame);
                animationSnapshot.nodes.insert(animationSnapshot.nodes.end(),
                    std::make_move_iterator(toolbarNodes.begin()),
                    std::make_move_iterator(toolbarNodes.end()));
            }
            const auto addMenuGroup = [&](std::string id, std::string name) {
                SemanticNode node;
                node.id = "main/menu/" + id;
                node.parentId = "main/menu-bar";
                node.role = SemanticRole::Group;
                node.name = std::move(name);
                node.visible = mainMenuBarVisible;
                node.expanded = true;
                animationSnapshot.nodes.push_back(std::move(node));
            };
            const auto addMenuCommand = [&](std::string id, std::string parent,
                                            std::string name, bool enabled,
                                            std::string value = {}, bool selected = false,
                                            SemanticRole role = SemanticRole::Button) {
                SemanticNode node;
                node.id = "main/menu/" + id;
                node.parentId = "main/menu/" + parent;
                node.role = role;
                node.name = std::move(name);
                node.value = std::move(value);
                node.visible = mainMenuBarVisible;
                node.enabled = enabled;
                node.selected = selected;
                node.actions = {"activate"};
                animationSnapshot.nodes.push_back(std::move(node));
            };
            addMenuGroup("file", "File");
            addMenuGroup("edit", "Edit");
            addMenuGroup("assets", "Assets");
            addMenuGroup("gameobject", "GameObject");
            addMenuGroup("component", "Component");
            addMenuGroup("window", "Window");
            addMenuGroup("help", "Help");

            // File
            addMenuCommand("file/new-scene-tab", "file", "New Scene Tab",
                           !projectLocked && !tabActionsLocked);
            addMenuCommand("file/open-scene", "file", "Open Scene", !projectLocked);
            addMenuCommand("file/open-project", "file", "Open Project Folder", !projectLocked);
            addMenuCommand("file/save", "file", "Save current Scene",
                           !projectLocked && workspace->dirty(),
                           workspace->dirty() ? "modified" : "saved");
            const bool hasDirtyAnyDoc = hasDirtySceneTab || hasDirtyAnimationTab;
            addMenuCommand("file/save-all", "file", "Save All Scenes",
                           !projectLocked && !tabActionsLocked && hasDirtyAnyDoc,
                           hasDirtyAnyDoc ? "modified documents available" : "all saved");
            addMenuCommand("file/save-as", "file", "Save Scene As", !projectLocked);
            addMenuCommand("file/exit", "file", "Exit Editor", true);

            // Edit
            const bool animFocused = animationWindowFocused && showAnimationEditorWindow;
            addMenuCommand("edit/undo", "edit", "Undo",
                           !projectLocked && can_execute_focused_undo(animFocused, documentManager, *editCommands));
            addMenuCommand("edit/redo", "edit", "Redo",
                           !projectLocked && can_execute_focused_redo(animFocused, documentManager, *editCommands));
            addMenuCommand("edit/validate", "edit", "Validate Scene", true);
            addMenuCommand("edit/preferences", "edit", "Preferences", true);

            // Assets
            const auto targetDir = projectPanel.selected_directory();
            const auto selectedEntry = projectPanel.selected_entry();
            const bool hasSelectedAsset = selectedEntry.has_value();
            addMenuCommand("assets/create-folder", "assets", "Create Folder", !projectLocked);
            addMenuCommand("assets/create-script", "assets", "Create Behavior Script", !projectLocked);
            addMenuCommand("assets/create-scene", "assets", "Create Scene", !projectLocked);
            addMenuCommand("assets/create-template", "assets", "Create Object Template", !projectLocked);
            addMenuCommand("assets/create-animation", "assets", "Create Animation Clip", !projectLocked);
            addMenuCommand("assets/delete", "assets", "Delete Asset", hasSelectedAsset && !projectLocked);
            addMenuCommand("assets/refresh", "assets", "Refresh", true);

            // GameObject
            const auto selectedObjId = (selection != nullptr && selection->current().objectId)
                ? selection->current().objectId : std::nullopt;
            addMenuCommand("gameobject/create-empty", "gameobject", "Create Empty", !projectLocked);
            addMenuCommand("gameobject/create-empty-child", "gameobject", "Create Empty Child", !projectLocked && selectedObjId.has_value());
            addMenuCommand("gameobject/create-sprite", "gameobject", "2D Sprite", !projectLocked);
            addMenuCommand("gameobject/create-camera", "gameobject", "Camera", !projectLocked);
            addMenuCommand("gameobject/create-audio", "gameobject", "Audio Object", !projectLocked);
            addMenuCommand("gameobject/frame-selected", "gameobject", "Frame Selected", selectedObjId.has_value());

            // Component
            const bool canAddComponent = !projectLocked && selectedObjId.has_value();
            addMenuCommand("component/audio-source", "component", "Audio Source", canAddComponent);
            addMenuCommand("component/camera", "component", "Camera", canAddComponent);
            addMenuCommand("component/script", "component", "Script", canAddComponent);
            addMenuCommand("component/sprite-renderer", "component", "Sprite Renderer", canAddComponent);
            addMenuCommand("component/box-collider", "component", "Box Collider", canAddComponent);
            addMenuCommand("component/rigidbody", "component", "Rigidbody", canAddComponent);
            addMenuCommand("component/animator", "component", "Animator", canAddComponent);

            // Window
            for (const auto &[id, label] : std::array{
                     std::pair{"hierarchy", "Hierarchy"}, std::pair{"scene", "Scene"},
                     std::pair{"inspector", "Inspector"}, std::pair{"project", "Project and Assets"},
                     std::pair{"console", "Console"}, std::pair{"validation", "Validation"}})
            {
                const std::string panelId = "panel." + std::string(id);
                addMenuCommand("window/" + std::string(id), "window", label, true,
                    preferences.panelVisibility[panelId] ? "shown" : "hidden",
                    preferences.panelVisibility[panelId], SemanticRole::CheckBox);
            }
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
            addMenuCommand("window/game", "window", "Game", true,
                           preferences.panelVisibility["panel.game"] ? "shown" : "hidden",
                           preferences.panelVisibility["panel.game"], SemanticRole::CheckBox);
#endif
            addMenuCommand("window/animation", "window", "Animation Assets", true,
                           showAnimationEditorWindow ? "shown" : "hidden",
                           showAnimationEditorWindow, SemanticRole::CheckBox);
            addMenuCommand("window/run-settings", "window", "Build and Run Settings", true,
                           preferences.panelVisibility["panel.run"] ? "shown" : "hidden",
                           preferences.panelVisibility["panel.run"], SemanticRole::CheckBox);
            addMenuCommand("window/reset-layout", "window", "Reset Layout", true);
            const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
            addMenuCommand("window/fullscreen", "window", "Fullscreen", true,
                           fullscreen ? "enabled" : "disabled", fullscreen,
                           SemanticRole::CheckBox);

            // Help
            addMenuCommand("help/shortcuts", "help", "Keyboard Shortcuts", true);

            SemanticNode sceneTabsRoot;
            sceneTabsRoot.id = "main/scene-tabs";
            sceneTabsRoot.role = SemanticRole::Group;
            sceneTabsRoot.name = "Scene tabs";
            sceneTabsRoot.visible = sceneTabBarVisible;
            sceneTabsRoot.enabled = sceneTabActionsEnabled;
            sceneTabsRoot.expanded = true;
            animationSnapshot.nodes.push_back(std::move(sceneTabsRoot));

            SemanticNode newSceneNode;
            newSceneNode.id = "main/scene-tabs/new";
            newSceneNode.parentId = "main/scene-tabs";
            newSceneNode.role = SemanticRole::Button;
            newSceneNode.name = "Create Scene tab";
            newSceneNode.help = "Create a new untitled Scene document.";
            newSceneNode.bounds = newSceneBounds;
            newSceneNode.visible = sceneTabBarVisible;
            newSceneNode.enabled = sceneTabActionsEnabled;
            newSceneNode.actions = {"activate"};
            animationSnapshot.nodes.push_back(std::move(newSceneNode));

            for (const TabId id : documentManager.tab_ids())
            {
                const auto *tab = documentManager.find(id);
                if (tab == nullptr)
                    continue;
                const auto &document = tab->workspace().document();
                const std::string name = document.sourcePath
                    ? path_to_utf8(document.sourcePath->filename()) : "Untitled Scene";
                const auto sourceStatus = std::find_if(cachedSourceStatuses.begin(),
                    cachedSourceStatuses.end(), [&](const DocumentSourceStatus &item) {
                        return item.tabId == id;
                    });
                SemanticNode tabNode;
                tabNode.id = "main/scene-tabs/activate/" + std::to_string(id.value);
                tabNode.parentId = "main/scene-tabs";
                tabNode.role = SemanticRole::Tab;
                tabNode.name = name;
                tabNode.bounds = sceneTabBounds.contains(id.value)
                    ? std::optional<SemanticBounds>{sceneTabBounds.at(id.value)}
                    : std::nullopt;
                tabNode.value = tab->workspace().dirty() ? "modified" : "saved";
                if (sourceStatus != cachedSourceStatuses.end())
                {
                    switch (sourceStatus->file.state)
                    {
                    case SourceFileState::Changed:
                        tabNode.value += "; source changed externally";
                        tabNode.error = true;
                        tabNode.help = accessibleText(sourceStatus->file.message);
                        break;
                    case SourceFileState::Missing:
                        tabNode.value += "; source missing";
                        tabNode.error = true;
                        tabNode.help = accessibleText(sourceStatus->file.message);
                        break;
                    case SourceFileState::Current:
                        tabNode.value += "; source current";
                        break;
                    case SourceFileState::NoSource:
                        tabNode.value += "; no source path";
                        break;
                    case SourceFileState::Unavailable:
                        tabNode.value += "; source identity unavailable";
                        tabNode.error = true;
                        tabNode.help = accessibleText(sourceStatus->file.message);
                        break;
                    }
                }
                tabNode.visible = sceneTabBarVisible;
                tabNode.enabled = sceneTabActionsEnabled;
                tabNode.selected = id == activeDocumentTab->id();
                tabNode.focused = focusedSceneTabIds.contains(id.value);
                tabNode.actions = {"activate"};
                if (tabNode.focused && animationSnapshot.focusedRevealPath.empty())
                    animationSnapshot.focusedRevealPath = {"main/scene-tabs", tabNode.id};
                animationSnapshot.nodes.push_back(std::move(tabNode));

                SemanticNode closeNode;
                closeNode.id = "main/scene-tabs/close/" + std::to_string(id.value);
                closeNode.parentId = "main/scene-tabs";
                closeNode.role = SemanticRole::Button;
                closeNode.name = "Close Scene tab " + name;
                closeNode.bounds = sceneTabCloseBounds.contains(id.value)
                    ? std::optional<SemanticBounds>{sceneTabCloseBounds.at(id.value)}
                    : std::nullopt;
                closeNode.help = tab->workspace().dirty()
                    ? "Closing asks whether to save this modified Scene."
                    : "Close this Scene tab.";
                closeNode.value = tab->workspace().dirty() ? "modified" : "saved";
                closeNode.visible = sceneTabBarVisible;
                closeNode.enabled = sceneTabActionsEnabled;
                closeNode.actions = {"activate"};
                animationSnapshot.nodes.push_back(std::move(closeNode));
            }

            if (!projectOpened)
            {
                SemanticNode welcomeRoot;
                welcomeRoot.id = "main/welcome";
                welcomeRoot.role = SemanticRole::Window;
                welcomeRoot.name = "Welcome to Platformator";
                welcomeRoot.visible = welcomeContentsVisible;
                welcomeRoot.expanded = true;
                animationSnapshot.nodes.push_back(std::move(welcomeRoot));

                SemanticNode openProjectNode;
                openProjectNode.id = "main/welcome/open-project";
                openProjectNode.parentId = "main/welcome";
                openProjectNode.role = SemanticRole::Button;
                openProjectNode.name = "Open Project Folder";
                openProjectNode.help = "Choose a project folder to open. Cancelling keeps the current scene.";
                openProjectNode.bounds = welcomeOpenProjectBounds;
                openProjectNode.visible = welcomeContentsVisible;
                openProjectNode.enabled = !projectLocked;
                openProjectNode.focused = welcomeOpenProjectFocused;
                openProjectNode.actions = {"activate"};
                if (openProjectNode.focused && animationSnapshot.focusedRevealPath.empty())
                    animationSnapshot.focusedRevealPath = {"main/welcome", openProjectNode.id};
                animationSnapshot.nodes.push_back(std::move(openProjectNode));
            }
            }

            if (hierarchyContentsVisible)
            {
                if (needSemanticRebuild)
                {
                    const std::string hierarchyScope =
                        "main/tab/" + std::to_string(activeDocumentTab->id().value) +
                        "/document/" + std::to_string(workspace->document().documentGeneration) +
                        "/hierarchy";
                    auto hierarchyNodes = hierarchyPanel.semantic_nodes(
                        workspace->document(), selection->current(), "main/tab/" +
                        std::to_string(activeDocumentTab->id().value) + "/document/" +
                        std::to_string(workspace->document().documentGeneration));
                    for (auto &node : hierarchyNodes)
                    {
                        if (node.focused)
                            animationSnapshot.focusedRevealPath = {hierarchyScope, node.id};
                        animationSnapshot.nodes.push_back(std::move(node));
                    }
                }
            }
            if (projectContentsVisible)
            {
                if (needSemanticRebuild)
                {
                    SemanticNode projectRoot;
                    projectRoot.id = "main/panel/project";
                    projectRoot.role = SemanticRole::Window;
                    projectRoot.name = "Project";
                    projectRoot.visible = true;
                    projectRoot.expanded = true;
                    animationSnapshot.nodes.push_back(std::move(projectRoot));

                    SemanticNode assetRoot;
                    assetRoot.id = "main/panel/project/asset-root";
                    assetRoot.parentId = "main/panel/project";
                    assetRoot.role = SemanticRole::Text;
                    assetRoot.name = "Project asset folder";
                    assetRoot.value = projectContext.snapshot().assetRoot.generic_string();
                    assetRoot.visible = true;
                    animationSnapshot.nodes.push_back(std::move(assetRoot));

                    auto projectNodes = projectPanel.semantic_nodes(
                        workspace->document(), selection->current(), "main/panel/project");
                    for (auto &node : projectNodes)
                    {
                        if (node.id.starts_with("main/panel/project/assign/"))
                            node.enabled = !projectLocked && !documentEditingLocked;
                        if (node.focused)
                            animationSnapshot.focusedRevealPath = {
                                "main/panel/project", node.id};
                        animationSnapshot.nodes.push_back(std::move(node));
                    }
                }
            }
            if (animationSemanticsVisible)
            {
                if (needSemanticRebuild)
                {
                for (const TabId id : documentManager.animation_tab_ids())
                {
                    const auto *tab = documentManager.find_animation_tab(id);
                    if (tab == nullptr)
                        continue;
                    const std::string name = tab->document().source_path()
                        ? tab->document().source_path()->filename().string() : "Untitled animation";
                    SemanticNode tabNode;
                    tabNode.id = "animation/tab/" + std::to_string(id.value);
                    tabNode.parentId = animationRootId;
                    tabNode.role = SemanticRole::Tab;
                    tabNode.name = name;
                    tabNode.value = tab->document().dirty() ? "modified" : "saved";
                    tabNode.selected = documentManager.active_animation_tab_id() == id;
                    tabNode.enabled = !projectLocked;
                    tabNode.actions = {"activate"};
                    animationSnapshot.nodes.push_back(std::move(tabNode));

                    SemanticNode closeNode;
                    closeNode.id = "animation/close/" + std::to_string(id.value);
                    closeNode.parentId = animationRootId;
                    closeNode.role = SemanticRole::Button;
                    closeNode.name = "Close animation tab " + name;
                    closeNode.enabled = !projectLocked;
                    closeNode.actions = {"activate"};
                    animationSnapshot.nodes.push_back(std::move(closeNode));
                }
                auto *activeAnimationTab = documentManager.active_animation_tab();
                SemanticNode saveNode;
                saveNode.id = "animation/save";
                saveNode.parentId = animationRootId;
                saveNode.role = SemanticRole::Button;
                saveNode.name = "Save active animation asset";
                saveNode.enabled = activeAnimationTab != nullptr && !projectLocked;
                saveNode.value = activeAnimationTab != nullptr &&
                    activeAnimationTab->document().dirty() ? "modified" : "saved";
                saveNode.actions = {"activate"};
                animationSnapshot.nodes.push_back(std::move(saveNode));

                if (activeAnimationTab != nullptr)
                {
                    const std::string scope = "animation/editor/" +
                                              std::to_string(activeAnimationTab->id().value);
                    auto panelNodes = activeAnimationTab->panel().semantic_nodes(
                        activeAnimationTab->document(), activeAnimationTab->asset_root(), scope,
                        [&](const std::filesystem::path &path) {
                            return projectPanel.prepared_animation_texture(path);
                        });
                    for (auto &node : panelNodes)
                    {
                        if (node.parentId.empty())
                            node.parentId = animationRootId;
                        node.enabled = node.enabled && !projectLocked;
                        animationSnapshot.nodes.push_back(std::move(node));
                    }
                }
                }
            }
            if (closeAnimationPromptTab)
                ImGui::OpenPopup("Close Animation Tab");
            if (ImGui::BeginPopupModal("Close Animation Tab", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize))
            {
                auto *closing = closeAnimationPromptTab
                    ? documentManager.find_animation_tab(*closeAnimationPromptTab) : nullptr;
                if (closing == nullptr)
                {
                    closeAnimationPromptTab.reset();
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    const std::string name = closing->document().source_path()
                        ? closing->document().source_path()->filename().string()
                        : "Untitled animation";
                    ImGui::TextWrapped("Save changes to %s before closing this animation tab?",
                                       name.c_str());
                    if (ImGui::Button("Save"))
                    {
                        const auto identity = closing->identity();
                        const auto result = documentManager.close_animation_tab(
                            *closeAnimationPromptTab, UnsavedDecision::Save);
                        if (result)
                        {
                            closeAnimationPromptTab.reset();
                            statusMessage = "Animation tab saved and closed.";
                            ImGui::CloseCurrentPopup();
                        }
                        else
                        {
                            closeAnimationPromptTab.reset();
                            ImGui::CloseCurrentPopup();
                            statusMessage = "Animation tab remains open: " + result.message;
                            if (result.conflict)
                                show_animation_conflict(identity);
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Discard"))
                    {
                        documentManager.close_animation_tab(*closeAnimationPromptTab,
                                                            UnsavedDecision::Discard);
                        closeAnimationPromptTab.reset();
                        statusMessage = "Animation changes discarded; tab closed.";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel"))
                    {
                        closeAnimationPromptTab.reset();
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndPopup();
            }

            if (externalAnimationConflict)
                ImGui::OpenPopup("Animation changed on disk");
            if (ImGui::BeginPopupModal("Animation changed on disk", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize))
            {
                const DocumentIdentity identity = externalAnimationConflict->identity;
                const SourceFileStatus observedStatus = externalAnimationConflict->observed;
                auto *tab = documentManager.find_animation_tab(identity.tabId);
                if (tab == nullptr || tab->identity() != identity)
                {
                    externalAnimationConflict.reset();
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    const auto sourceStatus = tab->document().source_file_status();
                    if (sourceStatus.state == SourceFileState::Current ||
                        sourceStatus.state == SourceFileState::NoSource)
                    {
                        externalAnimationConflict.reset();
                        ImGui::CloseCurrentPopup();
                    }
                    else
                    {
                        const bool changedSinceObserved =
                            sourceStatus.state != observedStatus.state ||
                            sourceStatus.diskIdentity != observedStatus.diskIdentity;
                        const std::string name = tab->document().source_path()
                            ? tab->document().source_path()->filename().string() : "Animation asset";
                        ImGui::TextWrapped("%s. Choose how to resolve %s on disk.",
                            sourceStatus.message.empty()
                                ? "The saved animation changed outside the editor"
                                : sourceStatus.message.c_str(), name.c_str());
                        if (changedSinceObserved)
                            ImGui::TextWrapped("A newer disk version appeared after this decision opened. Review it before overwriting.");
                        if (ImGui::Button("Reload from Disk"))
                        {
                            const auto reloaded = documentManager.reload_animation_tab(identity);
                            if (reloaded)
                            {
                                externalAnimationConflict.reset();
                                if (closeAnimationPromptTab == identity.tabId)
                                    closeAnimationPromptTab.reset();
                                statusMessage = reloaded.message;
                                ImGui::CloseCurrentPopup();
                                if (pendingAction == PendingAction::Exit)
                                    execute_pending(UnsavedDecision::Save);
                            }
                            else
                                statusMessage = reloaded.message;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Keep Work"))
                        {
                            tab->acknowledge_external_change(sourceStatus);
                            externalAnimationConflict.reset();
                            statusMessage = "Keeping the in-memory animation; the external file was left untouched.";
                            ImGui::CloseCurrentPopup();
                            if (pendingAction == PendingAction::Exit)
                                showUnsavedPrompt = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Review latest version"))
                        {
                            externalAnimationConflict->observed = sourceStatus;
                            statusMessage = "Reviewed the latest animation file version. Confirm Overwrite to replace it.";
                        }
                        ImGui::SameLine();
                        const bool canOverwrite = !changedSinceObserved &&
                            (observedStatus.state == SourceFileState::Changed ||
                             observedStatus.state == SourceFileState::Missing);
                        ImGui::BeginDisabled(!canOverwrite);
                        if (ImGui::Button("Overwrite"))
                        {
                            const auto saved = documentManager.save_animation_tab(
                                identity, AssetOverwriteObservation{observedStatus.diskIdentity});
                            if (saved)
                            {
                                externalAnimationConflict.reset();
                                statusMessage = saved.message;
                                ImGui::CloseCurrentPopup();
                                continue_after_animation_save(identity);
                            }
                            else
                                statusMessage = saved.message;
                        }
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        if (ImGui::Button("Save As..."))
                        {
                            externalAnimationSaveAsPending = true;
                            externalAnimationConflict.reset();
                            request_animation_save_as(identity);
                            statusMessage = "Choose a destination for the animation asset...";
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::EndPopup();
            }

            if (animationDestinationOverwrite)
                ImGui::OpenPopup("Animation destination exists");
            if (ImGui::BeginPopupModal("Animation destination exists", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize))
            {
                const auto context = *animationDestinationOverwrite;
                const auto *tab = documentManager.find_animation_tab(context.identity.tabId);
                if (tab == nullptr || tab->identity() != context.identity)
                {
                    animationDestinationOverwrite.reset();
                    externalAnimationSaveAsPending = false;
                    pendingAnimationSaveAsIdentity.reset();
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    ImGui::TextWrapped("Replace the observed destination %s with this animation?",
                                       context.path.filename().string().c_str());
                    if (ImGui::Button("Overwrite observed version"))
                    {
                        save_animation_as_to(context.identity, context.path, context.observation);
                        if (!animationDestinationOverwrite)
                            ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Review latest version"))
                    {
                        std::string identityError;
                        const auto observed = native_file_identity(context.path, identityError);
                        if (identityError.empty())
                        {
                            animationDestinationOverwrite->observation =
                                AssetOverwriteObservation{observed};
                            statusMessage = observed
                                ? "Reviewed the current destination version. Confirm overwrite to replace it."
                                : "Reviewed that the destination is currently missing. Confirm overwrite to save there.";
                        }
                        else
                            statusMessage = "Could not inspect overwrite destination: " + identityError;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Choose another path"))
                    {
                        const bool returningToConflict = context.returnToExternalConflict;
                        animationDestinationOverwrite.reset();
                        externalAnimationSaveAsPending = returningToConflict;
                        request_animation_save_as(context.identity);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel"))
                    {
                        animationDestinationOverwrite.reset();
                        externalAnimationSaveAsPending = false;
                        pendingAnimationSaveAsIdentity.reset();
                        if (context.returnToExternalConflict)
                            show_animation_conflict(context.identity);
                        else if (pendingAction == PendingAction::Exit)
                            showUnsavedPrompt = true;
                        statusMessage = "Animation Save As was cancelled; the animation tab remains open.";
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndPopup();
            }

            const auto pipelineStageName = [](PipelineStage stage) -> const char * {
                switch (stage)
                {
                case PipelineStage::Idle: return "Idle";
                case PipelineStage::Configuring: return "Configuring";
                case PipelineStage::Building: return "Building";
                case PipelineStage::QueryingCatalog: return "Checking behavior catalog";
                case PipelineStage::Launching: return "Launching";
                case PipelineStage::Running: return "Running";
                }
                return "Unknown";
            };
            bool showConsole = show_panel("panel.console");
            ImGuiContext *previousConsoleContext = activate_panel_context("panel.console");
            const auto consoleFrame = begin_panel_window(showConsole,
                "Console###panel.console", &showConsole);
            if (consoleFrame.contentsVisible)
            {
                consolePanel.draw(gamePipeline.status(), gamePipeline.console());
                if (needSemanticRebuild)
                {
                    auto consoleNodes = consolePanel.semantic_nodes(
                        gamePipeline.status(), gamePipeline.console(), "main/panel/console");
                    animationSnapshot.nodes.insert(animationSnapshot.nodes.end(),
                        std::make_move_iterator(consoleNodes.begin()),
                        std::make_move_iterator(consoleNodes.end()));
                }
            }
            end_panel_window(consoleFrame);
            restore_panel_context(previousConsoleContext);
            preferences.panelVisibility["panel.console"] = showConsole;

#if PLATFORMATOR_ENABLE_EDITOR_SESSION
            bool showGame = show_panel("panel.game");
            ImGuiContext *previousGameContext = activate_panel_context("panel.game");
            SDL_Renderer *gameFrameRenderer = renderer;
            if (const auto assignment = preferences.panelHostAssignments.find("panel.game");
                assignment != preferences.panelHostAssignments.end() &&
                assignment->second != main_host_layout_id)
                if (const auto host = detachedHosts.find(assignment->second); host != detachedHosts.end())
                    gameFrameRenderer = host->second->renderer();
            gamePanel.set_renderer(gameFrameRenderer);
            auto *playSession = gamePipeline.play_session();
            ImGuiID sceneDockId = 0x00003000;
            if (const ImGuiWindow *sceneWindow = ImGui::FindWindowByName("Scene###panel.scene"))
            {
                if (sceneWindow->DockNode != nullptr)
                    sceneDockId = sceneWindow->DockNode->ID;
                else if (sceneWindow->DockId != 0)
                    sceneDockId = sceneWindow->DockId;
            }
            ImGui::SetNextWindowDockID(sceneDockId, ImGuiCond_FirstUseEver);
            const auto gameFrame = begin_panel_window(showGame,
                "Game###panel.game", &showGame);
            if (gameFrame.contentsVisible)
            {
                gamePanel.draw(playSession, true);
                if (needSemanticRebuild)
                {
                    SemanticNode root;
                    root.id = "main/panel/game";
                    root.role = SemanticRole::Window;
                    root.name = "Game";
                    root.visible = true;
                    root.expanded = true;
                    animationSnapshot.nodes.push_back(std::move(root));
                    auto nodes = gamePanel.semantic_nodes(playSession, "main/panel/game");
                    for (auto &node : nodes)
                        animationSnapshot.nodes.push_back(std::move(node));
                }
            }
            else
                gamePanel.set_visible(playSession, false);
            end_panel_window(gameFrame);
            restore_panel_context(previousGameContext);
            preferences.panelVisibility["panel.game"] = showGame;
#endif

            bool showInspector = show_panel("panel.inspector");
            ImGuiContext *previousInspectorContext = activate_panel_context("panel.inspector");
            SDL_Renderer *inspectorFrameRenderer = renderer;
            std::uint64_t inspectorHostGeneration = mainAccessibilityGeneration.value;
            if (const auto assignment = preferences.panelHostAssignments.find("panel.inspector");
                assignment != preferences.panelHostAssignments.end() &&
                assignment->second != main_host_layout_id)
                if (const auto host = detachedHosts.find(assignment->second); host != detachedHosts.end())
                {
                    inspectorFrameRenderer = host->second->renderer();
                    inspectorHostGeneration = host->second->identity().generation.value;
                }
            inspectorPanel.set_host_generation(
                inspectorHostGeneration, workspace != nullptr ? &workspace->document_for_edit() : nullptr);
            projectPanel.set_frame_renderer(inspectorFrameRenderer);
            const auto inspectorFrame = begin_panel_window(showInspector,
                "Inspector###panel.inspector", &showInspector);
            if (!inspectorFrame.contentsVisible)
                inspectorPanel.cancel_active_interactions(
                    workspace != nullptr ? &workspace->document_for_edit() : nullptr);
            if (inspectorFrame.contentsVisible)
            {
                ImGui::BeginDisabled(documentEditingLocked);
                inspectorPanel.draw(workspace->document_for_edit(), *selection, *editCommands,
                                     behaviorCatalog, browse_asset, &projectSession.resource_index());
                ImGui::EndDisabled();

                if (needSemanticRebuild)
                {
                    SemanticNode root;
                    root.id = "main/panel/inspector";
                    root.role = SemanticRole::Window;
                    root.name = "Inspector";
                    root.visible = true;
                    root.expanded = true;
                    animationSnapshot.nodes.push_back(std::move(root));

                    const auto &currentSelection = selection->current();
                    SemanticNode selected;
                    selected.id = "main/panel/inspector/selection";
                    selected.parentId = "main/panel/inspector";
                    selected.role = SemanticRole::Group;
                    selected.name = currentSelection.objectId
                        ? "Selected GameObject " + std::to_string(*currentSelection.objectId)
                        : "No GameObject selected";
                    selected.value = currentSelection.selectedObjectIds.size() > 1
                        ? std::to_string(currentSelection.selectedObjectIds.size()) + " objects selected"
                        : (currentSelection.componentId
                            ? "Component " + std::to_string(*currentSelection.componentId)
                            : "GameObject");
                    selected.visible = true;
                    selected.selected = currentSelection.objectId.has_value();
                    selected.focused = currentSelection.focusedPanel == FocusedPanel::Inspector;
                    animationSnapshot.nodes.push_back(std::move(selected));
                    auto inspectorNodes = inspectorPanel.semantic_nodes(
                        workspace->document(), currentSelection, "main/panel/inspector",
                        &behaviorCatalog);
                    for (auto &node : inspectorNodes)
                    {
                        node.enabled = node.enabled && !documentEditingLocked;
                        if (node.focused && animationSnapshot.focusedRevealPath.empty())
                            animationSnapshot.focusedRevealPath = {
                                "main/panel/inspector", node.id};
                        animationSnapshot.nodes.push_back(std::move(node));
                    }
                }
            }
            end_panel_window(inspectorFrame);
            projectPanel.set_frame_renderer(renderer);
            restore_panel_context(previousInspectorContext);
            preferences.panelVisibility["panel.inspector"] = showInspector;

            bool showRun = show_panel("panel.run");
            ImGuiContext *previousRunContext = activate_panel_context("panel.run");
            if (showRun && !runPanelVisibleLastFrame)
                ImGui::SetNextWindowFocus();
            const auto runFrame = begin_panel_window(showRun,
                "Build and Run###panel.run", &showRun);
            if (runFrame.contentsVisible)
            {
                const auto previousRunPreferences = preferences.run;
                const auto action = runSettingsPanel.draw(preferences.run, gamePipeline.status(),
                    projectLocked || catalogQuery.running(), workspace->dirty());
                if (needSemanticRebuild)
                {
                    SemanticNode root;
                    root.id = "main/panel/run";
                    root.role = SemanticRole::Window;
                    root.name = "Build and Run";
                    root.visible = true;
                    root.expanded = true;
                    animationSnapshot.nodes.push_back(std::move(root));

                    SemanticNode status;
                    status.id = "main/panel/run/status";
                    status.parentId = "main/panel/run";
                    status.role = SemanticRole::Text;
                    status.name = "Build and Run status";
                    status.value = std::string(pipelineStageName(gamePipeline.status().stage));
                    if (!gamePipeline.status().message.empty())
                        status.value += ": " + accessibleText(gamePipeline.status().message);
                    status.visible = true;
                    animationSnapshot.nodes.push_back(std::move(status));

                    const auto addRunButton = [&](std::string id, std::string name, bool enabled) {
                        SemanticNode node;
                        node.id = "main/panel/run/" + id;
                        node.parentId = "main/panel/run";
                        node.role = SemanticRole::Button;
                        node.name = std::move(name);
                        node.visible = true;
                        node.enabled = enabled;
                        node.focused = runSettingsPanel.focused_control() == id;
                        node.actions = {"activate"};
                        if (node.focused && animationSnapshot.focusedRevealPath.empty())
                            animationSnapshot.focusedRevealPath = {"main/panel/run", node.id};
                        animationSnapshot.nodes.push_back(std::move(node));
                    };
                    const bool runBusy = projectLocked || catalogQuery.running();
                    addRunButton("build", "Build project", projectOpened && !runBusy);
                    addRunButton("build-and-run", "Build and Run", projectOpened && !runBusy);
                    addRunButton("stop", "Stop owned operation", gamePipeline.busy() ||
                        catalogQuery.running() || pendingPipelineBuildOnly || pendingRunValidation.active());
                    addRunButton("refresh-discovery", "Rediscover Build Targets",
                        projectOpened && !runBusy);
                    addRunButton("browse-executable", "Browse for game executable",
                        projectOpened && !runBusy);
                    addRunButton("refresh-sources", "Recheck Sources", projectOpened && !runBusy);
                    auto runOptionNodes = runSettingsPanel.semantic_nodes(
                        preferences.run, "main/panel/run", runBusy);
                    animationSnapshot.nodes.insert(animationSnapshot.nodes.end(),
                        std::make_move_iterator(runOptionNodes.begin()),
                        std::make_move_iterator(runOptionNodes.end()));
                }
                if (preferences.run != previousRunPreferences)
                    deferredPreferenceSave.schedule(std::chrono::steady_clock::now());
                if (sourceInputIdentity.empty())
                    ImGui::TextDisabled("Scanning configured CMake and source inputs...");
                if (!projectSession.source_notifications_available())
                    ImGui::TextDisabled("Source notifications are unavailable; bounded identity checks run every 5 seconds.");
                if (action == RunPanelAction::RefreshDiscovery)
                {
                    statusMessage = "Refreshed project presets, built targets, and executable choices.";
                }
                else if (action == RunPanelAction::BrowseExecutable)
                {
                    std::filesystem::path defaultLocation = activeProjectRoot;
                    if (!preferences.run.executable.empty())
                    {
                        auto configured = native_path_from_utf8(preferences.run.executable);
                        if (!configured.is_absolute())
                            configured = activeProjectRoot / configured;
                        std::error_code pathError;
                        if (std::filesystem::is_directory(configured, pathError) && !pathError)
                            defaultLocation = configured;
                        else if (!configured.parent_path().empty())
                            defaultLocation = configured.parent_path();
                    }
                    dialogs->open_executable(window, activeDocumentTab->identity(), defaultLocation);
                    statusMessage = "Choose the game executable for the current scene tab.";
                }
                else if (action == RunPanelAction::RefreshSources)
                {
                    try
                    {
                        request_source_scan();
                        nextSourceScan = std::chrono::steady_clock::now() + source_scan_interval();
                        statusMessage = "Refreshing CMake and source input identity...";
                    }
                    catch (const std::exception &error)
                    {
                        statusMessage = std::string("Could not refresh source identity: ") + error.what();
                    }
                }
                else if (action == RunPanelAction::Build) queue_pipeline(true);
                else if (action == RunPanelAction::BuildAndRun) request_build_and_run();
                else if (action == RunPanelAction::Stop)
                    stop_owned_operation();
            }
            end_panel_window(runFrame);
            restore_panel_context(previousRunContext);
            runPanelVisibleLastFrame = showRun;
            preferences.panelVisibility["panel.run"] = showRun;

            if (statusMessage.empty() && !editCommands->last_error().empty())
                statusMessage = editCommands->last_error();
            bool showValidationPanel = show_panel("panel.validation");
            ImGuiContext *previousValidationContext = activate_panel_context("panel.validation");
            if (showValidationPanel && !validationPanelVisibleLastFrame)
                ImGui::SetNextWindowFocus();
            const auto validationFrame = begin_panel_window(showValidationPanel,
                "Validation###panel.validation", &showValidationPanel);
            if (validationFrame.contentsVisible)
            {
                const bool validationCurrent =
                    validationService.is_current(workspace->document(), validation);
                if (ImGui::Button("Validate (F6)"))
                    request_validation();
                const bool validationRefreshFocused = ImGui::IsItemFocused();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", validationCurrent ? "Current" : "Pending");
                if (!validationCurrent)
                    ImGui::TextDisabled("Previous results are withheld until the current revision finishes.");
                else if (validation.issues.empty())
                    ImGui::TextUnformatted("No known-field issues.");
                else
                    for (std::size_t index = 0; index < validation.issues.size(); ++index)
                    {
                        const auto &issue = validation.issues[index];
                        const std::string label = std::string(issue.blocksRun ? "Run blocked: " : "Warning: ") +
                            issue.fieldPath + ": " + issue.message + "###validation.issue." +
                            std::to_string(index);
                        if (ImGui::Selectable(label.c_str()) && issue.objectId &&
                            workspace->document().indexes.objectsById.contains(*issue.objectId))
                        {
                            const auto component = issue.componentId
                                ? workspace->document().indexes.componentsById.find(*issue.componentId)
                                : workspace->document().indexes.componentsById.end();
                            if (issue.componentId && component == workspace->document().indexes.componentsById.end())
                                continue;
                            if (issue.componentId && component->second.ownerObjectId != *issue.objectId)
                                continue;
                            selection->set(workspace->document(), Selection{
                                issue.objectId, issue.componentId, std::nullopt, FocusedPanel::Inspector});
                            inspectorPanel.reveal_field(*issue.objectId, issue.componentId,
                                                        issue.fieldPath);
                            preferences.panelVisibility["panel.inspector"] = true;
                        }
                    }
                if (ImGui::CollapsingHeader("Details"))
                {
                    ImGui::Text("Objects: %zu | Components: %zu",
                                workspace->document().indexes.objectsById.size(),
                                workspace->document().indexes.componentsById.size());
                    const auto currentHistory = editCommands->history_status();
                    if (currentHistory.overBudget)
                        ImGui::TextWrapped("History limit: retained %zu bytes of %zu; keeping the newest oversized edit.",
                                           currentHistory.retainedBytes, currentHistory.budgetBytes);
                    else
                        ImGui::Text("History: %zu undo / %zu redo | %zu / %zu bytes retained",
                                    currentHistory.undoCommands, currentHistory.redoCommands,
                                    currentHistory.retainedBytes, currentHistory.budgetBytes);
                    ImGui::TextUnformatted("Preview does not instantiate behaviors or simulate gameplay.");
                }
                if (!statusMessage.empty()) ImGui::TextWrapped("%s", statusMessage.c_str());

                if (needSemanticRebuild)
                {
                    SemanticNode root;
                    root.id = "main/panel/validation";
                    root.role = SemanticRole::Window;
                    root.name = "Validation";
                    root.visible = true;
                    root.expanded = true;
                    animationSnapshot.nodes.push_back(std::move(root));

                    SemanticNode refresh;
                    refresh.id = "main/panel/validation/refresh";
                    refresh.parentId = "main/panel/validation";
                    refresh.role = SemanticRole::Button;
                    refresh.name = "Validate Scene";
                    refresh.help = "Runs validation for the current document revision.";
                    refresh.visible = true;
                    refresh.enabled = !projectLocked;
                    refresh.focused = validationRefreshFocused;
                    refresh.actions = {"activate"};
                    if (refresh.focused && animationSnapshot.focusedRevealPath.empty())
                        animationSnapshot.focusedRevealPath = {"main/panel/validation", refresh.id};
                    animationSnapshot.nodes.push_back(std::move(refresh));

                    SemanticNode validationStatus;
                    validationStatus.id = "main/panel/validation/status";
                    validationStatus.parentId = "main/panel/validation";
                    validationStatus.role = SemanticRole::Text;
                    validationStatus.name = "Validation status";
                    validationStatus.value = validationCurrent ?
                        (validation.blocksRun ? "Current; Run blocked" : "Current; Run allowed") : "Pending";
                    validationStatus.error = validationCurrent && validation.blocksRun;
                    validationStatus.visible = true;
                    animationSnapshot.nodes.push_back(std::move(validationStatus));

                    SemanticNode counts;
                    counts.id = "main/panel/validation/counts";
                    counts.parentId = "main/panel/validation";
                    counts.role = SemanticRole::Text;
                    counts.name = "Scene object and component counts";
                    counts.value = std::to_string(workspace->document().indexes.objectsById.size()) +
                        " objects, " + std::to_string(workspace->document().indexes.componentsById.size()) +
                        " components";
                    counts.visible = true;
                    animationSnapshot.nodes.push_back(std::move(counts));

                    if (validationCurrent)
                    {
                        const auto &validationSelection = selection->current();
                        const std::size_t visibleIssueCount = std::min<std::size_t>(64, validation.issues.size());
                        for (std::size_t index = 0; index < visibleIssueCount; ++index)
                        {
                            const auto &issue = validation.issues[index];
                            SemanticNode issueNode;
                            issueNode.id = "main/tab/" + std::to_string(activeDocumentTab->id().value) +
                                "/document/" + std::to_string(workspace->document().documentGeneration) +
                                "/validation/issue/" + std::to_string(index);
                            issueNode.parentId = "main/panel/validation";
                            issueNode.role = SemanticRole::ListItem;
                            issueNode.name = accessibleText(
                                (issue.blocksRun ? "Run blocked: " : "Warning: ") +
                                issue.fieldPath + ": " + issue.message, 512);
                            issueNode.value = issue.blocksRun ? "error" : "warning";
                            issueNode.help = accessibleText(issue.message, 512);
                            issueNode.visible = true;
                            issueNode.error = issue.blocksRun;
                            issueNode.selected = issue.objectId &&
                                validationSelection.objectId == issue.objectId &&
                                validationSelection.componentId == issue.componentId;
                            issueNode.focused = issueNode.selected &&
                                validationSelection.focusedPanel == FocusedPanel::Inspector;
                            if (issue.objectId &&
                                workspace->document().indexes.objectsById.contains(*issue.objectId))
                                issueNode.actions = {"activate"};
                            animationSnapshot.nodes.push_back(std::move(issueNode));
                        }
                        if (validation.issues.size() > visibleIssueCount)
                        {
                            SemanticNode more;
                            more.id = "main/panel/validation/more-issues";
                            more.parentId = "main/panel/validation";
                            more.role = SemanticRole::Text;
                            more.name = "Additional validation issues";
                            more.value = std::to_string(validation.issues.size() - visibleIssueCount) +
                                " more issues are not in the current semantic page";
                            more.visible = true;
                            animationSnapshot.nodes.push_back(std::move(more));
                        }
                    }
                }
            }
            end_panel_window(validationFrame);
            restore_panel_context(previousValidationContext);
            validationPanelVisibleLastFrame = showValidationPanel;
            preferences.panelVisibility["panel.validation"] = showValidationPanel;

            bool showScene = show_panel("panel.scene");
            scenePanel.set_visible(showScene);
            ImGuiContext *previousSceneContext = activate_panel_context("panel.scene");
            SDL_Window *sceneWindow = window;
            SDL_Renderer *sceneRenderer = renderer;
            const auto sceneAssignment = preferences.panelHostAssignments.find("panel.scene");
            if (sceneAssignment != preferences.panelHostAssignments.end() &&
                sceneAssignment->second != main_host_layout_id)
            {
                const auto sceneHost = detachedHosts.find(sceneAssignment->second);
                if (sceneHost != detachedHosts.end())
                {
                    sceneWindow = sceneHost->second->window();
                    sceneRenderer = sceneHost->second->renderer();
                }
            }
            if (showScene)
            {
                DocumentSession &sceneDocument = workspace->document_for_edit();
                if (!scenePanel.view_matches_document(sceneDocument))
                {
                    const SavedTabPreferences *savedView = nullptr;
                    if (sceneDocument.sourcePath)
                        for (const auto &tab : preferences.openTabs)
                            if (tab.scenePath.lexically_normal() == sceneDocument.sourcePath->lexically_normal())
                            {
                                savedView = &tab;
                                break;
                            }
                    if (savedView != nullptr)
                        scenePanel.restore_view_state(sceneDocument, savedView->viewCenterX,
                                                      savedView->viewCenterY, savedView->viewZoom);
                    else
                        scenePanel.reset_view_state(sceneDocument);
                }
                const auto oldSnapPreferences = preferences.snap;
                const auto hiddenBeforeScene = hiddenObjectIds;
                const auto lockedBeforeScene = lockedObjectIds;
                scenePanel.draw(sceneDocument, *scenePreview, *selection, *editCommands,
                                sceneWindow, sceneRenderer, preferences.snap, hasModal,
                                !documentEditingLocked, &hiddenObjectIds, &lockedObjectIds,
                                sceneRenderer == renderer
                                    ? std::function<SDL_Texture *(SDL_Texture *, int, int)>{}
                                    : std::function<SDL_Texture *(SDL_Texture *, int, int)>{
                                        [&](SDL_Texture *source, int width, int height) {
                                            return sceneTextures.texture_for(renderer, source,
                                                scenePreview->render_revision(), sceneRenderer,
                                                width, height);
                                        }});
                if (needSemanticRebuild)
                {
                    auto sceneNodes = scenePanel.semantic_nodes(
                        sceneDocument, selection->current(), preferences.snap, "main/panel/scene");
                    animationSnapshot.nodes.insert(animationSnapshot.nodes.end(),
                        std::make_move_iterator(sceneNodes.begin()),
                        std::make_move_iterator(sceneNodes.end()));
                }
                if (preferences.snap != oldSnapPreferences)
                    schedule_preference_save();
                if (hiddenObjectIds != hiddenBeforeScene || lockedObjectIds != lockedBeforeScene)
                    schedule_preference_save();
                if (sceneDocument.sourcePath)
                {
                    sceneViewPreferencesDirty = sceneViewPreferencesDirty ||
                        preferences.activeTabPath != sceneDocument.sourcePath;
                    preferences.activeTabPath = *sceneDocument.sourcePath;
                    auto tab = std::find_if(preferences.openTabs.begin(), preferences.openTabs.end(),
                        [&](const SavedTabPreferences &candidate) {
                            return candidate.scenePath.lexically_normal() ==
                                   sceneDocument.sourcePath->lexically_normal();
                        });
                    if (tab == preferences.openTabs.end())
                    {
                        if (preferences.openTabs.size() >= 128)
                            preferences.openTabs.erase(preferences.openTabs.begin());
                        preferences.openTabs.push_back(SavedTabPreferences{*sceneDocument.sourcePath});
                        tab = std::prev(preferences.openTabs.end());
                    }
                    const auto view = scenePanel.view_state();
                    activeDocumentTab->view_state() = SceneViewState{
                        view.center.x, view.center.y, view.zoom};
                    activeDocumentTab->set_expanded_hierarchy_ids(hierarchyPanel.expanded_object_ids());
                    const bool viewChanged = tab->viewCenterX != view.center.x ||
                                             tab->viewCenterY != view.center.y || tab->viewZoom != view.zoom;
                    sceneViewPreferencesDirty = sceneViewPreferencesDirty || viewChanged;
                    tab->viewCenterX = view.center.x;
                    tab->viewCenterY = view.center.y;
                    tab->viewZoom = view.zoom;
                    const auto expanded = hierarchyPanel.expanded_object_ids();
                    if (tab->expandedHierarchyIds != expanded)
                    {
                        tab->expandedHierarchyIds = expanded;
                        sceneViewPreferencesDirty = true;
                    }
                    tab->hasExpansionState = true;
                    if (sceneViewPreferencesDirty && !scenePanel.has_active_input())
                    {
                        schedule_preference_save();
                        sceneViewPreferencesDirty = false;
                    }
                }
                else
                {
                    sceneViewPreferencesDirty = sceneViewPreferencesDirty ||
                        preferences.activeTabPath.has_value();
                    preferences.activeTabPath.reset();
                }
            }
            else
                scenePanel.cancel_gesture(*editCommands, workspace->document_for_edit());
            showScene = scenePanel.is_visible();
            preferences.panelVisibility["panel.scene"] = showScene;
            restore_panel_context(previousSceneContext);
            if (!scenePreview->last_error().empty())
                statusMessage = scenePreview->last_error();

            if (preferences.panelVisibility != panelStateBefore)
                schedule_preference_save();

            const auto pathDialogAction = draw_file_dialog_path_entry(
                showManualPath, dialogError, manualPath.data(), manualPath.size());
            if (pathDialogAction == FileDialogPathAction::Submit)
            {
                dialogs->submit_manual_path(manualPathKind,
                                            manualPathIdentity.value_or(activeDocumentTab->identity()),
                                            native_path_from_utf8(manualPath.data()));
                openSceneDialogPending = manualPathKind == AsyncRequestKind::OpenScene;
                statusMessage = manualPathKind == AsyncRequestKind::OpenScene
                                    ? "Checking the selected scene path..."
                                    : "Checking the selected path...";
                dialogError.clear();
                manualPath.fill('\0');
                manualPathIdentity.reset();
            }
            else if (pathDialogAction == FileDialogPathAction::Cancel)
            {
                dialogError.clear();
                manualPath.fill('\0');
                if (externalAnimationSaveAsPending)
                {
                    externalAnimationSaveAsPending = false;
                    pendingAnimationSaveAsIdentity.reset();
                    if (manualPathIdentity)
                    {
                        if (const auto *tab = documentManager.find_animation_tab(
                                manualPathIdentity->tabId);
                            tab != nullptr && tab->identity() == *manualPathIdentity)
                            show_animation_conflict(*manualPathIdentity);
                    }
                }
                manualPathIdentity.reset();
                if (externalSaveAsPending)
                {
                    externalSaveAsPending = false;
                    showExternalConflictPrompt = true;
                }
                if (closeAfterSaveTab)
                {
                    closeAfterSaveTab.reset();
                    closePromptTab.reset();
                    statusMessage = "Path entry was cancelled; the scene tab remains open.";
                }
                if (pendingAction != PendingAction::None)
                    showUnsavedPrompt = true;
                else
                    statusMessage = "Path entry was cancelled; the current scene and project are unchanged.";
            }

            if (showUnsavedPrompt)
                ImGui::OpenPopup("Unsaved scene changes");
            if (ImGui::BeginPopupModal("Unsaved scene changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("Save changes before continuing?");
                if (ImGui::Button("Save"))
                {
                    showUnsavedPrompt = false;
                    ImGui::CloseCurrentPopup();
                    execute_pending(UnsavedDecision::Save);
                }
                if (pendingAction != PendingAction::BuildAndRun || workspace->document().sourcePath)
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Discard"))
                    {
                        showUnsavedPrompt = false;
                        ImGui::CloseCurrentPopup();
                        execute_pending(UnsavedDecision::Discard);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    showUnsavedPrompt = false;
                    ImGui::CloseCurrentPopup();
                    execute_pending(UnsavedDecision::Cancel);
                }
                ImGui::EndPopup();
            }
            if (showExternalConflictPrompt)
                ImGui::OpenPopup("Scene changed on disk");
            if (ImGui::BeginPopupModal("Scene changed on disk", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                const auto externalStatus = workspace->source_file_status();
                ImGui::TextWrapped("%s. Choose how to resolve the scene on disk.",
                    externalStatus.message.empty()
                        ? "The saved scene changed outside the editor"
                        : externalStatus.message.c_str());
                if (ImGui::Button("Reload from Disk"))
                {
                    showExternalConflictPrompt = false;
                    ImGui::CloseCurrentPopup();
                    cancel_scene_gesture();
                    const auto reloaded = workspace->reload_source_file();
                    if (reloaded)
                    {
                        selection->repair(workspace->document());
                        scenePreview->reconcile(workspace->document(), true);
                        validationService.cancel();
                        validationService.request(workspace->document());
                        clear_validation_dependencies();
                        validation = ValidationResult{};
                        activeDocumentTab->clear_external_change_acknowledgement();
                        statusMessage = "Reloaded the scene from disk; the editor history was reset.";
                        if (pendingAction != PendingAction::None)
                            execute_pending(UnsavedDecision::Discard);
                    }
                    else
                        statusMessage = reloaded.message;
                }
                ImGui::SameLine();
                if (ImGui::Button("Keep Work"))
                {
                    activeDocumentTab->acknowledge_external_change();
                    showExternalConflictPrompt = false;
                    ImGui::CloseCurrentPopup();
                    statusMessage = "Keeping the in-memory scene; the external file was left untouched.";
                    if (pendingAction != PendingAction::None)
                    {
                        if (workspace->dirty())
                            showUnsavedPrompt = true;
                        else
                            execute_pending(UnsavedDecision::Discard);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Overwrite"))
                {
                    showExternalConflictPrompt = false;
                    ImGui::CloseCurrentPopup();
                    commit_scene_gesture();
                    const auto saved = workspace->save(true);
                    if (saved)
                    {
                        activeDocumentTab->clear_external_change_acknowledgement();
                        statusMessage = saved.message;
                        if (workspace->document().sourcePath)
                            record_adopted_scene(preferences, *workspace->document().sourcePath);
                        persist_preferences();
                        if (pendingAction != PendingAction::None)
                            execute_pending(UnsavedDecision::Discard);
                    }
                    else
                        statusMessage = saved.message;
                }
                ImGui::SameLine();
                if (ImGui::Button("Save As..."))
                {
                    showExternalConflictPrompt = false;
                    ImGui::CloseCurrentPopup();
                    externalSaveAsPending = true;
                    request_save_as();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    activeDocumentTab->acknowledge_external_change();
                    showExternalConflictPrompt = false;
                    ImGui::CloseCurrentPopup();
                    if (pendingAction != PendingAction::None)
                        showUnsavedPrompt = true;
                }
                ImGui::EndPopup();
            }
            if (showOverwritePrompt)
                ImGui::OpenPopup("Destination already exists");
            if (ImGui::BeginPopupModal("Destination already exists", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextWrapped("Replace the existing destination with the current scene?");
                if (ImGui::Button("Overwrite"))
                {
                    showOverwritePrompt = false;
                    ImGui::CloseCurrentPopup();
                    save_as_to(pendingSaveAsPath, true);
                }
                ImGui::SameLine();
                if (ImGui::Button("Choose another path"))
                {
                    showOverwritePrompt = false;
                    ImGui::CloseCurrentPopup();
                    request_save_as();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    const bool returningToExternalConflict = externalSaveAsPending;
                    externalSaveAsPending = false;
                    showOverwritePrompt = false;
                    ImGui::CloseCurrentPopup();
                    pendingSaveAsPath.clear();
                    if (returningToExternalConflict)
                        showExternalConflictPrompt = true;
                    else if (closeAfterSaveTab)
                    {
                        closeAfterSaveTab.reset();
                        closePromptTab.reset();
                        statusMessage = "Overwrite was cancelled; the scene tab remains open.";
                    }
                    else if (pendingAction != PendingAction::None)
                        showUnsavedPrompt = true;
                }
                ImGui::EndPopup();
            }

            std::string windowTitle = "Platformator Editor — " + projectLabel + " — " + sceneLabel;
            if (workspace->dirty())
                windowTitle += " *";
            if (!operationLabel.empty())
                windowTitle += " — " + operationLabel;
            if (windowTitle != lastWindowTitle)
            {
                SDL_SetWindowTitle(window, windowTitle.c_str());
                lastWindowTitle = windowTitle;
            }
            std::string semanticError;
            if (needSemanticRebuild && accessibilitySnapshotRevision != 0)
            {
                std::vector<SemanticHostIdentity> detachedSemanticHosts;
                detachedSemanticHosts.reserve(detachedHosts.size());
                for (const auto &[hostId, host] : detachedHosts)
                {
                    std::string name = "Detached editor window";
                    for (const auto &[panelId, assignedHost] : preferences.panelHostAssignments)
                        if (assignedHost == hostId)
                        {
                            name = panel_title(panelId);
                            break;
                        }
                    const auto &identity = host->identity();
                    detachedSemanticHosts.push_back(
                        {identity.layoutId, identity.generation, std::move(name)});
                }
                auto partitions = partition_semantic_snapshot(
                    std::move(animationSnapshot),
                    {mainHost.layoutId, mainHost.generation, "Platformator Editor"},
                    detachedSemanticHosts, preferences.panelHostAssignments);
                if (partitions.empty())
                    statusMessage = "Editor accessibility snapshots could not be partitioned by host.";
                else
                    for (auto &partition : partitions)
                        if (!accessibilitySnapshots.publish(std::move(partition), &semanticError))
                        {
                            if (!semanticError.empty())
                                statusMessage = "Editor accessibility snapshot could not be published: " + semanticError;
                            break;
                        }
                s_lastSemanticKey = currentSemanticKey;
                s_hasPublishedSemanticSnapshot = true;
            }
            projectSession.resource_index().end_visible_reference_frame();
            std::vector<HostLayoutId> failedDetachedHosts;
            for (auto &[hostId, host] : detachedHosts)
            {
                if (!detachedFramesStarted.contains(hostId))
                    continue;
                try
                {
                    host->present();
                }
                catch (const std::exception &error)
                {
                    statusMessage = std::string("A detached host renderer failed; its panels were redocked: ") +
                                    error.what();
                    failedDetachedHosts.push_back(hostId);
                }
            }
            for (const HostLayoutId hostId : failedDetachedHosts)
                close_detached_host(hostId);
            if (runValidationReadyThisFrame &&
                pendingRunValidation.matches(*runValidationReadyThisFrame,
                                             activeDocumentTab->identity()) &&
                validationService.is_current(workspace->document(), validation) &&
                pendingRunValidation.complete(*runValidationReadyThisFrame,
                                              activeDocumentTab->identity()))
            {
                if (!validation_allows_run(validationService, workspace->document(), validation))
                {
                    statusMessage = "Run is blocked by current scene validation errors.";
                    preferences.panelVisibility["panel.validation"] = true;
                }
                else
                    request_build_and_run();
            }
            bool wantSaveIni = false;
            if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantSaveIniSettings)
            {
                wantSaveIni = true;
                std::size_t layoutSize = 0;
                if (const char *layout = ImGui::SaveIniSettingsToMemory(&layoutSize))
                    savedLayout.assign(layout, layoutSize);
                ImGui::GetIO().WantSaveIniSettings = false;
            }
            for (auto &[hostId, host] : detachedHosts)
            {
                if (host && host->context() && host->context()->IO.WantSaveIniSettings)
                {
                    wantSaveIni = true;
                    host->context()->IO.WantSaveIniSettings = false;
                }
            }
            if (wantSaveIni)
            {
                capture_detached_host_state();
                schedule_preference_save();
            }
            imgui->present(renderer);
            tickDriver.note_present(std::chrono::steady_clock::now(), applicationStart);
            projectPanel.end_frame();
            if (applyProjectRootsAfterFrame)
                apply_project_root_change();
            }
            return running;
        }, [&] {
            const auto now = std::chrono::steady_clock::now();
            std::array<EditorWaitClock::time_point, 8> deadlines{};
            std::size_t deadlineCount = 0;
            const auto include_deadline = [&](EditorWaitClock::time_point deadline) {
                if (deadline != EditorWaitClock::time_point::max())
                    deadlines[deadlineCount++] = deadline;
            };
            include_deadline(nextExternalFileCheck);
            if (projectSession.project_changes().coverage() != ProjectCoverageState::Current && !assetWatchScan)
                include_deadline(nextReferenceFallbackCheck);
            include_deadline(nextSourceScan);
            if (!assetWatchScan && assetWatchSchedule.pending())
                include_deadline(assetWatchSchedule.deadline());
            include_deadline(deferredPreferenceSave.deadline());
            if (const auto deadline = projectSession.project_changes().next_batch_deadline())
                include_deadline(*deadline);
            if (const auto deadline = projectPanel.next_animation_deadline())
                include_deadline(*deadline);
            if (animationWindowPlaybackVisible)
            {
                if (const auto *animationTab = documentManager.active_animation_tab())
                {
                    if (const auto deadline = animationTab->panel().next_playback_deadline(
                            animationTab->document().root(), now))
                        include_deadline(*deadline);
                }
            }

            if (inputSettleFrames > 0)
            {
                --inputSettleFrames;
                return;
            }

            // SDL input and coalesced worker-completion events wake this wait.
            // With no deadline, static panels do not need a periodic redraw.
            // Cap the idle wait at 250ms heartbeat so periodic maintenance runs.
            const auto maximumWait = std::chrono::milliseconds(250);
            const std::optional<EditorWaitClock::duration> pollingInterval =
                (gamePipeline.busy() || catalogQuery.running())
                    ? std::optional<EditorWaitClock::duration>(std::chrono::milliseconds(8))
                    : std::nullopt;
            const auto timeout = editor_wait_duration(
                now, std::span<const EditorWaitClock::time_point>(deadlines.data(), deadlineCount),
                maximumWait, pollingInterval);
            if (timeout.count() == 0)
                return;
            SDL_Event event{};
            if (SDL_WaitEventTimeout(&event, static_cast<int>(timeout.count())))
            {
                pendingWakeEvent = event;
            }
        }))
        {
        }

#if defined(__linux__)
        linuxAccessibility.stop();
#elif defined(__APPLE__)
        mainMacAccessibility.stop();
#elif defined(_WIN32)
        mainWindowsAccessibility.stop();
#endif
        capture_detached_host_state();
        for (auto &[hostId, host] : detachedHosts)
        {
#if defined(__APPLE__)
            if (const auto provider = detachedMacAccessibility.find(hostId);
                provider != detachedMacAccessibility.end())
                provider->second->stop();
#elif defined(_WIN32)
            if (const auto provider = detachedWindowsAccessibility.find(hostId);
                provider != detachedWindowsAccessibility.end())
                provider->second->stop();
#else
            (void)hostId;
#endif
            const auto identity = host->identity();
            accessibilitySnapshots.retire(identity.layoutId, identity.generation);
            sceneTextures.release_renderer(host->renderer());
            projectPanel.release_renderer(host->renderer());
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
            gamePanel.release_renderer(host->renderer());
#endif
        }
#if defined(__APPLE__)
        detachedMacAccessibility.clear();
#elif defined(_WIN32)
        detachedWindowsAccessibility.clear();
#endif
        detachedHosts.clear();
        accessibilitySnapshots.retire(mainAccessibilityHost, mainAccessibilityGeneration);
        std::size_t layoutSize = 0;
        if (const char *layout = ImGui::SaveIniSettingsToMemory(&layoutSize))
            savedLayout.assign(layout, layoutSize);
        persist_preferences();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "Platformator Editor: %s\n", error.what());
        return 1;
    }
}
} // namespace

int run_editor(int argc, char *argv[])
{
    return run_editor_impl(argc, argv, std::nullopt);
}

int run_editor_probe(int argc, char *argv[], EditorAppProbeOptions options)
{
    return run_editor_impl(argc, argv, options);
}
} // namespace platformator_editor
