#include "editorapp.h"
#include "actions.h"
#include "filedialogs.h"
#include "panels/console.h"
#include "panels/hierarchy.h"
#include "panels/inspector.h"
#include "panels/project.h"
#include "panels/runsettings.h"
#include "preferences.h"
#include "process.h"
#include "project.h"
#include "preview.h"
#include "panelwindow.h"
#include "style.h"
#include "toolbar.h"
#include "validation.h"
#include "viewport.h"
#include "workspace.h"

#include <array>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sstream>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <platformator/runtime.h>

namespace platformator_editor
{
namespace
{
constexpr ImGuiID kEditorDockspaceId = 0x6E706EF3;

std::string read_default_layout()
{
#if defined(PLATFORMATOR_EDITOR_RESOURCE_DIR)
    const auto path = std::filesystem::path(PLATFORMATOR_EDITOR_RESOURCE_DIR) / "default-layout.ini";
    std::ifstream input(path, std::ios::binary);
    if (input)
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
#endif
    return {};
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
        ImGui::CreateContext();
        contextCreated_ = true;
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
    }

    ImGuiSdlSession(const ImGuiSdlSession &) = delete;
    ImGuiSdlSession &operator=(const ImGuiSdlSession &) = delete;

    ~ImGuiSdlSession()
    {
        cleanup();
    }

    void begin_frame()
    {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }

    void present(SDL_Renderer *renderer)
    {
        ImGui::Render();
        if (!SDL_SetRenderDrawColor(renderer, 36, 38, 42, 255) || !SDL_RenderClear(renderer))
            throw std::runtime_error(std::string("SDL renderer clear failed: ") + SDL_GetError());
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
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

std::vector<std::string> production_cmake_options(const std::string &target)
{
    std::vector<std::string> options{"-DCMAKE_EXPORT_COMPILE_COMMANDS=OFF",
                                     "-DPLATFORMATOR_SANITIZER=none",
                                     "-DPLATFORMATOR_ENABLE_DEBUG_TOOLS=OFF",
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
    if (!preferences.configurePreset.empty())
    {
        std::vector<std::string> configure{"cmake", "--preset", preferences.configurePreset};
        std::vector<std::string> build{"cmake", "--build", "--preset", preferences.buildPreset};
        if (preferences.production)
        {
            configure.emplace_back("-DCMAKE_BUILD_TYPE=Release");
            const auto options = production_cmake_options(preferences.target);
            configure.insert(configure.end(), options.begin(), options.end());
            build.insert(build.end(), {"--config", "Release"});
        }
        build.insert(build.end(), {"--target", preferences.target});
        return {std::move(configure), std::move(build), {}};
    }

    const auto executable = std::filesystem::path(preferences.executable);
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
        const auto options = production_cmake_options(preferences.target);
        configure.insert(configure.end(), options.begin(), options.end());
    }
    else
    {
        const auto options = development_cmake_options(preferences.target);
        configure.insert(configure.end(), options.begin(), options.end());
    }
    return {std::move(configure),
            {"cmake", "--build", buildDirectory.string(), "--target", preferences.target}, {}};
}

std::vector<std::string> make_run_arguments(const RunPreferences &preferences)
{
    std::vector<std::string> arguments{"--window-width", std::to_string(preferences.width),
                                       "--window-height", std::to_string(preferences.height)};
    if (preferences.fullscreen)
        arguments.emplace_back("--fullscreen");
    if (preferences.maximized)
        arguments.emplace_back("--maximized");
    if (preferences.preserveAspect)
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
    PropertyAddress address;
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

int run_editor(int argc, char *argv[])
{
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
                    if (!std::filesystem::is_regular_file(restoredRoot / "CMakeLists.txt"))
                        throw std::invalid_argument("last workspace no longer contains CMakeLists.txt");
                    options.projectRoot = restoredRoot;
                }
                catch (const std::exception &error)
                {
                    if (startupMessage.empty())
                        startupMessage = std::string("Last workspace could not be restored: ") + error.what();
                }
            }
        }
        if (!std::filesystem::is_regular_file(options.projectRoot / "CMakeLists.txt"))
            throw std::invalid_argument("project root must contain CMakeLists.txt: " + options.projectRoot.string());
        if (!options.assetRootProvided)
            options.assetRoot = canonical_directory(options.projectRoot / "assets", "asset root");

        WorkspacePreferences preferences;
        preferences.projectKey = options.projectRoot.string();
        std::string savedLayout;
        std::optional<ProjectPreferencePaths> preferencePaths;
        if (!preferenceRootPath.empty())
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
            if (lastWorkspacePath)
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

        DocumentWorkspace workspace(options.projectRoot, options.assetRoot);
        ProjectContext projectContext;
        projectContext.set_roots(options.projectRoot, options.assetRoot);
        std::filesystem::path activeProjectRoot = options.projectRoot;
        std::filesystem::path activeAssetRoot = options.assetRoot;
        ProjectScanService sourceScans(2);
        ProjectScanToken sourceScan;
        std::uint64_t sourceScanRequest = 0;
        std::string sourceInputIdentity;
        auto request_source_scan = [&] {
            sourceScan.cancel();
            sourceScan = {};
            sourceScan = sourceScans.request_scan(projectContext.snapshot(), activeProjectRoot,
                ProjectScanOptions{.recursive = true, .maxFiles = 50000, .maxEntries = 100000,
                                   .sourceInputsOnly = true});
            if (!sourceScan)
                throw std::runtime_error("source scan service reached its outstanding-scan limit");
            sourceScanRequest = sourceScan.request_id();
        };
        request_source_scan();
        const auto sourceScanInterval = std::chrono::seconds(5);
        auto nextSourceScan = std::chrono::steady_clock::now() + sourceScanInterval;
        ProjectScanService assetScans(8);
        ProjectPanel projectPanel(projectContext, assetScans, renderer);
        ProjectScanService assetWatchScans(1);
        ProjectScanToken assetWatchScan;
        std::string assetWatchIdentity;
        const auto assetWatchInterval = std::chrono::seconds(3);
        auto nextAssetWatchScan = std::chrono::steady_clock::now();
        auto reset_asset_watcher = [&] {
            assetWatchScan.cancel();
            assetWatchScan = {};
            assetWatchScans.cancel_pending_and_wait();
            assetWatchIdentity.clear();
            nextAssetWatchScan = std::chrono::steady_clock::now();
        };
        auto request_asset_watch_scan = [&] {
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
            const auto opened = workspace.open_document(startup.scenePath, UnsavedDecision::Discard);
            if (opened)
                record_adopted_scene(preferences, *workspace.document().sourcePath);
            else
                startupMessage = "Scene could not be opened: " + opened.message;
        }

        auto dialogs = std::make_unique<FileDialogService>(projectContext.snapshot().generation);
        SelectionModel selection;
        EditCommandService editCommands;
        workspace.bind_history(editCommands);
        editCommands.set_selection_provider([&selection] { return selection.current(); });
        auto scenePreview = std::make_unique<EditorPreview>(*runtime);
        scenePreview->reconcile(workspace.document(), true);
        SceneViewportPanel scenePanel;
        std::vector<int> previewChangedObjectIds;
        bool previewStructureChanged = false;
        editCommands.subscribe([&](const DocumentChange &change, const std::vector<int> &objectIds) {
            previewStructureChanged = previewStructureChanged || change.structural;
            previewChangedObjectIds.insert(previewChangedObjectIds.end(), objectIds.begin(), objectIds.end());
        });
        std::uint64_t previewDocumentGeneration = workspace.document().documentGeneration;
        HierarchyPanel hierarchyPanel;
        InspectorPanel inspectorPanel;
        RunSettingsPanel runSettingsPanel;
        ConsolePanel consolePanel;
        GamePipeline gamePipeline;
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
            io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io->IniFilename = nullptr;
            if (!savedLayout.empty())
                ImGui::LoadIniSettingsFromMemory(savedLayout.data(), savedLayout.size());
        };
        imgui = std::make_unique<ImGuiSdlSession>(window, renderer);
        initialize_imgui_context();
        auto rebuild_runtime_context = [&](const std::filesystem::path &assetRoot) {
            scenePreview.reset();
            imgui.reset();
            projectPanel.set_renderer(nullptr);
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
                scenePreview = std::make_unique<EditorPreview>(*runtime);
                scenePreview->reconcile(workspace.document(), true);
                imgui = std::make_unique<ImGuiSdlSession>(window, renderer);
                initialize_imgui_context();
            }
            catch (...)
            {
                scenePreview.reset();
                imgui.reset();
                projectPanel.set_renderer(nullptr);
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
        std::array<char, 4096> manualPath{};
        std::string dialogError;
        std::optional<PendingAssetAssignment> pendingAssetAssignment;
        std::string statusMessage = startupMessage;
        ValidationResult validation = workspace.validate();
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

        auto persist_preferences = [&] {
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
            if (lastWorkspacePath)
            {
                std::string error;
                if (!save_last_workspace(*lastWorkspacePath, activeProjectRoot, error))
                    statusMessage = error;
            }
            if (!preferencePaths)
                return;
            const auto stored = save_workspace_preferences(*preferencePaths, preferences, savedLayout);
            if (!stored.succeeded())
                statusMessage = stored.message;
            else if (!stored.message.empty())
                statusMessage = stored.message;
        };
        auto commit_scene_gesture = [&] { scenePanel.commit_gesture(); };
        auto cancel_scene_gesture = [&] {
            scenePanel.cancel_gesture(editCommands, workspace.document_for_edit());
        };
        auto start_pipeline_now = [&](bool buildOnly) {
            commit_scene_gesture();
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
                validation = workspace.validate();
                if (validation.blocksRun)
                {
                    statusMessage = "Run is blocked by current scene validation errors.";
                    preferences.panelVisibility["panel.validation"] = true;
                    return false;
                }
                if (workspace.dirty() || !workspace.document().sourcePath)
                {
                    statusMessage = "Save the scene before Build & Run.";
                    return false;
                }
            }

            const auto executable = std::filesystem::path(preferences.run.executable);
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

            PipelineRequest request;
            request.projectRoot = activeProjectRoot;
            request.assetRoot = activeAssetRoot;
            request.executablePath = executablePath;
            request.configureArgv = buildCommands.configure;
            request.buildArgv = buildCommands.build;
            request.runArguments = make_run_arguments(preferences.run);
            request.identity = identity;
            request.catalogProtocol = preferences.run.catalogProtocol;
            request.buildOnly = buildOnly;
            if (!buildOnly)
            {
                std::error_code sceneError;
                const auto savedScene = std::filesystem::canonical(*workspace.document().sourcePath, sceneError);
                if (sceneError || !std::filesystem::is_regular_file(savedScene))
                {
                    statusMessage = "The saved scene file is not available for Run.";
                    return false;
                }
                request.savedScenePath = savedScene;
                request.scene = workspace.document().root;
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
            const auto generation = pipelineGeneration++;
            if (!gamePipeline.start(std::move(request), behaviorCatalog, generation))
            {
                statusMessage = gamePipeline.status().message;
                return false;
            }
            activePipelineIdentity = std::move(identity);
            hasActivePipelineIdentity = true;
            statusMessage = buildOnly ? "Build started." : "Build & Run started.";
            return true;
        };
        auto queue_pipeline = [&](bool buildOnly) {
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
                nextSourceScan = std::chrono::steady_clock::now() + sourceScanInterval;
                statusMessage = buildOnly ? "Refreshing source identity before Build..."
                                          : "Refreshing source identity before Build & Run...";
            }
            catch (const std::exception &error)
            {
                pendingPipelineBuildOnly.reset();
                statusMessage = std::string("Could not refresh source identity: ") + error.what();
            }
        };
        auto undo_edit = [&] {
            commit_scene_gesture();
            const auto result = editCommands.undo(workspace.document_for_edit(), selection);
            statusMessage = result ? "Undo completed." : result.message;
            validation = workspace.validate();
        };
        auto redo_edit = [&] {
            commit_scene_gesture();
            const auto result = editCommands.redo(workspace.document_for_edit(), selection);
            statusMessage = result ? "Redo completed." : result.message;
            validation = workspace.validate();
        };
        auto finish_successful_adoption = [&] {
            validation = workspace.validate();
            if (workspace.document().sourcePath)
                record_adopted_scene(preferences, *workspace.document().sourcePath);
            persist_preferences();
            if (validation.blocksRun)
                statusMessage = "Scene opened with validation errors; Run is blocked until they are fixed.";
        };
        auto request_save_as = [&] {
            dialogs->save_scene(window, workspace.document().documentGeneration,
                                workspace.document().sourcePath.value_or(activeProjectRoot));
        };
        auto request_open_scene = [&] {
            openSceneDialogPending = true;
            statusMessage = "Choose a scene file to open...";
            dialogs->open_scene(window, workspace.document().documentGeneration, activeProjectRoot);
        };
        auto refresh_behavior_catalog = [&] {
            if (!behaviorCatalog.can_query())
            {
                statusMessage = "Catalog refresh is disabled because this target has not declared common-runner-v1.";
                return false;
            }
            const auto executable = preferences.run.executable.empty()
                ? std::filesystem::path{} : std::filesystem::path(preferences.run.executable);
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
            pendingAssetAssignment = PendingAssetAssignment{std::move(address), expectedAssetType, nullable};
            dialogs->open_asset(window, workspace.document().documentGeneration, activeAssetRoot,
                                expectedAssetType);
        };
        auto execute_pending = [&](UnsavedDecision decision) {
            if (decision == UnsavedDecision::Save)
                commit_scene_gesture();
            else if (decision == UnsavedDecision::Discard)
                cancel_scene_gesture();
            WorkspaceActionResult result{WorkspaceActionStatus::Failed, "no pending workspace action"};
            if (pendingAction == PendingAction::NewDocument)
                result = workspace.new_document(decision);
            else if (pendingAction == PendingAction::OpenDocument)
                result = workspace.open_document(pendingOpenPath, decision);
            else if (pendingAction == PendingAction::ChangeProjectRoots)
            {
                if (decision == UnsavedDecision::Cancel)
                    result = {WorkspaceActionStatus::Cancelled, {}};
                else if (decision == UnsavedDecision::Save && !workspace.document().sourcePath)
                {
                    request_save_as();
                    showUnsavedPrompt = false;
                    return;
                }
                else if (decision == UnsavedDecision::Save)
                    result = workspace.save();
                else
                    result = {WorkspaceActionStatus::Completed, {}};
            }
            else if (pendingAction == PendingAction::BuildAndRun)
            {
                if (decision == UnsavedDecision::Cancel)
                    result = {WorkspaceActionStatus::Cancelled, {}};
                else if (decision == UnsavedDecision::Discard && !workspace.dirty() &&
                         workspace.document().sourcePath)
                    result = {WorkspaceActionStatus::Completed, {}};
                else if (decision == UnsavedDecision::Save && !workspace.document().sourcePath)
                {
                    request_save_as();
                    showUnsavedPrompt = false;
                    return;
                }
                else if (decision == UnsavedDecision::Save)
                    result = workspace.save();
                else
                    result = {WorkspaceActionStatus::Failed, "Build & Run requires a saved scene."};
            }
            else if (pendingAction == PendingAction::Exit)
            {
                if (decision == UnsavedDecision::Cancel)
                    result = {WorkspaceActionStatus::Cancelled, {}};
                else if (decision == UnsavedDecision::Discard)
                    result = {WorkspaceActionStatus::Completed, {}};
                else
                    result = workspace.save();
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
                else
                    validation = workspace.validate();
                pendingAction = PendingAction::None;
                pendingOpenPath.reset();
                showUnsavedPrompt = false;
                statusMessage = result.message;
                if (decision == UnsavedDecision::Save && running)
                {
                    if (workspace.document().sourcePath)
                        record_adopted_scene(preferences, *workspace.document().sourcePath);
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
                else if (decision == UnsavedDecision::Discard &&
                         pendingAction == PendingAction::OpenDocument)
                {
                    pendingAction = PendingAction::None;
                    pendingOpenPath.reset();
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
            if (workspace.dirty())
                showUnsavedPrompt = true;
            else
                execute_pending(UnsavedDecision::Discard);
        };
        auto request_project_folder = [&] {
            if (gamePipeline.busy() || catalogQuery.running() || pendingPipelineBuildOnly ||
                pendingAction != PendingAction::None)
            {
                statusMessage = "Stop the active project operation before changing project roots.";
                return;
            }
            dialogs->select_project_folder(window, workspace.document().documentGeneration,
                                           activeProjectRoot);
        };
        auto save_as_to = [&](const std::filesystem::path &path, bool overwrite) {
            commit_scene_gesture();
            const auto saved = workspace.save_as(path, overwrite);
            if (saved)
            {
                statusMessage = saved.message;
                if (workspace.document().sourcePath)
                    record_adopted_scene(preferences, *workspace.document().sourcePath);
                persist_preferences();
                showOverwritePrompt = false;
                pendingSaveAsPath.clear();
                if (pendingAction != PendingAction::None)
                    execute_pending(UnsavedDecision::Discard);
            }
            else
            {
                statusMessage = saved.message;
                if (!overwrite && saved.message.find("already exists") != std::string::npos)
                {
                    pendingSaveAsPath = path;
                    showOverwritePrompt = true;
                }
            }
        };
        auto save_current = [&] {
            commit_scene_gesture();
            if (!workspace.document().sourcePath)
            {
                request_save_as();
                return;
            }
            const auto saved = workspace.save();
            if (saved)
            {
                statusMessage = saved.message;
                record_adopted_scene(preferences, *workspace.document().sourcePath);
                persist_preferences();
            }
            else
            {
                statusMessage = saved.message;
                if (saved.message.find("changed on disk") != std::string::npos)
                    showExternalConflictPrompt = true;
            }
        };
        auto request_build_and_run = [&] {
            commit_scene_gesture();
            if (gamePipeline.busy())
            {
                statusMessage = "A project operation is already running.";
                return;
            }
            validation = workspace.validate();
            if (validation.blocksRun)
            {
                statusMessage = "Run is blocked by current scene validation errors.";
                preferences.panelVisibility["panel.validation"] = true;
                return;
            }
            if (workspace.dirty())
            {
                pendingAction = PendingAction::BuildAndRun;
                showUnsavedPrompt = true;
                return;
            }
            if (!workspace.document().sourcePath)
            {
                pendingAction = PendingAction::BuildAndRun;
                request_save_as();
                return;
            }
            queue_pipeline(false);
        };
        auto stop_owned_operation = [&] {
            if (pendingPipelineBuildOnly)
            {
                pendingPipelineBuildOnly.reset();
                statusMessage = "Build request cancelled before the pipeline started.";
            }
            else if (pendingAction == PendingAction::BuildAndRun)
            {
                pendingAction = PendingAction::None;
                showUnsavedPrompt = false;
                statusMessage = "Build & Run cancelled before the pipeline started.";
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
                applyProjectRootsAfterFrame = false;
                pendingProjectRoots.reset();
                statusMessage = "Project roots stayed unchanged because a project operation became active.";
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

            const auto oldPreferencePaths = preferencePaths;
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
            sourceScanRequest = 0;

            try
            {
                projectContext.set_roots(candidate.projectRoot, candidate.assetRoot);
                const auto changed = workspace.set_project_roots(candidate.projectRoot,
                                                                  candidate.assetRoot,
                                                                  projectContext.snapshot().generation);
                if (!changed)
                    throw std::runtime_error(changed.message);
                activeProjectRoot = projectContext.snapshot().projectRoot;
                activeAssetRoot = projectContext.snapshot().assetRoot;
                preferences = std::move(nextPreferences);
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
                nextSourceScan = std::chrono::steady_clock::now() + sourceScanInterval;
                request_source_scan();
                if (workspace.document().sourcePath)
                    record_adopted_scene(preferences, *workspace.document().sourcePath);
                pendingProjectRoots.reset();
                applyProjectRootsAfterFrame = false;
                statusMessage = preferenceMessage.empty()
                                    ? "Project and asset roots changed; document and undo history were preserved."
                                    : preferenceMessage;
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
                        projectContext.set_roots(oldSnapshot.projectRoot, oldSnapshot.assetRoot);
                        const auto restored = workspace.set_project_roots(oldSnapshot.projectRoot,
                                                                          oldSnapshot.assetRoot,
                                                                          projectContext.snapshot().generation);
                        if (!restored)
                            throw std::runtime_error(restored.message);
                    }
                    activeProjectRoot = oldSnapshot.projectRoot;
                    activeAssetRoot = oldSnapshot.assetRoot;
                    preferences = std::move(oldPreferences);
                    set_runtime_window_preferences();
                    preferencePaths = oldPreferencePaths;
                    savedLayout = oldLayout;
                    activeScale = oldActiveScale;
                    lastAppliedScale = -1.0f;
                    dialogs = std::make_unique<FileDialogService>(projectContext.snapshot().generation);
                    rebuild_runtime_context(activeAssetRoot);
                    catalogIdentity.canonicalProjectPath = activeProjectRoot.string();
                    catalogIdentity.profile = preferences.run.buildPreset;
                    catalogIdentity.target = preferences.run.target;
                    catalogIdentity.executablePath = preferences.run.executable;
                    behaviorCatalog.configure(preferences.run.catalogProtocol == "common-runner-v1"
                                                  ? CatalogProtocol::CommonRunnerV1
                                                  : CatalogProtocol::Unavailable,
                                              catalogIdentity);
                    initialCatalogRefreshPending = behaviorCatalog.can_query();
                    nextSourceScan = std::chrono::steady_clock::now() + sourceScanInterval;
                    request_source_scan();
                    statusMessage = "Project change failed; the previous project was restored: " +
                                    replacementError;
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

        while (running)
        {
            catalogQuery.poll();
            gamePipeline.poll();
            if (!gamePipeline.busy())
                hasActivePipelineIdentity = false;

            for (auto &scanResult : sourceScans.take_ready(projectContext.snapshot().generation))
            {
                if (scanResult.requestId != sourceScanRequest)
                    continue;
                sourceScan = {};
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
                }
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
            if (!sourceScan && now >= nextSourceScan)
            {
                try
                {
                    request_source_scan();
                    nextSourceScan = now + sourceScanInterval;
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
                if (assetScanResult.cancelled)
                    continue;
                if (assetScanResult.truncated || !assetScanResult.error.empty() ||
                    assetScanResult.fileIdentity.empty())
                {
                    statusMessage = assetScanResult.error.empty()
                        ? "Asset watch could not inspect the whole asset folder; automatic refresh may be incomplete."
                        : "Asset watch failed: " + assetScanResult.error;
                }
                else
                {
                    if (!assetWatchIdentity.empty() && assetWatchIdentity != assetScanResult.fileIdentity)
                    {
                        projectPanel.refresh();
                        scenePreview->refresh_assets(workspace.document());
                        statusMessage = "Asset folder changed; the browser and Scene preview were refreshed.";
                    }
                    assetWatchIdentity = std::move(assetScanResult.fileIdentity);
                }
                nextAssetWatchScan = now + assetWatchInterval;
            }
            if (!assetWatchScan && now >= nextAssetWatchScan)
            {
                try
                {
                    request_asset_watch_scan();
                    nextAssetWatchScan = now + assetWatchInterval;
                }
                catch (const std::exception &error)
                {
                    assetWatchScan = {};
                    nextAssetWatchScan = now + assetWatchInterval;
                    statusMessage = std::string("Could not watch assets: ") + error.what();
                }
            }

            if (!gamePipeline.busy())
            {
                const auto executable = std::filesystem::path(preferences.run.executable);
                const auto selectedExecutable = executable.empty()
                    ? std::filesystem::path{}
                    : (executable.is_absolute() ? executable : activeProjectRoot / executable);
                catalogIdentity.canonicalProjectPath = activeProjectRoot.string();
                catalogIdentity.profile = preferences.run.buildPreset.empty()
                    ? (preferences.run.production ? "production" : "debug")
                    : preferences.run.buildPreset;
                catalogIdentity.target = preferences.run.target;
                catalogIdentity.executablePath = selectedExecutable.lexically_normal().string();
                catalogIdentity.binaryIdentity = executable_identity(selectedExecutable);
                catalogIdentity.sourceInputIdentity = sourceInputIdentity;
                behaviorCatalog.configure(preferences.run.catalogProtocol == "common-runner-v1"
                                              ? CatalogProtocol::CommonRunnerV1
                                              : CatalogProtocol::Unavailable,
                                          catalogIdentity);
            }
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                     event.window.windowID == SDL_GetWindowID(window)))
                    closeRequested = true;
            }

            if (closeRequested && gamePipeline.busy())
            {
                gamePipeline.stop();
                statusMessage = "Stopping the owned game process before closing.";
            }
            if (closeRequested && !gamePipeline.busy() && pendingAction == PendingAction::None &&
                !showExternalConflictPrompt && !showOverwritePrompt)
            {
                if (workspace.dirty())
                {
                    pendingAction = PendingAction::Exit;
                    showUnsavedPrompt = true;
                    closeRequested = false;
                }
                else
                    running = false;
            }
            if (!running)
                break;

            for (const auto &result : dialogs->take_ready(workspace.document().documentGeneration))
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
                if (result.identity.kind == AsyncRequestKind::OpenAsset &&
                    result.outcome == AsyncRequestOutcome::Cancelled)
                    pendingAssetAssignment.reset();
                if (result.identity.kind == AsyncRequestKind::SaveScene &&
                    result.outcome == AsyncRequestOutcome::Cancelled &&
                    pendingAction == PendingAction::BuildAndRun)
                {
                    pendingAction = PendingAction::None;
                    showUnsavedPrompt = false;
                    statusMessage = "Build & Run cancelled while choosing a scene path.";
                }
                if (result.outcome == AsyncRequestOutcome::Failed)
                {
                    dialogError = result.error;
                    manualPathKind = result.identity.kind;
                    showManualPath = true;
                    continue;
                }
                if (result.outcome != AsyncRequestOutcome::Selected)
                    continue;
                if (result.identity.kind == AsyncRequestKind::OpenScene)
                    begin_replacement(PendingAction::OpenDocument, result.selectedPath);
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
                    save_as_to(result.selectedPath, false);
                else if (result.identity.kind == AsyncRequestKind::OpenAsset && pendingAssetAssignment)
                {
                    auto assignment = std::move(*pendingAssetAssignment);
                    pendingAssetAssignment.reset();
                    std::error_code rootError;
                    const auto assetRoot = std::filesystem::canonical(activeAssetRoot, rootError);
                    std::error_code assetError;
                    const auto selected = std::filesystem::canonical(result.selectedPath, assetError);
                    const auto relative = selected.lexically_relative(assetRoot);
                    const bool contained = !rootError && !assetError && !relative.empty() && !relative.is_absolute() &&
                        *relative.begin() != "..";
                    if (!contained || !std::filesystem::is_regular_file(selected, assetError) || assetError)
                        statusMessage = "Selected file is outside the project asset root or is not a regular file.";
                    else
                    {
                        const auto reference = "assets/" + relative.generic_string();
                        std::string validationError;
                        if (!validate_asset_reference_assignment(workspace.document(), reference,
                            assignment.expectedAssetType, assignment.nullable, validationError))
                            statusMessage = validationError;
                        else
                        {
                            const auto changed = editCommands.set_property(workspace.document_for_edit(),
                                                                           assignment.address, reference);
                            statusMessage = changed ? "Asset reference assigned." : changed.message;
                        }
                    }
                }
            }

            const auto &currentDocument = workspace.document();
            if (currentDocument.documentGeneration != previewDocumentGeneration)
            {
                selection.repair(currentDocument);
                previewStructureChanged = true;
                previewDocumentGeneration = currentDocument.documentGeneration;
            }
            scenePreview->reconcile(currentDocument, previewStructureChanged, previewChangedObjectIds);
            previewStructureChanged = false;
            previewChangedObjectIds.clear();

            int pixelWidth = 0;
            int pixelHeight = 0;
            SDL_GetWindowSize(window, &logicalWidth, &logicalHeight);
            if (!SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight))
                pixelWidth = logicalWidth, pixelHeight = logicalHeight;
            const float dpiScale = logicalWidth > 0 && logicalHeight > 0
                ? std::max(static_cast<float>(pixelWidth) / logicalWidth,
                           static_cast<float>(pixelHeight) / logicalHeight) : 1.0f;
            const float totalUiScale = std::clamp(activeScale * dpiScale, 0.5f, 6.0f);
            if (std::abs(totalUiScale - lastAppliedScale) > 0.001f)
            {
                apply_editor_style(totalUiScale);
                lastAppliedScale = totalUiScale;
            }

            imgui->begin_frame();
            const bool hasModal = showUnsavedPrompt || showOverwritePrompt || showExternalConflictPrompt ||
                                  showManualPath ||
                                  ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
            const bool appFocused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
            const bool widgetOwnsInput = io->WantTextInput || ImGui::IsAnyItemActive();
            const EditorInputCapture shortcutCapture{
                appFocused, widgetOwnsInput,
                hasModal, scenePanel.has_active_input(), false};
            const bool shortcutAllowed = editor_action_allowed(EditorAction::NewScene, shortcutCapture);
            const auto f5Action = ImGui::IsKeyPressed(ImGuiKey_F5)
                ? dispatch_editor_key(EditorKey::F5, io->KeyShift, io->KeyCtrl, shortcutCapture)
                : std::nullopt;
            const bool ownedOperationActive = gamePipeline.busy() || catalogQuery.running() ||
                                              pendingPipelineBuildOnly.has_value();
            if (f5Action == EditorAction::Stop && ownedOperationActive)
                stop_owned_operation();
            if (shortcutAllowed && !gamePipeline.busy() && !catalogQuery.running() &&
                !pendingPipelineBuildOnly && pendingAction != PendingAction::BuildAndRun &&
                pendingAction != PendingAction::ChangeProjectRoots)
            {
                if (io->KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N))
                    begin_replacement(PendingAction::NewDocument);
                else if (io->KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
                    request_open_scene();
                else if (io->KeyCtrl && io->KeyShift && ImGui::IsKeyPressed(ImGuiKey_S))
                    request_save_as();
                else if (io->KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
                    save_current();
                else if (io->KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
                {
                    if (io->KeyShift)
                        redo_edit();
                    else
                        undo_edit();
                }
                else if (io->KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
                    redo_edit();
                else if (io->KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Equal) ||
                                        ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)))
                    scenePanel.adjust_zoom(1.1f);
                else if (io->KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Minus) ||
                                        ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)))
                    scenePanel.adjust_zoom(1.0f / 1.1f);
                else if (io->KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_B))
                    queue_pipeline(true);
                else if (f5Action == EditorAction::BuildAndRun)
                    request_build_and_run();
            }

            bool projectLocked = gamePipeline.busy() || catalogQuery.running() ||
                                 pendingPipelineBuildOnly.has_value() ||
                                 pendingAction == PendingAction::BuildAndRun ||
                                 pendingAction == PendingAction::ChangeProjectRoots;
            const bool documentEditingLocked = gamePipeline.busy() ||
                                               pendingPipelineBuildOnly.has_value() ||
                                               pendingAction == PendingAction::BuildAndRun ||
                                               pendingAction == PendingAction::ChangeProjectRoots;
            const auto panelStateBefore = preferences.panelVisibility;
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("New Scene", "Ctrl+N", false, !projectLocked))
                        begin_replacement(PendingAction::NewDocument);
                    if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, !projectLocked))
                        request_open_scene();
                    if (ImGui::MenuItem("Open Project Folder...", nullptr, false, !projectLocked))
                        request_project_folder();
                    ImGui::Separator();
                    if (ImGui::MenuItem("Save", "Ctrl+S", false, !projectLocked))
                        save_current();
                    if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, !projectLocked))
                        request_save_as();
                    ImGui::Separator();
                    if (ImGui::MenuItem("Exit"))
                        closeRequested = true;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Edit"))
                {
                    const auto history = editCommands.history_status();
                    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, history.canUndo && !projectLocked)) undo_edit();
                    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, history.canRedo && !projectLocked)) redo_edit();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View"))
                {
                    auto panel_item = [&](const char *label, const char *id,
                                          const char *shortcut = nullptr) {
                        bool visible = preferences.panelVisibility[id];
                        if (ImGui::MenuItem(label, shortcut, &visible))
                            preferences.panelVisibility[id] = visible;
                    };
                    panel_item("Hierarchy", "panel.hierarchy");
                    panel_item("Scene", "panel.scene");
                    panel_item("Inspector", "panel.inspector");
                    panel_item("Project and Assets", "panel.project");
                    panel_item("Console", "panel.console");
                    panel_item("Build and Run", "panel.run");
                    panel_item("Validation", "panel.validation");
                    ImGui::Separator();
                    if (ImGui::MenuItem("Reset Layout"))
                    {
                        reset_workspace_layout(preferences);
                        savedLayout = defaultLayout;
                        if (!savedLayout.empty())
                            ImGui::LoadIniSettingsFromMemory(savedLayout.data(), savedLayout.size());
                        activeScale = options.uiScaleProvided ? options.uiScale : 1.0f;
                        lastAppliedScale = -1.0f;
                        statusMessage = "Workspace layout and panel visibility were reset.";
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
                    ImGui::Separator();
                    if (ImGui::MenuItem("Zoom In", "Ctrl++")) scenePanel.adjust_zoom(1.1f);
                    if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) scenePanel.adjust_zoom(1.0f / 1.1f);
                    if (ImGui::MenuItem("Frame All", "F")) scenePanel.request_frame_all();
                    if (ImGui::MenuItem("Frame Selected", "Shift+F")) scenePanel.request_frame_selected();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Run"))
                {
                    if (ImGui::MenuItem("Build", "Ctrl+B", false, !projectLocked)) queue_pipeline(true);
                    if (ImGui::MenuItem("Build and Run", "F5", false, !projectLocked)) request_build_and_run();
                    if (ImGui::MenuItem("Stop", "Shift+F5", false,
                                        gamePipeline.busy() || catalogQuery.running() || pendingPipelineBuildOnly))
                        stop_owned_operation();
                    if (ImGui::MenuItem("Build and Run Settings"))
                        preferences.panelVisibility["panel.run"] = true;
                    ImGui::EndMenu();
                }

                ImGui::EndMainMenuBar();
            }

            EditorToolbarState toolbarState;
            toolbarState.projectLocked = projectLocked;
            toolbarState.operationActive = gamePipeline.busy() || catalogQuery.running() ||
                                           pendingPipelineBuildOnly.has_value();
            EditorToolbarActions toolbarActions;
            toolbarActions.openScene = request_open_scene;
            toolbarActions.save = save_current;
            toolbarActions.build = [&] { queue_pipeline(true); };
            toolbarActions.buildAndRun = request_build_and_run;
            toolbarActions.stop = stop_owned_operation;
            const auto toolbarFrame = draw_editor_toolbar(toolbarState, toolbarActions);
            submit_editor_dockspace(kEditorDockspaceId, toolbarFrame.height);
            const auto show_panel = [&](const char *stableId) {
                return preferences.panelVisibility[stableId];
            };
            bool showHierarchy = show_panel("panel.hierarchy");
            const auto hierarchyFrame = begin_panel_window(showHierarchy,
                "Hierarchy###panel.hierarchy", &showHierarchy);
            if (hierarchyFrame.contentsVisible)
            {
                ImGui::BeginDisabled(documentEditingLocked);
                hierarchyPanel.draw(workspace.document_for_edit(), selection, editCommands);
                ImGui::EndDisabled();
            }
            end_panel_window(hierarchyFrame);
            preferences.panelVisibility["panel.hierarchy"] = showHierarchy;

            bool showProject = show_panel("panel.project");
            const auto projectFrame = begin_panel_window(showProject,
                "Project and Assets###panel.project", &showProject);
            if (projectFrame.contentsVisible)
                projectPanel.draw(workspace.document_for_edit(), editCommands);
            end_panel_window(projectFrame);
            preferences.panelVisibility["panel.project"] = showProject;

            bool showConsole = show_panel("panel.console");
            const auto consoleFrame = begin_panel_window(showConsole,
                "Console###panel.console", &showConsole);
            if (consoleFrame.contentsVisible)
                consolePanel.draw(gamePipeline.status(), gamePipeline.console());
            end_panel_window(consoleFrame);
            preferences.panelVisibility["panel.console"] = showConsole;

            bool showInspector = show_panel("panel.inspector");
            const auto inspectorFrame = begin_panel_window(showInspector,
                "Inspector###panel.inspector", &showInspector);
            if (inspectorFrame.contentsVisible)
            {
                ImGui::BeginDisabled(documentEditingLocked);
                inspectorPanel.draw(workspace.document_for_edit(), selection, editCommands,
                                     behaviorCatalog, browse_asset);
                ImGui::EndDisabled();
                if (!inspectorPanel.feedback().empty())
                    ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.38f, 1.0f), "%s", inspectorPanel.feedback().c_str());
            }
            end_panel_window(inspectorFrame);
            preferences.panelVisibility["panel.inspector"] = showInspector;

            bool showRun = show_panel("panel.run");
            const auto runFrame = begin_panel_window(showRun,
                "Build and Run###panel.run", &showRun);
            if (runFrame.contentsVisible)
            {
                const auto previousRunPreferences = preferences.run;
                const auto action = runSettingsPanel.draw(preferences.run, gamePipeline.status(),
                    projectLocked || catalogQuery.running(), workspace.dirty());
                if (preferences.run != previousRunPreferences)
                    persist_preferences();
                if (sourceInputIdentity.empty())
                    ImGui::TextDisabled("Scanning configured CMake and source inputs...");
                if (action == RunPanelAction::RefreshSources)
                {
                    try
                    {
                        request_source_scan();
                        nextSourceScan = std::chrono::steady_clock::now() + sourceScanInterval;
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
            preferences.panelVisibility["panel.run"] = showRun;

            bool showValidationPanel = show_panel("panel.validation");
            const auto validationFrame = begin_panel_window(showValidationPanel,
                "Validation###panel.validation", &showValidationPanel);
            if (validationFrame.contentsVisible)
            {
                ImGui::Text("Objects: %zu | Components: %zu", workspace.document().indexes.objectsById.size(),
                            workspace.document().indexes.componentsById.size());
                const auto currentHistory = editCommands.history_status();
                if (currentHistory.overBudget)
                    ImGui::TextWrapped("History limit: retained %zu bytes of %zu; keeping the newest oversized edit.",
                                       currentHistory.retainedBytes, currentHistory.budgetBytes);
                else
                    ImGui::Text("History: %zu undo / %zu redo | %zu / %zu bytes retained",
                                currentHistory.undoCommands, currentHistory.redoCommands,
                                currentHistory.retainedBytes, currentHistory.budgetBytes);
                ImGui::TextUnformatted("Preview does not instantiate behaviors or simulate gameplay.");
                if (validation.issues.empty()) ImGui::TextUnformatted("No known-field issues.");
                for (const auto &issue : validation.issues)
                    ImGui::TextWrapped("%s %s: %s", issue.blocksRun ? "Run blocked:" : "Warning:",
                                       issue.fieldPath.c_str(), issue.message.c_str());
                if (!statusMessage.empty()) ImGui::TextWrapped("%s", statusMessage.c_str());
            }
            end_panel_window(validationFrame);
            preferences.panelVisibility["panel.validation"] = showValidationPanel;

            bool showScene = show_panel("panel.scene");
            scenePanel.set_visible(showScene);
            if (showScene)
                scenePanel.draw(workspace.document_for_edit(), *scenePreview, selection, editCommands,
                                window, renderer, hasModal, !documentEditingLocked);
            else
                scenePanel.cancel_gesture(editCommands, workspace.document_for_edit());
            showScene = scenePanel.is_visible();
            preferences.panelVisibility["panel.scene"] = showScene;
            if (!scenePreview->last_error().empty())
                statusMessage = scenePreview->last_error();

            if (preferences.panelVisibility != panelStateBefore)
                persist_preferences();

            const auto pathDialogAction = draw_file_dialog_path_entry(
                showManualPath, dialogError, manualPath.data(), manualPath.size());
            if (pathDialogAction == FileDialogPathAction::Submit)
            {
                dialogs->submit_manual_path(manualPathKind, workspace.document().documentGeneration,
                                            std::filesystem::path(manualPath.data()));
                openSceneDialogPending = manualPathKind == AsyncRequestKind::OpenScene;
                statusMessage = manualPathKind == AsyncRequestKind::OpenScene
                                    ? "Checking the selected scene path..."
                                    : "Checking the selected path...";
                dialogError.clear();
                manualPath.fill('\0');
            }
            else if (pathDialogAction == FileDialogPathAction::Cancel)
            {
                dialogError.clear();
                manualPath.fill('\0');
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
                    execute_pending(UnsavedDecision::Save);
                if (pendingAction != PendingAction::BuildAndRun)
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Discard"))
                        execute_pending(UnsavedDecision::Discard);
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    execute_pending(UnsavedDecision::Cancel);
                ImGui::EndPopup();
            }
            if (showExternalConflictPrompt)
                ImGui::OpenPopup("Scene changed on disk");
            if (ImGui::BeginPopupModal("Scene changed on disk", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextWrapped("The saved scene changed outside the editor. Overwrite it or save this work elsewhere.");
                if (ImGui::Button("Overwrite"))
                {
                    commit_scene_gesture();
                    const auto saved = workspace.save(true);
                    if (saved)
                    {
                        showExternalConflictPrompt = false;
                        statusMessage = saved.message;
                        if (workspace.document().sourcePath)
                            record_adopted_scene(preferences, *workspace.document().sourcePath);
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
                    request_save_as();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    showExternalConflictPrompt = false;
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
                    save_as_to(pendingSaveAsPath, true);
                ImGui::SameLine();
                if (ImGui::Button("Choose another path"))
                {
                    showOverwritePrompt = false;
                    request_save_as();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    showOverwritePrompt = false;
                    pendingSaveAsPath.clear();
                    if (pendingAction != PendingAction::None)
                        showUnsavedPrompt = true;
                }
                ImGui::EndPopup();
            }

            const std::string windowTitle = std::string("Platformator Editor") +
                                            (workspace.dirty() ? " *" : "");
            SDL_SetWindowTitle(window, windowTitle.c_str());
            imgui->present(renderer);
            if (applyProjectRootsAfterFrame)
                apply_project_root_change();
            SDL_Delay(1);
        }

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
} // namespace platformator_editor
