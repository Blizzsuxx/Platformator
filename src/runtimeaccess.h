#pragma once

#include <cstdint>
#include <string>

#include <json.hpp>

class AnimationClip;
class AudioWrapper;
class GameManager;
class PhysicsManager;
class SDLWindow;
class TextureWrapper;
class PathManager;
struct SDL_Surface;

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
    static TextureWrapper *loadTexture(const std::string &filePath);
    static TextureWrapper *findTexture(const std::string &filePath);
    static std::uint64_t textureCacheBytes() noexcept;
    static TextureWrapper *reloadTexture(const std::string &filePath);
    static TextureWrapper *reloadTexture(const std::string &filePath, SDL_Surface *preparedSurface);
    static AudioWrapper *loadAudio(const std::string &filePath);
    static AudioWrapper *reloadAudio(const std::string &filePath);
    static AnimationClip *loadAnimationClip(const std::string &filePath);
    static AnimationClip *findAnimationClip(const std::string &filePath);
    static AnimationClip *reloadAnimationClip(const std::string &filePath);
    static AnimationClip *reloadAnimationClip(const std::string &filePath,
                                               const nlohmann::json &preparedDocument);
    static void freeTexture(TextureWrapper *textureWrapper);
    static void freeAudio(AudioWrapper *audioWrapper);
    static void freeAnimationClip(AnimationClip *animationClip);
    static bool playAndForget(AudioWrapper *audioWrapper, float gain, int loopCount);
};
} // namespace platformator_detail
