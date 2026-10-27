#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include <SDL3_mixer/SDL_mixer.h>
#include <json.hpp>

#include "platformator/baseobject.h"

class AudioWrapper : public Asset
{
public:
    struct ReleaseReference
    {
        void operator()(AudioWrapper *audio) const;
    };

    // Owns one existing reference; use retain() to acquire a new reference.
    using Reference = std::unique_ptr<AudioWrapper, ReleaseReference>;
    static Reference retain(AudioWrapper *audio);

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
