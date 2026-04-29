#include "animator.h"

#include <algorithm>

#include "sprite.h"
#include "texturewrapper.h"

Animator::Animator(GameObject *gameObject)
    : Component(gameObject, ComponentType::ANIMATOR), currentAnimationClip(nullptr), currentFrameIndex(0), accumulatedTime(0.0f), playbackSpeed(1.0f), playing(false), gameManagerIndex(SIZE_MAX)
{
}

Animator::~Animator()
{
}

size_t Animator::getCurrentFrameIndex() const
{
    return currentFrameIndex;
}

float Animator::getPlaybackSpeed() const
{
    return playbackSpeed;
}

bool Animator::getIsPlaying() const
{
    return playing;
}

bool Animator::getIsPaused() const
{
    return !playing;
}

bool Animator::play(const AnimationClip *animationClip)
{
    if (animationClip == nullptr || animationClip->getFrames().empty())
    {
        return false;
    }

    if (currentAnimationClip == animationClip)
    {
        if (playing)
        {
            return true;
        }

        playing = true;
        return true;
    }

    currentAnimationClip = animationClip;
    currentFrameIndex = 0;
    accumulatedTime = 0.0;
    playing = true;
    applyCurrentFrame();
    return true;
}

void Animator::stop()
{
    currentFrameIndex = 0;
    accumulatedTime = 0.0;
    playing = false;
    applyCurrentFrame();
}

void Animator::pause()
{
    playing = false;
}

void Animator::resume()
{
    playing = getCurrentClip() != nullptr;
}

void Animator::setPlaybackSpeed(float playbackSpeed)
{
    this->playbackSpeed = std::max(0.0f, playbackSpeed);
}

void Animator::update(double timeDelta)
{
    const AnimationClip *clip = getCurrentClip();
    if (!playing || clip == nullptr || clip->getFrames().empty() || playbackSpeed <= 0.0f)
    {
        return;
    }

    accumulatedTime += timeDelta * static_cast<double>(playbackSpeed);

    while (playing)
    {
        const double frameDuration = getCurrentFrameDuration();
        if (frameDuration <= 0.0 || accumulatedTime < frameDuration)
        {
            break;
        }

        accumulatedTime -= frameDuration;
        advanceFrame();
    }
}

void Animator::applyCurrentFrame() const
{
    const AnimationClip *clip = getCurrentClip();
    if (clip == nullptr || clip->getFrames().empty() || currentFrameIndex >= clip->getFrames().size())
    {
        return;
    }

    Sprite *sprite = getGameObject()->getComponent<Sprite>();
    if (sprite == nullptr)
    {
        return;
    }

    const AnimationFrame &frame = clip->getFrames()[currentFrameIndex];
    TextureWrapper *textureWrapper = frame.textureWrapper;
    sprite->setTextureWrapper(textureWrapper);

    if (frame.hasSourceRect)
    {
        sprite->setSourceRect(frame.sourceRect);
    }
    else
    {
        sprite->clearSourceRect();
    }

    float width = clip->getWidth();
    float height = clip->getHeight();
    if (width <= 0.0f && frame.hasSourceRect)
    {
        width = frame.sourceRect.w;
    }
    if (height <= 0.0f && frame.hasSourceRect)
    {
        height = frame.sourceRect.h;
    }

    if ((width <= 0.0f || height <= 0.0f) && textureWrapper != nullptr && textureWrapper->getTexture() != nullptr)
    {
        float textureWidth = 0.0f;
        float textureHeight = 0.0f;
        SDL_GetTextureSize(textureWrapper->getTexture(), &textureWidth, &textureHeight);
        if (width <= 0.0f)
        {
            width = textureWidth;
        }
        if (height <= 0.0f)
        {
            height = textureHeight;
        }
    }

    sprite->setWidth(width);
    sprite->setHeight(height);
}

const AnimationClip *Animator::getCurrentClip() const
{
    return currentAnimationClip;
}

double Animator::getCurrentFrameDuration() const
{
    const AnimationClip *clip = getCurrentClip();
    if (clip == nullptr || clip->getFrames().empty() || currentFrameIndex >= clip->getFrames().size())
    {
        return 0.0;
    }

    const AnimationFrame &frame = clip->getFrames()[currentFrameIndex];
    if (frame.duration > 0.0f)
    {
        return frame.duration;
    }

    if (clip->getFramesPerSecond() <= 0.0f)
    {
        return 0.0;
    }

    return 1.0 / clip->getFramesPerSecond();
}

void Animator::advanceFrame()
{
    const AnimationClip *clip = getCurrentClip();
    if (clip == nullptr || clip->getFrames().empty())
    {
        return;
    }

    if (currentFrameIndex + 1 < clip->getFrames().size())
    {
        currentFrameIndex++;
        applyCurrentFrame();
    }
    else if (clip->getLoop())
    {
        currentFrameIndex = 0;
        applyCurrentFrame();
    }
    else
    {
        accumulatedTime = 0.0;
        playing = false;
    }
}

const std::string &Animator::getCurrentClipName() const
{
    const AnimationClip *clip = getCurrentClip();
    if (clip == nullptr)
    {
        static const std::string emptyString;
        return emptyString;
    }

    return clip->getName();
}

size_t Animator::getGameManagerIndex() const
{
    return gameManagerIndex;
}

void Animator::setGameManagerIndex(size_t index)
{
    gameManagerIndex = index;
}