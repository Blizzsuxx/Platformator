#pragma once

#include "gameobject.h"
#include "sprite.h"
#include "constants.h"

#include <json.hpp>
#include "jsonhelpers.h"

class Camera : public Component
{
public:
    Camera(GameObject *gameObject);
    Camera(GameObject *gameObject, float w, float h);
    Camera(GameObject *gameObject, float x, float y, float w, float h);

    ~Camera();

    void render(Sprite *sprite, SDL_Renderer *renderer);

    // Getters
    const SDL_FRect &getCamera() const;

    // Setters
    void setCamera(const SDL_FRect &camera);

private:
    SDL_FRect camera;
};

template <>
struct ComponentTypeFor<Camera>
{
    static constexpr ComponentType value = ComponentType::CAMERA;
};

void to_json(nlohmann::json &j, const Camera &camera)
{
    j = nlohmann::json{{"camera", camera.getCamera()}};
}

void from_json(const nlohmann::json &j, Camera &camera)
{
    camera.setCamera(j.at("camera").get<SDL_FRect>());
}