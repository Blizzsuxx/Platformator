#pragma once

#include <cstddef>
#include <cstdint>
#include <new>

#include <SDL3/SDL.h>
#include <json.hpp>

#include "platformator/gameobject.h"
#include "platformator/texturewrapper.h"

class Sprite : public Component
{
public:
	static void *operator new(size_t size);
	static void operator delete(void *ptr, size_t size) noexcept;
	static void *operator new(size_t size, const std::nothrow_t &) noexcept;
	static void operator delete(void *ptr, const std::nothrow_t &) noexcept;
	static void *operator new(size_t, void *ptr) noexcept
	{
		return ptr;
	}
	static void operator delete(void *, void *) noexcept
	{
	}
	static void *operator new(size_t size, std::align_val_t alignment)
	{
		return ::operator new(size, alignment);
	}
	static void operator delete(void *ptr, size_t, std::align_val_t alignment) noexcept
	{
		::operator delete(ptr, alignment);
	}
	static void operator delete(void *ptr, std::align_val_t alignment) noexcept
	{
		::operator delete(ptr, alignment);
	}
	static void *operator new(size_t size, std::align_val_t alignment, const std::nothrow_t &tag) noexcept
	{
		return ::operator new(size, alignment, tag);
	}
	static void operator delete(void *ptr, std::align_val_t alignment, const std::nothrow_t &) noexcept
	{
		::operator delete(ptr, alignment);
	}

	Sprite();
	explicit Sprite(GameObject *gameObject, TextureWrapper *textureWrapper = nullptr,
	                SDL_FlipMode flip = SDL_FLIP_NONE, float width = 0.0f, float height = 0.0f);
	Sprite(GameObject *gameObject, TextureWrapper *textureWrapper, float width, float height);
	Sprite(GameObject *gameObject, const char *filePath, SDL_FlipMode flip = SDL_FLIP_NONE,
	       float width = 0.0f, float height = 0.0f);
	Sprite(GameObject *gameObject, const char *filePath, float width, float height);
	~Sprite() override = default;
	Component *clone() const override;

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
	std::int32_t getZOrder() const;
	SDL_Color getColor() const;
	std::uint8_t getAlpha() const;

	void setTextureWrapper(TextureWrapper *textureWrapper);
	void setTextureRegion(const TextureRegion &region);
	void setFlip(SDL_FlipMode flip);
	void setWidth(float width);
	void setHeight(float height);
	void setSourceRect(const SDL_FRect &sourceRect);
	void clearSourceRect();
	void setZOrder(std::int32_t zOrder);
	void setColor(const SDL_Color &color);
	void setColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255);
	void setAlpha(std::uint8_t alpha);

	bool getIsRegisteredInWindow() const;
	void setIsRegisteredInWindow(bool isRegisteredInWindow);
	size_t getWindowIndex() const;
	void setWindowIndex(size_t windowIndex);

private:
	TextureRegion region;
	SDL_FlipMode flip;
	float width;
	float height;
	std::int32_t zOrder = 0;
	SDL_Color color{255, 255, 255, 255};
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