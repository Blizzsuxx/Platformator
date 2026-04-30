#include "audio.h"

#include <algorithm>

#include <SDL3/SDL_properties.h>

#include "gamemanager.h"
#include "sdlwindow.h"

Audio::Audio(GameObject *gameObject)
    : Audio(gameObject, (AudioWrapper *)nullptr, 1.0f, false)
{
    SDLWindow *window = GameManager::getInstance().getWindow();
    track = MIX_CreateTrack(window->getMixer());
}

Audio::Audio() : Component(ComponentType::AUDIO), audioWrapper(nullptr), track(nullptr), gain(1.0f), loopCount(0), autoPlay(false), gameManagerIndex(SIZE_MAX)
{
    SDLWindow *window = GameManager::getInstance().getWindow();
    track = MIX_CreateTrack(window->getMixer());
}

Audio::Audio(GameObject *gameObject, const char *filePath, float gain, bool autoPlay, float loopCount)
    : Audio(gameObject, (AudioWrapper *)nullptr, gain, autoPlay, loopCount)
{
    if (filePath != nullptr)
    {
        GameManager &gameManager = GameManager::getInstance();
        AudioWrapper *newAudioWrapper = gameManager.loadAudio(filePath);

        SDLWindow *window = GameManager::getInstance().getWindow();
        track = MIX_CreateTrack(window->getMixer());
        setAudio(newAudioWrapper);
        setGain(gain);

        if (autoPlay)
        {
            play();
        }
    }
}

Audio::Audio(GameObject *gameObject, AudioWrapper *audioWrapper, float gain, bool autoPlay, float loopCount)
    : Component(gameObject, ComponentType::AUDIO), audioWrapper(audioWrapper), track(nullptr), gain(gain), loopCount(loopCount), autoPlay(autoPlay), gameManagerIndex(SIZE_MAX)
{
    if (audioWrapper != nullptr)
    {
        SDLWindow *window = GameManager::getInstance().getWindow();
        track = MIX_CreateTrack(window->getMixer());
        setAudio(audioWrapper);
        setGain(gain);

        if (autoPlay)
        {
            play();
        }
    }
}

Audio::~Audio()
{
    freeAudio();
    MIX_DestroyTrack(track);
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
    return track;
}

AudioWrapper *Audio::getAudioWrapper() const
{
    return audioWrapper;
}

float Audio::getGain() const
{
    return track != nullptr ? MIX_GetTrackGain(track) : gain;
}

bool Audio::isPlaying() const
{
    return track != nullptr && MIX_TrackPlaying(track);
}

bool Audio::isPaused() const
{
    return track != nullptr && MIX_TrackPaused(track);
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

    bool played = MIX_PlayTrack(track, options);
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

    return MIX_StopTrack(track, fadeOutFrames);
}

bool Audio::pause()
{
    if (track == nullptr)
    {
        return false;
    }

    return MIX_PauseTrack(track);
}

bool Audio::resume()
{
    if (track == nullptr)
    {
        return false;
    }

    return MIX_ResumeTrack(track);
}

void Audio::setAudio(AudioWrapper *audio)
{
    if (this->audioWrapper == audio)
    {
        return;
    }

    freeAudio();
    this->audioWrapper = audio;
    if (audioWrapper != nullptr)
    {
        audioWrapper->addReference();
    }

    MIX_SetTrackAudio(track, nullptr);
    if (audioWrapper != nullptr)
    {
        MIX_SetTrackAudio(track, audioWrapper->getAudio());
    }
}

void Audio::setGain(float gain)
{
    this->gain = std::max(0.0f, gain);
    if (track != nullptr)
    {
        MIX_SetTrackGain(track, this->gain);
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

size_t Audio::getGameManagerIndex() const
{
    return gameManagerIndex;
}

void Audio::setGameManagerIndex(size_t index)
{
    gameManagerIndex = index;
}