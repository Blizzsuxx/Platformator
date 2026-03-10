#include "sprite.h"

Sprite::Sprite(GameObject *gameObject) : Sprite(gameObject, nullptr, SDL_FLIP_NONE)
{
}

Sprite::Sprite(GameObject *gameObject, SDL_Texture *texture) : Sprite(gameObject, texture, SDL_FLIP_NONE)
{
}

Sprite::Sprite(GameObject *gameObject, SDL_Texture *texture, SDL_FlipMode flip) : Sprite(gameObject, texture, flip, 0, 0)
{
}

Sprite::Sprite(GameObject *gameObject, SDL_Texture *texture, SDL_FlipMode flip, float width, float height) : Component(gameObject, SPRITE), texture(texture), flip(flip), width(width), height(height)
{
    if (texture != nullptr && width == 0 && height == 0)
    {
        SDL_GetTextureSize(texture, &this->width, &this->height);
    }
}

Sprite::~Sprite()
{
}

// Getters
SDL_Texture *Sprite::getTexture() const
{
    return texture;
}

SDL_FlipMode Sprite::getFlip() const
{
    return flip;
}

float Sprite::getWidth() const
{
    return width;
}

float Sprite::getHeight() const
{
    return height;
}

// Setters
void Sprite::setTexture(SDL_Texture *texture)
{
    this->texture = texture;
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
