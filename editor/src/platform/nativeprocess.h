#pragma once

#include "process.h"

#include <memory>
#include <string_view>

namespace platformator_editor
{
// OS-specific implementations own all native IDs, handles, pipes, and wait state.
// The editor process service sees only requests, bounded output, and completion records.
class NativeProcess
{
  public:
    virtual ~NativeProcess() = default;
    virtual bool start(ProcessRequest request, std::uint64_t generation,
                       ProcessService::OutputCallback output,
                       ProcessService::CompletionCallback completion) = 0;
    virtual void poll() = 0;
    virtual void stop() = 0;
    virtual void shutdown() noexcept = 0;
    virtual bool running() const noexcept = 0;
    virtual int process_id() const noexcept = 0;
};

std::unique_ptr<NativeProcess> make_native_process();
std::string_view native_process_containment_note() noexcept;
} // namespace platformator_editor
