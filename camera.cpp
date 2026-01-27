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
    const int cameraX = static_cast<int>(sprite->getGameObject()->getX());
    const int cameraY = static_cast<int>(sprite->getGameObject()->getY());
    SDL_Rect renderQuad = { cameraX - camera.x, cameraY - camera.y, sprite->getWidth(), sprite->getHeight() };

    SDL_RenderCopyEx(renderer, sprite->getTexture(), nullptr, &renderQuad, sprite->getGameObject()->getRotation(), nullptr, sprite->getFlip());
}

// Getters
const SDL_Rect& Camera::getCamera() const
{
    return camera;
}

// Setters
void Camera::setCamera(const SDL_Rect& camera)
{
    this->camera = camera;
}

