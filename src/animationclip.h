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
    AnimationFrame(AnimationFrame &&other) noexcept;
    ~AnimationFrame();

    void setTextureWrapper(TextureWrapper *newTextureWrapper);
    TextureWrapper *getTextureWrapper() const;

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

void to_json(nlohmann::json &j, const AnimationFrame &frame)
{
    j = nlohmann::json{
        {"duration", frame.duration},
        {"sourceRect", frame.sourceRect},
        {"hasSourceRect", frame.hasSourceRect},
        {"textureWrapperFilePath", frame.textureWrapper ? frame.textureWrapper->getFilePath() : ""}};
}
void from_json(const nlohmann::json &j, AnimationFrame &frame)
{
    j.at("duration").get_to(frame.duration);
    j.at("hasSourceRect").get_to(frame.hasSourceRect);
    j.at("sourceRect").get_to(frame.sourceRect);
    std::string textureWrapperFilePath;
    j.at("textureWrapperFilePath").get_to(textureWrapperFilePath);
    if (!textureWrapperFilePath.empty())
    {
        frame.setTextureWrapper(GameManager::getInstance().loadTexture(textureWrapperFilePath));
    }
    else
    {
        frame.setTextureWrapper(nullptr);
    }
}

void to_json(nlohmann::json &j, const AnimationClip &clip)
{
    j = nlohmann::json{
        {"frames", clip.frames},
        {"framesPerSecond", clip.framesPerSecond},
        {"loop", clip.loop},
        {"width", clip.width},
        {"height", clip.height},
        {"name", clip.name},
        {"filePath", clip.filePath}};
}
void from_json(const nlohmann::json &j, AnimationClip &clip)
{
    j.at("frames").get_to(clip.frames);
    j.at("framesPerSecond").get_to(clip.framesPerSecond);
    j.at("loop").get_to(clip.loop);
    j.at("width").get_to(clip.width);
    j.at("height").get_to(clip.height);
    j.at("name").get_to(clip.name);
    j.at("filePath").get_to(clip.filePath);
}