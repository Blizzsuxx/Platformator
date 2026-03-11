#include "texturewrapper.h"
#include "gamemanager.h"
#include "sprite.h"

TextureWrapper::TextureWrapper(SDL_Texture *texture, const std::string &filePath) : texture(texture), filePath(filePath), referencingSprites()
{
}

TextureWrapper::~TextureWrapper()
{
    freeTextureIfNoReferences();
}

SDL_Texture *TextureWrapper::getTexture() const
{
    return texture;
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
        if (texture != nullptr)
        {
            GameManager::getInstance().freeTexture(filePath);
            texture = nullptr;
        }
        return true;
    }
    return false;
}

bool TextureWrapper::removeReferenceAndFreeIfNoReferences(Sprite *sprite)
{
    removeReference(sprite);
    return freeTextureIfNoReferences();
}