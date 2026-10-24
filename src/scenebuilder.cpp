#include "scenebuilder.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_set>

#include "gamemanager.h"
#include "platformator/scriptregistration.h"
#include "scenecomponentio.h"
#include "inputvalidation.h"
#include "platformator/animator.h"
#include "platformator/audio.h"
#include "platformator/boxcollider.h"
#include "platformator/camera.h"
#include "platformator/circlecollider.h"
#include "platformator/scriptcomponent.h"
#include "platformator/sprite.h"

namespace
{
using Json = nlohmann::json;
using Kind = platformator_detail::ObjectKind;
using platformator_detail::positive;
void validateIds(const Json &record, std::unordered_set<int> &ids, int64_t &next)
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
        validateIds(child, ids, next);
}
template <typename T> T *retainAcquisition(std::vector<T *> &ledger, T *asset)
{
    asset->addReference();
    try
    {
        ledger.push_back(asset);
    }
    catch (...)
    {
        asset->removeReferenceAndFreeIfNoReferences();
        throw;
    }
    return asset;
}
} // namespace

SceneBuildContext::SceneBuildContext(GameManager &manager) : manager(manager)
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
    for (auto *clip : clips)
        clip->removeReferenceAndFreeIfNoReferences();
    for (auto *asset : audio)
        asset->removeReferenceAndFreeIfNoReferences();
    for (auto *texture : textures)
        texture->removeReferenceAndFreeIfNoReferences();
}

void SceneBuildContext::addIdentity(BaseObject &object, Kind kind)
{
    manager.registry.add(object, kind, false);
    ids.emplace(object.getId(), &object);
}

GameObject *SceneBuildContext::buildObject(const Json &record)
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
    object->setId(record.at("id").get<int>());
    object->setName(record.at("name").get<std::string>());
    object->setTag(record.at("tag").get<std::string>());
    const auto position = record.at("position").get<Eigen::Vector2f>();
    const auto scale = record.at("scale").get<Eigen::Vector2f>();
    const auto rotation = record.at("rotation").get<float>();
    object->setPosition(position);
    object->setRotation(rotation);
    object->setScale(scale);
    object->setActive(record.at("active").get<bool>());
    addIdentity(*object, Kind::GameObject);
    for (const auto &entry : record.at("components"))
    {
        std::unique_ptr<Component> component;
        Kind kind;
        switch (entry.at("type").get<int>())
        {
        case ANIMATOR: {
            auto value = std::make_unique<Animator>();
            deserialize(entry, *value, *this);
            kind = Kind::Animator;
            component = std::move(value);
            break;
        }
        case AUDIO: {
            auto value = std::make_unique<Audio>();
            deserialize(entry, *value, *this);
            kind = Kind::Audio;
            component = std::move(value);
            break;
        }
        case CAMERA: {
            auto value = std::make_unique<Camera>();
            from_json(entry, *value);
            positive(value->getWidth(), "camera.width");
            positive(value->getHeight(), "camera.height");
            kind = Kind::Camera;
            component = std::move(value);
            break;
        }
        case COLLIDER:
            if (entry.at("colliderType").get<int>() == static_cast<int>(ColliderType::BoxCollider))
            {
                auto value = std::make_unique<BoxCollider>();
                from_json(entry, *value);
                kind = Kind::BoxCollider;
                component = std::move(value);
            }
            else if (entry.at("colliderType").get<int>() == static_cast<int>(ColliderType::CircleCollider))
            {
                auto value = std::make_unique<CircleCollider>();
                from_json(entry, *value);
                kind = Kind::CircleCollider;
                component = std::move(value);
            }
            else
                throw std::invalid_argument("Unknown collider shape");
            break;
        case RIGID_BODY: {
            auto value = std::make_unique<Rigidbody>();
            from_json(entry, *value);
            kind = Kind::Rigidbody;
            component = std::move(value);
            break;
        }
        case SCRIPT: {
            auto value = std::make_unique<ScriptComponent>();
            from_json(entry, *value);
            kind = Kind::ScriptComponent;
            component = std::move(value);
            break;
        }
        case SPRITE: {
            auto value = std::make_unique<Sprite>();
            deserialize(entry, *value, *this);
            kind = Kind::Sprite;
            component = std::move(value);
            break;
        }
        default:
            throw std::invalid_argument("Unknown component type");
        }
        auto *raw = component.get();
        object->addComponent(raw);
        component.release();
        addIdentity(*raw, kind);
    }
    for (const auto &child : record.at("children"))
        object->addChild(buildObject(child));
    return object;
}

void SceneBuildContext::prepare(const Scene &scene)
{
    path = scene.resolvedFilePath().string();
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
            validateIds(record, fileIds, nextFileId);
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
            object->setId(static_cast<int>(nextFileId++));
            object->addComponent<Camera>();
            object->getComponent<Camera>()->setId(static_cast<int>(nextFileId++));
            addIdentity(*object, Kind::GameObject);
            addIdentity(*object->getComponent<Camera>(), Kind::Camera);
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
    auto found = ids.find(id);
    return found == ids.end() ? nullptr : findHandle(found->second->getHandle(), kind);
}
TextureWrapper *SceneBuildContext::loadTexture(const std::string &path)
{
    return retainAcquisition(textures, manager.loadTexture(path, true));
}
AudioWrapper *SceneBuildContext::loadAudio(const std::string &path)
{
    return retainAcquisition(audio, manager.loadAudio(path, true));
}
AnimationClip *SceneBuildContext::loadAnimationClip(const std::string &path)
{
    return retainAcquisition(clips, manager.loadAnimationClip(path, *this, true));
}
