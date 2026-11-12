#include "platformator/sprite.h"
#include "runtimeaccess.h"
#include "platformator/texturewrapper.h"
#include "sdlwindow.h"

Sprite::Sprite()
    : Component(ComponentType::SPRITE), region(), flip(SDL_FLIP_NONE), width(0.0f), height(0.0f),
      isRegisteredInWindow(false), windowIndex(SIZE_MAX)
{
}

Sprite::Sprite(GameObject *gameObject, TextureWrapper *textureWrapper, float width, float height)
    : Sprite(gameObject, textureWrapper, SDL_FLIP_NONE, width, height)
{
}

Sprite::Sprite(GameObject *gameObject, const char *filePath, float width, float height)
    : Sprite(gameObject, filePath, SDL_FLIP_NONE, width, height)
{
}

Sprite::Sprite(GameObject *gameObject, const char *filePath, SDL_FlipMode flip, float width, float height)
    : Component(gameObject, SPRITE), region(), flip(flip), width(width), height(height),
      isRegisteredInWindow(false), windowIndex(SIZE_MAX)
{
    if (filePath != nullptr)
    {
        TextureWrapper *newTextureWrapper = platformator_detail::RuntimeAccess::loadTexture(filePath);
        setTextureWrapper(newTextureWrapper);
    }
}

Sprite::Sprite(GameObject *gameObject, TextureWrapper *textureWrapper, SDL_FlipMode flip, float width, float height)
    : Component(gameObject, SPRITE), region(), flip(flip), width(width), height(height),
      isRegisteredInWindow(false), windowIndex(SIZE_MAX)
{
    if (textureWrapper != nullptr && width == 0 && height == 0)
    {
        SDL_GetTextureSize(textureWrapper->getTexture(), &this->width, &this->height);
    }
    setTextureWrapper(textureWrapper);
}

SDL_Texture *Sprite::getTexture() const
{
    const TextureWrapper *wrapper = region.getTextureWrapper();
    return wrapper != nullptr ? wrapper->getTexture() : nullptr;
}

SDL_FlipMode Sprite::getFlip() const
{
    return flip;
}

float Sprite::getWidth() const
{
    return width * getGameObject()->getScale().x();
}

float Sprite::getWidthWithoutScale() const
{
    return width;
}

float Sprite::getHeight() const
{
    return height * getGameObject()->getScale().y();
}

float Sprite::getHeightWithoutScale() const
{
    return height;
}

bool Sprite::hasSourceRect() const
{
    return region.hasSourceRect;
}

const SDL_FRect *Sprite::getSourceRect() const
{
    return region.hasSourceRect ? &region.sourceRect : nullptr;
}

TextureWrapper *Sprite::getTextureWrapper() const
{
    return region.getTextureWrapper();
}

const TextureRegion &Sprite::getTextureRegion() const
{
    return region;
}

void Sprite::setFlip(SDL_FlipMode flip)
{
    this->flip = flip;
}

void Sprite::setWidth(float width)
{
    if (this->width == width)
        return;
    this->width = width;
    if (isRegisteredInWindow)
        platformator_detail::RuntimeAccess::window()->notifySpriteGeometryChanged(this);
}

void Sprite::setHeight(float height)
{
    if (this->height == height)
        return;
    this->height = height;
    if (isRegisteredInWindow)
        platformator_detail::RuntimeAccess::window()->notifySpriteGeometryChanged(this);
}

void Sprite::setSourceRect(const SDL_FRect &sourceRect)
{
    region.setSourceRect(sourceRect);
}

void Sprite::clearSourceRect()
{
    region.clearSourceRect();
}

void Sprite::setTextureWrapper(TextureWrapper *textureWrapper)
{
    if (region.getTextureWrapper() == textureWrapper)
    {
        return;
    }

    const bool previouslyDrawable = getTexture() != nullptr;
    region.setTextureWrapper(textureWrapper);
    if (isRegisteredInWindow && previouslyDrawable != (getTexture() != nullptr))
        platformator_detail::RuntimeAccess::window()->notifySpriteGeometryChanged(this);
}

void Sprite::setTextureRegion(const TextureRegion &newRegion)
{
    const bool previouslyDrawable = getTexture() != nullptr;
    region = newRegion;
    if (isRegisteredInWindow && previouslyDrawable != (getTexture() != nullptr))
        platformator_detail::RuntimeAccess::window()->notifySpriteGeometryChanged(this);
}

bool Sprite::getIsRegisteredInWindow() const
{
    return isRegisteredInWindow;
}

void Sprite::setIsRegisteredInWindow(bool isRegisteredInWindow)
{
    this->isRegisteredInWindow = isRegisteredInWindow;
}

size_t Sprite::getWindowIndex() const
{
    return windowIndex;
}

void Sprite::setWindowIndex(size_t windowIndex)
{
    this->windowIndex = windowIndex;
}
