#pragma once

#include <cstddef>
#include <vector>

#include <SDL3_mixer/SDL_mixer.h>

class Audio;
class AudioWrapper;

class AudioManager
{
  public:
    explicit AudioManager(bool headless);
    ~AudioManager();
    AudioManager(const AudioManager &) = delete;
    AudioManager &operator=(const AudioManager &) = delete;
    AudioManager(AudioManager &&) = delete;
    AudioManager &operator=(AudioManager &&) = delete;

    MIX_Mixer *getMixer() const noexcept;
    bool setPaused(bool paused) noexcept;

    bool playAndForget(AudioWrapper *audioWrapper, float gain = 1.0f, int loopCount = 0);
    static bool playTrackWithLoops(MIX_Track *track, int loopCount) noexcept;
    void update(bool fixedStepDelta);
    void clearTransientAudio();
    void scheduleReplay(Audio *audio);
    void cancelReplay(Audio *audio);

  private:
    struct TransientAudioPlayback
    {
        MIX_Track *track;
        AudioWrapper *audioWrapper;
    };

    void close();
    void releaseTransientAudioPlayback(size_t index);

    MIX_Mixer *mixer = nullptr;
    bool mixerInitialized = false;
    std::vector<TransientAudioPlayback> transientAudioPlaybacks;
    std::vector<Audio *> pendingAudioReplays;
};
