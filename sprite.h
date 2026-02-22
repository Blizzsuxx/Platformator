#pragma once

#include <SDL2/SDL.h>
#include "gameobject.h"

class Sprite : public Component
{
public:
    Sprite(GameObject *gameObject);
    Sprite(GameObject *gameObject, SDL_Texture *texture);
    Sprite(GameObject *gameObject, SDL_Texture *texture, SDL_RendererFlip flip);
    Sprite(GameObject *gameObject, SDL_Texture *texture, SDL_RendererFlip flip, int width, int height);
    ~Sprite();

    // Getters
    SDL_Texture *getTexture() const;
    SDL_RendererFlip getFlip() const;
    int getWidth() const;
    int getHeight() const;

    // Setters
    void setTexture(SDL_Texture *texture);
    void setFlip(SDL_RendererFlip flip);
    void setWidth(int width);
    void setHeight(int height);

private:
    SDL_Texture *texture;
    SDL_RendererFlip flip;
    int width;
    int height;
};
