#include "document.h"
#include "editcommands.h"
#include "process.h"
#include "project.h"
#include "selection.h"

#include <algorithm>
#include <atomic>
#include <chrono>
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
#include <vector>

#include <unistd.h>

#include <json.hpp>
#include <platformator/gameobject.h>

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
    service.wait_for_idle();
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
        process.poll();
        if (!completed)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!result.launched || result.exitCode != 0)
        throw std::runtime_error("probe child process failed: " + result.error);
    return Json{{"processes_started", 1}, {"output_bytes", outputBytes},
                {"console_retained_bytes", console.text().size()}, {"console_truncated", console.truncated()}};
}

Json resource_mode(int cycles, const std::filesystem::path &scratch)
{
    Json rows = Json::array();
    for (int cycle = 0; cycle < cycles; ++cycle)
    {
        allocationCalls.store(0, std::memory_order_relaxed);
        allocatedBytes.store(0, std::memory_order_relaxed);
        countAllocations.store(true, std::memory_order_release);
        Json workloads = Json::array();
        workloads.push_back(workload_observation("tiny", 10, false));
        workloads.push_back(workload_observation("objects_1000", 1000, false));
        workloads.push_back(workload_observation("objects_10000", 10000, false));
        workloads.push_back(workload_observation("deep_parent_128", 128, true));
        const auto tasks = task_observation(scratch / ("cycle-" + std::to_string(cycle)));
        const auto process = process_observation();
        countAllocations.store(false, std::memory_order_release);
        rows.push_back(Json{{"cycle", cycle}, {"workloads", std::move(workloads)}, {"task", tasks},
                            {"process", process},
                            {"allocation", {{"calls", allocationCalls.load(std::memory_order_relaxed)},
                                             {"bytes", allocatedBytes.load(std::memory_order_relaxed)}}},
                            {"resident_kib", resident_kib()}});
    }
    return Json{{"schema_version", 1}, {"mode", "resources"}, {"cycles", cycles}, {"observations", rows}};
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
            throw std::invalid_argument("usage: platformator_editor_probe resources --cycles N --scratch PATH | timing --repetitions N --warmup N --frames N");
        const std::string mode = argv[1];
        int cycles = 20;
        int repetitions = 3;
        int warmupFrames = 5;
        int measuredFrames = 30;
        std::filesystem::path scratch = std::filesystem::temp_directory_path() / "platformator-editor-probe";
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
            else throw std::invalid_argument("unknown option: " + std::string(option));
        }
        Json result = mode == "resources" ? resource_mode(cycles, scratch)
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
