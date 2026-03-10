#include "audio.h"

Audio::Audio(GameObject *gameObject) : Component(gameObject, ComponentType::AUDIO), audio(nullptr)
{
}

Audio::~Audio()
{
    if (audio != nullptr)
    {
        MIX_DestroyAudio(audio);
    }
}

// Getters
MIX_Audio *Audio::getAudio() const
{
    return audio;
}

// Setters
void Audio::setAudio(MIX_Audio *audio)
{
    this->audio = audio;
}