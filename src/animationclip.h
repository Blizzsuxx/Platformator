#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <json.hpp>
#include "jsonhelpers.h"

#include "texturewrapper.h"

struct AnimationFrame
{
    TextureWrapper *textureWrapper;
    float duration;
    SDL_FRect sourceRect;
    bool hasSourceRect;

    AnimationFrame();
    AnimationFrame(TextureWrapper *textureWrapper, float duration);
    AnimationFrame(TextureWrapper *textureWrapper, const SDL_FRect &sourceRect, float duration);
    AnimationFrame(const AnimationFrame &other);
    AnimationFrame &operator=(const AnimationFrame &other);
    AnimationFrame(AnimationFrame &&other) noexcept;
    AnimationFrame &operator=(AnimationFrame &&other) noexcept;
    ~AnimationFrame();

    void setTextureWrapper(TextureWrapper *newTextureWrapper);
    TextureWrapper *getTextureWrapper() const;

private:
    void retainTexture() const;
    void releaseTexture();
};

struct AnimationClip : public Asset
{
    AnimationClip();
    AnimationClip(std::vector<AnimationFrame> frames, double framesPerSecond, bool loop, float width, float height, std::string name, std::string filePath = "");
    ~AnimationClip();

    const std::vector<AnimationFrame> &getFrames() const;
    double getFramesPerSecond() const;
    bool getLoop() const;
    float getWidth() const;
    float getHeight() const;
    const std::string &getName() const;

    void addReference();
    bool removeReferenceAndFreeIfNoReferences();

    std::vector<AnimationFrame> frames;
    double framesPerSecond;
    bool loop;
    float width;
    float height;
    std::string name;
    size_t referenceCount;
};

void to_json(nlohmann::json &j, const AnimationFrame &frame);
void from_json(const nlohmann::json &j, AnimationFrame &frame);

void to_json(nlohmann::json &j, const AnimationClip &clip);