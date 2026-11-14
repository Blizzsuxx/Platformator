#include "process.h"

#include "behaviorcatalog.h"
#include "process_internal.h"
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
#include "playsession.h"
#if defined(_WIN32)
#include "platform/sessionchannel_windows.h"
#else
#include "platform/sessionchannel_posix.h"
#endif
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <set>
#include <string>
#include <utility>

namespace platformator_editor
{
namespace
{
constexpr std::string_view kConsoleTruncationMarker = "\n[console truncated at 1 MiB]\n";

std::string path_to_utf8(const std::filesystem::path &path)
{
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char *>(encoded.data()), encoded.size());
}

std::string pipeline_stage_name(PipelineStage stage)
{
    switch (stage)
    {
    case PipelineStage::Configuring: return "Configure";
    case PipelineStage::Building: return "Build";
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
    ++revision_;
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
    ++revision_;
}

const std::string &BoundedConsole::text() const noexcept
{
    return text_;
}

bool BoundedConsole::truncated() const noexcept
{
    return truncated_;
}

std::uint64_t BoundedConsole::revision() const noexcept
{
    return revision_;
}

struct GamePipeline::Impl
{
    ProcessService process;
    PipelineRequest request;
    PipelineStatus status;
    BoundedConsole console;
    BehaviorCatalog *catalog = nullptr;
    PipelineIdentity currentIdentity;
    bool active = false;
    bool stopRequested = false;
    bool identityInvalidated = false;
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
    PlaySession playSession;
    std::string mirroredSessionStdout;
    std::string mirroredSessionStderr;
#endif

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

    void finish(PipelineOutcome outcome, std::string message, bool preserveCatalog = false)
    {
        if (!active)
            return;
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
        if (request.embeddedGameView && playSession.state() != PlaySessionState::Idle &&
            !playSession.process_running())
        {
            if (playSession.state() != PlaySessionState::Stopping)
                playSession.request_stop();
            if (playSession.child_reaped())
                playSession.finish_stop_after_reap();
        }
#endif
        if (!preserveCatalog && outcome != PipelineOutcome::Succeeded && catalog != nullptr &&
            (status.stage == PipelineStage::Configuring || status.stage == PipelineStage::Building ||
             identityInvalidated))
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
        console.append(output.bytes);
    }

    void fail(std::string message, bool preserveCatalog = false)
    {
        console.append("\n[Error] ");
        console.append(message);
        console.append("\n");
        finish(PipelineOutcome::Failed, std::move(message), preserveCatalog);
    }

    void start_build()
    {
        enter(PipelineStage::Building);
        start_process(request.buildArgv, ProcessOutputMode::Merged);
    }

    void prepare_launch_and_start()
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
        request.identity.executablePath = path_to_utf8(canonicalExecutable);
        request.identity.binaryIdentity = std::to_string(binarySize) + ":" +
            std::to_string(binaryModified.time_since_epoch().count());
        currentIdentity = request.identity;
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
        if (request.embeddedGameView && !playSession.finalize_pipeline_identity(request.identity))
        {
            fail("play session could not validate the built target: " + playSession.last_error());
            return;
        }
#endif
        start_launch();
    }

    bool scene_behaviors_are_registered(std::string &error) const
    {
        if (catalog == nullptr || !catalog->snapshot())
            return true;
        const auto allowed = catalog_behavior_names(catalog->snapshot()->document);
        std::set<std::string> used;
        collect_behavior_names(request.scene, used);
        for (const auto &name : used)
        {
            if (!allowed.contains(name))
            {
                error = "scene behavior '" + name + "' is not defined in scanned project sources";
                return false;
            }
        }
        return true;
    }

    void start_launch()
    {
        if (catalog == nullptr || catalog->state() != CatalogState::Current || !catalog->snapshot())
        {
            fail("behavior catalog is not current; refresh project schemas before Run");
            return;
        }
        std::string error;
        if (!scene_behaviors_are_registered(error))
        {
            fail(std::move(error), true);
            return;
        }
        enter(PipelineStage::Launching);
        std::vector<std::string> argv;
        argv.reserve(request.runArguments.size() + 6);
        argv.push_back(path_to_utf8(request.executablePath));
        argv.insert(argv.end(), request.runArguments.begin(), request.runArguments.end());
        argv.emplace_back("--scene");
        argv.push_back(path_to_utf8(request.savedScenePath));
        argv.emplace_back("--asset-root");
        argv.push_back(path_to_utf8(request.assetRoot));
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
        if (request.embeddedGameView)
        {
            if (!request.sceneDocumentIdentity || request.sceneSavedFileIdentity.empty())
            {
                fail("embedded Game view requires a complete immutable saved-scene identity");
                return;
            }
#if defined(_WIN32)
            auto provider = make_windows_session_channel_provider();
#else
            auto provider = make_posix_session_channel_provider();
#endif
            std::string channelError;
            auto channels = provider == nullptr ? nullptr
                                                : provider->create_pair(
                                                      session_frame_mapping_limit,
                                                      channelError);
            if (!channels)
            {
                fail(channelError.empty()
                    ? "could not create local Game session channels"
                    : "could not create local Game session channels: " + channelError);
                return;
            }
            PlaySessionLaunchIdentity launchIdentity{
                *request.sceneDocumentIdentity, request.sceneSavedHistoryToken,
                request.sceneSavedFileIdentity, request.identity};
            if (!playSession.matches(launchIdentity) ||
                playSession.state() != PlaySessionState::Building)
            {
                fail("play session launch identity changed before process startup");
                return;
            }
            ProcessRequest sessionRequest;
            sessionRequest.argv = std::move(argv);
            sessionRequest.workingDirectory = request.projectRoot;
            sessionRequest.outputMode = ProcessOutputMode::Separate;
            status.command = display_command(sessionRequest.argv);
            if (!playSession.start_process(std::move(sessionRequest), std::move(channels)))
            {
                const auto message = playSession.last_error().empty()
                    ? "embedded Game process could not be started"
                    : playSession.last_error();
                if (playSession.child_reaped())
                    playSession.finish_stop_after_reap();
                fail(message);
                return;
            }
            mirroredSessionStdout.clear();
            mirroredSessionStderr.clear();
            status.message = "embedded game is starting";
            return;
        }
#endif
        if (start_process(std::move(argv), ProcessOutputMode::Merged) && active)
        {
            enter(PipelineStage::Running);
            const auto ownershipNote = native_process_containment_note();
            if (ownershipNote.empty())
                status.message = "game process is running";
            else
            {
                status.message = "game process is running; " + std::string(ownershipNote);
                console.append("[Process ownership] ");
                console.append(ownershipNote);
                console.append("\n");
            }
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
                finish(PipelineOutcome::Succeeded, "build completed");
            else
                prepare_launch_and_start();
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
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
        if (impl_->request.embeddedGameView && impl_->playSession.state() != PlaySessionState::Idle)
            impl_->playSession.request_stop();
#endif
        impl_->process.stop();
    }
}

bool GamePipeline::start(PipelineRequest request, BehaviorCatalog &catalog, std::uint64_t generation)
{
    auto &impl = *impl_;
    if (impl.active || impl.process.running())
        return false;
    impl.console.clear();
    if (request.projectRoot.empty() || request.assetRoot.empty() ||
        request.buildArgv.empty() || request.executablePath.empty())
    {
        impl.status = PipelineStatus{PipelineStage::Idle, PipelineOutcome::Failed, generation,
                                     "pipeline request is missing a root or command", {}};
        return false;
    }
    if (!request.buildOnly && request.savedScenePath.empty())
    {
        impl.status = PipelineStatus{PipelineStage::Idle, PipelineOutcome::Failed, generation,
                                     "automatic Run requires a saved scene", {}};
        return false;
    }
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
    if (request.embeddedGameView && (request.buildOnly || !request.sceneDocumentIdentity ||
                                     request.sceneSavedFileIdentity.empty()))
    {
        impl.status = PipelineStatus{PipelineStage::Idle, PipelineOutcome::Failed, generation,
                                     "embedded Game view requires a saved scene launch identity", {}};
        return false;
    }
#else
    if (request.embeddedGameView)
    {
        impl.status = PipelineStatus{PipelineStage::Idle, PipelineOutcome::Failed, generation,
                                     "this editor build does not include the opt-in editor-session feature", {}};
        return false;
    }
#endif

    if (request.identity.canonicalProjectPath.empty())
        request.identity.canonicalProjectPath = path_to_utf8(request.projectRoot);
    if (request.identity.profile.empty())
        request.identity.profile = "debug";
    if (request.identity.executablePath.empty())
        request.identity.executablePath = path_to_utf8(request.executablePath);
    impl.request = std::move(request);
    impl.catalog = &catalog;
    impl.currentIdentity = impl.request.identity;
    impl.status = PipelineStatus{};
    impl.status.generation = generation;
    impl.status.workingDirectory = impl.request.projectRoot;
    impl.active = true;
    impl.stopRequested = false;
    impl.identityInvalidated = false;
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
    if (impl.request.embeddedGameView)
    {
        const auto &identity = *impl.request.sceneDocumentIdentity;
        PlaySessionLaunchIdentity launchIdentity{identity, impl.request.sceneSavedHistoryToken,
            impl.request.sceneSavedFileIdentity, impl.request.identity};
        if (!impl.playSession.begin_configuring(std::move(launchIdentity)) ||
            !impl.playSession.begin_building())
        {
            impl.status = PipelineStatus{PipelineStage::Idle, PipelineOutcome::Failed, generation,
                impl.playSession.last_error().empty()
                    ? "play session could not begin build" : impl.playSession.last_error(), {}};
            return false;
        }
        impl.mirroredSessionStdout.clear();
        impl.mirroredSessionStderr.clear();
    }
#endif
    if (impl.request.configureArgv.empty())
    {
        impl.enter(PipelineStage::Building);
        return impl.start_process(impl.request.buildArgv, ProcessOutputMode::Merged);
    }
    impl.enter(PipelineStage::Configuring);
    return impl.start_process(impl.request.configureArgv, ProcessOutputMode::Merged);
}

void GamePipeline::poll()
{
    auto &impl = *impl_;
    if (!impl.active)
        return;
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
    if (impl.request.embeddedGameView &&
        (impl.status.stage == PipelineStage::Launching || impl.status.stage == PipelineStage::Running))
    {
        impl.playSession.poll_process();
        const auto mirror = [](const BoundedConsole &source, std::string &previous,
                               BoundedConsole &destination) {
            const auto &current = source.text();
            if (current.size() >= previous.size() && current.starts_with(previous))
                destination.append(std::string_view(current).substr(previous.size()));
            else if (current != previous)
                destination.append("\n[session output continued after bounded transcript truncation]\n");
            previous = current;
        };
        mirror(impl.playSession.stdout_log(), impl.mirroredSessionStdout, impl.console);
        mirror(impl.playSession.stderr_log(), impl.mirroredSessionStderr, impl.console);
        if (impl.playSession.state() == PlaySessionState::Running ||
            impl.playSession.state() == PlaySessionState::Paused)
        {
            if (impl.status.stage != PipelineStage::Running)
                impl.enter(PipelineStage::Running);
            impl.status.message = impl.playSession.state() == PlaySessionState::Paused
                ? "embedded game is paused" : "embedded game is running";
        }
        if ((impl.playSession.state() == PlaySessionState::Stopping ||
             impl.playSession.state() == PlaySessionState::Failed) &&
            impl.playSession.child_reaped())
        {
            const std::string error = impl.playSession.last_error();
            impl.playSession.finish_stop_after_reap();
            if (impl.stopRequested)
                impl.finish(PipelineOutcome::Cancelled, "operation stopped");
            else if (impl.identityInvalidated)
                impl.fail("project, source, target, or executable identity changed during the embedded game session");
            else
                impl.fail(error.empty() ? "embedded game process exited unexpectedly" : error);
        }
        return;
    }
#endif
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
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
    if (impl.request.embeddedGameView &&
        (impl.status.stage == PipelineStage::Launching || impl.status.stage == PipelineStage::Running))
    {
        impl.playSession.request_stop();
        return;
    }
#endif
    if (impl.process.running())
        impl.process.stop();
    else
        impl.finish(PipelineOutcome::Cancelled, "operation stopped");
}

void GamePipeline::update_identity(PipelineIdentity identity)
{
    auto &impl = *impl_;
    impl.currentIdentity = std::move(identity);
    if (impl.active && impl.currentIdentity != impl.request.identity)
    {
        impl.identityInvalidated = true;
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
        if (impl.request.embeddedGameView &&
            (impl.status.stage == PipelineStage::Launching || impl.status.stage == PipelineStage::Running))
        {
            impl.playSession.request_stop();
            return;
        }
#endif
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

BoundedConsole &GamePipeline::console() noexcept
{
    return impl_->console;
}

const BoundedConsole &GamePipeline::console() const noexcept
{
    return impl_->console;
}

bool GamePipeline::busy() const noexcept
{
    return impl_ && impl_->active;
}

ProcessService::ProcessService() : impl_(std::make_unique<ProcessState>())
{
    impl_->native = make_native_process();
}

ProcessService::~ProcessService()
{
    if (impl_ && impl_->native)
        impl_->native->shutdown();
}

bool ProcessService::start(ProcessRequest request, std::uint64_t generation, OutputCallback output,
                           CompletionCallback completion)
{
    return impl_ && impl_->native &&
           impl_->native->start(std::move(request), generation, std::move(output),
                                std::move(completion));
}

void ProcessService::poll()
{
    if (impl_ && impl_->native)
        impl_->native->poll();
}

void ProcessService::stop()
{
    if (impl_ && impl_->native)
        impl_->native->stop();
}

bool ProcessService::running() const noexcept
{
    return impl_ && impl_->native && impl_->native->running();
}

int ProcessService::process_id() const noexcept
{
    return impl_ && impl_->native ? impl_->native->process_id() : -1;
}

PlaySession *GamePipeline::play_session() noexcept
{
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
    return impl_ != nullptr && impl_->request.embeddedGameView ? &impl_->playSession : nullptr;
#else
    return nullptr;
#endif
}

const PlaySession *GamePipeline::play_session() const noexcept
{
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
    return impl_ != nullptr && impl_->request.embeddedGameView ? &impl_->playSession : nullptr;
#else
    return nullptr;
#endif
}
} // namespace platformator_editor
