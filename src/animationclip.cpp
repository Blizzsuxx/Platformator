#include "animationclip.h"

#include <utility>

#include "runtimeaccess.h"

AnimationFrame::AnimationFrame()
    : textureWrapper(nullptr), duration(0.0f), sourceRect{0.0f, 0.0f, 0.0f, 0.0f}, hasSourceRect(false)
{
}

AnimationFrame::AnimationFrame(TextureWrapper *textureWrapper, float duration)
    : textureWrapper(textureWrapper), duration(duration), sourceRect{0.0f, 0.0f, 0.0f, 0.0f}, hasSourceRect(false)
{
    retainTexture();
}

AnimationFrame::AnimationFrame(TextureWrapper *textureWrapper, const SDL_FRect &sourceRect, float duration)
    : textureWrapper(textureWrapper), duration(duration), sourceRect(sourceRect), hasSourceRect(true)
{
    retainTexture();
}

AnimationFrame::AnimationFrame(const AnimationFrame &other)
    : textureWrapper(other.textureWrapper), duration(other.duration), sourceRect(other.sourceRect), hasSourceRect(other.hasSourceRect)
{
    retainTexture();
}

AnimationFrame &AnimationFrame::operator=(const AnimationFrame &other)
{
    if (this == &other)
    {
        return *this;
    }

    setTextureWrapper(other.textureWrapper);
    duration = other.duration;
    sourceRect = other.sourceRect;
    hasSourceRect = other.hasSourceRect;
    return *this;
}

AnimationFrame::AnimationFrame(AnimationFrame &&other) noexcept
    : textureWrapper(other.textureWrapper), duration(other.duration), sourceRect(other.sourceRect), hasSourceRect(other.hasSourceRect)
{
    other.textureWrapper = nullptr;
}

AnimationFrame &AnimationFrame::operator=(AnimationFrame &&other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    releaseTexture();
    textureWrapper = other.textureWrapper;
    duration = other.duration;
    sourceRect = other.sourceRect;
    hasSourceRect = other.hasSourceRect;
    other.textureWrapper = nullptr;
    return *this;
}

AnimationFrame::~AnimationFrame()
{
    releaseTexture();
}

void AnimationFrame::retainTexture() const
{
    if (textureWrapper != nullptr)
    {
        textureWrapper->addReference();
    }
}

void AnimationFrame::releaseTexture()
{
    if (textureWrapper != nullptr)
    {
        TextureWrapper *currentTextureWrapper = textureWrapper;
        textureWrapper = nullptr;
        currentTextureWrapper->removeReferenceAndFreeIfNoReferences();
    }
}

TextureWrapper *AnimationFrame::getTextureWrapper() const
{
    return textureWrapper;
}

void AnimationFrame::setTextureWrapper(TextureWrapper *newTextureWrapper)
{
    if (textureWrapper != newTextureWrapper)
    {
        releaseTexture();
        textureWrapper = newTextureWrapper;
        retainTexture();
    }
}

AnimationClip::AnimationClip()
    : Asset(), frames(), framesPerSecond(12.0), loop(true), width(0.0f), height(0.0f), name(), referenceCount(0)
{
}

AnimationClip::AnimationClip(std::vector<AnimationFrame> frames, double framesPerSecond, bool loop, float width, float height, std::string name, std::string filePath)
    : Asset(std::move(filePath)), frames(std::move(frames)), framesPerSecond(framesPerSecond), loop(loop), width(width), height(height), name(std::move(name)), referenceCount(0)
{
}

AnimationClip::~AnimationClip()
{
    frames.clear();
}

const std::vector<AnimationFrame> &AnimationClip::getFrames() const
{
    return frames;
}

double AnimationClip::getFramesPerSecond() const
{
    return framesPerSecond;
}

bool AnimationClip::getLoop() const
{
    return loop;
}

float AnimationClip::getWidth() const
{
    return width;
}

float AnimationClip::getHeight() const
{
    return height;
}

const std::string &AnimationClip::getName() const
{
    return name;
}

void AnimationClip::addReference()
{
    referenceCount++;
}

bool AnimationClip::removeReferenceAndFreeIfNoReferences()
{
    if (referenceCount > 0)
    {
        referenceCount--;
    }
    if (referenceCount == 0 && !getFilePath().empty())
    {
        platformator_detail::RuntimeAccess::freeAnimationClip(this);
        return true;
    }

    return false;
}