#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <json.hpp>
#include "platformator/referenceresolver.h"

class GameManager;
class Scene;
class GameObject;

class SceneBuildContext : public platformator::ReferenceResolver
{
    friend class GameManager;

  public:
    explicit SceneBuildContext(GameManager &manager);
    ~SceneBuildContext();
    void prepare(const Scene &scene);
    BaseObject *findObject(int id, platformator_detail::ObjectKind kind) override;
    BaseObject *findHandle(platformator::ObjectHandle handle, platformator_detail::ObjectKind kind) override;
    TextureWrapper *loadTexture(const std::string &path) override;
    AudioWrapper *loadAudio(const std::string &path) override;
    AnimationClip *loadAnimationClip(const std::string &path) override;

  private:
    GameObject *buildObject(const nlohmann::json &record);
    void addIdentity(BaseObject &object, platformator_detail::ObjectKind kind);
    GameManager &manager;
    std::string path;
    std::vector<GameObject *> objects;
    std::unordered_map<int, BaseObject *> ids;
    int64_t nextFileId = 0;
    std::vector<TextureWrapper *> textures;
    std::vector<AudioWrapper *> audio;
    std::vector<AnimationClip *> clips;
};
