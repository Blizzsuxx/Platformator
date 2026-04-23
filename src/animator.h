#pragma once

#include <cstddef>
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include "texturewrapper.h"

#include "gameobject.h"

struct AnimationFrame
{
    TextureWrapper *textureWrapper;
    float duration;
    SDL_FRect sourceRect;
    bool hasSourceRect;

    AnimationFrame(TextureWrapper *textureWrapper, float duration);
    AnimationFrame(TextureWrapper *textureWrapper, const SDL_FRect &sourceRect, float duration);
};

struct AnimationClip
{
    std::vector<AnimationFrame> frames;
    double framesPerSecond;
    bool loop;
    float width;
    float height;
    std::string name;
    bool stopAtEnd;

    AnimationClip();
    AnimationClip(const std::vector<TextureWrapper *> &frameTextures, double framesPerSecond, bool loop, float width, float height, const std::string &name, bool stopAtClipEnd);
    AnimationClip(const std::vector<AnimationFrame> &frames, double framesPerSecond, bool loop, float width, float height, const std::string &name, bool stopAtClipEnd);
};

class Animator : public Component
{
public:
    Animator(GameObject *gameObject);
    ~Animator() override;

    const std::vector<AnimationClip> &getClips() const;
    size_t getCurrentFrameIndex() const;
    float getPlaybackSpeed() const;
    bool getIsPlaying() const;
    bool getIsPaused() const;

    void addClip(const AnimationClip &clip);
    void addClip(AnimationClip &&clip);
    bool removeClip(const std::string &name);

    bool play(const std::string &name);
    bool play(size_t clipIndex);
    void stop();
    void pause();
    void resume();
    void setPlaybackSpeed(float playbackSpeed);
    void update(double timeDelta);
    const std::string &getCurrentClipName() const;

private:
    std::vector<AnimationClip> clips;
    size_t currentClipIndex;
    size_t currentFrameIndex;
    double accumulatedTime;
    float playbackSpeed;
    bool playing;

    size_t findClipIndex(const std::string &name) const;
    double getCurrentFrameDuration() const;
    void applyCurrentFrame() const;
    void advanceFrame();
    void playNext();
};

template <>
struct ComponentTypeFor<Animator>
{
    static constexpr ComponentType value = ComponentType::ANIMATOR;
};
