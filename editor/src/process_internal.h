#pragma once

#include "process.h"

#include <chrono>
#include <sys/types.h>

namespace platformator_editor
{
struct ProcessState
{
    pid_t pid = -1;
    pid_t processGroup = -1;
    int standardOutputFd = -1;
    int standardErrorFd = -1;
    ProcessOutputMode outputMode = ProcessOutputMode::Merged;
    std::uint64_t generation = 0;
    ProcessService::OutputCallback output;
    ProcessService::CompletionCallback completion;
    bool childReaped = false;
    int waitStatus = 0;
    bool stopRequested = false;
    bool killSent = false;
    std::chrono::steady_clock::time_point killDeadline{};
    std::string ioError;
};

bool process_start_posix(ProcessState &state, ProcessRequest request, std::uint64_t generation,
                         ProcessService::OutputCallback output,
                         ProcessService::CompletionCallback completion);
void process_poll_posix(ProcessState &state);
void process_stop_posix(ProcessState &state);
void process_force_cleanup_posix(ProcessState &state);
} // namespace platformator_editor
