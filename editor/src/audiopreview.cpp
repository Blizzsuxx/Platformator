#include "audiopreview.h"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <system_error>
#include <utility>

namespace platformator_editor
{
bool is_supported_audio_extension(std::string_view extension) noexcept
{
    std::string lower(extension);
    for (char &ch : lower)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return lower == ".wav" || lower == ".ogg" || lower == ".mp3" || lower == ".flac";
}

struct AudioPreviewService::Impl
{
    enum class RequestStage
    {
        Prepare,
        Load,
        Warm
    };

    struct Request
    {
        std::uint64_t id = 0;
        std::filesystem::path path;
        std::filesystem::path requestedPath;
        RequestStage stage = RequestStage::Prepare;
    };

    struct Result
    {
        std::uint64_t id = 0;
        std::filesystem::path path;
        std::filesystem::path requestedPath;
        MIX_Audio *audio = nullptr;
        bool needsMixer = false;
        bool warmOnly = false;
        bool mixerFailure = false;
        std::string error;
    };

    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    bool shuttingDown = false;
    std::uint64_t latestRequest = 0;
    std::optional<Request> queued;
    std::optional<Result> ready;
    MIX_Mixer *mixer = nullptr;
    MIX_Track *track = nullptr;
    MIX_Audio *audio = nullptr;
    bool mixInitAttempted = false;
    bool mixInitDone = false;
    bool mixerReady = false;
    std::string mixInitError;
    std::function<void()> wakeCallback;
    bool warmQueued = false;

    void release_audio()
    {
        if (track != nullptr)
            MIX_SetTrackAudio(track, nullptr);
        if (audio != nullptr)
        {
            MIX_DestroyAudio(audio);
            audio = nullptr;
        }
    }

    void destroy()
    {
        if (track != nullptr)
        {
            MIX_StopTrack(track, 0);
            MIX_SetTrackAudio(track, nullptr);
            MIX_DestroyTrack(track);
            track = nullptr;
        }
        if (audio != nullptr)
        {
            MIX_DestroyAudio(audio);
            audio = nullptr;
        }
        if (mixer != nullptr)
        {
            MIX_DestroyMixer(mixer);
            mixer = nullptr;
        }
        if (mixInitDone)
        {
            MIX_Quit();
            mixInitDone = false;
        }
    }

    void run()
    {
        while (true)
        {
            Request request;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return shuttingDown || queued.has_value(); });
                if (shuttingDown)
                    return;
                request = std::move(*queued);
                queued.reset();
            }

            Result result;
            result.id = request.id;
            result.path = std::move(request.path);
            result.requestedPath = std::move(request.requestedPath);
            if (request.stage == RequestStage::Prepare || request.stage == RequestStage::Warm)
            {
                result.warmOnly = request.stage == RequestStage::Warm;
                std::error_code error;
                if (request.stage == RequestStage::Prepare)
                {
                    result.path = std::filesystem::weakly_canonical(result.path, error);
                    if (error || !std::filesystem::is_regular_file(result.path, error) || error)
                        result.error = "Audio file is not available: " + result.requestedPath.string();
                }
                if (result.error.empty())
                {
                    bool mixerReady = false;
                    {
                        std::lock_guard lock(mutex);
                        mixerReady = this->mixerReady;
                    }
                    if (!mixerReady)
                    {
                        if (!mixInitAttempted)
                        {
                            mixInitAttempted = true;
                            if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
                                mixInitError = std::string("Audio subsystem unavailable: ") + SDL_GetError();
                            else if (!MIX_Init())
                                mixInitError = std::string("SDL_mixer initialization failed: ") + SDL_GetError();
                            else
                                mixInitDone = true;
                        }
                        if (mixInitDone)
                            result.needsMixer = true;
                        else
                        {
                            result.error = mixInitError.empty()
                                ? "SDL_mixer initialization failed." : mixInitError;
                            result.mixerFailure = true;
                        }
                    }
                    else
                    {
                        if (request.stage == RequestStage::Prepare)
                        {
                            request.stage = RequestStage::Load;
                            request.path = result.path;
                        }
                    }
                }
            }

            if (result.error.empty() && !result.needsMixer && request.stage == RequestStage::Load)
            {
                MIX_Mixer *mixer = nullptr;
                {
                    std::lock_guard lock(mutex);
                    mixer = this->mixer;
                }
                if (mixer == nullptr)
                    result.error = "Audio mixer is not ready.";
                else
                {
                    const auto encoded = result.path.u8string();
                    const std::string utf8Path(reinterpret_cast<const char *>(encoded.data()), encoded.size());
                    result.audio = MIX_LoadAudio(mixer, utf8Path.c_str(), false);
                    if (result.audio == nullptr)
                        result.error = std::string("Failed to load audio asset: ") + SDL_GetError();
                }
            }

            if (request.stage == RequestStage::Load && result.error.empty() && result.audio == nullptr)
                result.error = "Audio preview setup did not produce an audio stream.";

            bool publishResult = false;
            std::function<void()> wakeCallback;
            {
                std::lock_guard lock(mutex);
                publishResult = !shuttingDown && request.id == latestRequest;
                if (publishResult)
                {
                    if (ready && ready->audio != nullptr)
                        MIX_DestroyAudio(ready->audio);
                    ready = std::move(result);
                    wakeCallback = this->wakeCallback;
                }
            }
            if (!publishResult && result.audio != nullptr)
                MIX_DestroyAudio(result.audio);
            else if (wakeCallback)
                wakeCallback();

            if (request.stage == RequestStage::Prepare && result.error.empty() && !result.needsMixer)
            {
                std::lock_guard lock(mutex);
                if (!shuttingDown && request.id == latestRequest)
                {
                    queued = Request{request.id, result.path, result.requestedPath, RequestStage::Load};
                    wake.notify_one();
                }
            }
        }
    }
};

AudioPreviewService::AudioPreviewService() : impl_(std::make_unique<Impl>())
{
    impl_->worker = std::thread([state = impl_.get()] { state->run(); });
}

bool AudioPreviewService::warm_queued_for_testing() const noexcept
{
    if (!impl_)
        return false;
    std::lock_guard lock(impl_->mutex);
    return impl_->warmQueued;
}

AudioPreviewService::~AudioPreviewService()
{
    shutdown();
}

void AudioPreviewService::set_wake_callback(std::function<void()> callback)
{
    if (!impl_)
        return;
    std::lock_guard lock(impl_->mutex);
    impl_->wakeCallback = std::move(callback);
}

void AudioPreviewService::warm()
{
    if (!impl_)
        return;
    std::lock_guard lock(impl_->mutex);
    if (impl_->shuttingDown || impl_->mixerReady || impl_->warmQueued)
        return;
    impl_->warmQueued = true;
    const std::uint64_t id = ++impl_->latestRequest;
    impl_->queued = Impl::Request{id, {}, {}, Impl::RequestStage::Warm};
    impl_->wake.notify_one();
}

bool AudioPreviewService::toggle_or_play(const std::filesystem::path &audioPath)
{
    if (audioPath.empty() || !is_supported_audio_extension(audioPath.extension().string()))
    {
        lastError_ = "Unsupported audio format: " + audioPath.extension().string();
        return false;
    }

    const auto requested = audioPath.lexically_normal();
    if ((state_ == AudioPlaybackState::Loading || state_ == AudioPlaybackState::Playing) &&
        requestedPath_ && *requestedPath_ == requested)
    {
        stop();
        return true;
    }

    if (impl_->track != nullptr)
        MIX_StopTrack(impl_->track, 0);
    impl_->release_audio();
    activePath_ = requested;
    requestedPath_ = requested;
    state_ = AudioPlaybackState::Loading;
    lastError_.clear();

    {
        std::lock_guard lock(impl_->mutex);
        impl_->warmQueued = false;
        const std::uint64_t id = ++impl_->latestRequest;
        if (impl_->ready && impl_->ready->audio != nullptr)
            MIX_DestroyAudio(impl_->ready->audio);
        impl_->ready.reset();
        impl_->queued = Impl::Request{id, requested, requested, Impl::RequestStage::Prepare};
    }
    impl_->wake.notify_one();
    return true;
}

void AudioPreviewService::stop()
{
    if (impl_)
    {
        std::lock_guard lock(impl_->mutex);
        impl_->warmQueued = false;
        ++impl_->latestRequest;
        impl_->queued.reset();
        if (impl_->ready && impl_->ready->audio != nullptr)
            MIX_DestroyAudio(impl_->ready->audio);
        impl_->ready.reset();
    }
    if (impl_ && impl_->track != nullptr)
        MIX_StopTrack(impl_->track, 0);
    if (impl_)
        impl_->release_audio();
    activePath_.reset();
    requestedPath_.reset();
    state_ = AudioPlaybackState::Stopped;
}

void AudioPreviewService::poll()
{
    std::optional<Impl::Result> ready;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->ready)
        {
            ready = std::move(impl_->ready);
            impl_->ready.reset();
        }
    }
    if (ready)
    {
        bool current = false;
        bool mixerReady = false;
        {
            std::lock_guard lock(impl_->mutex);
            current = ready->id == impl_->latestRequest && !impl_->shuttingDown;
            mixerReady = impl_->mixerReady;
        }
        if (!current)
        {
            if (ready->audio != nullptr)
                MIX_DestroyAudio(ready->audio);
        }
        else if (!ready->error.empty())
        {
            lastError_ = std::move(ready->error);
            mixerAvailable_ = !ready->mixerFailure;
            activePath_.reset();
            requestedPath_.reset();
            state_ = AudioPlaybackState::Stopped;
            if (ready->warmOnly)
            {
                std::lock_guard lock(impl_->mutex);
                impl_->warmQueued = false;
            }
        }
        else if (ready->needsMixer && !mixerReady)
        {
            // SDL_mixer requires device-backed mixer creation on the main thread.
            lastError_.clear();
            impl_->mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
            if (impl_->mixer == nullptr)
            {
                lastError_ = std::string("No audio playback device available: ") + SDL_GetError();
                mixerAvailable_ = false;
            }
            else
            {
                impl_->track = MIX_CreateTrack(impl_->mixer);
                if (impl_->track == nullptr)
                {
                    lastError_ = std::string("Failed to create audio preview track: ") + SDL_GetError();
                    mixerAvailable_ = false;
                    MIX_DestroyMixer(impl_->mixer);
                    impl_->mixer = nullptr;
                }
            }
            if (!lastError_.empty())
            {
                activePath_.reset();
                requestedPath_.reset();
                state_ = AudioPlaybackState::Stopped;
                std::lock_guard lock(impl_->mutex);
                impl_->warmQueued = false;
            }
            else
            {
                {
                    std::lock_guard lock(impl_->mutex);
                    impl_->mixerReady = true;
                    impl_->warmQueued = false;
                    if (!ready->warmOnly)
                        impl_->queued = Impl::Request{ready->id, ready->path,
                                                      ready->requestedPath, Impl::RequestStage::Load};
                }
                if (!ready->warmOnly)
                    impl_->wake.notify_one();
            }
        }
        else if (ready->audio != nullptr)
        {
            if (impl_->track == nullptr || !MIX_SetTrackAudio(impl_->track, ready->audio))
            {
                lastError_ = std::string("Failed to bind audio preview track: ") + SDL_GetError();
                MIX_DestroyAudio(ready->audio);
                activePath_.reset();
                requestedPath_.reset();
                state_ = AudioPlaybackState::Stopped;
            }
            else if (!MIX_PlayTrack(impl_->track, 0))
            {
                lastError_ = std::string("Failed to play audio preview: ") + SDL_GetError();
                MIX_SetTrackAudio(impl_->track, nullptr);
                MIX_DestroyAudio(ready->audio);
                activePath_.reset();
                requestedPath_.reset();
                state_ = AudioPlaybackState::Stopped;
            }
            else
            {
                impl_->audio = ready->audio;
                activePath_ = std::move(ready->path);
                requestedPath_ = std::move(ready->requestedPath);
                state_ = AudioPlaybackState::Playing;
                mixerAvailable_ = true;
                lastError_.clear();
            }
        }
    }

    if (state_ == AudioPlaybackState::Playing && impl_->track != nullptr &&
        !MIX_TrackPlaying(impl_->track))
    {
        impl_->release_audio();
        activePath_.reset();
        requestedPath_.reset();
        state_ = AudioPlaybackState::Stopped;
    }
}

void AudioPreviewService::shutdown()
{
    if (!impl_)
        return;
    {
        std::lock_guard lock(impl_->mutex);
        impl_->shuttingDown = true;
        ++impl_->latestRequest;
        impl_->queued.reset();
    }
    impl_->wake.notify_one();
    if (impl_->worker.joinable())
        impl_->worker.join();
    if (impl_->ready && impl_->ready->audio != nullptr)
        MIX_DestroyAudio(impl_->ready->audio);
    impl_->ready.reset();
    stop();
    impl_->destroy();
}

bool AudioPreviewService::is_playing(const std::filesystem::path &audioPath) const noexcept
{
    return state_ == AudioPlaybackState::Playing && requestedPath_ &&
        *requestedPath_ == audioPath.lexically_normal();
}

std::int64_t AudioPreviewService::mixer_output_position_frames() const noexcept
{
    if (!impl_ || impl_->track == nullptr)
        return -1;
    return static_cast<std::int64_t>(MIX_GetTrackPlaybackPosition(impl_->track));
}
} // namespace platformator_editor
