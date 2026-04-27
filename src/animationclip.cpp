#include "animationclip.h"
#include "gamemanager.h"

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

AnimationClip::AnimationClip(double framesPerSecond, bool loop, float width, float height, const char *filePath)
    : frames(), framesPerSecond(framesPerSecond), loop(loop), width(width), height(height), filePath(filePath), referenceCount(0)
{
}

AnimationClip::AnimationClip(const char *filePath)
    : frames(), framesPerSecond(0.0), loop(false), width(0.0f), height(0.0f), filePath(filePath), referenceCount(0)
{
    // Load the animation clip data from the file
    // This is a placeholder implementation and should be replaced with actual file loading logic
    // For example, you could parse a JSON or XML file to populate the frames and other properties

    // Example of adding a frame (this should be replaced with actual data from the file)
    // TextureWrapper *textureWrapper = GameManager::getInstance().loadTexture("example_frame_texture.png");
    // frames.emplace_back(textureWrapper, 0.1f); // Add a frame with a duration of 0.1 seconds
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

const std::string &AnimationClip::getFilePath() const
{
    return filePath;
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
    if (referenceCount == 0)
    {
        GameManager::getInstance().freeAnimationClip(this);
        return true;
    }

    return false;
}