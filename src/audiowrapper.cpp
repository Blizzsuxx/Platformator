#include "audiowrapper.h"
#include "gamemanager.h"

AudioWrapper::AudioWrapper(MIX_Audio *audio, const std::string &filePath) : audio(audio), filePath(filePath), referenceCount(0)
{
}

AudioWrapper::~AudioWrapper()
{
    destroyAudio();
}

MIX_Audio *AudioWrapper::getAudio() const
{
    return audio;
}

const std::string &AudioWrapper::getFilePath() const
{
    return filePath;
}

void AudioWrapper::addReference()
{
    referenceCount++;
}

bool AudioWrapper::removeReferenceAndFreeIfNoReferences()
{
    if (referenceCount > 0)
    {
        referenceCount--;
    }
    if (referenceCount == 0)
    {
        GameManager::getInstance().freeAudio(this);
        return true;
    }
    return false;
}

void AudioWrapper::destroyAudio()
{
    if (audio != nullptr)
    {
        MIX_DestroyAudio(audio);
        audio = nullptr;
    }
}