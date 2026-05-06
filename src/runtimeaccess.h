#pragma once

#include <string>

class AnimationClip;
class AudioWrapper;
class GameManager;
class SDLWindow;
class TextureWrapper;

namespace platformator
{
    class Runtime;
}

namespace platformator_detail
{
    class RuntimeAccess
    {
    public:
        static platformator::Runtime &runtime();
        static GameManager &gameManager();
        static SDLWindow *window();
        static TextureWrapper *loadTexture(const std::string &filePath);
        static AudioWrapper *loadAudio(const std::string &filePath);
        static AnimationClip *loadAnimationClip(const std::string &filePath);
        static void freeTexture(TextureWrapper *textureWrapper);
        static void freeAudio(AudioWrapper *audioWrapper);
        static void freeAnimationClip(AnimationClip *animationClip);
        static bool playAndForget(AudioWrapper *audioWrapper, float gain, int loopCount);
    };
}