#pragma once

#include <cstddef>
#include <SDL3/SDL.h>
#include "gameobject.h"

#include <json.hpp>
#include "jsonhelpers.h"

class TextureWrapper;

class Sprite : public Component
{
public:
    Sprite();
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
    bool hasSourceRect() const;
    const SDL_FRect *getSourceRect() const;
    float getWidthWithoutScale() const;
    float getHeightWithoutScale() const;

    // Setters
    void setTextureWrapper(TextureWrapper *textureWrapper);
    void setFlip(SDL_FlipMode flip);
    void setWidth(float width);
    void setHeight(float height);
    void setSourceRect(const SDL_FRect &sourceRect);
    void clearSourceRect();

    bool getIsRegisteredInWindow() const;
    void setIsRegisteredInWindow(bool isRegisteredInWindow);
    size_t getWindowIndex() const;
    void setWindowIndex(size_t windowIndex);

private:
    TextureWrapper *textureWrapper;
    SDL_FlipMode flip;
    float width;
    float height;
    SDL_FRect sourceRect;
    bool sourceRectEnabled;
    bool isRegisteredInWindow;
    size_t windowIndex;

    void freeTexture();
};

template <>
struct ComponentTypeFor<Sprite>
{
    static constexpr ComponentType value = ComponentType::SPRITE;
};

void to_json(nlohmann::json &j, const Sprite &sprite);
void from_json(const nlohmann::json &j, Sprite &sprite);