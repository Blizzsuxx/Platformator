#include "platformator/animationclip.h"

#include <cmath>
#include <utility>

#include "runtimeaccess.h"

AnimationClip::AnimationClip() : Asset(), frames(), loop(true)
{
}

AnimationClip::AnimationClip(std::vector<AnimationFrame> frames, bool loop, std::string filePath)
    : Asset(std::move(filePath)), frames(std::move(frames)), loop(loop)
{
}

AnimationClip::~AnimationClip()
{
    frames.clear();
}

bool is_valid_animation_frame_duration(double durationSeconds) noexcept
{
    return std::isfinite(durationSeconds) && durationSeconds > 0.0;
}

const std::vector<AnimationFrame> &AnimationClip::getFrames() const
{
    return frames;
}

bool AnimationClip::getLoop() const
{
    return loop;
}

void AnimationClip::freeFromRuntime()
{
    platformator_detail::RuntimeAccess::freeAnimationClip(this);
}
