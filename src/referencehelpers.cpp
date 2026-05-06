#include "assetreference.h"
#include "objectreference.h"

#include "gamemanager.h"
#include "platformator/runtime.h"
#include "runtimeaccess.h"

GameManager &getGameManagerInstance()
{
    return GameManager::getInstance();
}

namespace platformator_detail
{
    TextureWrapper *loadTextureAssetReference(platformator::Runtime &runtime, const std::string &assetPath)
    {
        return runtime.loadTexture(assetPath);
    }

    AudioWrapper *loadAudioAssetReference(platformator::Runtime &runtime, const std::string &assetPath)
    {
        return runtime.loadAudio(assetPath);
    }

    AnimationClip *loadAnimationClipAssetReference(platformator::Runtime &runtime, const std::string &assetPath)
    {
        return runtime.loadAnimationClip(assetPath);
    }

    BaseObject *resolveObjectReference(platformator::Runtime &runtime, int objectId)
    {
        return runtime.getObjectById(objectId);
    }
} // namespace platformator_detail