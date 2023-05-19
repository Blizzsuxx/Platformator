#include "audio.h"

Audio::Audio(GameObject *gameObject) : Component(gameObject, ComponentType::AUDIO)
{
}

Audio::~Audio()
{
    delete chunk;
}

// Getters
Mix_Chunk *Audio::getChunk() const
{
    return chunk;
}

// Setters
void Audio::setChunk(Mix_Chunk *chunk)
{
    this->chunk = chunk;
}