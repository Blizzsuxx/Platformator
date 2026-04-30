#include "camera.h"

Camera::Camera() : Component(ComponentType::CAMERA), camera{0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT}
{
}

Camera::Camera(GameObject *gameObject) : Component(gameObject, CAMERA), camera{0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT}
{
}

Camera::Camera(GameObject *gameObject, float w, float h) : Component(gameObject, CAMERA), camera{0.0f, 0.0f, w, h}
{
}

Camera::Camera(GameObject *gameObject, float x, float y, float w, float h) : Component(gameObject, CAMERA), camera{x, y, w, h}
{
}

Camera::~Camera()
{
}

void Camera::render(Sprite *sprite, SDL_Renderer *renderer)
{
    const int cameraX = static_cast<int>(sprite->getGameObject()->getX());
    const int cameraY = static_cast<int>(sprite->getGameObject()->getY());
    SDL_FRect renderQuad = {cameraX - camera.x, cameraY - camera.y, sprite->getWidth(), sprite->getHeight()};

    SDL_RenderTextureRotated(renderer, sprite->getTexture(), nullptr, &renderQuad, sprite->getGameObject()->getRotationInDegrees(), nullptr, sprite->getFlip());
}

// Getters
const SDL_FRect &Camera::getCamera() const
{
    return camera;
}

// Setters
void Camera::setCamera(const SDL_FRect &camera)
{
    this->camera = camera;
}
