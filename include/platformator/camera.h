#pragma once

#include <SDL3/SDL.h>
#include <json.hpp>

#include "platformator/gameobject.h"

class Sprite;
class SDLWindow;

namespace platformator_detail
{
struct SpriteRenderView;
}

class Camera : public Component
{
public:
	Camera();
	Camera(GameObject *gameObject);
	Camera(GameObject *gameObject, float x, float y, float w, float h);
	Camera(GameObject *gameObject, float w, float h);
	~Camera();

	void render(Sprite *sprite, SDL_Renderer *renderer, int outputWidth, int outputHeight);
	Eigen::Vector2f worldToScreenPoint(const Eigen::Vector2f &worldPoint, int outputWidth, int outputHeight) const;
	Eigen::Vector2f worldToScreenSize(const Eigen::Vector2f &worldSize, int outputWidth, int outputHeight) const;
	SDL_FRect worldToScreenRect(const SDL_FRect &worldRect, int outputWidth, int outputHeight) const;

	SDL_FRect getCamera() const;
	float getHeight() const;
	float getWidth() const;

	void setCamera(const SDL_FRect &camera);
	void setHeight(float height);
	void setWidth(float width);

private:
    friend class SDLWindow;

	float width;
	float height;

    platformator_detail::SpriteRenderView prepareRenderView(int outputWidth, int outputHeight,
                                                             bool cullingEligible) const;
    void renderWithView(Sprite *sprite, SDL_Renderer *renderer,
                        const platformator_detail::SpriteRenderView &view, bool applyCulling = true) const;
    const Eigen::Vector2f &getPublishedSpritePosition(Sprite *sprite) const;
    void prepareSpriteGeometry(Sprite *sprite, const platformator_detail::SpriteRenderView &view,
                               SDL_FRect &destination, float &angleDegrees) const;

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
