#pragma once

#include <cstddef>

#include <SDL3/SDL.h>
#include <json.hpp>

#include "platformator/gameobject.h"
#include "platformator/texturewrapper.h"

class Sprite : public Component
{
public:
	Sprite();
	explicit Sprite(GameObject *gameObject, TextureWrapper *textureWrapper = nullptr,
	                SDL_FlipMode flip = SDL_FLIP_NONE, float width = 0.0f, float height = 0.0f);
	Sprite(GameObject *gameObject, TextureWrapper *textureWrapper, float width, float height);
	Sprite(GameObject *gameObject, const char *filePath, SDL_FlipMode flip = SDL_FLIP_NONE,
	       float width = 0.0f, float height = 0.0f);
	Sprite(GameObject *gameObject, const char *filePath, float width, float height);
	~Sprite() override = default;

	SDL_Texture *getTexture() const;
	TextureWrapper *getTextureWrapper() const;
	const TextureRegion &getTextureRegion() const;
	SDL_FlipMode getFlip() const;
	float getWidth() const;
	float getHeight() const;
	bool hasSourceRect() const;
	const SDL_FRect *getSourceRect() const;
	float getWidthWithoutScale() const;
	float getHeightWithoutScale() const;

	void setTextureWrapper(TextureWrapper *textureWrapper);
	void setTextureRegion(const TextureRegion &region);
	void setFlip(SDL_FlipMode flip);
	void setWidth(float width);
	void setHeight(float height);
	void setSourceRect(const SDL_FRect &sourceRect);
	void clearSourceRect();

	bool getIsRegisteredInWindow() const;
	void setIsRegisteredInWindow(bool isRegisteredInWindow);
	size_t getWindowIndex() const;
	void setWindowIndex(size_t windowIndex);

private:
	TextureRegion region;
	SDL_FlipMode flip;
	float width;
	float height;
	bool isRegisteredInWindow;
	size_t windowIndex;
};

template <>
struct ComponentTypeFor<Sprite>
{
	static constexpr ComponentType value = ComponentType::SPRITE;
};

void to_json(nlohmann::json &j, const Sprite &sprite);
void from_json(const nlohmann::json &j, Sprite &sprite);