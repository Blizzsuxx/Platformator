#include "project.h"
#include "test_support.h"

#include <algorithm>
#include <stdexcept>
#include <string>

using namespace platformator_editor;
using namespace platformator_editor_test;

void run_project_cases()
{
    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    const auto scanRoot = assetRoot / "nested";
    std::filesystem::create_directories(scanRoot);
    std::ofstream(scanRoot / "z.scene") << "z";
    std::ofstream(scanRoot / "a.scene") << "a";
    std::ofstream(assetRoot / "root.scene") << "root";
    std::ofstream(projectRoot / "CMakeLists.txt") << "project(test)";
    std::ofstream(projectRoot / "CMakePresets.json") << "{}";
    std::ofstream(projectRoot / "main.cpp") << "int main() {}";

    ProjectContext context;
    context.set_roots(projectRoot, assetRoot);
    const auto first = context.snapshot();
    require(first.projectRoot == std::filesystem::canonical(projectRoot) &&
                first.assetRoot == std::filesystem::canonical(assetRoot),
            "project context roots must be canonicalized");
    bool rejected = false;
    try
    {
        context.set_roots(temporary.path() / "missing", assetRoot);
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    require(rejected && context.snapshot().generation == first.generation &&
                context.snapshot().projectRoot == first.projectRoot,
            "failed project replacement must preserve the current canonical context");

    {
        ProjectScanService scans(2);
        const auto request = scans.request_scan(first, assetRoot, ProjectScanOptions{.recursive = true,
                                                                                     .maxFiles = 100});
        scans.wait_for_idle();
        auto results = scans.take_ready(first.generation);
        require(results.size() == 1 && results.front().requestId == request.request_id() &&
                    results.front().files.size() == 3,
                "recursive scans must publish a complete immutable file snapshot");
        require(std::is_sorted(results.front().files.begin(), results.front().files.end()),
                "scan results must have deterministic path order");

        const auto expanded = scans.request_scan(first, assetRoot,
            ProjectScanOptions{.recursive = false, .includeDirectories = true, .maxDirectories = 8});
        scans.wait_for_idle();
        results = scans.take_ready(first.generation);
        require(results.size() == 1 && results.front().requestId == expanded.request_id() &&
                    results.front().files.size() == 1 && results.front().directories.size() == 1 &&
                    results.front().directories.front() == scanRoot,
                "an expanded asset directory scan must return only its immediate files and directories");

        const auto identityScan = scans.request_scan(first, projectRoot,
            ProjectScanOptions{.recursive = true, .maxFiles = 100, .maxEntries = 1000,
                               .sourceInputsOnly = true});
        scans.wait_for_idle();
        results = scans.take_ready(first.generation);
        require(results.size() == 1 && results.front().requestId == identityScan.request_id() &&
                    results.front().files.size() == 3 && !results.front().sourceInputIdentity.empty(),
                "source identity scans must include source and CMake inputs, not runtime assets");
        const auto originalIdentity = results.front().sourceInputIdentity;
        std::ofstream(projectRoot / "main.cpp", std::ios::trunc) << "int main() { return 0; }\n";
        const auto changedIdentityScan = scans.request_scan(first, projectRoot,
            ProjectScanOptions{.recursive = true, .maxFiles = 100, .maxEntries = 1000,
                               .sourceInputsOnly = true});
        scans.wait_for_idle();
        results = scans.take_ready(first.generation);
        require(results.size() == 1 && results.front().requestId == changedIdentityScan.request_id() &&
                    results.front().sourceInputIdentity != originalIdentity,
                "source identity scans must detect configured source input changes");

        const auto stale = scans.request_scan(first, assetRoot, ProjectScanOptions{.recursive = true,
                                                                                    .maxFiles = 100});
        scans.wait_for_idle();
        require(scans.take_ready(first.generation + 1).empty(),
                "results from a replaced project generation must not reach the UI");
        stale.cancel();

        for (int i = 0; i < 5; ++i)
            scans.request_scan(first, assetRoot, ProjectScanOptions{.recursive = true, .maxFiles = 100});
        scans.wait_for_idle();
        results = scans.take_ready(first.generation);
        require(results.size() <= 2,
                "completed scan publications must remain within the configured queue bound");

        auto cancelled = scans.request_scan(first, assetRoot, ProjectScanOptions{.recursive = true,
                                                                                 .maxFiles = 100});
        cancelled.cancel();
        scans.wait_for_idle();
        results = scans.take_ready(first.generation);
        require(results.size() == 1 && results.front().cancelled,
                "cancelled scans must publish a cancellation result without retaining owner state");
    }

    context.set_roots(projectRoot, assetRoot);
    require(context.snapshot().generation == first.generation,
            "unchanged canonical roots must not create a new project generation");
}
