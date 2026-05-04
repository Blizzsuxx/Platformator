#include "camera.h"

#include <algorithm>

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

void Camera::render(Sprite *sprite, SDL_Renderer *renderer, int outputWidth, int outputHeight)
{
    float renderW = sprite->getWidth();
    float renderH = sprite->getHeight();

    float renderX = sprite->getGameObject()->getX() - renderW / 2.0f;
    float renderY = sprite->getGameObject()->getY() - renderH / 2.0f;

    SDL_FRect worldRect = {renderX, renderY, renderW, renderH};
    SDL_FRect renderQuad = worldToScreenRect(worldRect, outputWidth, outputHeight);
    SDL_RenderTextureRotated(renderer, sprite->getTexture(), sprite->getSourceRect(), &renderQuad, sprite->getGameObject()->getRotationInDegrees(), nullptr, sprite->getFlip());
}

Eigen::Vector2f Camera::worldToScreenPoint(const Eigen::Vector2f &worldPoint, int outputWidth, int outputHeight) const
{
    return Eigen::Vector2f(
        (worldPoint.x() - getGameObject()->getX()) * getScaleX(outputWidth),
        (worldPoint.y() - getGameObject()->getY()) * getScaleY(outputHeight));
}

Eigen::Vector2f Camera::worldToScreenSize(const Eigen::Vector2f &worldSize, int outputWidth, int outputHeight) const
{
    return Eigen::Vector2f(worldSize.x() * getScaleX(outputWidth), worldSize.y() * getScaleY(outputHeight));
}

SDL_FRect Camera::worldToScreenRect(const SDL_FRect &worldRect, int outputWidth, int outputHeight) const
{
    Eigen::Vector2f screenOrigin = worldToScreenPoint(Eigen::Vector2f(worldRect.x, worldRect.y), outputWidth, outputHeight);
    Eigen::Vector2f screenSize = worldToScreenSize(Eigen::Vector2f(worldRect.w, worldRect.h), outputWidth, outputHeight);
    return SDL_FRect{screenOrigin.x(), screenOrigin.y(), screenSize.x(), screenSize.y()};
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
    setWidth(camera.w);
    setHeight(camera.h);
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
    this->width = std::max(1.0f, width);
}

void Camera::setHeight(float height)
{
    this->height = std::max(1.0f, height);
}

float Camera::getScaleX(int outputWidth) const
{
    return static_cast<float>(outputWidth) / width;
}

float Camera::getScaleY(int outputHeight) const
{
    return static_cast<float>(outputHeight) / height;
}