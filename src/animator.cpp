#include "animator.h"

#include <algorithm>

#include "gamemanager.h"
#include "sprite.h"
#include "texturewrapper.h"

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

AnimationFrame::AnimationFrame(AnimationFrame &&other) noexcept
    : textureWrapper(other.textureWrapper), duration(other.duration), sourceRect(other.sourceRect), hasSourceRect(other.hasSourceRect)
{
    other.textureWrapper = nullptr;
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

AnimationClip::AnimationClip() : frames(), framesPerSecond(12.0f), loop(true), width(0.0f), height(0.0f), name(""), stopAtEnd(false)
{
}

AnimationClip::AnimationClip(const std::vector<TextureWrapper *> &frameTextures, double framesPerSecond, bool loop, float width, float height, const std::string &name, bool stopAtClipEnd)
    : frames(), framesPerSecond(framesPerSecond), loop(loop), width(width), height(height), name(name), stopAtEnd(stopAtClipEnd)
{
    frames.reserve(frameTextures.size());
    for (TextureWrapper *textureWrapper : frameTextures)
    {
        frames.emplace_back(textureWrapper, 1.0f / framesPerSecond);
    }
}

AnimationClip::AnimationClip(const std::vector<AnimationFrame> &frames, double framesPerSecond, bool loop, float width, float height, const std::string &name, bool stopAtClipEnd)
    : frames(frames), framesPerSecond(framesPerSecond), loop(loop), width(width), height(height), name(name), stopAtEnd(stopAtClipEnd)
{
}

Animator::Animator(GameObject *gameObject)
    : Component(gameObject, ComponentType::ANIMATOR), clips(), currentClipIndex(SIZE_MAX), currentFrameIndex(0), accumulatedTime(0.0), playbackSpeed(1.0f), playing(false), gameManagerIndex(SIZE_MAX)
{
}

Animator::~Animator()
{
}

const std::vector<AnimationClip> &Animator::getClips() const
{
    return clips;
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

void Animator::addClip(const AnimationClip &clip)
{

    clips.push_back(clip);
}

void Animator::addClip(AnimationClip &&clip)
{
    clips.push_back(std::move(clip));
}

bool Animator::removeClip(const std::string &name)
{
    size_t clipIndex = findClipIndex(name);
    if (clipIndex == SIZE_MAX)
    {
        return false;
    }

    clips.erase(clips.begin() + static_cast<std::ptrdiff_t>(clipIndex));

    if (currentClipIndex == clipIndex)
    {
        currentFrameIndex = 0;
        accumulatedTime = 0.0;
    }
    else if (currentClipIndex != SIZE_MAX && clipIndex < currentClipIndex)
    {
        currentClipIndex--;
    }

    return true;
}

bool Animator::play(const std::string &name)
{
    size_t clipIndex = findClipIndex(name);
    if (clipIndex == SIZE_MAX)
    {
        return false;
    }

    return play(clipIndex);
}

bool Animator::play(size_t clipIndex)
{
    if (clipIndex >= clips.size())
    {
        return false;
    }

    currentClipIndex = clipIndex;
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
    playing = true;
}

void Animator::setPlaybackSpeed(float playbackSpeed)
{
    this->playbackSpeed = std::max(0.0f, playbackSpeed);
}

void Animator::update(double timeDelta)
{
    if (!playing || currentClipIndex == SIZE_MAX || playbackSpeed <= 0.0f)
    {
        return;
    }

    accumulatedTime += timeDelta * playbackSpeed;

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
    if (currentClipIndex >= clips.size())
    {
        return;
    }

    const AnimationClip &clip = clips[currentClipIndex];
    Sprite *sprite = getGameObject()->getComponent<Sprite>();
    if (sprite == nullptr)
    {
        return;
    }

    const AnimationFrame &frame = clip.frames[currentFrameIndex];
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

    float width = clip.width;
    float height = clip.height;
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

size_t Animator::findClipIndex(const std::string &name) const
{
    for (size_t index = 0; index < clips.size(); ++index)
    {
        if (clips[index].name == name)
        {
            return index;
        }
    }

    return SIZE_MAX;
}

double Animator::getCurrentFrameDuration() const
{
    const AnimationClip &clip = clips[currentClipIndex];
    const AnimationFrame &frame = clip.frames[currentFrameIndex];
    if (frame.duration > 0.0f)
    {
        return frame.duration;
    }

    if (clip.framesPerSecond <= 0.0f)
    {
        return 0.0;
    }

    return 1.0 / clip.framesPerSecond;
}

void Animator::advanceFrame()
{
    const AnimationClip &clip = clips[currentClipIndex];
    if (currentFrameIndex + 1 < clip.frames.size())
    {
        currentFrameIndex++;
        applyCurrentFrame();
    }
    else if (clip.loop)
    {
        currentFrameIndex = 0;
        applyCurrentFrame();
    }
    else if (clip.stopAtEnd)
    {
        stop();
    }
    else
    {

        playNext();
    }
}

void Animator::playNext()
{
    if (currentClipIndex + 1 < clips.size())
    {
        play(currentClipIndex + 1);
    }
    else
    {
        stop();
    }
}

const std::string &Animator::getCurrentClipName() const
{
    if (currentClipIndex >= clips.size())
    {
        static const std::string emptyString;
        return emptyString;
    }

    return clips[currentClipIndex].name;
}

size_t Animator::getGameManagerIndex() const
{
    return gameManagerIndex;
}

void Animator::setGameManagerIndex(size_t index)
{
    gameManagerIndex = index;
}