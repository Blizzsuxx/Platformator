#include "editorapp.h"
#include "platform/watch.h"
#include "project.h"
#include "projectsession.h"
#include "test_support.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

using namespace platformator_editor;

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void test_directory_exclusion_rules()
{
    const std::vector<std::string_view> skippedDirs = {
        ".git", ".venv", ".agents", ".codex", ".specify", ".cache",
        "bin", "build", "dist", "vendored", "vendor", "node_modules",
        "specs", "benchmark", "diplomski", "paper", "docs"
    };

    for (const auto &dir : skippedDirs)
    {
        require(should_skip_watched_directory(dir), "watched directory rule must skip excluded dir");
        require(should_skip_directory(dir), "project directory rule must skip excluded dir");
        require(should_skip_project_directory(dir), "project session directory rule must skip excluded dir");
    }

    const std::vector<std::string_view> allowedDirs = {
        "assets", "src", "scenes", "textures", "levels", "scripts", "audio", "prefabs"
    };

    for (const auto &dir : allowedDirs)
    {
        require(!should_skip_watched_directory(dir), "watched directory rule must not skip valid asset/source dir");
        require(!should_skip_directory(dir), "project directory rule must not skip valid asset/source dir");
        require(!should_skip_project_directory(dir), "project session directory rule must not skip valid asset/source dir");
    }
}

void test_idle_wait_scheduling()
{
    const auto now = EditorWaitClock::now();
    const auto maximumWait = std::chrono::milliseconds(250);

    // 1. With no deadlines and no polling interval, wait must reach maximumWait (250ms).
    const auto noDeadlinesWait = editor_wait_duration(now, {}, maximumWait, std::nullopt);
    require(noDeadlinesWait.count() >= 250, "idle wait with no deadlines must reach maximumWait");

    // 2. With a future deadline at +100ms, wait must be bounded to 100ms.
    const auto futureDeadline = now + std::chrono::milliseconds(100);
    const auto boundedWait = editor_wait_duration(now, {&futureDeadline, 1}, maximumWait, std::nullopt);
    require(boundedWait.count() >= 99 && boundedWait.count() <= 101, "idle wait must round to nearest future deadline");

    // 3. Stale deadline in the past: if nextReferenceFallbackCheck was erroneously included
    // while in the past, wait would be 0ms.
    const auto staleDeadline = now - std::chrono::seconds(2);
    const auto staleWait = editor_wait_duration(now, {&staleDeadline, 1}, maximumWait, std::nullopt);
    require(staleWait.count() == 0, "stale deadline collapses wait to 0ms (illustrating the bug)");

    // But with the fix, when coverage is Current, stale deadline is excluded, preserving idle wait!
    const auto validFutureDeadline = now + std::chrono::seconds(2);
    const auto fixedWait = editor_wait_duration(now, {&validFutureDeadline, 1}, maximumWait, std::nullopt);
    require(fixedWait.count() >= 250, "excluding stale deadline preserves 250ms idle heartbeat wait");
}

void test_source_hash_caching_and_directory_skipping()
{
    clear_source_hash_cache();
    require(source_hash_cache_size() == 0, "hash cache must initially be empty");

    platformator_editor_test::TemporaryDirectory tempDir;
    const auto projectRoot = tempDir.path() / "my_project";
    const auto assetRoot = projectRoot / "assets";
    const auto srcDir = projectRoot / "src";
    const auto venvDir = projectRoot / ".venv";
    const auto buildDir = projectRoot / "build";

    std::filesystem::create_directories(srcDir);
    std::filesystem::create_directories(assetRoot);
    std::filesystem::create_directories(venvDir);
    std::filesystem::create_directories(buildDir);

    // Create source files
    const auto mainFile = srcDir / "main.cpp";
    {
        std::ofstream out(mainFile);
        out << "int main() { return 0; }\n";
    }

    // Create a file in .venv which must be skipped
    {
        std::ofstream out(venvDir / "pyvenv.cfg");
        out << "home = /usr/bin\n";
    }

    ProjectContext context;
    context.set_roots(projectRoot, assetRoot);
    const auto project = context.snapshot();

    ProjectScanService scans(1);
    const auto token1 = scans.request_scan(project, projectRoot,
        ProjectScanOptions{.recursive = true, .sourceInputsOnly = true, .trackFileMetadata = true});
    scans.wait_for_idle();
    auto results1 = scans.take_ready(project.generation);
    require(!results1.empty(), "scan 1 must return a result");
    const auto &res1 = results1.front();

    require(!res1.sourceInputIdentity.empty(), "scan must produce sourceInputIdentity");
    require(source_hash_cache_size() == 1, "hash cache must contain 1 cached source file");

    // Verify .venv and build are NOT in res1.inventoryEntries or files
    for (const auto &[path, id] : res1.inventoryEntries)
    {
        require(path.string().find(".venv") == std::string::npos, "inventoryEntries must not contain .venv");
        require(path.string().find("build") == std::string::npos, "inventoryEntries must not contain build");
    }

    // Second scan: source file is unchanged, cache should be hit
    const auto token2 = scans.request_scan(project, projectRoot,
        ProjectScanOptions{.recursive = true, .sourceInputsOnly = true, .trackFileMetadata = true});
    scans.wait_for_idle();
    auto results2 = scans.take_ready(project.generation);
    require(!results2.empty(), "scan 2 must return a result");
    const auto &res2 = results2.front();
    require(res2.sourceInputIdentity == res1.sourceInputIdentity, "unchanged file must produce identical identity");
    require(source_hash_cache_size() == 1, "cache size remains 1");

    // Modify file: identity must change and cache should update
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::ofstream out(mainFile);
        out << "int main() { return 42; }\n";
    }
    const auto token3 = scans.request_scan(project, projectRoot,
        ProjectScanOptions{.recursive = true, .sourceInputsOnly = true, .trackFileMetadata = true});
    scans.wait_for_idle();
    auto results3 = scans.take_ready(project.generation);
    require(!results3.empty(), "scan 3 must return a result");
    const auto &res3 = results3.front();
    require(res3.sourceInputIdentity != res1.sourceInputIdentity, "modified file must produce distinct identity");
}
} // namespace

int main()
{
    try
    {
        test_directory_exclusion_rules();
        test_idle_wait_scheduling();
        test_source_hash_caching_and_directory_skipping();
        std::cout << "All project watch and idle wait regression tests passed.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Test failure: " << e.what() << "\n";
        return 1;
    }
}
