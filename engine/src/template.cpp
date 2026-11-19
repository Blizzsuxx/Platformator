#include "platformator/template.h"

#include <stdexcept>
#include <unordered_set>

#include "gamemanager.h"
#include "inputvalidation.h"
#include "platformator/animationclip.h"
#include "platformator/audiowrapper.h"
#include "platformator/behavior.h"
#include "platformator/referenceresolver.h"
#include "platformator/scriptcomponent.h"
#include "platformator/texturewrapper.h"
#include "runtimeaccess.h"
#include "scenebuilder.h"

namespace
{
bool matchesExpectedObjectKind(platformator_detail::ObjectKind actual, platformator_detail::ObjectKind expected)
{
    using platformator_detail::ObjectKind;
    return expected == ObjectKind::BaseObject || expected == actual ||
           (expected == ObjectKind::Component && actual != ObjectKind::GameObject) ||
           (expected == ObjectKind::Collider &&
            (actual == ObjectKind::BoxCollider || actual == ObjectKind::CircleCollider));
}

class TemplateBuildResolver final : public platformator::ReferenceResolver
{
  public:
    TemplateBuildResolver(GameManager &manager,
                          const std::unordered_map<int, platformator_detail::ObjectKind> &protoKinds,
                          std::optional<std::string> &invalidInternalReferenceError,
                          std::vector<Asset::Reference<TextureWrapper>> &textures,
                          std::vector<Asset::Reference<AudioWrapper>> &audio,
                          std::vector<Asset::Reference<AnimationClip>> &clips,
                          std::vector<Asset::Reference<Template>> &nestedTemplates)
        : manager(manager), protoKinds(protoKinds), invalidInternalReferenceError(invalidInternalReferenceError),
          textures(textures), audio(audio), clips(clips), nestedTemplates(nestedTemplates)
    {
    }

    bool shouldResolveObjectReferences() const override
    {
        return true;
    }

    bool allowDeferredInternalReferenceError() const override
    {
        return true;
    }

    bool isExternalObjectId(int id) const override
    {
        return !protoKinds.contains(id);
    }

    BaseObject *findObject(int id, platformator_detail::ObjectKind expected) override
    {
        const auto it = protoKinds.find(id);
        if (it != protoKinds.end() && !matchesExpectedObjectKind(it->second, expected) &&
            !invalidInternalReferenceError.has_value())
        {
            invalidInternalReferenceError = "Missing reference target or wrong kind for ID " + std::to_string(id);
        }
        return nullptr;
    }

    BaseObject *findHandle(platformator::ObjectHandle, platformator_detail::ObjectKind) override
    {
        return nullptr;
    }

    TextureWrapper *loadTexture(const std::string &path) override
    {
        TextureWrapper *loaded = manager.loadTexture(path, true);
        textures.push_back(Asset::retain(loaded));
        return loaded;
    }

    AudioWrapper *loadAudio(const std::string &path) override
    {
        AudioWrapper *loaded = manager.loadAudio(path, true);
        audio.push_back(Asset::retain(loaded));
        return loaded;
    }

    AnimationClip *loadAnimationClip(const std::string &path) override
    {
        AnimationClip *loaded = manager.loadAnimationClip(path, *this, true);
        clips.push_back(Asset::retain(loaded));
        return loaded;
    }

    Template *loadTemplate(const std::string &path) override
    {
        bool deferred = false;
        Template *loaded = manager.loadTemplate(path, true, &deferred);
        if (deferred)
        {
            deferredCyclicPaths.insert(path);
            return nullptr;
        }
        nestedTemplates.push_back(Asset::retain(loaded));
        return loaded;
    }

    bool allowDeferredTemplate(const std::string &path) override
    {
        return deferredCyclicPaths.contains(path);
    }

  private:
    GameManager &manager;
    const std::unordered_map<int, platformator_detail::ObjectKind> &protoKinds;
    std::optional<std::string> &invalidInternalReferenceError;
    std::vector<Asset::Reference<TextureWrapper>> &textures;
    std::vector<Asset::Reference<AudioWrapper>> &audio;
    std::vector<Asset::Reference<AnimationClip>> &clips;
    std::vector<Asset::Reference<Template>> &nestedTemplates;
    std::unordered_set<std::string> deferredCyclicPaths;
};

class CloneReferenceResolver final : public platformator::ReferenceResolver
{
  public:
    CloneReferenceResolver(GameManager &manager, const std::unordered_map<int, BaseObject *> &protoIdToClone)
        : manager(manager), protoIdToClone(protoIdToClone)
    {
    }

    bool isExternalObjectId(int id) const override
    {
        return !protoIdToClone.contains(id);
    }

    BaseObject *findObject(int id, platformator_detail::ObjectKind kind) override
    {
        const auto it = protoIdToClone.find(id);
        if (it == protoIdToClone.end())
            return nullptr;
        return manager.resolveHandle(it->second->getHandle(), kind);
    }

    BaseObject *findHandle(platformator::ObjectHandle handle, platformator_detail::ObjectKind kind) override
    {
        return manager.resolveHandle(handle, kind);
    }

    TextureWrapper *loadTexture(const std::string &path) override
    {
        return manager.loadTexture(path, true);
    }

    AudioWrapper *loadAudio(const std::string &path) override
    {
        return manager.loadAudio(path, true);
    }

    AnimationClip *loadAnimationClip(const std::string &path) override
    {
        return manager.loadAnimationClip(path, *this, true);
    }

    Template *loadTemplate(const std::string &path) override
    {
        return manager.loadTemplate(path, true);
    }

  private:
    GameManager &manager;
    const std::unordered_map<int, BaseObject *> &protoIdToClone;
};
} // namespace

GameObject *Template::cloneSubtree(const GameObject &proto, GameObject *clonedParent,
                                   const Eigen::Vector2f &localPosition, float rotation,
                                   std::vector<std::unique_ptr<GameObject, ObjectDeleter>> &clonedObjects,
                                   std::unordered_map<int, BaseObject *> &protoIdToClone,
                                   std::vector<ScriptComponent *> &clonedScripts)
{
    std::unique_ptr<GameObject, Template::ObjectDeleter> cloneObj(new GameObject());
    GameObject *rawClone = cloneObj.get();
    clonedObjects.push_back(std::move(cloneObj));

    rawClone->setName(proto.getName());
    rawClone->setTag(proto.getTag());
    protoIdToClone.emplace(proto.getId(), rawClone);

    if (clonedParent != nullptr)
        clonedParent->addChild(rawClone);

    // Apply transform and component-affecting scale before attaching components and recursing into children.
    rawClone->setLocalPosition(localPosition);
    rawClone->setRotation(rotation);
    rawClone->setScale(proto.getScale());
    rawClone->setActive(proto.getActive());

    for (size_t i = 0; i < COMPONENT_TYPE_COUNT; ++i)
    {
        if (const Component *comp = proto.getComponents()[i])
        {
            std::unique_ptr<Component> compClone(comp->clone());
            Component *rawComp = compClone.get();
            rawClone->addComponent(rawComp);
            compClone.release();
            protoIdToClone.emplace(comp->getId(), rawComp);
        }
    }

    if (auto *script = rawClone->getComponent<ScriptComponent>())
        clonedScripts.push_back(script);

    for (const GameObject *child : proto.getChildren())
        cloneSubtree(*child, rawClone, child->getLocalPosition(), child->getRotation(), clonedObjects, protoIdToClone,
                     clonedScripts);

    return rawClone;
}

void Template::ObjectDeleter::operator()(GameObject *object) const
{
    delete object;
}

Template::~Template() = default;

void Template::freeFromRuntime()
{
    platformator_detail::RuntimeAccess::freeTemplate(this);
}

void Template::loadFromJsonDocument(const nlohmann::json &document, GameManager &manager)
{
    if (!document.is_object())
        throw std::invalid_argument("Template document must be a JSON object");
    if (!document.contains("format") || !document["format"].is_string() ||
        document["format"].get<std::string>() != "platformator.object-template")
        throw std::invalid_argument("Invalid template format");
    if (!document.contains("version") || !document["version"].is_number_integer() ||
        document["version"].get<int>() != 1)
        throw std::invalid_argument("Unsupported template version");
    if (!document.contains("root") || !document["root"].is_object())
        throw std::invalid_argument("Template document must contain a root object");

    const auto &rootRecord = document["root"];
    if (document.contains("name") && document["name"].is_string())
        name = document["name"].get<std::string>();
    else if (rootRecord.contains("name") && rootRecord["name"].is_string())
        name = rootRecord["name"].get<std::string>();

    std::unordered_set<int> fileIds;
    int64_t nextFileId = 0;
    platformator_detail::validateObjectSubtreeIds(rootRecord, fileIds, nextFileId);
    objects.reserve(fileIds.size());

    struct BuildState
    {
        std::vector<std::unique_ptr<GameObject, ObjectDeleter>> &objects;
        std::unordered_map<int, platformator_detail::ObjectKind> protoKinds;
    };
    BuildState buildState{objects, {}};
    buildState.protoKinds.reserve(fileIds.size());

    const platformator_detail::SubtreeBuildSink sink{
        &buildState,
        [](void *context) -> GameObject * {
            auto *state = static_cast<BuildState *>(context);
            std::unique_ptr<GameObject, ObjectDeleter> object(new GameObject());
            GameObject *raw = object.get();
            state->objects.push_back(std::move(object));
            return raw;
        },
        [](void *context, BaseObject &object, platformator_detail::ObjectKind kind) {
            static_cast<BuildState *>(context)->protoKinds.emplace(object.getId(), kind);
        }};

    TemplateBuildResolver buildResolver(manager, buildState.protoKinds, invalidInternalReferenceError, textures, audio,
                                        clips, nestedTemplates);
    platformator_detail::buildObjectSubtree(rootRecord, buildResolver, sink, nullptr);

    for (const auto &object : objects)
    {
        if (auto *script = object->getComponent<ScriptComponent>())
        {
            for (Behavior *behavior : script->getPendingBehaviors())
                behavior->resolveReferences(buildResolver);
        }
    }
}

GameObject *Template::instantiate(GameObject *parent) const
{
    return instantiateImpl(std::nullopt, std::nullopt, parent);
}

GameObject *Template::instantiate(const Eigen::Vector2f &worldPosition, float worldRotation, GameObject *parent) const
{
    return instantiateImpl(worldPosition, worldRotation, parent);
}

GameObject *Template::instantiate(const Eigen::Vector2f &worldPosition, GameObject *parent) const
{
    return instantiateImpl(worldPosition, std::nullopt, parent);
}

GameObject *Template::instantiateImpl(std::optional<Eigen::Vector2f> worldPosition, std::optional<float> worldRotation,
                                      GameObject *parent) const
{
    auto &manager = platformator_detail::RuntimeAccess::gameManager();
    manager.assertCanMutate();

    if (objects.empty())
        throw std::logic_error("Cannot instantiate an empty template");

    if (parent != nullptr)
    {
        if (parent->getHandle().runtimeSerial != manager.registry.serial() ||
            parent->getLifecycle() != ObjectLifecycle::Live)
            throw std::invalid_argument("Parent GameObject must be live in the active runtime");
    }

    if (worldPosition.has_value() && !worldPosition->allFinite())
        throw std::invalid_argument("worldPosition must be finite");
    if (worldRotation.has_value())
        platformator_detail::finite(*worldRotation, "worldRotation");

    if (invalidInternalReferenceError.has_value())
        throw std::runtime_error(*invalidInternalReferenceError);

    const GameObject &protoRoot = *objects.front();
    const Eigen::Vector2f initialRootPos =
        worldPosition.has_value()
            ? *worldPosition
            : (parent != nullptr ? (parent->getPosition() + protoRoot.getLocalPosition()).eval()
                                 : protoRoot.getLocalPosition());
    const float initialRootRot = worldRotation.value_or(protoRoot.getRotation());

    std::vector<std::unique_ptr<GameObject, ObjectDeleter>> clonedObjects;
    clonedObjects.reserve(objects.size());
    std::unordered_map<int, BaseObject *> protoIdToClone;
    protoIdToClone.reserve(objects.size() * 2);
    std::vector<ScriptComponent *> clonedScripts;

    GameObject *clonedRoot = cloneSubtree(protoRoot, nullptr, initialRootPos, initialRootRot, clonedObjects,
                                          protoIdToClone, clonedScripts);

    CloneReferenceResolver cloneResolver(manager, protoIdToClone);
    if (!clonedScripts.empty())
    {
        // Resolve deferred cyclic TemplateAssetRef paths while clones remain Staged so failures roll back via RAII.
        for (ScriptComponent *script : clonedScripts)
        {
            for (Behavior *behavior : script->getPendingBehaviors())
                behavior->resolveTemplateAssets(cloneResolver);
        }
    }

    manager.registerGameObjectSubtree(clonedRoot);
    for (auto &obj : clonedObjects)
        obj.release();

    bool committed = false;
    struct RollbackGuard
    {
        GameManager &manager;
        GameObject *root;
        bool &committed;
        ~RollbackGuard()
        {
            if (!committed)
            {
                manager.removeGameObject(root);
                manager.flushIfIdle();
            }
        }
    } rollbackGuard{manager, clonedRoot, committed};

    if (parent != nullptr)
    {
        parent->addChild(clonedRoot);
        if (!worldPosition.has_value())
            clonedRoot->setLocalPosition(protoRoot.getLocalPosition());
    }

    if (!clonedScripts.empty())
    {
        for (ScriptComponent *script : clonedScripts)
        {
            for (Behavior *behavior : script->getPendingBehaviors())
                behavior->resolveReferences(cloneResolver);
            script->markPendingReferencesResolved();
        }
    }

    committed = true;
    manager.flushIfIdle();
    return clonedRoot;
}
