#include "platformator/template.h"

#include <stdexcept>
#include <unordered_set>

#include "core/gamemanager.h"
#include "core/inputvalidation.h"
#include "core/runtimeaccess.h"
#include "platformator/animationclip.h"
#include "platformator/audiowrapper.h"
#include "platformator/behavior.h"
#include "platformator/scriptcomponent.h"
#include "platformator/texturewrapper.h"
#include "scene/referenceresolver.h"
#include "scene/scenebuilder.h"

namespace
{
class TemplateBuildResolver final : public platformator_detail::RetainingReferenceResolver
{
  public:
    TemplateBuildResolver(GameManager &manager,
                          const std::unordered_map<int, platformator_detail::ObjectKind> &protoKinds,
                          std::optional<std::string> &invalidInternalReferenceError,
                          platformator_detail::AssetRetentionLedger &retainedAssets)
        : RetainingReferenceResolver(manager, retainedAssets), protoKinds(protoKinds),
          invalidInternalReferenceError(invalidInternalReferenceError)
    {
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
        if (it != protoKinds.end() && !platformator_detail::objectKindMatches(it->second, expected) &&
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

    Template *loadTemplate(const std::string &path) override
    {
        bool deferred = false;
        Template *loaded = manager.assetStore().loadTemplate(path, true, &deferred);
        if (deferred)
        {
            deferredCyclicPaths.insert(path);
            return nullptr;
        }
        return retainedAssets.retain(loaded);
    }

    bool allowDeferredTemplate(const std::string &path) override
    {
        return deferredCyclicPaths.contains(path);
    }

  private:
    const std::unordered_map<int, platformator_detail::ObjectKind> &protoKinds;
    std::optional<std::string> &invalidInternalReferenceError;
    std::unordered_set<std::string> deferredCyclicPaths;
};

class CloneReferenceResolver final : public platformator_detail::RuntimeReferenceResolver
{
  public:
    CloneReferenceResolver(GameManager &manager, const platformator_detail::FlatIdMap &protoIdToClone)
        : RuntimeReferenceResolver(manager), protoIdToClone(protoIdToClone)
    {
    }

    bool isExternalObjectId(int id) const override
    {
        return protoIdToClone.find(id) == nullptr;
    }

    BaseObject *findObject(int id, platformator_detail::ObjectKind kind) override
    {
        BaseObject *found = protoIdToClone.find(id);
        if (found == nullptr)
            return nullptr;
        return manager.resolveHandle(found->getHandle(), kind);
    }

    BaseObject *findHandle(platformator::ObjectHandle handle, platformator_detail::ObjectKind kind) override
    {
        return manager.resolveHandle(handle, kind);
    }

  private:
    const platformator_detail::FlatIdMap &protoIdToClone;
};
} // namespace

struct Template::CloneContext
{
    std::vector<std::unique_ptr<GameObject, ObjectDeleter>> clonedObjects;
    std::vector<GameObject *> rawClones;
    platformator_detail::FlatIdMap protoIdToClone;
    std::vector<ScriptComponent *> clonedScripts;
    bool trackIds = false;
};

GameObject *Template::cloneSubtree(const GameObject &proto, GameObject *clonedParent,
                                   const Eigen::Vector2f &localPosition, float rotation, CloneContext &context)
{
    std::unique_ptr<GameObject, Template::ObjectDeleter> cloneObj(new GameObject());
    GameObject *rawClone = cloneObj.get();
    context.clonedObjects.push_back(std::move(cloneObj));
    context.rawClones.push_back(rawClone);

    rawClone->setName(proto.getName());
    rawClone->setTag(proto.getTag());
    if (context.trackIds)
        context.protoIdToClone.insertOrAssign(proto.getId(), rawClone);

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
            if (context.trackIds)
                context.protoIdToClone.insertOrAssign(comp->getId(), rawComp);
        }
    }

    if (auto *script = rawClone->getComponent<ScriptComponent>())
        context.clonedScripts.push_back(script);

    for (const GameObject *child : proto.getChildren())
        cloneSubtree(*child, rawClone, child->getLocalPosition(), child->getRotation(), context);

    return rawClone;
}

void Template::ObjectDeleter::operator()(GameObject *object) const
{
    delete object;
}

Template::Template() = default;
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

    retainedAssets = std::make_unique<platformator_detail::AssetRetentionLedger>();
    TemplateBuildResolver buildResolver(manager, buildState.protoKinds, invalidInternalReferenceError, *retainedAssets);
    platformator_detail::buildObjectSubtree(rootRecord, buildResolver, sink, nullptr);

    hasScripts = false;
    for (const auto &object : objects)
    {
        if (auto *script = object->getComponent<ScriptComponent>())
        {
            hasScripts = true;
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

    if (worldPosition.has_value())
        platformator_detail::finite(*worldPosition, "worldPosition");
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

    CloneContext context;
    context.clonedObjects.reserve(objects.size());
    context.rawClones.reserve(objects.size());
    context.trackIds = hasScripts;
    if (hasScripts)
        context.protoIdToClone.reserve(objects.size() * 2);

    GameObject *clonedRoot = cloneSubtree(protoRoot, nullptr, initialRootPos, initialRootRot, context);

    CloneReferenceResolver cloneResolver(manager, context.protoIdToClone);
    if (!context.clonedScripts.empty())
    {
        // Resolve deferred cyclic TemplateAssetRef paths while clones remain Staged so failures roll back via RAII.
        for (ScriptComponent *script : context.clonedScripts)
        {
            for (Behavior *behavior : script->getPendingBehaviors())
                behavior->resolveTemplateAssets(cloneResolver);
        }
    }

    manager.registerGameObjectSubtree(std::span<GameObject *const>(context.rawClones));
    for (auto &obj : context.clonedObjects)
        obj.release();

    bool committed = false;
    struct RollbackGuard
    {
        GameManager &manager;
        GameObject *root;
        std::vector<ScriptComponent *> &scripts;
        bool &committed;
        ~RollbackGuard() noexcept
        {
            if (!committed)
            {
                for (ScriptComponent *script : scripts)
                {
                    script->destructionDelivered = true;
                }
                try
                {
                    manager.removeGameObject(root);
                    manager.flushIfIdle();
                }
                catch (...)
                {
                }
            }
        }
    } rollbackGuard{manager, clonedRoot, context.clonedScripts, committed};

    if (parent != nullptr)
    {
        parent->addChild(clonedRoot);
        if (!worldPosition.has_value())
            clonedRoot->setLocalPosition(protoRoot.getLocalPosition());
    }

    if (!context.clonedScripts.empty())
    {
        for (ScriptComponent *script : context.clonedScripts)
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
