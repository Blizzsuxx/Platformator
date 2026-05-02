#include "camera.h"

Camera::Camera() : Component(ComponentType::CAMERA), width(SCREEN_WIDTH), height(SCREEN_HEIGHT)
{
}

Camera::Camera(GameObject *gameObject) : Component(gameObject, CAMERA), width(SCREEN_WIDTH), height(SCREEN_HEIGHT)
{
}

Camera::Camera(GameObject *gameObject, float w, float h) : Component(gameObject, CAMERA), width(w), height(h)
{
}

Camera::~Camera()
{
}

void Camera::render(Sprite *sprite, SDL_Renderer *renderer)
{
    float renderX = sprite->getGameObject()->getX() - getGameObject()->getX();
    float renderY = sprite->getGameObject()->getY() - getGameObject()->getY();

    float renderW = sprite->getWidth();
    float renderH = sprite->getHeight();

    renderX -= renderW / 2;
    renderY -= renderH / 2;

    SDL_FRect renderQuad = {renderX, renderY, renderW, renderH};
    SDL_RenderTextureRotated(renderer, sprite->getTexture(), sprite->getSourceRect(), &renderQuad, sprite->getGameObject()->getRotationInDegrees(), nullptr, sprite->getFlip());
}

// Getters
SDL_FRect Camera::getCamera() const
{
    SDL_FRect cameraRect = {getGameObject()->getX(), getGameObject()->getY(), width, height};
    return cameraRect;
}

// Setters
void Camera::setCamera(const SDL_FRect &camera)
{
    getGameObject()->setPosition({camera.x, camera.y});
    width = camera.w;
    height = camera.h;
}

float Camera::getWidth() const
{
    return width;
}

float Camera::getHeight() const
{
    return height;
}

void Camera::setWidth(float width)
{
    this->width = width;
}

void Camera::setHeight(float height)
{
    this->height = height;
}