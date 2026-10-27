#include "platformator/audiowrapper.h"
#include "runtimeaccess.h"

void AudioWrapper::ReleaseReference::operator()(AudioWrapper *audio) const
{
    audio->removeReferenceAndFreeIfNoReferences();
}

AudioWrapper::Reference AudioWrapper::retain(AudioWrapper *audio)
{
    if (audio)
    {

        audio->addReference();
    }

    return Reference(audio);
}

AudioWrapper::AudioWrapper(MIX_Audio *audio, const std::string &filePath)
    : Asset(filePath), audio(audio), referenceCount(0)
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
        platformator_detail::RuntimeAccess::freeAudio(this);
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
