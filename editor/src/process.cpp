#include "process.h"

#include "behaviorcatalog.h"
#include "process_internal.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <utility>

namespace platformator_editor
{
namespace
{
constexpr std::chrono::milliseconds kPipelineCatalogTimeout{10000};
constexpr std::string_view kConsoleTruncationMarker = "\n[console truncated at 1 MiB]\n";

CatalogIdentity catalog_identity(const PipelineIdentity &identity)
{
    return CatalogIdentity{identity.canonicalProjectPath, identity.profile, identity.target,
                           identity.executablePath, identity.binaryIdentity,
                           identity.sourceInputIdentity};
}

CatalogProtocol catalog_protocol(std::string_view protocol)
{
    return protocol == "common-runner-v1" ? CatalogProtocol::CommonRunnerV1
                                           : CatalogProtocol::Unavailable;
}

std::string pipeline_stage_name(PipelineStage stage)
{
    switch (stage)
    {
    case PipelineStage::Configuring: return "Configure";
    case PipelineStage::Building: return "Build";
    case PipelineStage::QueryingCatalog: return "Catalog";
    case PipelineStage::Launching: return "Launch";
    case PipelineStage::Running: return "Game";
    case PipelineStage::Idle: return "Idle";
    }
    return "Operation";
}

std::string display_command(const std::vector<std::string> &argv)
{
    std::string result;
    for (const auto &argument : argv)
    {
        if (!result.empty())
            result.push_back(' ');
        const bool quote = argument.find_first_of(" \t\n\"") != std::string::npos;
        if (quote)
            result.push_back('"');
        for (const char byte : argument)
        {
            if (quote && byte == '"')
                result.push_back('\\');
            result.push_back(byte);
        }
        if (quote)
            result.push_back('"');
    }
    return result;
}

void collect_behavior_names(const Json &scene, std::set<std::string> &names)
{
    std::vector<const Json *> pending{&scene};
    while (!pending.empty())
    {
        const Json *value = pending.back();
        pending.pop_back();
        if (value->is_array())
        {
            for (const auto &entry : *value)
                pending.push_back(&entry);
            continue;
        }
        if (!value->is_object())
            continue;
        const auto components = value->find("components");
        if (components != value->end() && components->is_array())
        {
            for (const auto &component : *components)
            {
                if (!component.is_object() || component.value("type", -1) != 5)
                    continue;
                const auto behaviors = component.find("behaviors");
                if (behaviors == component.end() || !behaviors->is_array())
                    continue;
                for (const auto &behavior : *behaviors)
                    if (behavior.is_object() && behavior.contains("type") && behavior["type"].is_string())
                        names.insert(behavior["type"].get<std::string>());
            }
        }
        const auto children = value->find("children");
        if (children != value->end() && children->is_array())
            for (const auto &child : *children)
                pending.push_back(&child);
    }
}

std::set<std::string> catalog_behavior_names(const Json &document)
{
    std::set<std::string> names;
    const auto behaviors = document.find("behaviors");
    if (behaviors != document.end() && behaviors->is_array())
        for (const auto &behavior : *behaviors)
            if (behavior.is_object() && behavior.contains("name") && behavior["name"].is_string())
                names.insert(behavior["name"].get<std::string>());
    return names;
}
} // namespace

void BoundedConsole::append(std::string_view bytes)
{
    if (truncated_ || bytes.empty())
        return;
    if (bytes.size() <= maximumBytes - text_.size())
    {
        text_.append(bytes.data(), bytes.size());
        return;
    }

    const std::size_t contentLimit = maximumBytes - kConsoleTruncationMarker.size();
    if (text_.size() > contentLimit)
        text_.resize(contentLimit);
    const std::size_t available = contentLimit - text_.size();
    text_.append(bytes.data(), std::min(available, bytes.size()));
    text_.append(kConsoleTruncationMarker.data(), kConsoleTruncationMarker.size());
    truncated_ = true;
}

void BoundedConsole::clear() noexcept
{
    text_.clear();
    truncated_ = false;
}

const std::string &BoundedConsole::text() const noexcept
{
    return text_;
}

bool BoundedConsole::truncated() const noexcept
{
    return truncated_;
}

struct GamePipeline::Impl
{
    ProcessService process;
    PipelineRequest request;
    PipelineStatus status;
    BoundedConsole console;
    BehaviorCatalog *catalog = nullptr;
    PipelineIdentity currentIdentity;
    std::chrono::steady_clock::time_point catalogStartedAt{};
    bool active = false;
    bool stopRequested = false;
    bool identityInvalidated = false;
    bool catalogQueryActive = false;
    bool catalogTimedOut = false;

    void enter(PipelineStage stage)
    {
        status.stage = stage;
        status.transitions.push_back(stage);
        if (stage != PipelineStage::Idle)
        {
            console.append("\n[");
            console.append(pipeline_stage_name(stage));
            console.append("]\n");
        }
    }

    void finish(PipelineOutcome outcome, std::string message)
    {
        if (!active)
            return;
        if (catalogQueryActive && catalog != nullptr)
        {
            catalog->invalidate(message.empty() ? "catalog query was cancelled" : message);
            catalogQueryActive = false;
        }
        if (outcome != PipelineOutcome::Succeeded && catalog != nullptr &&
            (status.stage == PipelineStage::Configuring || status.stage == PipelineStage::Building ||
             status.stage == PipelineStage::QueryingCatalog || identityInvalidated))
            catalog->invalidate(message.empty() ? "pipeline did not verify the current build" : message);
        status.outcome = outcome;
        status.message = std::move(message);
        active = false;
        stopRequested = false;
        enter(PipelineStage::Idle);
    }

    bool start_process(std::vector<std::string> argv, ProcessOutputMode outputMode)
    {
        status.command = display_command(argv);
        ProcessRequest processRequest;
        processRequest.argv = std::move(argv);
        processRequest.workingDirectory = request.projectRoot;
        processRequest.outputMode = outputMode;
        const auto generation = status.generation;
        const bool started = process.start(
            std::move(processRequest), generation,
            [this](ProcessOutput output) { on_output(std::move(output)); },
            [this](ProcessCompletion completion) { on_completion(std::move(completion)); });
        if (!started && active)
            fail("could not start " + pipeline_stage_name(status.stage) + " process");
        return started;
    }

    void on_output(ProcessOutput output)
    {
        if (!active || output.generation != status.generation)
            return;
        if (status.stage == PipelineStage::QueryingCatalog && catalog != nullptr)
        {
            const bool accepted = output.stream == ProcessStream::Stderr
                                      ? catalog->append_stderr(output.bytes)
                                      : catalog->append_stdout(output.bytes);
            if (!accepted && process.running())
                process.stop();
            if (output.stream == ProcessStream::Stderr)
            {
                console.append("[Catalog] ");
                console.append(output.bytes);
            }
            return;
        }
        console.append(output.bytes);
    }

    void fail(std::string message)
    {
        console.append("\n[Error] ");
        console.append(message);
        console.append("\n");
        finish(PipelineOutcome::Failed, std::move(message));
    }

    void start_build()
    {
        enter(PipelineStage::Building);
        start_process(request.buildArgv, ProcessOutputMode::Merged);
    }

    void start_catalog_query()
    {
        std::error_code identityError;
        const auto canonicalExecutable = std::filesystem::canonical(request.executablePath, identityError);
        if (identityError || !std::filesystem::is_regular_file(canonicalExecutable))
        {
            fail("the configured build target did not produce the selected executable");
            return;
        }
        const auto binarySize = std::filesystem::file_size(canonicalExecutable, identityError);
        if (identityError)
        {
            fail("could not read the built executable identity: " + identityError.message());
            return;
        }
        const auto binaryModified = std::filesystem::last_write_time(canonicalExecutable, identityError);
        if (identityError)
        {
            fail("could not read the built executable identity: " + identityError.message());
            return;
        }
        request.executablePath = canonicalExecutable;
        request.identity.executablePath = canonicalExecutable.string();
        request.identity.binaryIdentity = std::to_string(binarySize) + ":" +
            std::to_string(binaryModified.time_since_epoch().count());
        currentIdentity = request.identity;
        if (catalog != nullptr)
            catalog->configure(catalog_protocol(request.catalogProtocol), catalog_identity(request.identity));
        if (catalog == nullptr || !catalog->begin_query())
        {
            fail(catalog == nullptr ? "behavior catalog is unavailable" : catalog->last_error());
            return;
        }
        catalogQueryActive = true;
        catalogTimedOut = false;
        catalogStartedAt = std::chrono::steady_clock::now();
        enter(PipelineStage::QueryingCatalog);
        start_process({request.executablePath.string(), "--editor-catalog"}, ProcessOutputMode::Separate);
    }

    bool scene_behaviors_are_registered(std::string &error) const
    {
        if (catalog == nullptr || !catalog->snapshot())
        {
            error = "the built executable has no current behavior catalog";
            return false;
        }
        const auto allowed = catalog_behavior_names(catalog->snapshot()->document);
        std::set<std::string> used;
        collect_behavior_names(request.scene, used);
        for (const auto &name : used)
        {
            if (!allowed.contains(name))
            {
                error = "scene behavior '" + name + "' is not registered by the built target";
                return false;
            }
        }
        return true;
    }

    void start_launch()
    {
        std::string error;
        if (!scene_behaviors_are_registered(error))
        {
            fail(std::move(error));
            return;
        }
        enter(PipelineStage::Launching);
        std::vector<std::string> argv;
        argv.reserve(request.runArguments.size() + 6);
        argv.push_back(request.executablePath.string());
        argv.insert(argv.end(), request.runArguments.begin(), request.runArguments.end());
        argv.emplace_back("--scene");
        argv.push_back(request.savedScenePath.string());
        argv.emplace_back("--asset-root");
        argv.push_back(request.assetRoot.string());
        if (start_process(std::move(argv), ProcessOutputMode::Merged) && active)
        {
            enter(PipelineStage::Running);
            status.message = "game process is running";
        }
    }

    void on_completion(ProcessCompletion completion)
    {
        if (!active || completion.generation != status.generation)
            return;
        if (identityInvalidated)
        {
            fail("project, source, target, or executable identity changed during the operation; run again to verify it");
            return;
        }
        if (stopRequested)
        {
            finish(PipelineOutcome::Cancelled, "operation stopped");
            return;
        }
        if (status.stage == PipelineStage::QueryingCatalog)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - catalogStartedAt);
            const bool current = catalog != nullptr &&
                catalog->finish_query(completion.launched ? completion.exitCode : -1,
                                      elapsed, catalogTimedOut);
            catalogQueryActive = false;
            if (!current)
            {
                fail(catalog == nullptr ? "behavior catalog is unavailable" : catalog->last_error());
                return;
            }
            start_launch();
            return;
        }
        if (!completion.launched || completion.exitCode != 0)
        {
            std::string message = completion.error;
            if (message.empty())
                message = pipeline_stage_name(status.stage) + " process exited with status " +
                          std::to_string(completion.exitCode);
            fail(std::move(message));
            return;
        }

        switch (status.stage)
        {
        case PipelineStage::Configuring:
            start_build();
            break;
        case PipelineStage::Building:
            if (request.buildOnly)
            {
                if (catalog != nullptr)
                    catalog->invalidate("build-only completed; run must query the resulting executable again");
                finish(PipelineOutcome::Succeeded, "build completed");
            }
            else
                start_catalog_query();
            break;
        case PipelineStage::QueryingCatalog:
            break;
        case PipelineStage::Launching:
        case PipelineStage::Running:
            finish(PipelineOutcome::Succeeded, "game process exited successfully");
            break;
        case PipelineStage::Idle:
            break;
        }
    }
};

GamePipeline::GamePipeline() : impl_(std::make_unique<Impl>())
{
}

GamePipeline::~GamePipeline()
{
    if (impl_ && impl_->active)
    {
        if (impl_->catalogQueryActive && impl_->catalog != nullptr)
            impl_->catalog->invalidate("application closed while catalog verification was active");
        impl_->process.stop();
    }
}

bool GamePipeline::start(PipelineRequest request, BehaviorCatalog &catalog, std::uint64_t generation)
{
    auto &impl = *impl_;
    if (impl.active || impl.process.running())
        return false;
    impl.console.clear();
    if (request.projectRoot.empty() || request.assetRoot.empty() || request.configureArgv.empty() ||
        request.buildArgv.empty() || request.executablePath.empty())
    {
        impl.status = PipelineStatus{PipelineStage::Idle, PipelineOutcome::Failed, generation,
                                     "pipeline request is missing a root or command", {}};
        return false;
    }
    if (!request.buildOnly && (request.catalogProtocol != "common-runner-v1" ||
                               request.savedScenePath.empty()))
    {
        impl.status = PipelineStatus{PipelineStage::Idle, PipelineOutcome::Failed, generation,
                                     "automatic Run requires a saved scene and common-runner-v1 catalog support", {}};
        return false;
    }

    if (request.identity.canonicalProjectPath.empty())
        request.identity.canonicalProjectPath = request.projectRoot.string();
    if (request.identity.profile.empty())
        request.identity.profile = "debug";
    if (request.identity.executablePath.empty())
        request.identity.executablePath = request.executablePath.string();
    impl.request = std::move(request);
    impl.catalog = &catalog;
    impl.currentIdentity = impl.request.identity;
    catalog.configure(catalog_protocol(impl.request.catalogProtocol),
                      catalog_identity(impl.request.identity));
    impl.status = PipelineStatus{};
    impl.status.generation = generation;
    impl.active = true;
    impl.stopRequested = false;
    impl.identityInvalidated = false;
    impl.catalogQueryActive = false;
    impl.catalogTimedOut = false;
    impl.enter(PipelineStage::Configuring);
    return impl.start_process(impl.request.configureArgv, ProcessOutputMode::Merged);
}

void GamePipeline::poll()
{
    auto &impl = *impl_;
    if (!impl.active)
        return;
    if (impl.status.stage == PipelineStage::QueryingCatalog &&
        std::chrono::steady_clock::now() - impl.catalogStartedAt > kPipelineCatalogTimeout)
    {
        impl.catalogTimedOut = true;
        if (impl.process.running())
            impl.process.stop();
    }
    if (impl.identityInvalidated && !impl.process.running())
    {
        impl.fail("project, source, target, or executable identity changed during the operation; run again to verify it");
        return;
    }
    impl.process.poll();
}

void GamePipeline::stop()
{
    auto &impl = *impl_;
    if (!impl.active)
        return;
    impl.stopRequested = true;
    if (impl.process.running())
        impl.process.stop();
    else
        impl.finish(PipelineOutcome::Cancelled, "operation stopped");
}

void GamePipeline::update_identity(PipelineIdentity identity)
{
    auto &impl = *impl_;
    impl.currentIdentity = std::move(identity);
    if (impl.catalog != nullptr)
    {
        impl.catalog->configure(catalog_protocol(impl.request.catalogProtocol),
                               catalog_identity(impl.currentIdentity));
    }
    if (impl.active && impl.currentIdentity != impl.request.identity)
    {
        impl.identityInvalidated = true;
        if (impl.process.running())
            impl.process.stop();
        else
            impl.fail("project, source, target, or executable identity changed during the operation; run again to verify it");
    }
}

const PipelineStatus &GamePipeline::status() const noexcept
{
    return impl_->status;
}

const BoundedConsole &GamePipeline::console() const noexcept
{
    return impl_->console;
}

bool GamePipeline::busy() const noexcept
{
    return impl_ && impl_->active;
}

ProcessService::ProcessService() : impl_(new ProcessState)
{
}

ProcessService::~ProcessService()
{
    if (impl_ == nullptr)
        return;
    impl_->output = {};
    impl_->completion = {};
    process_stop_posix(*impl_);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (impl_->pid > 0 && std::chrono::steady_clock::now() < deadline)
    {
        process_poll_posix(*impl_);
        if (impl_->pid > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (impl_->pid > 0)
        process_force_cleanup_posix(*impl_);
    delete impl_;
    impl_ = nullptr;
}

bool ProcessService::start(ProcessRequest request, std::uint64_t generation, OutputCallback output,
                           CompletionCallback completion)
{
    return process_start_posix(*impl_, std::move(request), generation, std::move(output),
                               std::move(completion));
}

void ProcessService::poll()
{
    process_poll_posix(*impl_);
}

void ProcessService::stop()
{
    process_stop_posix(*impl_);
}

bool ProcessService::running() const noexcept
{
    return impl_ != nullptr && impl_->pid > 0;
}

int ProcessService::process_id() const noexcept
{
    return impl_ != nullptr && impl_->pid > 0 ? static_cast<int>(impl_->pid) : -1;
}
} // namespace platformator_editor
