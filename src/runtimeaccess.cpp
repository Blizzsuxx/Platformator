#include "runtimeaccess.h"

#include "gamemanager.h"
#include "platformator/runtime.h"
#include "sdlwindow.h"

namespace platformator_detail
{
    platformator::Runtime &RuntimeAccess::runtime()
    {
        return platformator::Runtime::current();
    }

    GameManager &RuntimeAccess::gameManager()
    {
        if (platformator::Runtime *activeRuntime = platformator::Runtime::tryGetCurrent(); activeRuntime != nullptr && activeRuntime->gameManager != nullptr)
        {
            return activeRuntime->getGameManager();
        }

        return GameManager::getInstance();
    }

    PhysicsManager *RuntimeAccess::physicsManager()
    {
        return gameManager().getPhysicsManager();
    }

    SDLWindow *RuntimeAccess::window()
    {
        return gameManager().getWindow();
    }

    TextureWrapper *RuntimeAccess::loadTexture(const std::string &filePath)
    {
        return runtime().loadTexture(filePath);
    }

    AudioWrapper *RuntimeAccess::loadAudio(const std::string &filePath)
    {
        return runtime().loadAudio(filePath);
    }

    AnimationClip *RuntimeAccess::loadAnimationClip(const std::string &filePath)
    {
        return runtime().loadAnimationClip(filePath);
    }

    void RuntimeAccess::freeTexture(TextureWrapper *textureWrapper)
    {
        gameManager().freeTexture(textureWrapper);
    }

    void RuntimeAccess::freeAudio(AudioWrapper *audioWrapper)
    {
        gameManager().freeAudio(audioWrapper);
    }

    void RuntimeAccess::freeAnimationClip(AnimationClip *animationClip)
    {
        gameManager().freeAnimationClip(animationClip);
    }

    bool RuntimeAccess::playAndForget(AudioWrapper *audioWrapper, float gain, int loopCount)
    {
        return window()->playAndForget(audioWrapper, gain, loopCount);
    }
}