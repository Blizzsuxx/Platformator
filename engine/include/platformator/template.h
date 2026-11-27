#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <json.hpp>

#include "platformator/baseobject.h"
#include "platformator/gameobject.h"

class GameManager;

namespace platformator_detail
{
struct AssetRetentionLedger;
}

class Template : public Asset
{
    friend class GameManager;

  public:
    Template();
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
    struct CloneContext;

    static GameObject *cloneSubtree(const GameObject &proto, GameObject *clonedParent,
                                    const Eigen::Vector2f &localPosition, float rotation, CloneContext &context);
    void loadFromJsonDocument(const nlohmann::json &document, GameManager &manager);
    GameObject *instantiateImpl(std::optional<Eigen::Vector2f> worldPosition, std::optional<float> worldRotation,
                                GameObject *parent) const;

    std::optional<std::string> invalidInternalReferenceError;
    std::unique_ptr<platformator_detail::AssetRetentionLedger> retainedAssets;
    // Destroy prototype GameObjects before the retained dependencies.
    std::vector<std::unique_ptr<GameObject, ObjectDeleter>> objects;
    bool hasScripts = false;
};
