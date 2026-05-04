#include "camera.h"

#include <algorithm>

Camera::Camera() : Component(ComponentType::CAMERA), width(SCREEN_WIDTH), height(SCREEN_HEIGHT)
{
}

Camera::Camera(GameObject *gameObject) : Component(gameObject, CAMERA), width(SCREEN_WIDTH), height(SCREEN_HEIGHT)
{
}

Camera::Camera(GameObject *gameObject, float x, float y, float w, float h) : Component(gameObject, CAMERA), width(w), height(h)
{
    gameObject->setPosition(Eigen::Vector2f(x, y));
}

Camera::Camera(GameObject *gameObject, float w, float h) : Component(gameObject, CAMERA), width(w), height(h)
{
}

Camera::~Camera()
{
}

void Camera::render(Sprite *sprite, SDL_Renderer *renderer, int outputWidth, int outputHeight, bool keepAspectRatio)
{
    float renderW = sprite->getWidth();
    float renderH = sprite->getHeight();

    float renderX = sprite->getGameObject()->getX() - renderW / 2.0f;
    float renderY = sprite->getGameObject()->getY() - renderH / 2.0f;

    SDL_FRect worldRect = {renderX, renderY, renderW, renderH};
    SDL_FRect renderQuad = worldToScreenRect(worldRect, outputWidth, outputHeight, keepAspectRatio);
    SDL_RenderTextureRotated(renderer, sprite->getTexture(), sprite->getSourceRect(), &renderQuad, sprite->getGameObject()->getRotationInDegrees(), nullptr, sprite->getFlip());
}

Eigen::Vector2f Camera::worldToScreenPoint(const Eigen::Vector2f &worldPoint, int outputWidth, int outputHeight, bool keepAspectRatio) const
{
    return Eigen::Vector2f(
        getOffsetX(outputWidth, outputHeight, keepAspectRatio) + (worldPoint.x() - getGameObject()->getX()) * getScaleX(outputWidth, outputHeight, keepAspectRatio),
        getOffsetY(outputWidth, outputHeight, keepAspectRatio) + (worldPoint.y() - getGameObject()->getY()) * getScaleY(outputWidth, outputHeight, keepAspectRatio));
}

Eigen::Vector2f Camera::worldToScreenSize(const Eigen::Vector2f &worldSize, int outputWidth, int outputHeight, bool keepAspectRatio) const
{
    return Eigen::Vector2f(
        worldSize.x() * getScaleX(outputWidth, outputHeight, keepAspectRatio),
        worldSize.y() * getScaleY(outputWidth, outputHeight, keepAspectRatio));
}

SDL_FRect Camera::worldToScreenRect(const SDL_FRect &worldRect, int outputWidth, int outputHeight, bool keepAspectRatio) const
{
    Eigen::Vector2f screenOrigin = worldToScreenPoint(Eigen::Vector2f(worldRect.x, worldRect.y), outputWidth, outputHeight, keepAspectRatio);
    Eigen::Vector2f screenSize = worldToScreenSize(Eigen::Vector2f(worldRect.w, worldRect.h), outputWidth, outputHeight, keepAspectRatio);
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

float Camera::getOffsetX(int outputWidth, int outputHeight, bool keepAspectRatio) const
{
    if (!keepAspectRatio)
    {
        return 0.0f;
    }

    return (static_cast<float>(outputWidth) - width * getUniformScale(outputWidth, outputHeight)) * 0.5f;
}

float Camera::getOffsetY(int outputWidth, int outputHeight, bool keepAspectRatio) const
{
    if (!keepAspectRatio)
    {
        return 0.0f;
    }

    return (static_cast<float>(outputHeight) - height * getUniformScale(outputWidth, outputHeight)) * 0.5f;
}

float Camera::getScaleX(int outputWidth, int outputHeight, bool keepAspectRatio) const
{
    if (!keepAspectRatio)
    {
        return static_cast<float>(outputWidth) / width;
    }

    return getUniformScale(outputWidth, outputHeight);
}

float Camera::getScaleY(int outputWidth, int outputHeight, bool keepAspectRatio) const
{
    if (!keepAspectRatio)
    {
        return static_cast<float>(outputHeight) / height;
    }

    return getUniformScale(outputWidth, outputHeight);
}

float Camera::getUniformScale(int outputWidth, int outputHeight) const
{
    return std::min(static_cast<float>(outputWidth) / width, static_cast<float>(outputHeight) / height);
}