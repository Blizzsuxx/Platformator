#pragma once

#include <SDL3/SDL.h>
#include "gameobject.h"

class Sprite : public Component
{
public:
    Sprite(GameObject *gameObject);
    Sprite(GameObject *gameObject, SDL_Texture *texture);
    Sprite(GameObject *gameObject, SDL_Texture *texture, SDL_FlipMode flip);
    Sprite(GameObject *gameObject, SDL_Texture *texture, SDL_FlipMode flip, float width, float height);
    ~Sprite();

    // Getters
    SDL_Texture *getTexture() const;
    SDL_FlipMode getFlip() const;
    float getWidth() const;
    float getHeight() const;

    // Setters
    void setTexture(SDL_Texture *texture);
    void setFlip(SDL_FlipMode flip);
    void setWidth(float width);
    void setHeight(float height);

private:
    SDL_Texture *texture;
    SDL_FlipMode flip;
    float width;
    float height;
};

template <>
struct ComponentTypeFor<Sprite>
{
    static constexpr ComponentType value = ComponentType::SPRITE;
};
