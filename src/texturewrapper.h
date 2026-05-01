#pragma once

#include <SDL3/SDL_render.h>
#include <string>
#include "json.hpp"
#include "jsonhelpers.h"
#include "baseobject.h"

class Sprite;

// TextureWrapper is a wrapper so that you can use one texture for multiple sprites without worrying about freeing the texture multiple times.
class TextureWrapper : public Asset
{
public:
    TextureWrapper(SDL_Texture *texture, const std::string &filePath);
    ~TextureWrapper();

    SDL_Texture *getTexture() const;
    void addReference();
    bool removeReferenceAndFreeIfNoReferences();

private:
    SDL_Texture *texture;
    size_t referenceCount;

    void destroyTexture();
};

void to_json(nlohmann::json &j, const TextureWrapper &textureWrapper);