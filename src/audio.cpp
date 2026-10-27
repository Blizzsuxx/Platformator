#include "platformator/audio.h"

#include <algorithm>
#include <stdexcept>

#include <SDL3/SDL_properties.h>

#include "runtimeaccess.h"
#include "sdlwindow.h"

Audio::Audio(GameObject *gameObject) : Audio(gameObject, static_cast<AudioWrapper *>(nullptr), 1.0f, false, 0)
{
}

Audio::Audio() : Audio(nullptr)
{
}

Audio::Audio(GameObject *gameObject, const char *filePath, float gain, bool autoPlay, float loopCount)
    : Audio(gameObject, static_cast<AudioWrapper *>(nullptr), gain, autoPlay, loopCount)
{
    if (filePath)
    {
        setAudio(platformator_detail::RuntimeAccess::loadAudio(filePath));
        if (autoPlay)
            play();
    }
}

Audio::Audio(GameObject *gameObject, AudioWrapper *audioWrapper, float gain, bool autoPlay, float loopCount)
    : Component(gameObject, ComponentType::AUDIO), audioWrapper(nullptr), gain(gain), loopCount(loopCount),
      autoPlay(autoPlay)
{
    auto *mixer = platformator_detail::RuntimeAccess::window()->getMixer();
    if (mixer)
    {
        track.reset(MIX_CreateTrack(mixer));
        if (!track)
            throw std::runtime_error(std::string("Failed to create audio track: ") + SDL_GetError());
    }
    setAudio(audioWrapper);
    setGain(gain);
    if (autoPlay && audioWrapper)
        play();
}

Audio::~Audio()
{
    freeAudio();
}

void Audio::freeAudio()
{
    if (audioWrapper != nullptr)
    {
        stop();
        AudioWrapper *currentAudioWrapper = audioWrapper;
        audioWrapper = nullptr;
        currentAudioWrapper->removeReferenceAndFreeIfNoReferences();
    }
}

MIX_Track *Audio::getTrack() const
{
    return track.get();
}

AudioWrapper *Audio::getAudioWrapper() const
{
    return audioWrapper;
}

float Audio::getGain() const
{
    return track != nullptr ? MIX_GetTrackGain(track.get()) : gain;
}

bool Audio::isPlaying() const
{
    return track != nullptr && MIX_TrackPlaying(track.get());
}

bool Audio::isPaused() const
{
    return track != nullptr && MIX_TrackPaused(track.get());
}

bool Audio::play()
{
    if (audioWrapper == nullptr)
    {
        return false;
    }

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

    bool played = MIX_PlayTrack(track.get(), options);
    if (options != 0)
    {
        SDL_DestroyProperties(options);
    }

    return played;
}

bool Audio::play(AudioWrapper *audioWrapper)
{
    setAudio(audioWrapper);
    return play();
}

bool Audio::replay(AudioWrapper *audioWrapper, Sint64 fadeOutFrames)
{
    setAudio(audioWrapper);
    if (!stop(fadeOutFrames))
    {
        return false;
    }

    return play();
}

bool Audio::stop(Sint64 fadeOutFrames)
{
    if (track == nullptr)
    {
        return true;
    }

    return MIX_StopTrack(track.get(), fadeOutFrames);
}

bool Audio::pause()
{
    if (track == nullptr)
    {
        return false;
    }

    return MIX_PauseTrack(track.get());
}

bool Audio::resume()
{
    if (track == nullptr)
    {
        return false;
    }

    return MIX_ResumeTrack(track.get());
}

void Audio::setAudio(AudioWrapper *audio)
{
    if (audioWrapper == audio)
        return;
    if (audio && !track)
        throw std::logic_error("Audio assets require an enabled audio backend");
    if (audio)
        audio->addReference();
    if (track && !MIX_SetTrackAudio(track.get(), audio ? audio->getAudio() : nullptr))
    {
        if (audio)
            audio->removeReferenceAndFreeIfNoReferences();
        throw std::runtime_error(std::string("Failed to bind audio track: ") + SDL_GetError());
    }
    auto *old = audioWrapper;
    audioWrapper = audio;
    if (old)
        old->removeReferenceAndFreeIfNoReferences();
}

void Audio::setGain(float gain)
{
    this->gain = std::max(0.0f, gain);
    if (track != nullptr)
    {
        MIX_SetTrackGain(track.get(), this->gain);
    }
}

void Audio::setLoopCount(float loopCount)
{
    this->loopCount = loopCount;
}

float Audio::getLoopCount() const
{
    return loopCount;
}

const std::string &Audio::getFilePath() const
{
    static const std::string emptyString;
    return audioWrapper != nullptr ? audioWrapper->getFilePath() : emptyString;
}

bool Audio::getAutoPlay() const
{
    return autoPlay;
}

bool Audio::playAndForget(AudioWrapper *audioWrapper)
{
    if (audioWrapper == nullptr)
    {
        return false;
    }

    return platformator_detail::RuntimeAccess::playAndForget(audioWrapper, gain, loopCount);
}

bool Audio::playAndForget()
{
    if (audioWrapper == nullptr)
    {
        return false;
    }

    return playAndForget(audioWrapper);
}
