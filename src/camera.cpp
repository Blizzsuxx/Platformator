#include "platformator/camera.h"

#include <algorithm>
#include <cmath>

#include "constants.h"
#include "platformator/sprite.h"
#include "spriterendergeometry.h"
#include "workprobe.h"

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
    const auto view = prepareRenderView(outputWidth, outputHeight, false);
    renderWithView(sprite, renderer, view);
}

platformator_detail::SpriteRenderView Camera::prepareRenderView(int outputWidth, int outputHeight,
                                                                 bool cullingEligible) const
{
    const auto &cameraPosition = getGameObject()->worldPosition;
    const float scaleX = getScaleX(outputWidth);
    const float scaleY = getScaleY(outputHeight);
    platformator_detail::SpriteRenderView view{cameraPosition.x(), cameraPosition.y(), scaleX, scaleY,
                                               outputWidth, outputHeight, false};
    const bool validMapping = outputWidth > 0 && outputHeight > 0 && std::isfinite(view.cameraX) &&
                              std::isfinite(view.cameraY) && std::isfinite(scaleX) && scaleX > 0.0f &&
                              std::isfinite(scaleY) && scaleY > 0.0f;
    view.cullingEligible = cullingEligible && validMapping;
    PLATFORMATOR_RENDER_VIEW_PREPARED();
    return view;
}

void Camera::renderWithView(Sprite *sprite, SDL_Renderer *renderer,
                            const platformator_detail::SpriteRenderView &view, bool applyCulling) const
{
    SDL_FRect renderQuad{};
    float angleDegrees = 0.0f;
    prepareSpriteGeometry(sprite, view, renderQuad, angleDegrees);

    if (applyCulling && view.cullingEligible && sprite->getTexture() != nullptr)
    {
        PLATFORMATOR_RENDER_VISIBILITY_CHECK();
        if (platformator_detail::classifySpriteForRender(renderQuad, angleDegrees, view) ==
            platformator_detail::SpriteRenderClassification::Reject)
            return;
    }
    PLATFORMATOR_RENDER_SUBMISSION(sprite);
    platformator_detail::submitSpriteRender(renderer, sprite->getTexture(), sprite->getSourceRect(),
                                             renderQuad, angleDegrees, sprite->getFlip());
}

void Camera::prepareSpriteGeometry(Sprite *sprite, const platformator_detail::SpriteRenderView &view,
                                  SDL_FRect &renderQuad, float &angleDegrees) const
{
    float renderW = sprite->getWidth();
    float renderH = sprite->getHeight();

    const auto &spritePosition = getPublishedSpritePosition(sprite);
    renderQuad = platformator_detail::makeSpriteRenderDestination(
        spritePosition.x(), spritePosition.y(), renderW, renderH, view);
    angleDegrees = sprite->getGameObject()->getRotationInDegrees();
}

const Eigen::Vector2f &Camera::getPublishedSpritePosition(Sprite *sprite) const
{
    return sprite->getGameObject()->worldPosition;
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
