#include "camera.h"

Camera::Camera(GameObject *gameObject) : Component(gameObject, CAMERA), camera(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT)
{
}

Camera::Camera(GameObject *gameObject, int w, int h) : Component(gameObject, CAMERA), camera(0, 0, w, h)
{
}

Camera::Camera(GameObject *gameObject, int x, int y, int w, int h) : Component(gameObject, CAMERA), camera(x, y, w, h)
{
}

Camera::~Camera()
{
}

void Camera::render(Sprite *sprite, SDL_Renderer *renderer)
{
    const int cameraX = static_cast<int>(sprite->getGameObject()->getX());
    const int cameraY = static_cast<int>(sprite->getGameObject()->getY());
    SDL_Rect renderQuad = {cameraX - camera.x, cameraY - camera.y, sprite->getWidth(), sprite->getHeight()};

    SDL_RenderCopyEx(renderer, sprite->getTexture(), nullptr, &renderQuad, sprite->getGameObject()->getRotationInDegrees(), nullptr, sprite->getFlip());
}

// Getters
const SDL_Rect &Camera::getCamera() const
{
    return camera;
}

// Setters
void Camera::setCamera(const SDL_Rect &camera)
{
    this->camera = camera;
}
