#include "scene/scenebuilder.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_set>

#include "core/gamemanager.h"
#include "core/pathmanager.h"
#include "platformator/scriptregistration.h"
#include "scene/scenecomponentio.h"
#include "core/inputvalidation.h"
#include "platformator/animator.h"
#include "platformator/audio.h"
#include "platformator/boxcollider.h"
#include "platformator/camera.h"
#include "platformator/circlecollider.h"
#include "platformator/scriptcomponent.h"
#include "platformator/sprite.h"
#include "platformator/template.h"

namespace
{
using Json = nlohmann::json;
using Kind = platformator_detail::ObjectKind;
using platformator_detail::positive;
using platformator_detail::objectKind;

template <typename T> std::pair<std::unique_ptr<Component>, Kind> readComponent(const Json &entry)
{
    auto component = std::make_unique<T>();
    from_json(entry, *component);
    return {std::move(component), objectKind<T>()};
}

template <typename T>
std::pair<std::unique_ptr<Component>, Kind> readComponent(const Json &entry, platformator::ReferenceResolver &resolver)
{
    auto component = std::make_unique<T>();
    deserialize(entry, *component, resolver);
    return {std::move(component), objectKind<T>()};
}

std::pair<std::unique_ptr<Component>, Kind> buildComponentFromJson(
    const Json &entry, platformator::ReferenceResolver &resolver)
{
    switch (entry.at("type").get<int>())
    {
    case ANIMATOR:
        return readComponent<Animator>(entry, resolver);
    case AUDIO:
        return readComponent<Audio>(entry, resolver);
    case CAMERA:
        positive(entry.at("width").get<float>(), "camera.width");
        positive(entry.at("height").get<float>(), "camera.height");
        return readComponent<Camera>(entry);
    case COLLIDER:
        switch (entry.at("colliderType").get<int>())
        {
        case static_cast<int>(ColliderType::BoxCollider):
            return readComponent<BoxCollider>(entry);
        case static_cast<int>(ColliderType::CircleCollider):
            return readComponent<CircleCollider>(entry);
        default:
            throw std::invalid_argument("Unknown collider shape");
        }
    case RIGID_BODY:
        return readComponent<Rigidbody>(entry);
    case SCRIPT:
        return readComponent<ScriptComponent>(entry);
    case SPRITE:
        return readComponent<Sprite>(entry, resolver);
    default:
        throw std::invalid_argument("Unknown component type");
    }
}
} // namespace

namespace platformator_detail
{
void validateObjectSubtreeIds(const Json &record, std::unordered_set<int> &ids, int64_t &next)
{
    auto add = [&](const Json &value) {
        const int id = value.at("id").get<int>();
        if (id < 0 || !ids.insert(id).second)
            throw std::invalid_argument("Negative or duplicate file ID: " + std::to_string(id));
        next = std::max(next, static_cast<int64_t>(id) + 1);
    };
    add(record);
    unsigned slots = 0;
    if (!record.at("components").is_array() || !record.at("children").is_array())
        throw std::invalid_argument("components/children must be arrays");
    for (const auto &component : record.at("components"))
    {
        add(component);
        const int type = component.at("type").get<int>();
        if (type < 0 || type >= COMPONENT_TYPE_COUNT || (slots & (1u << type)))
            throw std::invalid_argument("Unknown or duplicate component slot");
        slots |= 1u << type;
    }
    for (const auto &child : record.at("children"))
        validateObjectSubtreeIds(child, ids, next);
}

GameObject *buildObjectSubtree(const Json &record, platformator::ReferenceResolver &resolver,
                               const SubtreeBuildSink &sink, GameObject *parent)
{
    auto *object = sink.allocateObject(sink.context);
    object->setId(record.at("id").get<int>());
    object->setName(record.at("name").get<std::string>());
    object->setTag(record.at("tag").get<std::string>());
    if (sink.onIdentity)
        sink.onIdentity(sink.context, *object, Kind::GameObject);
    if (parent != nullptr)
        parent->addChild(object);
    const auto position = record.at("position").get<Eigen::Vector2f>();
    const auto scale = record.at("scale").get<Eigen::Vector2f>();
    const auto rotation = record.at("rotation").get<float>();
    object->setLocalPosition(position);
    object->setRotation(rotation);
    object->setScale(scale);
    object->setActive(record.at("active").get<bool>());
    for (const auto &entry : record.at("components"))
    {
        auto [component, kind] = buildComponentFromJson(entry, resolver);
        object->addComponent(component.get());
        auto *raw = component.release();
        if (sink.onIdentity)
            sink.onIdentity(sink.context, *raw, kind);
    }
    for (const auto &child : record.at("children"))
        buildObjectSubtree(child, resolver, sink, object);
    return object;
}
} // namespace platformator_detail

SceneBuildContext::SceneBuildContext(GameManager &manager) : RetainingReferenceResolver(manager, retainedAssets)
{
}

SceneBuildContext::~SceneBuildContext()
{
    for (auto *object : objects)
    {
        for (auto *component : object->components)
            if (component && component->getHandle())
                manager.registry.erase(*component);
        if (object->getHandle())
            manager.registry.erase(*object);
        delete object;
    }
}

void SceneBuildContext::addIdentity(BaseObject &object, Kind kind)
{
    manager.registry.add(object, kind, false);
    ids.insertOrAssign(object.getId(), &object);
    if (kind != Kind::GameObject)
    {
        ++componentCount;
        if (kind == Kind::Animator)
            ++animatorCount;
    }
}

GameObject *SceneBuildContext::allocateStagedObject()
{
    auto *object = new GameObject();
    try
    {
        objects.push_back(object);
    }
    catch (...)
    {
        delete object;
        throw;
    }
    return object;
}

GameObject *SceneBuildContext::buildObject(const Json &record, GameObject *parent)
{
    const platformator_detail::SubtreeBuildSink sink{
        this,
        [](void *context) -> GameObject * { return static_cast<SceneBuildContext *>(context)->allocateStagedObject(); },
        [](void *context, BaseObject &obj, Kind kind) {
            static_cast<SceneBuildContext *>(context)->addIdentity(obj, kind);
        }};
    return platformator_detail::buildObjectSubtree(record, *this, sink, parent);
}

void SceneBuildContext::prepare(const Scene &scene)
{
    prepare(scene.filePath);
}

void SceneBuildContext::prepare(const std::string &sceneFilePath)
{
    path = PathManager::getInstance().resolveScenePath(sceneFilePath).string();
    try
    {
        std::ifstream file(path);
        if (!file)
            throw std::runtime_error("Cannot open scene");
        const auto document = Json::parse(file);
        if (!document.is_array())
            throw std::invalid_argument("Scene root must be an array");
        std::unordered_set<int> fileIds;
        for (const auto &record : document)
            platformator_detail::validateObjectSubtreeIds(record, fileIds, nextFileId);
        ids.reserve(fileIds.size() + 2);
        objects.reserve(fileIds.size());
        for (const auto &record : document)
            buildObject(record);
        bool camera = false;
        for (auto *object : objects)
            camera |= object->getActive() && object->getComponent<Camera>();
        if (!camera)
        {
            if (nextFileId > static_cast<int64_t>(std::numeric_limits<int>::max()) - 1)
                throw std::overflow_error("No IDs available for fallback camera");
            auto *object = allocateStagedObject();
            object->setId(static_cast<int>(nextFileId++));
            object->setName("Main Camera");
            object->addComponent<Camera>();
            object->getComponent<Camera>()->setId(static_cast<int>(nextFileId++));
            addIdentity(*object, Kind::GameObject);
            addIdentity(*object->getComponent<Camera>(), Kind::Camera);
        }
        for (auto *object : objects)
        {
            if (auto *script = object->getComponent<ScriptComponent>())
            {
                for (Behavior *behavior : script->getPendingBehaviors())
                    behavior->resolveTemplateAssets(*this);
            }
        }
    }
    catch (const std::exception &error)
    {
        throw std::runtime_error("Scene '" + path + "': " + error.what());
    }
}

BaseObject *SceneBuildContext::findHandle(platformator::ObjectHandle handle, Kind kind)
{
    auto *object = manager.registry.get(handle, kind, true);
    return object && object->getLifecycle() == ObjectLifecycle::Staged ? object : nullptr;
}
BaseObject *SceneBuildContext::findObject(int id, Kind kind)
{
    BaseObject *found = ids.find(id);
    return found == nullptr ? nullptr : findHandle(found->getHandle(), kind);
}
