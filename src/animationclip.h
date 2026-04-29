#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <vector>

#include "texturewrapper.h"

struct AnimationFrame
{
    TextureWrapper *textureWrapper;
    float duration;
    SDL_FRect sourceRect;
    bool hasSourceRect;

    AnimationFrame(TextureWrapper *textureWrapper, float duration);
    AnimationFrame(TextureWrapper *textureWrapper, const SDL_FRect &sourceRect, float duration);
    AnimationFrame(const AnimationFrame &other);
    AnimationFrame(AnimationFrame &&other) noexcept;
    ~AnimationFrame();

private:
    void retainTexture() const;
    void releaseTexture();
};

struct AnimationClip
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
    const std::string &getFilePath() const;

    void addReference();
    bool removeReferenceAndFreeIfNoReferences();

    std::vector<AnimationFrame> frames;
    double framesPerSecond;
    bool loop;
    float width;
    float height;
    std::string name;
    std::string filePath;
    size_t referenceCount;
};