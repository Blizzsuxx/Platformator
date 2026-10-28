#include "platformator/camera.h"

#include <algorithm>

#include "constants.h"
#include "platformator/sprite.h"
#include "runtimeaccess.h"
#include "sdlwindow.h"

Camera::Camera() : Component(ComponentType::CAMERA), width(SCREEN_WIDTH), height(SCREEN_HEIGHT)
{
}

Camera::Camera(GameObject *gameObject) : Component(gameObject, CAMERA), width(SCREEN_WIDTH), height(SCREEN_HEIGHT)
{
}

Camera::Camera(GameObject *gameObject, float x, float y, float w, float h)
    : Component(gameObject, CAMERA), width(w), height(h)
{
    gameObject->setPosition(Eigen::Vector2f(x, y));
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

    const auto &spritePosition = sprite->getGameObject()->worldPosition;
    const auto &cameraPosition = getGameObject()->worldPosition;
    const float scaleX = getScaleX(outputWidth), scaleY = getScaleY(outputHeight);
    SDL_FRect renderQuad = {(spritePosition.x() - renderW * 0.5f - cameraPosition.x()) * scaleX,
                            (spritePosition.y() - renderH * 0.5f - cameraPosition.y()) * scaleY, renderW * scaleX,
                            renderH * scaleY};
    SDL_RenderTextureRotated(renderer, sprite->getTexture(), sprite->getSourceRect(), &renderQuad,
                             sprite->getGameObject()->getRotationInDegrees(), nullptr, sprite->getFlip());
}

Eigen::Vector2f Camera::worldToScreenPoint(const Eigen::Vector2f &worldPoint, int outputWidth, int outputHeight) const
{
    return Eigen::Vector2f((worldPoint.x() - getGameObject()->getX()) * getScaleX(outputWidth),
                           (worldPoint.y() - getGameObject()->getY()) * getScaleY(outputHeight));
}

Eigen::Vector2f Camera::worldToScreenSize(const Eigen::Vector2f &worldSize, int outputWidth, int outputHeight) const
{
    return Eigen::Vector2f(worldSize.x() * getScaleX(outputWidth), worldSize.y() * getScaleY(outputHeight));
}

SDL_FRect Camera::worldToScreenRect(const SDL_FRect &worldRect, int outputWidth, int outputHeight) const
{
    Eigen::Vector2f screenOrigin =
        worldToScreenPoint(Eigen::Vector2f(worldRect.x, worldRect.y), outputWidth, outputHeight);
    Eigen::Vector2f screenSize =
        worldToScreenSize(Eigen::Vector2f(worldRect.w, worldRect.h), outputWidth, outputHeight);
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
    const float nextWidth = std::max(1.0f, width);
    if (this->width == nextWidth)
        return;
    this->width = nextWidth;
    if (getGameObject() && getHandle())
        platformator_detail::RuntimeAccess::window()->markViewDirty();
}

void Camera::setHeight(float height)
{
    const float nextHeight = std::max(1.0f, height);
    if (this->height == nextHeight)
        return;
    this->height = nextHeight;
    if (getGameObject() && getHandle())
        platformator_detail::RuntimeAccess::window()->markViewDirty();
}

float Camera::getScaleX(int outputWidth) const
{
    return static_cast<float>(outputWidth) / width;
}

float Camera::getScaleY(int outputHeight) const
{
    return static_cast<float>(outputHeight) / height;
}
