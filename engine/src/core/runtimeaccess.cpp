#include "core/runtimeaccess.h"

#include <stdexcept>

#include "audio/audiomanager.h"
#include "core/gamemanager.h"
#include "platformator/runtime.h"

namespace platformator_detail
{
PathManager &RuntimeAccess::pathManager()
{
    return *runtime().paths;
}

platformator::Runtime &RuntimeAccess::runtime()
{
    return platformator::Runtime::current();
}

GameManager &RuntimeAccess::gameManager()
{
    auto *active = platformator::Runtime::tryGetCurrent();
    if (active == nullptr || active->gameManager == nullptr)
    {
        throw std::logic_error("Platformator runtime is not active.");
    }

    return active->getGameManager();
}

PhysicsManager *RuntimeAccess::physicsManager()
{
    return gameManager().getPhysicsManager();
}

SDLWindow *RuntimeAccess::window()
{
    return gameManager().getWindow();
}

AudioManager *RuntimeAccess::audioManager()
{
    return gameManager().getAudioManager();
}

TextureWrapper *RuntimeAccess::loadTexture(const std::string &filePath)
{
    return runtime().loadTexture(filePath);
}

AudioWrapper *RuntimeAccess::loadAudio(const std::string &filePath)
{
    return runtime().loadAudio(filePath);
}

void RuntimeAccess::freeTexture(TextureWrapper *textureWrapper)
{
    gameManager().assetStore().freeTexture(textureWrapper);
}

void RuntimeAccess::freeAudio(AudioWrapper *audioWrapper)
{
    gameManager().assetStore().freeAudio(audioWrapper);
}

void RuntimeAccess::freeAnimationClip(AnimationClip *animationClip)
{
    gameManager().assetStore().freeAnimationClip(animationClip);
}

void RuntimeAccess::freeTemplate(Template *tpl)
{
    gameManager().assetStore().freeTemplate(tpl);
}

bool RuntimeAccess::playAndForget(AudioWrapper *audioWrapper, float gain, int loopCount)
{
    return audioManager()->playAndForget(audioWrapper, gain, loopCount);
}
} // namespace platformator_detail
