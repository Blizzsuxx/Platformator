#pragma once

#include "texturewrapper.h"
#include <SDL3/SDL.h>
#include <string>
#include <vector>

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
    AnimationClip(double framesPerSecond, bool loop, float width, float height, const char *filePath);
    AnimationClip(const char *filePath);
    ~AnimationClip();

    const std::vector<AnimationFrame> &getFrames() const;
    double getFramesPerSecond() const;
    bool getLoop() const;
    float getWidth() const;
    float getHeight() const;
    const std::string &getFilePath() const;

    void addReference();
    bool removeReferenceAndFreeIfNoReferences();

    std::vector<AnimationFrame> frames;
    double framesPerSecond;
    bool loop;
    float width;
    float height;
    std::string filePath;
    size_t referenceCount;
};