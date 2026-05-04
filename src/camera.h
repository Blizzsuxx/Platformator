#pragma once

#include "gameobject.h"
#include "sprite.h"
#include "constants.h"

#include <json.hpp>
#include "jsonhelpers.h"

class Camera : public Component
{
public:
    Camera();
    Camera(GameObject *gameObject);
    Camera(GameObject *gameObject, float w, float h);

    ~Camera();

    void render(Sprite *sprite, SDL_Renderer *renderer, int outputWidth, int outputHeight);
    Eigen::Vector2f worldToScreenPoint(const Eigen::Vector2f &worldPoint, int outputWidth, int outputHeight) const;
    Eigen::Vector2f worldToScreenSize(const Eigen::Vector2f &worldSize, int outputWidth, int outputHeight) const;
    SDL_FRect worldToScreenRect(const SDL_FRect &worldRect, int outputWidth, int outputHeight) const;

    // Getters
    SDL_FRect getCamera() const;
    float getHeight() const;
    float getWidth() const;

    // Setters
    void setCamera(const SDL_FRect &camera);
    void setHeight(float height);
    void setWidth(float width);

private:
    float width;
    float height;

    float getScaleX(int outputWidth) const;
    float getScaleY(int outputHeight) const;
};

template <>
struct ComponentTypeFor<Camera>
{
    static constexpr ComponentType value = ComponentType::CAMERA;
};

void to_json(nlohmann::json &j, const Camera &camera);
void from_json(const nlohmann::json &j, Camera &camera);