#pragma once

#include <string>

#include <SDL3_mixer/SDL_mixer.h>
#include "gameobject.h"
#include "audiowrapper.h"

#include "json.hpp"
#include "jsonhelpers.h"

class Audio : public Component
{
public:
    Audio(GameObject *gameObject);
    Audio(GameObject *gameObject, const char *filePath, float gain = 1.0f, bool autoPlay = false, float loopCount = 0);
    Audio(GameObject *gameObject, AudioWrapper *audioWrapper, float gain = 1.0f, bool autoPlay = false, float loopCount = 0);
    Audio();
    ~Audio() override;

    MIX_Track *getTrack() const;
    AudioWrapper *getAudioWrapper() const;
    const std::string &getFilePath() const;
    bool getAutoPlay() const;

    float getGain() const;
    bool isPlaying() const;
    bool isPaused() const;
    float getLoopCount() const;

    bool play();
    bool play(AudioWrapper *audioWrapper);
    bool replay(AudioWrapper *audioWrapper, Sint64 fadeOutFrames = 0);
    bool playAndForget(AudioWrapper *audioWrapper);
    bool playAndForget();
    bool stop(Sint64 fadeOutFrames = 0);
    bool pause();
    bool resume();

    void setAudio(AudioWrapper *audioWrapper);
    void setGain(float gain);
    void setLoopCount(float loopCount);

    size_t getGameManagerIndex() const;
    void setGameManagerIndex(size_t index);

private:
    AudioWrapper *audioWrapper;
    MIX_Track *track;
    float gain;
    int loopCount;
    bool autoPlay;
    size_t gameManagerIndex;

    void freeAudio();
};

template <>
struct ComponentTypeFor<Audio>
{
    static constexpr ComponentType value = ComponentType::AUDIO;
};

void to_json(nlohmann::json &j, const Audio &audio);
void from_json(const nlohmann::json &j, Audio &audio);