#pragma once

#include <cstddef>
#include <SDL3/SDL.h>
#include <string>

#include "animationclip.h"
#include "gameobject.h"
#include <json.hpp>
#include "jsonhelpers.h"

class Animator : public Component
{
public:
    Animator(GameObject *gameObject);
    ~Animator() override;

    size_t getCurrentFrameIndex() const;
    float getPlaybackSpeed() const;
    bool getIsPlaying() const;
    bool getIsPaused() const;
    const AnimationClip *getCurrentAnimationClip() const;
    const std::string &getCurrentClipName() const;

    bool play(const AnimationClip *animationClip);
    void stop();
    void pause();
    void resume();
    void setPlaybackSpeed(float playbackSpeed);
    void update(double timeDelta);

    size_t getGameManagerIndex() const;
    void setGameManagerIndex(size_t index);
    const AnimationClip *getCurrentClip() const;
    void setCurrentClip(const AnimationClip *clip);
    void setCurrentFrameIndex(size_t frameIndex);

private:
    const AnimationClip *currentAnimationClip;
    size_t currentFrameIndex;
    double accumulatedTime;
    float playbackSpeed;
    bool playing;
    size_t gameManagerIndex;

    double getCurrentFrameDuration() const;
    void applyCurrentFrame() const;
    void advanceFrame();
};

template <>
struct ComponentTypeFor<Animator>
{
    static constexpr ComponentType value = ComponentType::ANIMATOR;
};

void to_json(nlohmann::json &j, const Animator &animator);
void from_json(const nlohmann::json &j, Animator &animator);