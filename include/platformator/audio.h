#pragma once

#include <cstddef>
#include <string>
#include <memory>

#include <SDL3_mixer/SDL_mixer.h>
#include <json.hpp>

#include "platformator/audiowrapper.h"
#include "platformator/gameobject.h"

class Audio : public Component
{
  public:
    Audio(GameObject *gameObject);
    Audio(GameObject *gameObject, const char *filePath, float gain = 1.0f, bool autoPlay = false, float loopCount = 0);
    Audio(GameObject *gameObject, AudioWrapper *audioWrapper, float gain = 1.0f, bool autoPlay = false,
          float loopCount = 0);
    Audio();
    ~Audio() override;

    MIX_Track *getTrack() const;
    AudioWrapper *getAudioWrapper() const;
    const std::string &getFilePath() const;
    bool getAutoPlay() const;
    void setAutoPlay(bool value)
    {
        autoPlay = value;
    }

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


  private:
    AudioWrapper *audioWrapper;
    std::unique_ptr<MIX_Track, decltype(&MIX_DestroyTrack)> track{nullptr, MIX_DestroyTrack};
    float gain;
    int loopCount;
    bool autoPlay;

    void freeAudio();
};

template <> struct ComponentTypeFor<Audio>
{
    static constexpr ComponentType value = ComponentType::AUDIO;
};

void to_json(nlohmann::json &j, const Audio &audio);
void from_json(const nlohmann::json &j, Audio &audio);
