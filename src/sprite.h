#pragma once

#include <SDL3/SDL.h>
#include "gameobject.h"

class TextureWrapper;

class Sprite : public Component
{
public:
    Sprite(GameObject *gameObject);
    Sprite(GameObject *gameObject, TextureWrapper *textureWrapper);
    Sprite(GameObject *gameObject, TextureWrapper *textureWrapper, SDL_FlipMode flip);
    Sprite(GameObject *gameObject, TextureWrapper *textureWrapper, SDL_FlipMode flip, float width, float height);
    Sprite(GameObject *gameObject, const char *filePath);
    Sprite(GameObject *gameObject, const char *filePath, SDL_FlipMode flip);
    Sprite(GameObject *gameObject, const char *filePath, SDL_FlipMode flip, float width, float height);
    ~Sprite();

    // Getters
    SDL_Texture *getTexture() const;
    TextureWrapper *getTextureWrapper() const;
    SDL_FlipMode getFlip() const;
    float getWidth() const;
    float getHeight() const;

    // Setters
    void setTextureWrapper(TextureWrapper *textureWrapper);
    void setFlip(SDL_FlipMode flip);
    void setWidth(float width);
    void setHeight(float height);

private:
    TextureWrapper *textureWrapper;
    SDL_FlipMode flip;
    float width;
    float height;

    void freeTexture();
};

template <>
struct ComponentTypeFor<Sprite>
{
    static constexpr ComponentType value = ComponentType::SPRITE;
};
