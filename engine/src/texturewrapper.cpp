#include "platformator/texturewrapper.h"
#include "platformator/sprite.h"
#include <algorithm>
#include "runtimeaccess.h"

TextureWrapper::TextureWrapper(SDL_Texture *texture, const std::string &filePath)
    : Asset(filePath), texture(texture)
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

void TextureWrapper::freeFromRuntime()
{
    platformator_detail::RuntimeAccess::freeTexture(this);
}

void TextureWrapper::destroyTexture()
{
    if (texture != nullptr)
    {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
}
