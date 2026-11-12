#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <json.hpp>

#include "platformator/texturewrapper.h"

struct AnimationFrame
{
    TextureRegion region;
    float duration = 0.1f;

    AnimationFrame() = default;
    AnimationFrame(TextureWrapper *textureWrapper, float duration)
        : region(textureWrapper), duration(duration)
    {
    }
    AnimationFrame(TextureWrapper *textureWrapper, const SDL_FRect &sourceRect, float duration)
        : region(textureWrapper, sourceRect), duration(duration)
    {
    }

    void setTextureWrapper(TextureWrapper *newTextureWrapper)
    {
        region.setTextureWrapper(newTextureWrapper);
    }
    TextureWrapper *getTextureWrapper() const noexcept
    {
        return region.getTextureWrapper();
    }
};

struct AnimationClip : public Asset
{
    AnimationClip();
    AnimationClip(std::vector<AnimationFrame> frames, bool loop, std::string filePath = "");
    ~AnimationClip() override;

    const std::vector<AnimationFrame> &getFrames() const;
    bool getLoop() const;

    std::vector<AnimationFrame> frames;
    bool loop = true;

protected:
    void freeFromRuntime() override;
};

bool is_valid_animation_frame_duration(double durationSeconds) noexcept;

void to_json(nlohmann::json &j, const AnimationFrame &frame);
void from_json(const nlohmann::json &j, AnimationFrame &frame);

void to_json(nlohmann::json &j, const AnimationClip &clip);
void from_json(const nlohmann::json &j, AnimationClip &clip);
