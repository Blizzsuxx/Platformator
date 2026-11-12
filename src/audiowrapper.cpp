#include "platformator/audiowrapper.h"
#include "runtimeaccess.h"

AudioWrapper::AudioWrapper(MIX_Audio *audio, const std::string &filePath)
    : Asset(filePath), audio(audio)
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

void AudioWrapper::freeFromRuntime()
{
    platformator_detail::RuntimeAccess::freeAudio(this);
}

void AudioWrapper::destroyAudio()
{
    if (audio != nullptr)
    {
        MIX_DestroyAudio(audio);
        audio = nullptr;
    }
}
