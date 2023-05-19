#include "camera.h"

Camera::Camera(GameObject *gameObject) : Component(gameObject, CAMERA)
{
    camera.x = 0;
    camera.y = 0;
    camera.w = 0;
    camera.h = 0;
}

Camera::Camera(GameObject *gameObject, int w, int h) : Component(gameObject, CAMERA)
{
    camera.x = 0;
    camera.y = 0;
    camera.w = w;
    camera.h = h;
}

Camera::Camera(GameObject *gameObject, int x, int y, int w, int h) : Component(gameObject, CAMERA)
{
    camera.x = x;
    camera.y = y;
    camera.w = w;
    camera.h = h;
}

Camera::~Camera()
{
}

void Camera::render(Sprite *sprite, SDL_Renderer *renderer)
{
    SDL_Rect renderQuad = { sprite->getGameObject()->getX() - camera.x, sprite->getGameObject()->getY() - camera.y, sprite->getWidth(), sprite->getHeight() };

    SDL_RenderCopyEx(renderer, sprite->getTexture(), NULL, &renderQuad, sprite->getGameObject()->getRotation(), NULL, sprite->getFlip());
}

// Getters
const SDL_Rect& Camera::getCamera() const
{
    return camera;
}

// Setters
void Camera::setCamera(SDL_Rect& camera)
{
    this->camera = camera;
}

