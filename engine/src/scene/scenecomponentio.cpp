#include "scene/scenecomponentio.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include "platformator/animationclip.h"
#include "platformator/animator.h"
#include "platformator/audio.h"
#include "platformator/audiowrapper.h"
#include "platformator/boxcollider.h"
#include "platformator/camera.h"
#include "platformator/circlecollider.h"
#include "platformator/collider.h"
#include "platformator/gameobject.h"
#include "platformator/rigidbody.h"
#include "platformator/runtime.h"
#include "platformator/scriptcomponent.h"
#include "platformator/scriptregistration.h"
#include "platformator/sprite.h"
#include "platformator/texturewrapper.h"
#include "scene/referenceresolver.h"

namespace
{
void validate_frame_duration_json(const nlohmann::json &frame, std::size_t index)
{
    if (!frame.is_object() || !frame.contains("duration") || !frame["duration"].is_number())
        throw std::runtime_error("animation frame " + std::to_string(index) + " requires a numeric duration");
    const double durationValue = frame["duration"].get<double>();
    if (!is_valid_animation_frame_duration(durationValue) ||
        durationValue > static_cast<double>(std::numeric_limits<float>::max()) ||
        static_cast<float>(durationValue) <= 0.0f)
    {
        throw std::runtime_error("animation frame " + std::to_string(index) +
                                 " duration must convert to a finite positive float");
    }
}

void validate_clip_timing(const nlohmann::json &document)
{
    if (!document.is_object() || !document.contains("frames") || !document["frames"].is_array())
        throw std::runtime_error("animation clip requires a frame array");
    for (std::size_t index = 0; index < document["frames"].size(); ++index)
    {
        validate_frame_duration_json(document["frames"][index], index);
    }
}

void validate_clip_timing(const AnimationClip &clip)
{
    for (std::size_t index = 0; index < clip.frames.size(); ++index)
    {
        if (!is_valid_animation_frame_duration(clip.frames[index].duration))
            throw std::runtime_error("animation frame " + std::to_string(index) +
                                     " duration must be finite and positive");
    }
}

nlohmann::json serializeColliderBase(const Collider &collider)
{
    return nlohmann::json{{"id", collider.getId()},
                          {"offset", collider.getOffset()},
                          {"trigger", collider.getIsTrigger()},
                          {"collisionGroup", collider.getCollisionGroup()},
                          {"type", ComponentType::COLLIDER},
                          {"colliderType", collider.getColliderType()},
                          {"collisionMask", collider.getCollisionMask()}};
}

void deserializeColliderBase(const nlohmann::json &j, Collider &collider)
{
    collider.setId(j.at("id").get<int>());
    collider.setOffset(j.at("offset").get<Eigen::Vector2f>());
    collider.setIsTrigger(j.at("trigger").get<bool>());
    collider.setCollisionGroup(j.at("collisionGroup").get<uint64_t>());
    collider.setCollisionMask(j.at("collisionMask").get<uint64_t>());
}

template <typename IntT>
IntT parseCheckedJsonInt(const nlohmann::json &value, const std::string &fieldName, const char *rangeText)
{
    if (value.is_boolean() || !value.is_number_integer())
    {
        throw std::invalid_argument(fieldName + " must be an integer in " + rangeText);
    }
    if (value.is_number_unsigned())
    {
        const auto unsignedValue = value.get<std::uint64_t>();
        if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<IntT>::max()))
        {
            throw std::out_of_range(fieldName + " is outside " + rangeText);
        }
        return static_cast<IntT>(unsignedValue);
    }
    const auto signedValue = value.get<std::int64_t>();
    if (signedValue < static_cast<std::int64_t>(std::numeric_limits<IntT>::min()) ||
        signedValue > static_cast<std::int64_t>(std::numeric_limits<IntT>::max()))
    {
        throw std::out_of_range(fieldName + " is outside " + rangeText);
    }
    return static_cast<IntT>(signedValue);
}

template <typename T> void pushComponentIfPresent(nlohmann::json &components, const GameObject &gameObject)
{
    if (const T *component = gameObject.getComponent<T>())
    {
        components.push_back(*component);
    }
}

template <typename T> void deserializeWithActiveRuntime(const nlohmann::json &j, T &target)
{
    platformator_detail::ActiveReferenceResolver resolver(platformator::Runtime::current());
    deserialize(j, target, resolver);
}
} // namespace

// --- AnimationFrame & AnimationClip ---

void to_json(nlohmann::json &j, const AnimationFrame &frame)
{
    const TextureWrapper *wrapper = frame.getTextureWrapper();
    j = nlohmann::json{{"duration", frame.duration},
                       {"sourceRect", frame.region.sourceRect},
                       {"hasSourceRect", frame.region.hasSourceRect},
                       {"textureWrapperFilePath", wrapper ? wrapper->getFilePath() : ""}};
}

namespace
{
void deserializeValidatedFrame(const nlohmann::json &j, AnimationFrame &frame, platformator::ReferenceResolver &resolver)
{
    const float validatedDuration = static_cast<float>(j.at("duration").get<double>());
    j.at("hasSourceRect").get_to(frame.region.hasSourceRect);
    j.at("sourceRect").get_to(frame.region.sourceRect);
    frame.duration = validatedDuration;
    std::string textureWrapperFilePath;
    j.at("textureWrapperFilePath").get_to(textureWrapperFilePath);
    if (!textureWrapperFilePath.empty())
    {
        frame.setTextureWrapper(resolver.loadTexture(textureWrapperFilePath));
    }
    else
    {
        frame.setTextureWrapper(nullptr);
    }
}
} // namespace

void deserialize(const nlohmann::json &j, AnimationFrame &frame, platformator::ReferenceResolver &resolver)
{
    validate_frame_duration_json(j, 0);
    deserializeValidatedFrame(j, frame, resolver);
}

void to_json(nlohmann::json &j, const AnimationClip &clip)
{
    validate_clip_timing(clip);
    j = nlohmann::json{{"frames", clip.frames}, {"loop", clip.loop}, {"filePath", clip.getFilePath()}};
}

void deserialize(const nlohmann::json &j, AnimationClip &clip, platformator::ReferenceResolver &resolver)
{
    validate_clip_timing(j);
    clip.frames.clear();
    clip.frames.reserve(j.at("frames").size());
    for (const auto &frameJson : j.at("frames"))
    {
        clip.frames.emplace_back();
        deserializeValidatedFrame(frameJson, clip.frames.back(), resolver);
    }
    j.at("loop").get_to(clip.loop);

    std::string filePath;
    j.at("filePath").get_to(filePath);
    clip.setFilePath(filePath);
}

void from_json(const nlohmann::json &j, AnimationFrame &frame)
{
    deserializeWithActiveRuntime(j, frame);
}

void from_json(const nlohmann::json &j, AnimationClip &clip)
{
    deserializeWithActiveRuntime(j, clip);
}

// --- Animator ---

void to_json(nlohmann::json &j, const Animator &animator)
{
    const AnimationClip *currentClip = animator.getCurrentClip();
    j = nlohmann::json{{"id", animator.getId()},
                       {"currentFrameIndex", animator.getCurrentFrameIndex()},
                       {"playbackSpeed", animator.getPlaybackSpeed()},
                       {"playing", animator.getIsPlaying()},
                       {"type", ComponentType::ANIMATOR},
                       {"animationClipFilePath", currentClip ? currentClip->getFilePath() : ""}};
}

void deserialize(const nlohmann::json &j, Animator &animator, platformator::ReferenceResolver &resolver)
{
    animator.setId(j.at("id").get<int>());

    std::string animationClipFilePath;
    j.at("animationClipFilePath").get_to(animationClipFilePath);
    if (!animationClipFilePath.empty())
    {
        const AnimationClip *clip = resolver.loadAnimationClip(animationClipFilePath);
        animator.setCurrentClip(clip);
    }

    animator.setCurrentFrameIndex(j.at("currentFrameIndex").get<size_t>());
    animator.setPlaybackSpeed(j.at("playbackSpeed").get<float>());
    bool playing;
    j.at("playing").get_to(playing);
    if (playing)
    {
        animator.play(animator.getCurrentClip());
    }
}

// --- Audio ---

void to_json(nlohmann::json &j, const Audio &audio)
{
    j = nlohmann::json{
        {"id", audio.getId()},          {"filePath", audio.getFilePath()},
        {"gain", audio.getGain()},      {"loopCount", audio.getLoopCount()},
        {"type", ComponentType::AUDIO}, {"autoPlay", audio.getAutoPlay()},
    };
}

void deserialize(const nlohmann::json &j, Audio &audio, platformator::ReferenceResolver &resolver)
{
    audio.setId(j.at("id").get<int>());

    std::string filePath;
    j.at("filePath").get_to(filePath);
    audio.setAudio(filePath.empty() ? nullptr : resolver.loadAudio(filePath));

    audio.setGain(j.at("gain").get<float>());
    audio.setLoopCount(j.at("loopCount").get<int>());

    bool autoPlay;
    j.at("autoPlay").get_to(autoPlay);
    audio.setAutoPlay(autoPlay);
}

// --- Colliders ---

void to_json(nlohmann::json &j, const BoxCollider &boxCollider)
{
    j = serializeColliderBase(boxCollider);
    j["width"] = boxCollider.getWidthWithoutScale();
    j["height"] = boxCollider.getHeightWithoutScale();
}

void from_json(const nlohmann::json &j, BoxCollider &boxCollider)
{
    boxCollider.setWidth(j.at("width").get<float>());
    boxCollider.setHeight(j.at("height").get<float>());
    deserializeColliderBase(j, boxCollider);
}

void to_json(nlohmann::json &j, const CircleCollider &circleCollider)
{
    j = serializeColliderBase(circleCollider);
    j["radius"] = circleCollider.getRadiusWithoutScale();
}

void from_json(const nlohmann::json &j, CircleCollider &circleCollider)
{
    circleCollider.setRadius(j.at("radius").get<float>());
    deserializeColliderBase(j, circleCollider);
}

// --- Camera ---

void to_json(nlohmann::json &j, const Camera &camera)
{
    j = nlohmann::json{{"id", camera.getId()},
                       {"width", camera.getWidth()},
                       {"height", camera.getHeight()},
                       {"type", ComponentType::CAMERA}};
}

void from_json(const nlohmann::json &j, Camera &camera)
{
    camera.setId(j.at("id").get<int>());
    camera.setWidth(j.at("width").get<float>());
    camera.setHeight(j.at("height").get<float>());
}

// --- Rigidbody ---

void to_json(nlohmann::json &j, const Rigidbody &rigidbody)
{
    j = nlohmann::json{{"id", rigidbody.getId()},
                       {"velocity", rigidbody.getVelocity()},
                       {"force", rigidbody.getForce()},
                       {"mass", rigidbody.getMass()},
                       {"angularVelocity", rigidbody.getAngularVelocity()},
                       {"torque", rigidbody.getTorque()},
                       {"friction", rigidbody.getFriction()},
                       {"restitution", rigidbody.getRestitution()},
                       {"bodyType", rigidbody.getBodyType()},
                       {"type", ComponentType::RIGID_BODY},
                       {"gravity", rigidbody.getGravity()}};
}

void from_json(const nlohmann::json &j, Rigidbody &rigidbody)
{
    rigidbody.setId(j.at("id").get<int>());
    rigidbody.setBodyType(j.at("bodyType").get<BodyType>());
    rigidbody.setGravity(j.at("gravity").get<bool>());
    rigidbody.setVelocity(j.at("velocity").get<Eigen::Vector2f>());
    rigidbody.setForce(j.at("force").get<Eigen::Vector2f>());
    rigidbody.setMass(j.at("mass").get<float>());
    rigidbody.setAngularVelocity(j.at("angularVelocity").get<float>());
    rigidbody.setTorque(j.at("torque").get<float>());
    rigidbody.setFriction(j.at("friction").get<float>());
    rigidbody.setRestitution(j.at("restitution").get<float>());
}

// --- ScriptComponent ---

void to_json(nlohmann::json &j, const ScriptComponent &scriptComponent)
{
    j["id"] = scriptComponent.getId();
    j["behaviors"] = nlohmann::json::array();
    const auto serialize = [&](Behavior *behavior) {
        nlohmann::json behaviorJson;
        behavior->serialize(behaviorJson);
        j["behaviors"].push_back(behaviorJson);
    };
    for (auto *behavior : scriptComponent.getBehaviors())
        serialize(behavior);
    for (auto *behavior : scriptComponent.getPendingBehaviors())
        serialize(behavior);
    j["type"] = ComponentType::SCRIPT;
}

void from_json(const nlohmann::json &j, ScriptComponent &scriptComponent)
{
    scriptComponent.setId(j.at("id").get<int>());
    for (const auto &behaviorJson : j.at("behaviors"))
    {
        std::string type = behaviorJson.at("type").get<std::string>();
        std::unique_ptr<Behavior> behavior(platformator::createRegisteredBehavior(type));
        if (!behavior)
            throw std::runtime_error("Unknown behavior type: " + type);
        behavior->deserialize(behaviorJson);
        scriptComponent.addBehavior(behavior.get());
        behavior.release();
    }
}

// --- Sprite ---

void to_json(nlohmann::json &j, const Sprite &sprite)
{
    const SDL_Color color = sprite.getColor();
    j["id"] = sprite.getId();
    j["textureFilePath"] = sprite.getTextureWrapper() ? sprite.getTextureWrapper()->getFilePath() : "";
    j["flip"] = sprite.getFlip();
    j["width"] = sprite.getWidthWithoutScale();
    j["height"] = sprite.getHeightWithoutScale();
    j["zOrder"] = sprite.getZOrder();
    j["color"] = {{"r", color.r}, {"g", color.g}, {"b", color.b}, {"a", color.a}};
    j["sourceRectEnabled"] = sprite.hasSourceRect();
    if (sprite.hasSourceRect())
    {
        j["sourceRect"] = *sprite.getSourceRect();
    }
    j["type"] = ComponentType::SPRITE;
}

void deserialize(const nlohmann::json &j, Sprite &sprite, platformator::ReferenceResolver &resolver)
{
    std::int32_t zOrder = 0;
    if (const auto found = j.find("zOrder"); found != j.end())
    {
        zOrder = parseCheckedJsonInt<std::int32_t>(*found, "Sprite zOrder", "[-2147483648, 2147483647]");
    }

    SDL_Color color{255, 255, 255, 255};
    if (const auto foundColor = j.find("color"); foundColor != j.end())
    {
        if (!foundColor->is_object())
        {
            throw std::invalid_argument("Sprite color must be a JSON object with r, g, b, and a in [0, 255]");
        }
        const auto parseChannel = [&](const char *channelName) -> std::uint8_t {
            const auto ch = foundColor->find(channelName);
            const std::string field = std::string("Sprite color.") + channelName;
            if (ch == foundColor->end())
            {
                throw std::invalid_argument(field + " must be an integer in [0, 255]");
            }
            return parseCheckedJsonInt<std::uint8_t>(*ch, field, "[0, 255]");
        };
        color.r = parseChannel("r");
        color.g = parseChannel("g");
        color.b = parseChannel("b");
        color.a = parseChannel("a");
    }

    sprite.setId(j.at("id").get<int>());

    std::string filePath = j.at("textureFilePath").get<std::string>();
    if (!filePath.empty())
    {
        sprite.setTextureWrapper(resolver.loadTexture(filePath));
    }

    sprite.setFlip(j.at("flip").get<SDL_FlipMode>());
    sprite.setWidth(j.at("width").get<float>());
    sprite.setHeight(j.at("height").get<float>());
    if (j.at("sourceRectEnabled").get<bool>())
    {
        sprite.setSourceRect(j.at("sourceRect").get<SDL_FRect>());
    }
    sprite.setZOrder(zOrder);
    sprite.setColor(color);
}

void from_json(const nlohmann::json &j, Sprite &sprite)
{
    deserializeWithActiveRuntime(j, sprite);
}

// --- GameObject ---

void to_json(nlohmann::json &j, const GameObject &gameObject)
{
    j = nlohmann::json{{"id", gameObject.getId()},         {"rotation", gameObject.getRotation()},
                       {"active", gameObject.getActive()}, {"position", gameObject.getLocalPosition()},
                       {"scale", gameObject.getScale()},   {"name", gameObject.getName()},
                       {"tag", gameObject.getTag()}};

    j["children"] = nlohmann::json::array();
    for (const GameObject *child : gameObject.getChildren())
    {
        if (child != nullptr)
        {
            j["children"].push_back(*child);
        }
    }

    auto &components = j["components"] = nlohmann::json::array();
    if (const Collider *collider = gameObject.getComponent<Collider>())
    {
        if (collider->getColliderType() == ColliderType::BoxCollider)
        {
            components.push_back(*static_cast<const BoxCollider *>(collider));
        }
        else if (collider->getColliderType() == ColliderType::CircleCollider)
        {
            components.push_back(*static_cast<const CircleCollider *>(collider));
        }
    }
    pushComponentIfPresent<Rigidbody>(components, gameObject);
    pushComponentIfPresent<Sprite>(components, gameObject);
    pushComponentIfPresent<Animator>(components, gameObject);
    pushComponentIfPresent<ScriptComponent>(components, gameObject);
    pushComponentIfPresent<Camera>(components, gameObject);
    pushComponentIfPresent<Audio>(components, gameObject);
}
