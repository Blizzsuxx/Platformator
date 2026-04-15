#include "texturewrapper.h"
#include "sprite.h"
#include <algorithm>
#include "gamemanager.h"

TextureWrapper::TextureWrapper(SDL_Texture *texture, const std::string &filePath) : texture(texture), filePath(filePath), referencingSprites()
{
}

TextureWrapper::~TextureWrapper()
{
    destroyTexture();
}

SDL_Texture *TextureWrapper::getTexture() const
{
    return texture;
}

const std::string &TextureWrapper::getFilePath() const
{
    return filePath;
}

void TextureWrapper::addReference(Sprite *sprite)
{
    referencingSprites.push_back(sprite);
}

void TextureWrapper::removeReference(Sprite *sprite)
{
    referencingSprites.erase(std::remove(referencingSprites.begin(), referencingSprites.end(), sprite), referencingSprites.end());
}

bool TextureWrapper::freeTextureIfNoReferences()
{
    if (referencingSprites.empty())
    {
        GameManager::getInstance().freeTexture(filePath);
        return true;
    }
    return false;
}

bool TextureWrapper::removeReferenceAndFreeIfNoReferences(Sprite *sprite)
{
    removeReference(sprite);
    return freeTextureIfNoReferences();
}

void TextureWrapper::destroyTexture()
{
    if (texture != nullptr)
    {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
}