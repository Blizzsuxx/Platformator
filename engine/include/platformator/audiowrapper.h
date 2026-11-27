#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include <SDL3_mixer/SDL_mixer.h>

#include "platformator/baseobject.h"

class AudioWrapper : public Asset
{
public:
    // Owns one existing reference; use retain() to acquire a new reference.
    using Reference = Asset::Reference<AudioWrapper>;
    static Reference retain(AudioWrapper *audio)
    {
        return Asset::retain(audio);
    }

    AudioWrapper(MIX_Audio *audio, const std::string &filePath);
    ~AudioWrapper() override;

    MIX_Audio *getAudio() const;

protected:
    void freeFromRuntime() override;

private:
    MIX_Audio *audio;

    void destroyAudio();
};
