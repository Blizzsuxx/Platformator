#pragma once

#include <cstddef>
#include <string>

#include <SDL3/SDL_render.h>
#include <json.hpp>

#include "platformator/baseobject.h"

class Sprite;

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