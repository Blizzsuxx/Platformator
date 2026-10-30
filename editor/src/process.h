#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "document.h"

namespace platformator_editor
{
enum class ProcessOutputMode
{
    Merged,
    Separate
};

enum class ProcessStream
{
    Merged,
    Stdout,
    Stderr
};

struct ProcessRequest
{
    std::vector<std::string> argv;
    std::filesystem::path workingDirectory;
    ProcessOutputMode outputMode = ProcessOutputMode::Merged;
};

struct ProcessOutput
{
    std::uint64_t generation = 0;
    ProcessStream stream = ProcessStream::Merged;
    std::string bytes;
};

struct ProcessCompletion
{
    std::uint64_t generation = 0;
    bool launched = false;
    int exitCode = -1;
    int terminatingSignal = 0;
    std::string error;
};

struct ProcessState;

class ProcessService
{
  public:
    using OutputCallback = std::function<void(ProcessOutput)>;
    using CompletionCallback = std::function<void(ProcessCompletion)>;

    ProcessService();
    ~ProcessService();
    ProcessService(const ProcessService &) = delete;
    ProcessService &operator=(const ProcessService &) = delete;

    bool start(ProcessRequest request, std::uint64_t generation, OutputCallback output,
               CompletionCallback completion);
    void poll();
    void stop();

    bool running() const noexcept;
    int process_id() const noexcept;

  private:
    ProcessState *impl_ = nullptr;
};

class BehaviorCatalog;

struct PipelineIdentity
{
    std::string canonicalProjectPath;
    std::string profile;
    std::string target;
    std::string executablePath;
    std::string binaryIdentity;
    std::string sourceInputIdentity;

    bool operator==(const PipelineIdentity &) const = default;
};

struct PipelineRequest
{
    std::filesystem::path projectRoot;
    std::filesystem::path assetRoot;
    std::filesystem::path savedScenePath;
    std::filesystem::path executablePath;
    std::vector<std::string> configureArgv;
    std::vector<std::string> buildArgv;
    std::vector<std::string> runArguments;
    Json scene = Json::array();
    PipelineIdentity identity;
    std::string catalogProtocol = "unavailable";
    bool buildOnly = false;
};

enum class PipelineStage
{
    Idle,
    Configuring,
    Building,
    QueryingCatalog,
    Launching,
    Running
};

enum class PipelineOutcome
{
    None,
    Succeeded,
    Failed,
    Cancelled
};

struct PipelineStatus
{
    PipelineStage stage = PipelineStage::Idle;
    PipelineOutcome outcome = PipelineOutcome::None;
    std::uint64_t generation = 0;
    std::string message;
    std::string command;
    std::vector<PipelineStage> transitions;
};

class BoundedConsole
{
  public:
    static constexpr std::size_t maximumBytes = 1024U * 1024U;

    void append(std::string_view bytes);
    void clear() noexcept;
    const std::string &text() const noexcept;
    bool truncated() const noexcept;

  private:
    std::string text_;
    bool truncated_ = false;
};

class GamePipeline
{
  public:
    GamePipeline();
    ~GamePipeline();
    GamePipeline(const GamePipeline &) = delete;
    GamePipeline &operator=(const GamePipeline &) = delete;

    bool start(PipelineRequest request, BehaviorCatalog &catalog, std::uint64_t generation);
    void poll();
    void stop();
    void update_identity(PipelineIdentity identity);
    const PipelineStatus &status() const noexcept;
    const BoundedConsole &console() const noexcept;
    bool busy() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platformator_editor
