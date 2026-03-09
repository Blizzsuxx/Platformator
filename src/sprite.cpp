#include "sprite.h"

Sprite::Sprite(GameObject *gameObject) : Sprite(gameObject, nullptr, SDL_FLIP_NONE)
{
}

Sprite::Sprite(GameObject *gameObject, SDL_Texture *texture) : Sprite(gameObject, texture, SDL_FLIP_NONE)
{
}

Sprite::Sprite(GameObject *gameObject, SDL_Texture *texture, SDL_RendererFlip flip) : Sprite(gameObject, texture, flip, 0, 0)
{
}

Sprite::Sprite(GameObject *gameObject, SDL_Texture *texture, SDL_RendererFlip flip, int width, int height) : Component(gameObject, SPRITE), texture(texture), flip(flip), width(width), height(height)
{
    if (texture != nullptr && width == 0 && height == 0)
    {
        SDL_QueryTexture(texture, nullptr, nullptr, &this->width, &this->height);
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

SDL_RendererFlip Sprite::getFlip() const
{
    return flip;
}

int Sprite::getWidth() const
{
    return width;
}

int Sprite::getHeight() const
{
    return height;
}

// Setters
void Sprite::setTexture(SDL_Texture *texture)
{
    this->texture = texture;
}

void Sprite::setFlip(SDL_RendererFlip flip)
{
    this->flip = flip;
}

void Sprite::setWidth(int width)
{
    this->width = width;
}

void Sprite::setHeight(int height)
{
    this->height = height;
}
