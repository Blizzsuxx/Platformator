#include "document.h"
#include "behaviorcatalog.h"
#include "editcommands.h"
#include "process.h"
#include "project.h"
#include "preview.h"
#include "selection.h"
#include "workspace.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

#include <unistd.h>

#include <SDL3/SDL.h>

#include <json.hpp>
#include <platformator/gameobject.h>
#include <platformator/runtime.h>
#include <platformator/runtimeoptions.h>

#ifndef PLATFORMATOR_EDITOR_PROBE_CONFIGURATION
#define PLATFORMATOR_EDITOR_PROBE_CONFIGURATION "unknown"
#endif
#ifndef PLATFORMATOR_EDITOR_PROBE_COMPILER
#define PLATFORMATOR_EDITOR_PROBE_COMPILER "unknown"
#endif

namespace
{
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

std::atomic<bool> countAllocations{false};
std::atomic<std::uint64_t> allocationCalls{0};
std::atomic<std::uint64_t> allocatedBytes{0};
volatile std::sig_atomic_t captureInterrupted = 0;

void request_capture_stop(int) noexcept
{
    captureInterrupted = 1;
}

void check_capture_stop()
{
    if (captureInterrupted != 0)
        throw std::runtime_error("resource lifecycle probe was interrupted; completed progress rows are retained");
}

void note_allocation(std::size_t size)
{
    if (countAllocations.load(std::memory_order_relaxed))
    {
        allocationCalls.fetch_add(1, std::memory_order_relaxed);
        allocatedBytes.fetch_add(size, std::memory_order_relaxed);
    }
}

void *allocate(std::size_t size)
{
    if (void *memory = std::malloc(size == 0 ? 1 : size))
    {
        note_allocation(size);
        return memory;
    }
    throw std::bad_alloc();
}

void *allocate_aligned(std::size_t size, std::size_t alignment)
{
    void *memory = nullptr;
    if (posix_memalign(&memory, alignment, size == 0 ? alignment : size) == 0)
    {
        note_allocation(size);
        return memory;
    }
    throw std::bad_alloc();
}

Json make_object(int id, Json components = Json::array(), Json children = Json::array())
{
    return Json{{"id", id}, {"rotation", 0.0}, {"active", true},
                {"position", {{"x", 0.0}, {"y", 0.0}}},
                {"scale", {{"x", 1.0}, {"y", 1.0}}},
                {"name", "Object " + std::to_string(id)}, {"tag", ""},
                {"children", std::move(children)}, {"components", std::move(components)}};
}

Json make_flat_scene(std::size_t count)
{
    Json root = Json::array();
    root.get_ref<Json::array_t &>().reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        Json components = Json::array();
        components.push_back(Json{{"id", static_cast<int>(1000000 + index)}, {"type", SPRITE},
                                  {"textureFilePath", "assets/textures/pilot.png"}, {"flip", 0},
                                  {"sourceRect", {{"x", 0}, {"y", 0}, {"w", 0}, {"h", 0}}}});
        root.push_back(make_object(static_cast<int>(index), std::move(components)));
    }
    return root;
}

Json make_deep_scene(std::size_t depth)
{
    Json chain = make_object(0);
    Json *leaf = &chain;
    for (std::size_t index = 1; index < depth; ++index)
    {
        Json children = Json::array();
        children.push_back(make_object(static_cast<int>(index)));
        leaf->at("children") = std::move(children);
        leaf = &leaf->at("children").at(0);
    }
    leaf->at("components").push_back(Json{{"id", 1000000 + static_cast<int>(depth)},
                                           {"type", SPRITE},
                                           {"textureFilePath", "assets/textures/pilot.png"},
                                           {"flip", 0},
                                           {"sourceRect", {{"x", 0}, {"y", 0}, {"w", 0}, {"h", 0}}}});
    return Json::array({std::move(chain)});
}

platformator_editor::DocumentSession parse_scene(Json scene)
{
    auto parsed = platformator_editor::parse_document(std::move(scene));
    if (!parsed)
        throw std::runtime_error("probe scene could not be parsed: " + parsed.error);
    return std::move(*parsed.document);
}

std::uint64_t resident_kib()
{
    std::ifstream stream("/proc/self/statm");
    std::uint64_t totalPages = 0;
    std::uint64_t residentPages = 0;
    if (!(stream >> totalPages >> residentPages))
        return 0;
    const long pageSize = sysconf(_SC_PAGESIZE);
    return pageSize > 0 ? residentPages * static_cast<std::uint64_t>(pageSize) / 1024 : 0;
}

Json workload_observation(std::string name, std::size_t population, bool deep)
{
    auto document = parse_scene(deep ? make_deep_scene(population) : make_flat_scene(population));
    const auto objectCount = document.indexes.orderedObjectIds.size();
    std::size_t textureReferences = 0;
    std::vector<const Json *> pending;
    for (const auto &object : document.root)
        pending.push_back(&object);
    while (!pending.empty())
    {
        const auto *object = pending.back();
        pending.pop_back();
        const auto components = object->find("components");
        if (components != object->end() && components->is_array())
            for (const auto &component : *components)
                if (component.value("type", -1) == SPRITE && component.contains("textureFilePath"))
                    ++textureReferences;
        const auto children = object->find("children");
        if (children != object->end() && children->is_array())
            for (const auto &child : *children)
                pending.push_back(&child);
    }
    const std::size_t targetId = deep ? population - 1 : (population > 1 ? population / 2 : 0);
    if (!document.indexes.objectsById.contains(static_cast<int>(targetId)))
        throw std::runtime_error("probe target object is absent from document indexes");
    const auto serializedBytes = document.root.dump().size();

    platformator_editor::SelectionModel selection;
    platformator_editor::EditCommandService commands;
    commands.reset_history(document.documentGeneration);
    selection.set(document, platformator_editor::Selection{static_cast<int>(targetId)});
    platformator_editor::PropertyAddress address;
    address.documentGeneration = document.documentGeneration;
    address.objectId = static_cast<int>(targetId);
    address.fieldPath = {std::string("position"), std::string("x")};
    auto edited = commands.set_property(document, address, 1.0);
    if (!edited)
        throw std::runtime_error("probe property edit failed: " + edited.message);
    const auto retainedHistory = commands.history_status().retainedBytes;
    if (commands.undo(document, selection).status != platformator_editor::CommandStatus::Changed)
        throw std::runtime_error("probe property edit could not be undone");

    return Json{{"workload", std::move(name)}, {"objects", objectCount},
                {"texture_references", textureReferences}, {"serialized_scene_bytes", serializedBytes},
                {"edited_object_id", targetId}, {"history_retained_bytes", retainedHistory}};
}

Json runtime_workload_observation(const std::string &name, std::size_t population, bool deep,
                                  const std::filesystem::path &scratch)
{
    const auto projectRoot = scratch / ("runtime-" + name);
    const auto assetRoot = projectRoot / "assets";
    const auto textureDirectory = assetRoot / "textures";
    std::filesystem::create_directories(textureDirectory);
    const auto repositoryTexture = std::filesystem::current_path() / "assets" / "textures" /
                                  "black_square.png";
    std::filesystem::copy_file(repositoryTexture, textureDirectory / "pilot.png",
                               std::filesystem::copy_options::overwrite_existing);

    const Json scene = deep ? make_deep_scene(population) : make_flat_scene(population);
    const auto scenePath = projectRoot / "lifecycle.scene";
    {
        std::ofstream output(scenePath, std::ios::binary);
        output << scene.dump();
        if (!output)
            throw std::runtime_error("could not write Runtime lifecycle scene");
    }
    platformator_editor::DocumentWorkspace workspace(projectRoot, assetRoot);
    const auto adopted = workspace.open_document(scenePath, platformator_editor::UnsavedDecision::Discard);
    if (!adopted)
        throw std::runtime_error("Runtime lifecycle scene adoption failed: " + adopted.message);

    platformator_editor::SelectionModel selection;
    platformator_editor::EditCommandService commands;
    workspace.bind_history(commands);
    commands.set_selection_provider([&selection] { return selection.current(); });
    const int targetId = static_cast<int>(deep ? population - 1 : (population > 1 ? population / 2 : 0));
    const std::size_t expectedTexturedProxies = deep ? 1 : population;
    const std::size_t expectedProxyCount = expectedTexturedProxies;
    selection.set(workspace.document(), platformator_editor::Selection{targetId});
    platformator_editor::PropertyAddress address;
    address.documentGeneration = workspace.document().documentGeneration;
    address.objectId = targetId;
    address.fieldPath = {std::string("position"), std::string("x")};

    RuntimeOptions runtimeOptions;
    runtimeOptions.assetRoot = assetRoot.string();
    runtimeOptions.windowSettings.width = 320;
    runtimeOptions.windowSettings.height = 240;
    runtimeOptions.windowSettings.headless = false;
    Json runtimeObservation;
    {
        platformator::Runtime runtime(runtimeOptions);
        platformator_editor::EditorPreview preview(runtime);
        preview.reconcile(workspace.document(), true);
        auto edited = commands.set_property(workspace.document_for_edit(), address, 1.0);
        if (!edited)
            throw std::runtime_error("Runtime lifecycle property edit failed: " + edited.message);
        preview.reconcile(workspace.document(), false, edited.affectedObjectIds);
        const auto historyBytes = commands.history_status().retainedBytes;
        auto undone = commands.undo(workspace.document_for_edit(), selection);
        if (undone.status != platformator_editor::CommandStatus::Changed)
            throw std::runtime_error("Runtime lifecycle edit could not be undone");
        preview.reconcile(workspace.document(), false, undone.affectedObjectIds);
        if (!preview.render(0.0f, 0.0f, 640.0f, 480.0f, 320, 240))
            throw std::runtime_error("Runtime lifecycle preview render failed: " + preview.last_error());

        const auto live = preview.resource_counts();
        if (live.proxies != expectedProxyCount || live.texturedProxies != expectedTexturedProxies ||
            live.uniqueTextureWrappers != 1 ||
            live.textureWrapperReferences != expectedTexturedProxies ||
            !live.renderTargetAllocated)
            throw std::runtime_error("Runtime lifecycle did not retain the expected preview proxy/texture/target resources");
        const auto runtimeObjects = runtime.getAllGameObjects().size();
        if (runtimeObjects != expectedProxyCount)
            throw std::runtime_error("Runtime lifecycle GameObject count differs from the live preview proxies");
        const char *videoDriver = SDL_GetCurrentVideoDriver();
        const char *rendererName = SDL_GetRendererName(runtime.getRenderer());
        if (videoDriver == nullptr || rendererName == nullptr)
            throw std::runtime_error("SDL did not expose the Runtime lifecycle backend identity");
        preview.release();
        const auto released = preview.resource_counts();
        runtimeObservation = Json{
            {"adopted_document_generation", workspace.document().documentGeneration},
            {"objects", workspace.document().indexes.objectsById.size()},
            {"runtime_objects_with_preview", runtimeObjects},
            {"video_driver", videoDriver},
            {"renderer_name", rendererName},
            {"preview_proxies", live.proxies},
            {"textured_proxies", live.texturedProxies},
            {"unique_texture_wrappers", live.uniqueTextureWrappers},
            {"texture_wrapper_references", live.textureWrapperReferences},
            {"render_target_allocated", live.renderTargetAllocated},
            {"history_retained_bytes", historyBytes},
            {"document_dirty_after_undo", workspace.dirty()},
            {"released_proxies", released.proxies},
            {"released_texture_wrappers", released.uniqueTextureWrappers},
            {"render_target_after_release", released.renderTargetAllocated},
            {"runtime_objects_after_preview_release", runtime.getAllGameObjects().size()}};
    }
    return runtimeObservation;
}

Json task_observation(const std::filesystem::path &root)
{
    const auto projectRoot = root / "project";
    const auto assetRoot = projectRoot / "assets";
    std::filesystem::create_directories(assetRoot);
    std::filesystem::create_directories(projectRoot / "src" / "nested");
    for (int index = 0; index < 32; ++index)
    {
        std::ofstream file(projectRoot / "src" / "nested" / ("input-" + std::to_string(index) + ".cpp"));
        file << "int value_" << index << " = " << index << ";\n";
    }
    platformator_editor::ProjectContext context;
    context.set_roots(projectRoot, assetRoot);
    platformator_editor::ProjectScanService service(2, 2);
    platformator_editor::ProjectScanOptions options;
    options.recursive = true;
    options.includeDirectories = true;
    options.maxFiles = 64;
    options.maxDirectories = 16;
    options.maxEntries = 128;
    options.sourceInputsOnly = true;
    const auto token = service.request_scan(context.snapshot(), projectRoot, options);
    if (!token)
        throw std::runtime_error("probe project scan was rejected");
    if (captureInterrupted != 0)
        token.cancel();
    service.wait_for_idle();
    check_capture_stop();
    const auto results = service.take_ready(context.snapshot().generation);
    if (results.size() != 1 || !results.front().error.empty() || results.front().cancelled)
        throw std::runtime_error("probe project scan did not complete cleanly");
    const auto &result = results.front();
    return Json{{"files", result.files.size()}, {"directories", result.directories.size()},
                {"truncated", result.truncated}, {"source_identity_bytes", result.sourceInputIdentity.size()},
                {"completed_tasks", results.size()}};
}

Json process_observation()
{
    platformator_editor::ProcessService process;
    platformator_editor::BoundedConsole console;
    std::uint64_t outputBytes = 0;
    bool completed = false;
    platformator_editor::ProcessCompletion result;
    platformator_editor::ProcessRequest request;
    request.argv = {"/bin/sh", "-c", "head -c 1200000 /dev/zero"};
    request.workingDirectory = std::filesystem::current_path();
    request.outputMode = platformator_editor::ProcessOutputMode::Merged;
    if (!process.start(std::move(request), 1,
                       [&](platformator_editor::ProcessOutput output) {
                           outputBytes += output.bytes.size();
                           console.append(output.bytes);
                       },
                       [&](platformator_editor::ProcessCompletion completion) {
                           result = std::move(completion);
                           completed = true;
                       }))
        throw std::runtime_error("probe child process could not start");
    while (!completed)
    {
        if (captureInterrupted != 0)
        {
            process.stop();
            while (!completed)
            {
                process.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check_capture_stop();
        }
        process.poll();
        if (!completed)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!result.launched || result.exitCode != 0)
        throw std::runtime_error("probe child process failed: " + result.error);
    return Json{{"processes_started", 1}, {"output_bytes", outputBytes},
                {"console_retained_bytes", console.text().size()}, {"console_truncated", console.truncated()}};
}

std::string selected_game_source_identity(const std::filesystem::path &repositoryRoot)
{
    const auto root = std::filesystem::canonical(repositoryRoot);
    platformator_editor::ProjectContext context;
    context.set_roots(root, root / "assets");
    platformator_editor::ProjectScanService scans(2, 2);
    platformator_editor::ProjectScanOptions options;
    options.recursive = true;
    options.maxFiles = 50000;
    options.includeDirectories = true;
    options.maxDirectories = 20000;
    options.maxEntries = 100000;
    options.sourceInputsOnly = true;
    const auto scan = scans.request_scan(context.snapshot(), root, options);
    if (!scan)
        throw std::runtime_error("selected-game source identity scan was rejected");
    if (captureInterrupted != 0)
        scan.cancel();
    scans.wait_for_idle();
    check_capture_stop();
    const auto results = scans.take_ready(context.snapshot().generation);
    if (results.size() != 1 || !results.front().error.empty() || results.front().cancelled ||
        results.front().truncated || results.front().sourceInputIdentity.empty())
        throw std::runtime_error("selected-game source identity scan did not complete cleanly");
    return results.front().sourceInputIdentity;
}

Json game_pipeline_observation(const std::filesystem::path &repositoryRoot,
                              const std::string &sourceIdentity, int cycle,
                              bool buildSelectedGame)
{
    const auto projectRoot = std::filesystem::canonical(repositoryRoot);
    const auto assetRoot = std::filesystem::canonical(projectRoot / "assets");
    const auto scenePath = std::filesystem::canonical(assetRoot / "scenes" / "default.scene");
    const auto executable = projectRoot / "bin" / "debug" / "main";
    std::ifstream sceneStream(scenePath);
    if (!sceneStream)
        throw std::runtime_error("selected-game lifecycle scene is unavailable: " + scenePath.string());
    Json scene;
    sceneStream >> scene;

    platformator_editor::PipelineRequest request;
    request.projectRoot = projectRoot;
    request.assetRoot = assetRoot;
    request.savedScenePath = scenePath;
    request.executablePath = executable;
    request.configureArgv = buildSelectedGame
        ? std::vector<std::string>{"cmake", "--preset", "debug"}
        : std::vector<std::string>{"cmake", "-E", "true"};
    request.buildArgv = buildSelectedGame
        ? std::vector<std::string>{"cmake", "--build", "--preset", "debug", "--target", "main"}
        : std::vector<std::string>{"cmake", "-E", "true"};
    request.runArguments = {"--headless", "--smoke-steps", "1000000"};
    request.scene = std::move(scene);
    request.catalogProtocol = "common-runner-v1";
    request.identity.canonicalProjectPath = projectRoot.string();
    request.identity.profile = "debug";
    request.identity.target = "main";
    request.identity.executablePath = executable.string();
    request.identity.sourceInputIdentity = sourceIdentity;

    platformator_editor::GamePipeline pipeline;
    platformator_editor::BehaviorCatalog catalog;
    const auto started = Clock::now();
    if (!pipeline.start(std::move(request), catalog, static_cast<std::uint64_t>(cycle + 1)))
        throw std::runtime_error("selected-game pipeline did not start: " + pipeline.status().message);
    const auto deadline = Clock::now() + std::chrono::seconds(120);
    while (pipeline.busy() && pipeline.status().stage != platformator_editor::PipelineStage::Running &&
           Clock::now() < deadline)
    {
        if (captureInterrupted != 0)
        {
            pipeline.stop();
            while (pipeline.busy())
            {
                pipeline.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check_capture_stop();
        }
        pipeline.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (pipeline.status().stage != platformator_editor::PipelineStage::Running)
    {
        if (pipeline.busy())
        {
            pipeline.stop();
            while (pipeline.busy())
            {
                pipeline.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        throw std::runtime_error("selected-game pipeline failed before Run: " + pipeline.status().message +
                                 "\n" + pipeline.console().text());
    }
    const auto runReachedAt = Clock::now();
    const auto catalogCurrent = catalog.can_automatic_run();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    pipeline.poll();
    if (captureInterrupted != 0)
    {
        pipeline.stop();
        while (pipeline.busy())
        {
            pipeline.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check_capture_stop();
    }
    if (!pipeline.busy())
        throw std::runtime_error("selected-game process exited before the Stop observation");
    pipeline.stop();
    while (pipeline.busy())
    {
        pipeline.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (pipeline.status().outcome != platformator_editor::PipelineOutcome::Cancelled)
        throw std::runtime_error("selected-game Stop did not cancel the owned pipeline");
    const auto stoppedAt = Clock::now();
    std::error_code error;
    const auto executableBytes = std::filesystem::file_size(executable, error);
    if (error)
        throw std::runtime_error("selected-game executable identity is unavailable after build");
    const auto executableModified = std::filesystem::last_write_time(executable, error);
    return Json{{"cycle", cycle}, {"target", "main"}, {"profile", "debug"},
                {"project_root", projectRoot.string()}, {"asset_root", assetRoot.string()},
                {"scene_path", scenePath.string()}, {"executable_path", executable.string()},
                {"source_identity", sourceIdentity},
                {"executable_identity", std::to_string(executableBytes) + ":" +
                     (error ? std::string("unknown") :
                      std::to_string(executableModified.time_since_epoch().count()))},
                {"catalog_verified", catalogCurrent},
                {"build_mode", buildSelectedGame ? "warmup_build" : "reuse_existing_binary"},
                {"pipeline_to_run_ns",
                    std::chrono::duration_cast<std::chrono::nanoseconds>(runReachedAt - started).count()},
                {"stop_reap_ns",
                    std::chrono::duration_cast<std::chrono::nanoseconds>(stoppedAt - runReachedAt).count()},
                {"pipeline_cancelled", pipeline.status().outcome == platformator_editor::PipelineOutcome::Cancelled}};
}

Json resource_mode(int cycles, const std::filesystem::path &scratch,
                   const std::filesystem::path &progressPath)
{
    if (SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1) != 0)
        throw std::runtime_error("could not select SDL's dummy video driver for resource observations");
    if (SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 1) != 0)
        throw std::runtime_error("could not select SDL's dummy audio driver for resource observations");
    if (SDL_setenv_unsafe("SDL_RENDER_DRIVER", "software", 1) != 0)
        throw std::runtime_error("could not select SDL's software renderer for resource observations");
    const auto repositoryRoot = std::filesystem::current_path();
    const auto sourceIdentity = selected_game_source_identity(repositoryRoot);
    check_capture_stop();
    std::ofstream progress(progressPath, std::ios::binary | std::ios::trunc);
    if (!progress)
        throw std::runtime_error("could not open the bounded resource progress file");
    progress << Json{{"schema_version", 1}, {"mode", "resources-progress"},
                     {"warmup_cycles", 1}, {"cycles", cycles}}.dump() << '\n';
    progress.flush();
    if (!progress)
        throw std::runtime_error("could not write the resource progress header");
    Json rows = Json::array();
    for (const auto &[name, population, deep] : {
             std::tuple<std::string_view, std::size_t, bool>{"tiny", 10, false},
             {"objects_1000", 1000, false}, {"objects_10000", 10000, false},
             {"deep_parent_128", 128, true}})
    {
        check_capture_stop();
        runtime_workload_observation(std::string(name), population, deep, scratch / "warmup");
    }
    check_capture_stop();
    game_pipeline_observation(repositoryRoot, sourceIdentity, -1, true);
    check_capture_stop();
    task_observation(scratch / "warmup");
    check_capture_stop();
    process_observation();
    for (int cycle = 0; cycle < cycles; ++cycle)
    {
        allocationCalls.store(0, std::memory_order_relaxed);
        allocatedBytes.store(0, std::memory_order_relaxed);
        countAllocations.store(true, std::memory_order_release);
        Json workloads = Json::array();
        const auto add_workload = [&](std::string name, std::size_t population, bool deep) {
            check_capture_stop();
            auto observation = workload_observation(name, population, deep);
            observation["runtime"] = runtime_workload_observation(name, population, deep,
                scratch / ("cycle-" + std::to_string(cycle)));
            workloads.push_back(std::move(observation));
        };
        add_workload("tiny", 10, false);
        add_workload("objects_1000", 1000, false);
        add_workload("objects_10000", 10000, false);
        add_workload("deep_parent_128", 128, true);
        const auto tasks = task_observation(scratch / ("cycle-" + std::to_string(cycle)));
        const auto process = process_observation();
        const auto game = game_pipeline_observation(repositoryRoot, sourceIdentity, cycle, false);
        countAllocations.store(false, std::memory_order_release);
        Json row{{"cycle", cycle}, {"workloads", std::move(workloads)}, {"task", tasks},
                 {"process", process}, {"game_pipeline", game},
                 {"allocation", {{"calls", allocationCalls.load(std::memory_order_relaxed)},
                                  {"bytes", allocatedBytes.load(std::memory_order_relaxed)}}},
                 {"resident_kib", resident_kib()}};
        rows.push_back(row);
        progress << row.dump() << '\n';
        progress.flush();
        if (!progress)
            throw std::runtime_error("could not persist a completed resource lifecycle cycle");
    }
    return Json{{"schema_version", 1}, {"mode", "resources"}, {"warmup_cycles", 1},
                {"cycles", cycles}, {"observations", rows}};
}

struct TimingWorkload
{
    std::string name;
    platformator_editor::DocumentSession document;
    int objectId = 0;
};

TimingWorkload make_timing_workload(const std::string &name, std::size_t population, bool deep)
{
    TimingWorkload workload;
    workload.name = name;
    workload.document = parse_scene(deep ? make_deep_scene(population) : make_flat_scene(population));
    workload.objectId = static_cast<int>(deep ? population - 1 : (population > 1 ? population / 2 : 0));
    return workload;
}

Json timing_mode(int repetitions, int warmupFrames, int measuredFrames)
{
    std::vector<TimingWorkload> workloads;
    workloads.push_back(make_timing_workload("tiny", 10, false));
    workloads.push_back(make_timing_workload("objects_1000", 1000, false));
    workloads.push_back(make_timing_workload("objects_10000", 10000, false));
    workloads.push_back(make_timing_workload("deep_parent_128", 128, true));
    Json samples = Json::array();
    for (auto &workload : workloads)
    {
        platformator_editor::SelectionModel selection;
        platformator_editor::EditCommandService commands;
        commands.reset_history(workload.document.documentGeneration);
        platformator_editor::PropertyAddress address;
        address.documentGeneration = workload.document.documentGeneration;
        address.objectId = workload.objectId;
        address.fieldPath = {std::string("position"), std::string("x")};
        std::uint64_t sequence = 0;
        const auto step = [&]() {
            selection.set(workload.document, platformator_editor::Selection{workload.objectId});
            auto result = commands.set_property(workload.document, address,
                                                static_cast<double>((sequence % 999) + 1));
            if (!result)
                throw std::runtime_error("timing property edit failed: " + result.message);
            ++sequence;
        };
        for (int repetition = 0; repetition < repetitions; ++repetition)
        {
            for (int frame = 0; frame < warmupFrames; ++frame)
            {
                step();
                const auto undone = commands.undo(workload.document, selection);
                if (undone.status != platformator_editor::CommandStatus::Changed)
                    throw std::runtime_error("warmup edit could not be undone");
            }
            for (int frame = 0; frame < measuredFrames; ++frame)
            {
                const auto started = Clock::now();
                step();
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count();
                samples.push_back(Json{{"workload", workload.name}, {"repetition", repetition},
                                       {"frame", frame}, {"select_edit_ns", elapsed}});
                const auto undone = commands.undo(workload.document, selection);
                if (undone.status != platformator_editor::CommandStatus::Changed)
                    throw std::runtime_error("measured edit could not be undone");
            }
        }
    }
    return Json{{"schema_version", 1}, {"mode", "timing"}, {"repetitions", repetitions},
                {"warmup_frames", warmupFrames}, {"measured_frames", measuredFrames},
                {"samples", std::move(samples)}};
}

int positive_integer(std::string_view text, const char *name)
{
    std::size_t consumed = 0;
    const auto value = std::stoi(std::string(text), &consumed);
    if (consumed != text.size() || value <= 0)
        throw std::invalid_argument(std::string(name) + " must be a positive integer");
    return value;
}
} // namespace

void *operator new(std::size_t size) { return allocate(size); }
void *operator new[](std::size_t size) { return allocate(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }
void *operator new(std::size_t size, std::align_val_t alignment)
{
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void *operator new[](std::size_t size, std::align_val_t alignment)
{
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void operator delete(void *memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }

int main(int argc, char **argv)
{
    std::signal(SIGINT, request_capture_stop);
    std::signal(SIGTERM, request_capture_stop);
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--print-build-identity")
        {
            std::cout << Json{{"probe", "platformator-editor-probe"}, {"schema_version", 1},
                              {"configuration", PLATFORMATOR_EDITOR_PROBE_CONFIGURATION},
                              {"compiler", PLATFORMATOR_EDITOR_PROBE_COMPILER},
                              {"cxx_standard", 20}}.dump() << '\n';
            return 0;
        }
        if (argc < 3 || (std::string_view(argv[1]) != "resources" && std::string_view(argv[1]) != "timing"))
            throw std::invalid_argument("usage: platformator_editor_probe resources --cycles N --scratch PATH --progress PATH | timing --repetitions N --warmup N --frames N");
        const std::string mode = argv[1];
        int cycles = 20;
        int repetitions = 3;
        int warmupFrames = 5;
        int measuredFrames = 30;
        std::filesystem::path scratch = std::filesystem::temp_directory_path() / "platformator-editor-probe";
        std::filesystem::path progress;
        for (int index = 2; index < argc; ++index)
        {
            const std::string_view option = argv[index];
            if (index + 1 >= argc)
                throw std::invalid_argument("missing value for " + std::string(option));
            const std::string_view value = argv[++index];
            if (option == "--cycles") cycles = positive_integer(value, "--cycles");
            else if (option == "--repetitions") repetitions = positive_integer(value, "--repetitions");
            else if (option == "--warmup") warmupFrames = positive_integer(value, "--warmup");
            else if (option == "--frames") measuredFrames = positive_integer(value, "--frames");
            else if (option == "--scratch") scratch = std::filesystem::path(value);
            else if (option == "--progress") progress = std::filesystem::path(value);
            else throw std::invalid_argument("unknown option: " + std::string(option));
        }
        if (mode == "resources" && progress.empty())
            throw std::invalid_argument("resources mode requires a guarded --progress output path");
        Json result = mode == "resources" ? resource_mode(cycles, scratch, progress)
                                          : timing_mode(repetitions, warmupFrames, measuredFrames);
        result["identity"] = Json{{"probe", "platformator-editor-probe"}, {"schema_version", 1},
                                  {"configuration", PLATFORMATOR_EDITOR_PROBE_CONFIGURATION},
                                  {"compiler", PLATFORMATOR_EDITOR_PROBE_COMPILER}, {"cxx_standard", 20}};
        std::cout << result.dump() << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        countAllocations.store(false, std::memory_order_release);
        std::cerr << "platformator_editor_probe: " << error.what() << '\n';
        return 2;
    }
}
