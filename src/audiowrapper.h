#pragma once

#include <string>
#include <SDL3_mixer/SDL_mixer.h>

// AudioWrapper is a wrapper so that you can use one audio for multiple audio components without worrying about freeing the audio multiple times.
class AudioWrapper
{
public:
    AudioWrapper(MIX_Audio *audio, const std::string &filePath);
    ~AudioWrapper();

    MIX_Audio *getAudio() const;
    const std::string &getFilePath() const;
    void addReference();
    bool removeReferenceAndFreeIfNoReferences();

private:
    MIX_Audio *audio;
    std::string filePath;
    size_t referenceCount;

    void destroyAudio();
};