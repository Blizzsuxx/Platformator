#include "sprite.h"
#include "gamemanager.h"
#include "runtimeaccess.h"
#include "texturewrapper.h"

Sprite::Sprite() : Component(ComponentType::SPRITE), textureWrapper(nullptr), flip(SDL_FLIP_NONE), width(0.0f), height(0.0f), sourceRect(), sourceRectEnabled(false), isRegisteredInWindow(false), windowIndex(SIZE_MAX)
{
}

Sprite::Sprite(GameObject *gameObject) : Sprite(gameObject, (TextureWrapper *)nullptr, SDL_FLIP_NONE)
{
}

Sprite::Sprite(GameObject *gameObject, TextureWrapper *textureWrapper) : Sprite(gameObject, textureWrapper, SDL_FLIP_NONE)
{
}

Sprite::Sprite(GameObject *gameObject, TextureWrapper *textureWrapper, SDL_FlipMode flip) : Sprite(gameObject, textureWrapper, flip, 0, 0)
{
}

Sprite::Sprite(GameObject *gameObject, const char *filePath) : Sprite(gameObject, filePath, SDL_FLIP_NONE)
{
}

Sprite::Sprite(GameObject *gameObject, const char *filePath, SDL_FlipMode flip) : Sprite(gameObject, filePath, flip, 0, 0)
{
}

Sprite::Sprite(GameObject *gameObject, const char *filePath, SDL_FlipMode flip, float width, float height) : Component(gameObject, SPRITE), textureWrapper(nullptr), flip(flip), width(width), height(height), sourceRect(), sourceRectEnabled(false), isRegisteredInWindow(false), windowIndex(SIZE_MAX)
{
    if (filePath != nullptr)
    {
        GameManager &gameManager = getGameManagerInstance();
        TextureWrapper *newTextureWrapper = gameManager.loadTexture(filePath);
        setTextureWrapper(newTextureWrapper);
    }
}

Sprite::Sprite(GameObject *gameObject, TextureWrapper *textureWrapper, SDL_FlipMode flip, float width, float height) : Component(gameObject, SPRITE), textureWrapper(textureWrapper), flip(flip), width(width), height(height), sourceRect(), sourceRectEnabled(false), isRegisteredInWindow(false), windowIndex(SIZE_MAX)
{
    if (textureWrapper != nullptr && width == 0 && height == 0)
    {
        SDL_GetTextureSize(textureWrapper->getTexture(), &this->width, &this->height);
    }
    setTextureWrapper(textureWrapper);
}

Sprite::~Sprite()
{
    freeTexture();
}

void Sprite::freeTexture()
{
    if (textureWrapper != nullptr)
    {
        TextureWrapper *currentTextureWrapper = textureWrapper;
        textureWrapper = nullptr;
        currentTextureWrapper->removeReferenceAndFreeIfNoReferences();
    }
}

SDL_Texture *Sprite::getTexture() const
{
    return textureWrapper != nullptr ? textureWrapper->getTexture() : nullptr;
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
    return sourceRectEnabled;
}

const SDL_FRect *Sprite::getSourceRect() const
{
    return sourceRectEnabled ? &sourceRect : nullptr;
}

TextureWrapper *Sprite::getTextureWrapper() const
{
    return textureWrapper;
}

void Sprite::setFlip(SDL_FlipMode flip)
{
    this->flip = flip;
}

void Sprite::setWidth(float width)
{
    this->width = width;
}

void Sprite::setHeight(float height)
{
    this->height = height;
}

void Sprite::setSourceRect(const SDL_FRect &sourceRect)
{
    this->sourceRect = sourceRect;
    sourceRectEnabled = true;
}

void Sprite::clearSourceRect()
{
    sourceRect = SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
    sourceRectEnabled = false;
}

void Sprite::setTextureWrapper(TextureWrapper *textureWrapper)
{
    if (this->textureWrapper == textureWrapper)
    {
        return;
    }

    freeTexture();

    this->textureWrapper = textureWrapper;
    if (this->textureWrapper != nullptr)
    {
        this->textureWrapper->addReference();
    }
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