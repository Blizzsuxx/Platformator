#pragma once

#include <string>

struct AnimationClip;
class AudioManager;
class AudioWrapper;
class GameManager;
class PhysicsManager;
class SDLWindow;
class Template;
class TextureWrapper;
class PathManager;

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
    static PathManager &pathManager();
    static GameManager &gameManager();
    static PhysicsManager *physicsManager();
    static SDLWindow *window();
    static AudioManager *audioManager();
    static TextureWrapper *loadTexture(const std::string &filePath);
    static AudioWrapper *loadAudio(const std::string &filePath);
    static void freeTexture(TextureWrapper *textureWrapper);
    static void freeAudio(AudioWrapper *audioWrapper);
    static void freeAnimationClip(AnimationClip *animationClip);
    static void freeTemplate(Template *tpl);
    static bool playAndForget(AudioWrapper *audioWrapper, float gain, int loopCount);
};
} // namespace platformator_detail
