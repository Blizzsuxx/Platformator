#pragma once

#include <string>
#include <SDL3_mixer/SDL_mixer.h>
#include "baseobject.h"
#include "json.hpp"
#include "jsonhelpers.h"

// AudioWrapper is a wrapper so that you can use one audio for multiple audio components without worrying about freeing the audio multiple times.
class AudioWrapper : public Asset
{
public:
    AudioWrapper(MIX_Audio *audio, const std::string &filePath);
    ~AudioWrapper();

    MIX_Audio *getAudio() const;
    void addReference();
    bool removeReferenceAndFreeIfNoReferences();

private:
    MIX_Audio *audio;
    size_t referenceCount;

    void destroyAudio();
};

void to_json(nlohmann::json &j, const AudioWrapper &audioWrapper);