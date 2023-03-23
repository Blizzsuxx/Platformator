#pragma once

#include <SDL2/SDL.h>
#include "gameobject.h"

class Sprite : public Component
{
public:
    Sprite(GameObject* gameObject);
    ~Sprite();

    // Getters
    SDL_Texture* getTexture() const;

    // Setters
    void setTexture(SDL_Texture* texture);
    
private:
    SDL_Texture* texture;
};
