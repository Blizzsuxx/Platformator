#include "audiowrapper.h"
#include "gamemanager.h"
#include "runtimeaccess.h"

AudioWrapper::AudioWrapper(MIX_Audio *audio, const std::string &filePath) : Asset(filePath), audio(audio), referenceCount(0)
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
    if (referenceCount == 0 && !getFilePath().empty())
    {
        getGameManagerInstance().freeAudio(this);
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
