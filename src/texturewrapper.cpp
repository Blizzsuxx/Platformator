#include "texturewrapper.h"
#include "sprite.h"
#include <algorithm>
#include "gamemanager.h"

TextureWrapper::TextureWrapper(SDL_Texture *texture, const std::string &filePath) : Asset(filePath), texture(texture), referenceCount(0)
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
    if (referenceCount == 0 && !getFilePath().empty())
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