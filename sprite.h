#pragma once

#include "component.h"
#include <SDL2/SDL.h>

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
