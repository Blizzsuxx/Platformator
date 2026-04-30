#include "animator.h"
#include "animationclip.h"
#include "gamemanager.h"

void to_json(nlohmann::json &j, const Animator &animator)
{
    const AnimationClip *currentClip = animator.getCurrentClip();

    j = nlohmann::json{
        {"currentFrameIndex", animator.getCurrentFrameIndex()},
        {"playbackSpeed", animator.getPlaybackSpeed()},
        {"playing", animator.getIsPlaying()},
        {"type", ComponentType::ANIMATOR},
        {"animationClipFilePath", currentClip ? currentClip->getFilePath() : ""}};
}

void from_json(const nlohmann::json &j, Animator &animator)
{
    std::string animationClipFilePath;
    j.at("animationClipFilePath").get_to(animationClipFilePath);
    if (!animationClipFilePath.empty())
    {
        const AnimationClip *clip = GameManager::getInstance().loadAnimationClip(animationClipFilePath);
        animator.setCurrentClip(clip);
    }

    animator.setCurrentFrameIndex(j.at("currentFrameIndex").get<size_t>());
    animator.setPlaybackSpeed(j.at("playbackSpeed").get<float>());
    bool playing;
    j.at("playing").get_to(playing);
    if (playing)
    {
        animator.play(animator.getCurrentClip());
    }
}