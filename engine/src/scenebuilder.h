#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>
#include <json.hpp>
#include "flatidmap.h"
#include "platformator/referenceresolver.h"

class GameManager;
class Scene;
class GameObject;
class Template;

namespace platformator_detail
{
void validateObjectSubtreeIds(const nlohmann::json &record, std::unordered_set<int> &ids, int64_t &nextFileId);

struct SubtreeBuildSink
{
    void *context = nullptr;
    GameObject *(*allocateObject)(void *context) = nullptr;
    void (*onIdentity)(void *context, BaseObject &object, ObjectKind kind) = nullptr;
};

GameObject *buildObjectSubtree(const nlohmann::json &record, platformator::ReferenceResolver &resolver,
                               const SubtreeBuildSink &sink, GameObject *parent = nullptr);
} // namespace platformator_detail

class SceneBuildContext : public platformator::ReferenceResolver
{
    friend class GameManager;

  public:
    explicit SceneBuildContext(GameManager &manager);
    ~SceneBuildContext();
    void prepare(const Scene &scene);
    void prepare(const std::string &sceneFilePath);
    BaseObject *findObject(int id, platformator_detail::ObjectKind kind) override;
    BaseObject *findHandle(platformator::ObjectHandle handle, platformator_detail::ObjectKind kind) override;
    TextureWrapper *loadTexture(const std::string &path) override;
    AudioWrapper *loadAudio(const std::string &path) override;
    AnimationClip *loadAnimationClip(const std::string &path) override;
    Template *loadTemplate(const std::string &path) override;

  private:
    GameObject *buildObject(const nlohmann::json &record, GameObject *parent = nullptr);
    void addIdentity(BaseObject &object, platformator_detail::ObjectKind kind);
    GameManager &manager;
    std::string path;
    std::vector<GameObject *> objects;
    platformator_detail::FlatIdMap ids;
    int64_t nextFileId = 0;
    size_t componentCount = 0;
    size_t scriptCount = 0;
    size_t animatorCount = 0;
    std::vector<TextureWrapper *> textures;
    std::vector<AudioWrapper *> audio;
    std::vector<AnimationClip *> clips;
    std::vector<Template *> templates;
};
