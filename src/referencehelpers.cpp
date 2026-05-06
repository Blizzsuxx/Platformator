#include "assetreference.h"
#include "objectreference.h"

#include "gamemanager.h"
#include "runtimeaccess.h"

GameManager &getGameManagerInstance()
{
    return GameManager::getInstance();
}

TextureWrapper *loadTextureAssetReference(GameManager &gameManager, const std::string &assetPath)
{
    return gameManager.loadTexture(assetPath);
}

AudioWrapper *loadAudioAssetReference(GameManager &gameManager, const std::string &assetPath)
{
    return gameManager.loadAudio(assetPath);
}

AnimationClip *loadAnimationClipAssetReference(GameManager &gameManager, const std::string &assetPath)
{
    return gameManager.loadAnimationClip(assetPath);
}

BaseObject *resolveObjectReference(GameManager &gameManager, int objectId)
{
    return gameManager.getObjectById(objectId);
}