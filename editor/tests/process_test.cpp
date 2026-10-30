#include "process.h"
#include "behaviorcatalog.h"
#include "test_support.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#ifndef PLATFORMATOR_EDITOR_PROCESS_FIXTURE
#error "PLATFORMATOR_EDITOR_PROCESS_FIXTURE must name the test fixture executable"
#endif
#ifndef PLATFORMATOR_EDITOR_INVALID_CATALOG_FIXTURE
#error "PLATFORMATOR_EDITOR_INVALID_CATALOG_FIXTURE must name the malformed catalog fixture"
#endif

using namespace platformator_editor;
using namespace platformator_editor_test;
using namespace std::chrono_literals;

namespace
{
struct Capture
{
    std::string merged;
    std::string standardOutput;
    std::string standardError;
    bool completed = false;
    ProcessCompletion result;

    ProcessService::OutputCallback output()
    {
        return [this](ProcessOutput event) {
            if (event.stream == ProcessStream::Merged)
                merged += event.bytes;
            else if (event.stream == ProcessStream::Stdout)
                standardOutput += event.bytes;
            else
                standardError += event.bytes;
        };
    }

    ProcessService::CompletionCallback completion()
    {
        return [this](ProcessCompletion event) {
            completed = true;
            result = std::move(event);
        };
    }
};

ProcessRequest fixture_request(std::initializer_list<std::string> arguments,
                               const std::filesystem::path &workingDirectory,
                               ProcessOutputMode outputMode = ProcessOutputMode::Merged)
{
    ProcessRequest request;
    request.argv = {PLATFORMATOR_EDITOR_PROCESS_FIXTURE};
    request.argv.insert(request.argv.end(), arguments.begin(), arguments.end());
    request.workingDirectory = workingDirectory;
    request.outputMode = outputMode;
    return request;
}

void pump_until(ProcessService &process, Capture &capture, std::chrono::milliseconds timeout = 5000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!capture.completed && std::chrono::steady_clock::now() < deadline)
    {
        const auto started = std::chrono::steady_clock::now();
        process.poll();
        require(std::chrono::steady_clock::now() - started < 100ms,
                "process polling must not wait for child output or completion");
        if (!capture.completed)
            std::this_thread::sleep_for(1ms);
    }
    require(capture.completed, "child process did not complete before the test deadline");
}

bool process_is_live_or_non_zombie(pid_t pid)
{
    if (pid <= 0)
        return false;
    if (::kill(pid, 0) != 0 && errno == ESRCH)
        return false;
    std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!std::getline(stat, line))
        return false;
    const auto close = line.rfind(')');
    if (close == std::string::npos || close + 2 >= line.size())
        return true;
    return line[close + 2] != 'Z';
}

void test_argv_working_directory_and_generation()
{
    TemporaryDirectory temp;
    const auto cwd = temp.path() / "project with spaces";
    std::filesystem::create_directories(cwd);
    ProcessService process;
    Capture capture;
    require(process.start(fixture_request({"args", "two words", "雪"}, cwd), 41,
                          capture.output(), capture.completion()),
            "process should start with argv and cwd");
    pump_until(process, capture);
    require(capture.result.launched && capture.result.exitCode == 0,
            "fixture should exit successfully");
    require(capture.result.generation == 41, "completion must retain its request generation");
    require(capture.merged.find("cwd:" + cwd.string()) != std::string::npos,
            "child must run in the requested working directory");
    require(capture.merged.find("arg:two words\n") != std::string::npos,
            "argv elements containing spaces must remain a single argument");
    require(capture.merged.find("arg:雪\n") != std::string::npos,
            "UTF-8 argv elements must be preserved");
}

void test_large_output_is_drained_without_blocking()
{
    TemporaryDirectory temp;
    ProcessService process;
    Capture capture;
    require(process.start(fixture_request({"large"}, temp.path()), 42, capture.output(),
                          capture.completion()),
            "large-output fixture should start");
    pump_until(process, capture);
    require(capture.result.exitCode == 0, "large-output fixture should exit successfully");
    require(capture.merged.size() >= 2 * 1024 * 1024,
            "nonblocking polling must drain output larger than the kernel pipe buffer");
    require(capture.merged.ends_with("large-output-complete\n"),
            "the final output marker must be retained");
}

void test_merged_and_separate_stream_modes()
{
    TemporaryDirectory temp;
    {
        ProcessService process;
        Capture capture;
        require(process.start(fixture_request({"streams"}, temp.path(), ProcessOutputMode::Merged),
                              43, capture.output(), capture.completion()),
                "merged-stream fixture should start");
        pump_until(process, capture);
        require(capture.merged.find("stdout-marker\n") != std::string::npos &&
                    capture.merged.find("stderr-marker\n") != std::string::npos,
                "merged mode must preserve both child streams");
        require(capture.standardOutput.empty() && capture.standardError.empty(),
                "merged mode must identify one merged stream");
    }
    {
        ProcessService process;
        Capture capture;
        require(process.start(fixture_request({"streams"}, temp.path(), ProcessOutputMode::Separate),
                              44, capture.output(), capture.completion()),
                "separate-stream fixture should start");
        pump_until(process, capture);
        require(capture.standardOutput == "stdout-marker\n",
                "catalog stdout must remain separate from diagnostics");
        require(capture.standardError == "stderr-marker\n",
                "catalog stderr must remain separately captured");
    }
}

void test_failed_spawn_and_nonzero_exit()
{
    TemporaryDirectory temp;
    {
        ProcessService process;
        Capture capture;
        ProcessRequest request;
        request.argv = {(temp.path() / "missing-executable").string()};
        request.workingDirectory = temp.path();
        require(!process.start(std::move(request), 45, capture.output(), capture.completion()),
                "missing executable should fail to spawn");
        require(capture.completed && !capture.result.launched && !capture.result.error.empty(),
                "spawn failure must complete with an actionable error");
        require(!process.running(), "failed spawn must not leave an owned process running");
    }
    {
        ProcessService process;
        Capture capture;
        require(process.start(fixture_request({"fail"}, temp.path()), 46, capture.output(),
                              capture.completion()),
                "nonzero fixture should spawn");
        pump_until(process, capture);
        require(capture.result.launched && capture.result.exitCode == 23,
                "nonzero child status must be reported without being treated as spawn failure");
    }
}

void test_group_ownership_stop_reaping_and_repeat_stop()
{
    TemporaryDirectory temp;
    ProcessService process;
    Capture capture;
    require(process.start(fixture_request({"grandchild"}, temp.path()), 47, capture.output(),
                          capture.completion()),
            "grandchild fixture should start");
    const pid_t processId = static_cast<pid_t>(process.process_id());
    require(processId > 0, "service must expose the owned process ID for diagnostics");
    const auto outputDeadline = std::chrono::steady_clock::now() + 2s;
    while (capture.merged.find("grandchild:") == std::string::npos &&
           std::chrono::steady_clock::now() < outputDeadline)
    {
        process.poll();
        std::this_thread::sleep_for(1ms);
    }
    const auto marker = capture.merged.find("grandchild:");
    require(marker != std::string::npos, "fixture must publish its descendant ID");
    const auto end = capture.merged.find('\n', marker);
    const pid_t grandchild = static_cast<pid_t>(std::stol(capture.merged.substr(marker + 11, end - marker - 11)));
    require(grandchild > 0, "fixture must publish a valid descendant ID");
    require(::getpgid(processId) == processId, "child must own a fresh process group");
    require(::getpgid(grandchild) == processId, "grandchild must inherit the owned process group");

    process.stop();
    process.stop();
    pump_until(process, capture, 4s);
    require(capture.result.generation == 47, "stop completion must retain the operation generation");
    require(!process_is_live_or_non_zombie(grandchild),
            "stopping the owned group must terminate its grandchild");
    int status = 0;
    errno = 0;
    require(::waitpid(processId, &status, WNOHANG) == -1 && errno == ECHILD,
            "service must reap its direct child before publishing completion");
}

void test_stop_escalates_after_the_grace_period()
{
    TemporaryDirectory temp;
    ProcessService process;
    Capture capture;
    require(process.start(fixture_request({"ignore-term"}, temp.path()), 48, capture.output(),
                          capture.completion()),
            "signal-ignoring fixture should start");
    const auto outputDeadline = std::chrono::steady_clock::now() + 2s;
    while (capture.merged.find("pid:") == std::string::npos &&
           std::chrono::steady_clock::now() < outputDeadline)
    {
        process.poll();
        std::this_thread::sleep_for(1ms);
    }
    require(capture.merged.find("pid:") != std::string::npos,
            "signal-ignoring fixture must be ready before Stop");
    const auto stoppedAt = std::chrono::steady_clock::now();
    process.stop();
    pump_until(process, capture, 4s);
    const auto elapsed = std::chrono::steady_clock::now() - stoppedAt;
    require(capture.result.terminatingSignal == SIGKILL,
            "process group must be killed when TERM does not stop it");
    require(elapsed >= 2s, "SIGKILL escalation must wait for the two-second TERM grace period");
}

void pump_until(GamePipeline &pipeline, std::chrono::milliseconds timeout = 5000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (pipeline.busy() && std::chrono::steady_clock::now() < deadline)
    {
        pipeline.poll();
        std::this_thread::sleep_for(1ms);
    }
    pipeline.poll();
    require(!pipeline.busy(), "pipeline fixture operation must finish before the deadline");
}

PipelineIdentity pipeline_identity(const std::filesystem::path &project,
                                   const std::filesystem::path &executable)
{
    return PipelineIdentity{std::filesystem::canonical(project).string(), "debug", "fixture",
                            std::filesystem::canonical(executable).string(), "fixture-binary-v1",
                            "fixture-source-v1"};
}

PipelineRequest pipeline_request(const std::filesystem::path &project,
                                 const std::filesystem::path &executable,
                                 std::string_view configureMode = "args",
                                 std::string_view buildMode = "args")
{
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    const auto scenePath = project / "scene with spaces.scene";
    std::ofstream(scenePath) << "[]";
    PipelineRequest request;
    request.projectRoot = std::filesystem::canonical(project);
    request.assetRoot = std::filesystem::canonical(assets);
    request.savedScenePath = scenePath;
    request.executablePath = executable;
    request.configureArgv = {executable.string(), std::string(configureMode), "configure-snapshot"};
    request.buildArgv = {executable.string(), std::string(buildMode), "build-snapshot"};
    request.runArguments = {"args", "launch-snapshot"};
    request.scene = Json::array();
    request.identity = pipeline_identity(project, executable);
    request.catalogProtocol = "common-runner-v1";
    return request;
}

void test_pipeline_run_snapshot_and_stages()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project with spaces";
    std::filesystem::create_directories(project);
    const std::filesystem::path executable(PLATFORMATOR_EDITOR_PROCESS_FIXTURE);
    auto request = pipeline_request(project, executable);
    const auto expectedScene = request.savedScenePath.string();
    const auto expectedAssets = request.assetRoot.string();
    GamePipeline pipeline;
    BehaviorCatalog catalog;
    require(pipeline.start(request, catalog, 101), "a declared compatible target should start the pipeline");
    request.runArguments.push_back("changed-after-start");
    request.scene = Json::array({Json{{"name", "changed-after-start"}}});
    require(!pipeline.start(request, catalog, 102), "a pipeline must reject duplicate concurrent requests");
    pump_until(pipeline);

    const auto &status = pipeline.status();
    require(status.outcome == PipelineOutcome::Succeeded && status.generation == 101,
            "configure/build/catalog/launch should finish with the original operation generation");
    const std::vector<PipelineStage> expectedStages{
        PipelineStage::Configuring, PipelineStage::Building, PipelineStage::QueryingCatalog,
        PipelineStage::Launching, PipelineStage::Running, PipelineStage::Idle};
    require(status.transitions == expectedStages,
            "the run path must follow configure, build, query, launch, running, and idle in order");
    require(catalog.can_automatic_run(), "a current catalog from the built fixture must certify availability");
    const auto &console = pipeline.console().text();
    require(console.find("arg:configure-snapshot") != std::string::npos &&
                console.find("arg:build-snapshot") != std::string::npos &&
                console.find("arg:launch-snapshot") != std::string::npos &&
                console.find("arg:changed-after-start") == std::string::npos,
            "configure/build/run settings must come from the immutable operation snapshot");
    require(console.find("arg:--scene\n") != std::string::npos &&
                console.find("arg:" + expectedScene + "\n") != std::string::npos &&
                console.find("arg:--asset-root\n") != std::string::npos &&
                console.find("arg:" + expectedAssets + "\n") != std::string::npos,
            "launch argv must include the saved scene and explicit asset root as separate arguments");
}

void test_pipeline_build_only_failures_and_stop_recovery()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    std::filesystem::create_directories(project);
    const std::filesystem::path executable(PLATFORMATOR_EDITOR_PROCESS_FIXTURE);
    GamePipeline pipeline;
    BehaviorCatalog catalog;

    auto buildOnly = pipeline_request(project, executable);
    buildOnly.buildOnly = true;
    buildOnly.catalogProtocol = "unavailable";
    require(pipeline.start(buildOnly, catalog, 201),
            "build-only must remain available when catalog protocol is unavailable");
    pump_until(pipeline);
    require(pipeline.status().outcome == PipelineOutcome::Succeeded &&
                pipeline.status().transitions == std::vector<PipelineStage>{
                    PipelineStage::Configuring, PipelineStage::Building, PipelineStage::Idle} &&
                !catalog.can_automatic_run(),
            "build-only must end after Building without querying or launching");

    auto failedBuild = pipeline_request(project, executable, "args", "fail");
    require(pipeline.start(failedBuild, catalog, 202), "a new operation should start after build-only");
    pump_until(pipeline);
    require(pipeline.status().outcome == PipelineOutcome::Failed &&
                pipeline.console().text().find("arg:--scene") == std::string::npos,
            "a failed build must not launch a previously built executable");

    auto stopped = pipeline_request(project, executable, "grandchild", "args");
    require(pipeline.start(stopped, catalog, 203), "pipeline should accept an operation after failure");
    const auto readyDeadline = std::chrono::steady_clock::now() + 2s;
    while (pipeline.console().text().find("grandchild:") == std::string::npos &&
           std::chrono::steady_clock::now() < readyDeadline)
    {
        pipeline.poll();
        std::this_thread::sleep_for(1ms);
    }
    require(pipeline.busy(), "stop fixture should be active before cancellation");
    pipeline.stop();
    pump_until(pipeline, 4s);
    require(pipeline.status().outcome == PipelineOutcome::Cancelled &&
                pipeline.status().stage == PipelineStage::Idle,
            "Stop must cancel owned work and release the pipeline for later requests");
    auto recovered = pipeline_request(project, executable);
    recovered.buildOnly = true;
    require(pipeline.start(recovered, catalog, 204), "pipeline must accept work after Stop");
    pump_until(pipeline);
    require(pipeline.status().outcome == PipelineOutcome::Succeeded,
            "a successful operation must follow cancellation without stale busy state");
}

void test_pipeline_behavior_verification_and_identity_invalidation()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    std::filesystem::create_directories(project);
    const std::filesystem::path executable(PLATFORMATOR_EDITOR_PROCESS_FIXTURE);
    GamePipeline pipeline;
    BehaviorCatalog catalog;

    auto missingBehavior = pipeline_request(project, executable);
    missingBehavior.scene = Json::array({Json{{"components", Json::array({
        Json{{"type", 5}, {"behaviors", Json::array({Json{{"type", "Missing"}}})}}})}}});
    require(pipeline.start(missingBehavior, catalog, 301), "verification pipeline should start");
    pump_until(pipeline);
    require(pipeline.status().outcome == PipelineOutcome::Failed &&
                pipeline.status().transitions.back() == PipelineStage::Idle &&
                pipeline.console().text().find("arg:--scene") == std::string::npos,
            "a behavior absent from the newly queried catalog must block launch");

    auto currentRun = pipeline_request(project, executable);
    require(pipeline.start(currentRun, catalog, 302), "a valid behavior-free scene should run");
    pump_until(pipeline);
    require(catalog.state() == CatalogState::Current, "successful verification must establish current identity");

    auto changedWhileBuilding = pipeline_request(project, executable, "grandchild", "args");
    require(pipeline.start(changedWhileBuilding, catalog, 303),
            "new user operation may start from the current identity");
    auto changedIdentity = changedWhileBuilding.identity;
    changedIdentity.sourceInputIdentity = "changed-during-operation";
    changedIdentity.target = "different-target";
    pipeline.update_identity(changedIdentity);
    pump_until(pipeline, 4s);
    require(pipeline.status().outcome == PipelineOutcome::Failed &&
                catalog.state() == CatalogState::Stale,
            "source identity changes must invalidate the catalog and stop the frozen operation without retry");
}

void test_custom_entry_boundary_and_false_protocol_declaration()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "custom game";
    std::filesystem::create_directories(project);
    const auto sourcePath = project / "main.cpp";
    const std::string source = "int main() { return 0; }\n";
    std::ofstream(sourcePath) << source;
    GamePipeline pipeline;
    BehaviorCatalog catalog;

    auto customBuild = pipeline_request(project, std::filesystem::path(PLATFORMATOR_EDITOR_PROCESS_FIXTURE));
    customBuild.catalogProtocol = "unavailable";
    customBuild.buildOnly = true;
    require(pipeline.start(customBuild, catalog, 401),
            "an undeclared custom entry must remain buildable without catalog support");
    pump_until(pipeline);
    std::ifstream unchangedStream(sourcePath);
    const std::string unchanged((std::istreambuf_iterator<char>(unchangedStream)),
                                std::istreambuf_iterator<char>());
    require(unchanged == source && pipeline.status().transitions == std::vector<PipelineStage>{
                PipelineStage::Configuring, PipelineStage::Building, PipelineStage::Idle} &&
                pipeline.console().text().find("arg:--editor-catalog") == std::string::npos,
            "custom-entry Build-only must preserve source and skip catalog query/launch");

    auto falselyDeclared = pipeline_request(project,
        std::filesystem::path(PLATFORMATOR_EDITOR_INVALID_CATALOG_FIXTURE));
    require(pipeline.start(falselyDeclared, catalog, 402),
            "an explicitly declared target may be queried for protocol verification");
    pump_until(pipeline);
    require(pipeline.status().outcome == PipelineOutcome::Failed &&
                pipeline.status().transitions == std::vector<PipelineStage>{
                    PipelineStage::Configuring, PipelineStage::Building,
                    PipelineStage::QueryingCatalog, PipelineStage::Idle} &&
                pipeline.console().text().find("arg:--scene") == std::string::npos,
            "a false compatibility declaration must fail its catalog query before gameplay launch");
    std::ifstream stillUnchangedStream(sourcePath);
    const std::string stillUnchanged((std::istreambuf_iterator<char>(stillUnchangedStream)),
                                     std::istreambuf_iterator<char>());
    require(stillUnchanged == source,
            "catalog compatibility checks must never rewrite a custom game's source");
}

void test_bounded_console()
{
    BoundedConsole console;
    console.append(std::string(BoundedConsole::maximumBytes + 128, 'o'));
    require(console.truncated() && console.text().size() <= BoundedConsole::maximumBytes &&
                console.text().find("[console truncated at 1 MiB]") != std::string::npos,
            "console must cap retained output at one MiB with an explicit truncation marker");
}

void test_pipeline_catalog_limits()
{
    const PipelineIdentity identity{"/project", "debug", "fixture", "/fixture", "binary", "sources"};
    const std::string validCatalog =
        R"({"schema_version":1,"behaviors":[{"name":"Allowed","field_schema_available":true,"fields":[]}]})";
    BehaviorCatalog timeout;
    timeout.configure(CatalogProtocol::CommonRunnerV1,
                      CatalogIdentity{identity.canonicalProjectPath, identity.profile, identity.target,
                                      identity.executablePath, identity.binaryIdentity, identity.sourceInputIdentity});
    require(timeout.begin_query() && timeout.append_stdout(validCatalog) &&
                !timeout.finish_query(0, 10001ms) && timeout.state() == CatalogState::Unavailable,
            "catalog verification must reject queries exceeding ten seconds");

    BehaviorCatalog oversized;
    oversized.configure(CatalogProtocol::CommonRunnerV1,
                        CatalogIdentity{identity.canonicalProjectPath, identity.profile, identity.target,
                                        identity.executablePath, identity.binaryIdentity, identity.sourceInputIdentity});
    require(oversized.begin_query() &&
                !oversized.append_stdout(std::string(4U * 1024U * 1024U + 1U, 'x')) &&
                !oversized.finish_query(0, 100ms) &&
                oversized.last_error().find("four MiB") != std::string::npos,
            "catalog stdout/stderr must share a four MiB cap");
}

void run_live_pipeline(const std::filesystem::path &projectRoot,
                       const std::filesystem::path &assetRoot,
                       const std::filesystem::path &scenePath,
                       const std::filesystem::path &executablePath,
                       std::vector<std::string> configureArgv,
                       std::vector<std::string> buildArgv,
                       std::string target, std::string profile)
{
    std::ifstream sceneStream(scenePath);
    require(static_cast<bool>(sceneStream), "live pipeline scene must be readable");
    PipelineRequest request;
    request.projectRoot = std::filesystem::canonical(projectRoot);
    request.assetRoot = std::filesystem::canonical(assetRoot);
    request.savedScenePath = std::filesystem::canonical(scenePath);
    request.executablePath = executablePath;
    request.configureArgv = std::move(configureArgv);
    request.buildArgv = std::move(buildArgv);
    request.scene = Json::parse(sceneStream);
    request.catalogProtocol = "common-runner-v1";
    request.identity = PipelineIdentity{request.projectRoot.string(), profile, target,
                                        request.executablePath.string(), {}, "live-source-scan"};

    GamePipeline pipeline;
    BehaviorCatalog catalog;
    require(pipeline.start(std::move(request), catalog, 700),
            "live project pipeline must accept the configured build and scene snapshot");
    const auto runDeadline = std::chrono::steady_clock::now() + 180s;
    while (pipeline.busy() && pipeline.status().stage != PipelineStage::Running &&
           std::chrono::steady_clock::now() < runDeadline)
    {
        pipeline.poll();
        std::this_thread::sleep_for(2ms);
    }
    require(pipeline.status().stage == PipelineStage::Running,
            "live runner must configure, build, verify its catalog and reach Running; output: " +
                pipeline.status().message + "\n" + pipeline.console().text());
    require(catalog.can_automatic_run(), "live build must publish a current verified catalog");
    pipeline.stop();
    pump_until(pipeline, 5s);
    require(pipeline.status().outcome == PipelineOutcome::Cancelled &&
                pipeline.status().transitions == std::vector<PipelineStage>{
                    PipelineStage::Configuring, PipelineStage::Building, PipelineStage::QueryingCatalog,
                    PipelineStage::Launching, PipelineStage::Running, PipelineStage::Idle},
            "Stop must terminate the live game's owned process and return the pipeline to Idle");
}

int run_live_integrations(const std::filesystem::path &repositoryRoot)
{
    const auto root = std::filesystem::canonical(repositoryRoot);
    run_live_pipeline(root, root / "assets", root / "assets/scenes/default.scene",
                      root / "bin/debug/main",
                      {"cmake", "--preset", "debug"},
                      {"cmake", "--build", "--preset", "debug", "--target", "main"},
                      "main", "debug");
    const auto marioRoot = root / "examples/mario";
    const auto marioBuild = root / "bin/mario-catalog";
    const auto marioAssets = marioRoot / "assets";
    run_live_pipeline(marioRoot, marioAssets, marioRoot / "level1.scene",
                      marioBuild / "mario_example",
                      {"cmake", "-S", marioRoot.string(), "-B", marioBuild.string(),
                       "-DCMAKE_BUILD_TYPE=Debug"},
                      {"cmake", "--build", marioBuild.string(), "--target", "mario_example"},
                      "mario_example", "debug");
    std::cout << "live shared-runner and Mario configure/build/catalog/launch/Stop paths passed\n";
    return 0;
}
} // namespace

int main(int argc, char **argv)
{
    try
    {
        if (argc == 3 && std::string(argv[1]) == "--live-integrations")
            return run_live_integrations(argv[2]);
        if (argc != 1)
        {
            std::cerr << "usage: platformator_editor_process_test [--live-integrations REPOSITORY_ROOT]\n";
            return 2;
        }
        test_argv_working_directory_and_generation();
        test_large_output_is_drained_without_blocking();
        test_merged_and_separate_stream_modes();
        test_failed_spawn_and_nonzero_exit();
        test_group_ownership_stop_reaping_and_repeat_stop();
        test_stop_escalates_after_the_grace_period();
        test_pipeline_run_snapshot_and_stages();
        test_pipeline_build_only_failures_and_stop_recovery();
        test_pipeline_behavior_verification_and_identity_invalidation();
        test_custom_entry_boundary_and_false_protocol_declaration();
        test_bounded_console();
        test_pipeline_catalog_limits();
    }
    catch (const std::exception &error)
    {
        std::cerr << "platformator_editor_process_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
