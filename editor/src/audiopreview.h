#pragma once

#include <filesystem>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace platformator_editor
{
enum class AudioPlaybackState
{
    Stopped,
    Loading,
    Playing,
    Paused
};

bool is_supported_audio_extension(std::string_view extension) noexcept;

class AudioPreviewService
{
  public:
    AudioPreviewService();
    ~AudioPreviewService();
    AudioPreviewService(const AudioPreviewService &) = delete;
    AudioPreviewService &operator=(const AudioPreviewService &) = delete;

    bool toggle_or_play(const std::filesystem::path &audioPath);
    void set_wake_callback(std::function<void()> callback);
    void warm();
    void stop();
    void poll();
    void shutdown();

    AudioPlaybackState state() const noexcept { return state_; }
    const std::optional<std::filesystem::path> &active_path() const noexcept { return activePath_; }
    bool is_playing(const std::filesystem::path &audioPath) const noexcept;
    std::int64_t mixer_output_position_frames() const noexcept;
    bool mixer_available() const noexcept { return mixerAvailable_; }
    const std::string &last_error() const noexcept { return lastError_; }

  private:
    friend struct AudioPreviewServiceTestProbe;
    bool warm_queued_for_testing() const noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::optional<std::filesystem::path> activePath_;
    std::optional<std::filesystem::path> requestedPath_;
    AudioPlaybackState state_ = AudioPlaybackState::Stopped;
    bool mixerAvailable_ = true;
    std::string lastError_;
};
} // namespace platformator_editor
