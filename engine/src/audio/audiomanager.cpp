#include "audio/audiomanager.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

#include <SDL3/SDL.h>

#include "platformator/audio.h"
#include "platformator/audiowrapper.h"
#include "platformator/gameobject.h"

AudioManager::AudioManager(bool headless) : mixer(nullptr), mixerInitialized(false)
{
    if (headless)
    {
        return;
    }

    if (!MIX_Init())
    {
        std::printf("SDL_mixer could not initialize! SDL_mixer Error: %s", SDL_GetError());
        throw std::runtime_error(std::string("Failed to initialize SDL_mixer: ") + SDL_GetError());
    }
    mixerInitialized = true;

    mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (mixer == nullptr)
    {
        std::printf("SDL_mixer could not create a mixer device! SDL_mixer Error: %s", SDL_GetError());
        close();
        throw std::runtime_error(std::string("Failed to create SDL_mixer device: ") + SDL_GetError());
    }
}

AudioManager::~AudioManager()
{
    close();
}

void AudioManager::close()
{
    clearTransientAudio();
    pendingAudioReplays.clear();

    if (mixer != nullptr)
    {
        MIX_DestroyMixer(mixer);
        mixer = nullptr;
    }

    if (mixerInitialized)
    {
        MIX_Quit();
        mixerInitialized = false;
    }
}

MIX_Mixer *AudioManager::getMixer() const noexcept
{
    return mixer;
}

bool AudioManager::setPaused(bool paused) noexcept
{
    if (mixer == nullptr)
    {
        return true;
    }

    const auto device = static_cast<SDL_AudioDeviceID>(
        SDL_GetNumberProperty(MIX_GetMixerProperties(mixer), MIX_PROP_MIXER_DEVICE_NUMBER, 0));
    // Suspend this mixer's logical device, keeping individual track states
    // intact. This also freezes fades and tracks started during game pause.
    if (device == 0)
    {
        return SDL_SetError("Game mixer has no audio playback device");
    }
    return paused ? SDL_PauseAudioDevice(device) : SDL_ResumeAudioDevice(device);
}

bool AudioManager::playTrackWithLoops(MIX_Track *track, int loopCount) noexcept
{
    SDL_PropertiesID options = 0;
    if (loopCount != 0)
    {
        options = SDL_CreateProperties();
        if (options == 0 || !SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, loopCount))
        {
            if (options != 0)
            {
                SDL_DestroyProperties(options);
            }
            return false;
        }
    }

    const bool played = MIX_PlayTrack(track, options);
    if (options != 0)
    {
        SDL_DestroyProperties(options);
    }
    return played;
}

bool AudioManager::playAndForget(AudioWrapper *audioWrapper, float gain, int loopCount)
{
    if (audioWrapper == nullptr || mixer == nullptr)
    {
        return false;
    }

    std::unique_ptr<MIX_Track, decltype(&MIX_DestroyTrack)> track(MIX_CreateTrack(mixer), MIX_DestroyTrack);
    if (!track || !MIX_SetTrackAudio(track.get(), audioWrapper->getAudio()))
    {
        return false;
    }

    MIX_SetTrackGain(track.get(), std::max(0.0f, gain));

    if (!playTrackWithLoops(track.get(), loopCount))
    {
        return false;
    }

    transientAudioPlaybacks.push_back({track.get(), audioWrapper});
    audioWrapper->addReference();
    track.release();
    return true;
}

void AudioManager::update(bool fixedStepDelta)
{
    size_t index = 0;
    while (index < transientAudioPlaybacks.size())
    {
        MIX_Track *track = transientAudioPlaybacks[index].track;
        if (!MIX_TrackPlaying(track))
        {
            releaseTransientAudioPlayback(index);
            continue;
        }
        ++index;
    }

    index = 0;
    while (index < pendingAudioReplays.size())
    {
        Audio *audio = pendingAudioReplays[index];
        auto *owner = audio->getGameObject();
        if (audio->isRetired() || (owner && (owner->isRetired() || !owner->getActive())))
        {
            audio->cancelPendingReplay();
            continue;
        }
        if (fixedStepDelta || audio->isPlaying() || audio->isPaused())
        {
            ++index;
            continue;
        }
        pendingAudioReplays[index] = pendingAudioReplays.back();
        pendingAudioReplays.pop_back();
        audio->finishPendingReplay();
    }
}

void AudioManager::scheduleReplay(Audio *audio)
{
    pendingAudioReplays.push_back(audio);
}

void AudioManager::cancelReplay(Audio *audio)
{
    auto found = std::find(pendingAudioReplays.begin(), pendingAudioReplays.end(), audio);
    if (found != pendingAudioReplays.end())
    {
        *found = pendingAudioReplays.back();
        pendingAudioReplays.pop_back();
    }
}

void AudioManager::clearTransientAudio()
{
    while (!transientAudioPlaybacks.empty())
    {
        releaseTransientAudioPlayback(transientAudioPlaybacks.size() - 1);
    }
}

void AudioManager::releaseTransientAudioPlayback(size_t index)
{
    TransientAudioPlayback playback = transientAudioPlaybacks[index];
    const size_t lastIndex = transientAudioPlaybacks.size() - 1;
    if (index != lastIndex)
    {
        transientAudioPlaybacks[index] = transientAudioPlaybacks[lastIndex];
    }
    transientAudioPlaybacks.pop_back();

    MIX_DestroyTrack(playback.track);
    playback.audioWrapper->removeReferenceAndFreeIfNoReferences();
}
