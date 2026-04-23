#include "texturewrapper.h"
#include "sprite.h"
#include <algorithm>
#include "gamemanager.h"

TextureWrapper::TextureWrapper(SDL_Texture *texture, const std::string &filePath) : texture(texture), filePath(filePath), referenceCount(0)
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

void TextureWrapper::addReference()
{
    referenceCount++;
}

bool TextureWrapper::removeReferenceAndFreeIfNoReferences()
{
    if (referenceCount > 0)
    {
        referenceCount--;
    }
    if (referenceCount == 0)
    {
        GameManager::getInstance().freeTexture(this);
        return true;
    }
    return false;
}

void TextureWrapper::destroyTexture()
{
    if (texture != nullptr)
    {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
}