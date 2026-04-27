#include "animator.h"

#include <algorithm>

#include "gamemanager.h"
#include "sprite.h"
#include "texturewrapper.h"

Animator::Animator(GameObject *gameObject)
    : Component(gameObject, ComponentType::ANIMATOR), currentAnimationSet(nullptr), currentFrameIndex(0), accumulatedTime(0.0f), playbackSpeed(1.0f), playing(false), gameManagerIndex(SIZE_MAX)
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

bool Animator::play(const AnimationClip **animationClip)
{
    if (currentAnimationSet == animationSet)
    {
        if (playing)
        {
            return true;
        }
    }

    currentAnimationSet = animationSet;
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
    if (!playing || clip == nullptr || clip->frames.empty())
    {
        return;
    }

    const double effectivePlaybackSpeed = static_cast<double>(playbackSpeed) * static_cast<double>(currentAnimationSet->getPlaybackSpeed());
    if (effectivePlaybackSpeed <= 0.0)
    {
        return;
    }

    accumulatedTime += timeDelta * effectivePlaybackSpeed;

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
    if (clip == nullptr || clip->frames.empty() || currentFrameIndex >= clip->frames.size())
    {
        return;
    }

    Sprite *sprite = getGameObject()->getComponent<Sprite>();
    if (sprite == nullptr)
    {
        return;
    }

    const AnimationFrame &frame = clip->frames[currentFrameIndex];
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

    float width = clip->width;
    float height = clip->height;
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
    if (currentAnimationSet == nullptr || currentAnimationSet->getClips().empty())
    {
        return nullptr;
    }

    return &currentAnimationSet->getClips().front();
}

double Animator::getCurrentFrameDuration() const
{
    const AnimationClip *clip = getCurrentClip();
    if (clip == nullptr || clip->frames.empty() || currentFrameIndex >= clip->frames.size())
    {
        return 0.0;
    }

    const AnimationFrame &frame = clip->frames[currentFrameIndex];
    if (frame.duration > 0.0f)
    {
        return frame.duration;
    }

    if (clip->framesPerSecond <= 0.0f)
    {
        return 0.0;
    }

    return 1.0 / clip->framesPerSecond;
}

void Animator::advanceFrame()
{
    const AnimationClip *clip = getCurrentClip();
    if (clip == nullptr || clip->frames.empty())
    {
        return;
    }

    if (currentFrameIndex + 1 < clip->frames.size())
    {
        currentFrameIndex++;
        applyCurrentFrame();
    }
    else if (clip->loop)
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

    return clip->name;
}

size_t Animator::getGameManagerIndex() const
{
    return gameManagerIndex;
}

void Animator::setGameManagerIndex(size_t index)
{
    gameManagerIndex = index;
}