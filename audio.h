#pragma once

#include <SDL2/SDL_mixer.h>
#include "gameobject.h"

class Audio : public Component
{
public:
    Audio(GameObject* gameObject);
    ~Audio();

    // Getters
    Mix_Chunk* getChunk() const;

    // Setters
    void setChunk(Mix_Chunk* chunk);

private:
    Mix_Chunk* chunk;
};