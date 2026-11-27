#pragma once

#include <string>

#include <SDL3/SDL_render.h>

#include "platformator/baseobject.h"

class TextureWrapper : public Asset
{
public:
    TextureWrapper(SDL_Texture *texture, const std::string &filePath);
    ~TextureWrapper() override;

    SDL_Texture *getTexture() const;

protected:
    void freeFromRuntime() override;

private:
    SDL_Texture *texture;

    void destroyTexture();
};

struct TextureRegion
{
    TextureRegion() = default;
    explicit TextureRegion(TextureWrapper *textureWrapper)
        : texture(Asset::retain(textureWrapper))
    {
    }
    TextureRegion(TextureWrapper *textureWrapper, const SDL_FRect &sourceRect)
        : texture(Asset::retain(textureWrapper)), sourceRect(sourceRect), hasSourceRect(true)
    {
    }

    TextureRegion(const TextureRegion &other)
        : texture(Asset::retain(other.texture.get())), sourceRect(other.sourceRect),
          hasSourceRect(other.hasSourceRect)
    {
    }
    TextureRegion &operator=(const TextureRegion &other)
    {
        if (this != &other)
        {
            if (texture.get() != other.texture.get())
            {
                texture = Asset::retain(other.texture.get());
            }
            sourceRect = other.sourceRect;
            hasSourceRect = other.hasSourceRect;
        }
        return *this;
    }
    TextureRegion(TextureRegion &&) noexcept = default;
    TextureRegion &operator=(TextureRegion &&) noexcept = default;
    ~TextureRegion() = default;

    TextureWrapper *getTextureWrapper() const noexcept
    {
        return texture.get();
    }
    void setTextureWrapper(TextureWrapper *newTextureWrapper)
    {
        if (texture.get() != newTextureWrapper)
        {
            texture = Asset::retain(newTextureWrapper);
        }
    }
    void setSourceRect(const SDL_FRect &rect) noexcept
    {
        sourceRect = rect;
        hasSourceRect = true;
    }
    void clearSourceRect() noexcept
    {
        sourceRect = SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
        hasSourceRect = false;
    }

    Asset::Reference<TextureWrapper> texture;
    SDL_FRect sourceRect{0.0f, 0.0f, 0.0f, 0.0f};
    bool hasSourceRect = false;
};
