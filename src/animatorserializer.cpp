#include "scenecomponentio.h"
#include "referenceresolver.h"
#include "platformator/animator.h"
#include "platformator/animationclip.h"
#include "runtimeaccess.h"

void to_json(nlohmann::json &j, const Animator &animator)
{
    const AnimationClip *currentClip = animator.getCurrentClip();

    j = nlohmann::json{{"id", animator.getId()},
                       {"currentFrameIndex", animator.getCurrentFrameIndex()},
                       {"playbackSpeed", animator.getPlaybackSpeed()},
                       {"playing", animator.getIsPlaying()},
                       {"type", ComponentType::ANIMATOR},
                       {"animationClipFilePath", currentClip ? currentClip->getFilePath() : ""}};
}

void deserialize(const nlohmann::json &j, Animator &animator, platformator::ReferenceResolver &resolver)
{
    animator.setId(j.at("id").get<int>());

    std::string animationClipFilePath;
    j.at("animationClipFilePath").get_to(animationClipFilePath);
    if (!animationClipFilePath.empty())
    {
        const AnimationClip *clip = resolver.loadAnimationClip(animationClipFilePath);
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
void from_json(const nlohmann::json &j, Animator &animator)
{
    platformator_detail::ActiveReferenceResolver resolver(platformator::Runtime::current());
    deserialize(j, animator, resolver);
}
