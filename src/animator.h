#pragma once

#include <cstddef>
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include "texturewrapper.h"

#include "gameobject.h"

class AnimationSetAsset;

class Animator : public Component
{
public:
    Animator(GameObject *gameObject);
    ~Animator() override;

    size_t getCurrentFrameIndex() const;
    float getPlaybackSpeed() const;
    bool getIsPlaying() const;
    bool getIsPaused() const;

    bool play(const AnimationClip *animationClip);
    void stop();
    void pause();
    void resume();
    void setPlaybackSpeed(float playbackSpeed);
    void update(double timeDelta);

    size_t getGameManagerIndex() const;
    void setGameManagerIndex(size_t index);

private:
    const AnimationClip *currentAnimationClip;
    size_t currentFrameIndex;
    double accumulatedTime;
    float playbackSpeed;
    bool playing;
    size_t gameManagerIndex;
    SDL_FlipMode flip;

    const AnimationClip *getCurrentClip() const;
    double getCurrentFrameDuration() const;
    void applyCurrentFrame() const;
    void advanceFrame();
};

template <>
struct ComponentTypeFor<Animator>
{
    static constexpr ComponentType value = ComponentType::ANIMATOR;
};
