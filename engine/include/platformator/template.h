#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <json.hpp>

#include "platformator/baseobject.h"
#include "platformator/gameobject.h"

struct AnimationClip;
class AudioWrapper;
class GameManager;
class ScriptComponent;
class TextureWrapper;

namespace platformator
{
class ReferenceResolver;
}

class Template : public Asset
{
    friend class GameManager;

  public:
    Template() = default;
    ~Template() override;

    GameObject *instantiate(GameObject *parent = nullptr) const;
    GameObject *instantiate(const Eigen::Vector2f &worldPosition, float worldRotation,
                            GameObject *parent = nullptr) const;
    GameObject *instantiate(const Eigen::Vector2f &worldPosition, GameObject *parent = nullptr) const;

  protected:
    void freeFromRuntime() override;

  private:
    struct ObjectDeleter
    {
        void operator()(GameObject *object) const;
    };

    static GameObject *cloneSubtree(const GameObject &proto, GameObject *clonedParent,
                                    const Eigen::Vector2f &localPosition, float rotation,
                                    std::vector<std::unique_ptr<GameObject, ObjectDeleter>> &clonedObjects,
                                    std::unordered_map<int, BaseObject *> &protoIdToClone,
                                    std::vector<ScriptComponent *> &clonedScripts);
    void loadFromJsonDocument(const nlohmann::json &document, GameManager &manager);
    GameObject *instantiateImpl(std::optional<Eigen::Vector2f> worldPosition, std::optional<float> worldRotation,
                                GameObject *parent) const;

    std::optional<std::string> invalidInternalReferenceError;
    std::vector<Asset::Reference<TextureWrapper>> textures;
    std::vector<Asset::Reference<AudioWrapper>> audio;
    std::vector<Asset::Reference<AnimationClip>> clips;
    std::vector<Asset::Reference<Template>> nestedTemplates;
    // Destroy prototype GameObjects before the retained dependency vectors above.
    std::vector<std::unique_ptr<GameObject, ObjectDeleter>> objects;
};
