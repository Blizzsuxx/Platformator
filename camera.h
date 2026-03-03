#pragma once

#include <SDL2/SDL.h>
#include "gameobject.h"
#include "sprite.h"
#include "constants.h"

class Camera : public Component
{
public:
    Camera(GameObject *gameObject);
    Camera(GameObject *gameObject, int w, int h);
    Camera(GameObject *gameObject, int x, int y, int w, int h);

    ~Camera();

    void render(Sprite *sprite, SDL_Renderer *renderer);

    // Getters
    const SDL_Rect &getCamera() const;

    // Setters
    void setCamera(const SDL_Rect &camera);

private:
    SDL_Rect camera;
};

template <>
struct ComponentTypeFor<Camera>
{
    static constexpr ComponentType value = ComponentType::CAMERA;
};