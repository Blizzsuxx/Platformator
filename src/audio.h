#pragma once

#include <string>

#include <SDL3_mixer/SDL_mixer.h>
#include "gameobject.h"

class Audio : public Component
{
public:
    Audio(GameObject *gameObject);
    Audio(GameObject *gameObject, const char *filePath, float gain = 1.0f, bool autoPlay = false, float loopCount = 0);
    Audio(GameObject *gameObject, AudioWrapper *audioWrapper, float gain = 1.0f, bool autoPlay = false, float loopCount = 0);
    ~Audio() override;

    MIX_Audio *getAudio() const;
    MIX_Track *getTrack() const;
    float getGain() const;
    bool isPlaying() const;
    bool isPaused() const;
    float getLoopCount() const;

    bool play();
    bool stop(Sint64 fadeOutFrames = 0);
    bool pause();
    bool resume();

    void setAudio(AudioWrapper *audioWrapper);
    void setGain(float gain);
    void setLoopCount(float loopCount);

private:
    AudioWrapper *audioWrapper;
    MIX_Track *track;
    float gain;
    int loopCount;

    void freeAudio();
};

template <>
struct ComponentTypeFor<Audio>
{
    static constexpr ComponentType value = ComponentType::AUDIO;
};