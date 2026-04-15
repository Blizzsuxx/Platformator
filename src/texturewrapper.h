#pragma once

#include <SDL3/SDL_render.h>
#include <string>
#include <vector>

class Sprite;

// TextureWrapper is a wrapper so that you can use one texture for multiple sprites without worrying about freeing the texture multiple times.
class TextureWrapper
{
public:
    TextureWrapper(SDL_Texture *texture, const std::string &filePath);
    ~TextureWrapper();

    SDL_Texture *getTexture() const;
    const std::string &getFilePath() const;
    void addReference(Sprite *sprite);
    void removeReference(Sprite *sprite);
    bool removeReferenceAndFreeIfNoReferences(Sprite *sprite);

    bool freeTextureIfNoReferences();

private:
    SDL_Texture *texture;
    std::string filePath;
    std::vector<Sprite *> referencingSprites;

    void destroyTexture();
};