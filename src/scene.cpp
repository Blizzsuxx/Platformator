#include "scene.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <toml++/toml.hpp>

#include "animator.h"
#include "audio.h"
#include "behaviorfactoryregistry.h"
#include "boxcollider.h"
#include "camera.h"
#include "circlecollider.h"
#include "gamemanager.h"
#include "rigidbody.h"
#include "scriptcomponent.h"
#include "sprite.h"

namespace
{
    constexpr int64_t SCENE_VERSION_LEGACY = 1;
    constexpr int64_t SCENE_VERSION_CURRENT = 2;
    constexpr std::string_view SCENE_FORMAT = "platformator_scene";
    constexpr std::string_view ANIMATION_CLIP_FORMAT = "platformator_animset";
    constexpr uint64_t DEFAULT_COLLISION_GROUP = 1;
    constexpr uint64_t DEFAULT_COLLISION_MASK = 1;

    struct CameraConfig
    {
        bool enabled = false;
        uint64_t id = 0;
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct RigidbodyConfig
    {
        bool enabled = false;
        uint64_t id = 0;
        BodyType bodyType = BodyType::DYNAMIC;
        bool gravity = true;
        Eigen::Vector2f velocity = Eigen::Vector2f::Zero();
        Eigen::Vector2f force = Eigen::Vector2f::Zero();
        float mass = 1.0f;
        float angularVelocity = 0.0f;
        float torque = 0.0f;
        float friction = 1.0f;
        float restitution = 0.0f;
    };

    struct BoxColliderConfig
    {
        bool enabled = false;
        uint64_t id = 0;
        float width = 0.0f;
        float height = 0.0f;
        bool isTrigger = false;
        uint64_t collisionGroup = DEFAULT_COLLISION_GROUP;
        uint64_t collisionMask = DEFAULT_COLLISION_MASK;
    };

    struct CircleColliderConfig
    {
        bool enabled = false;
        uint64_t id = 0;
        float radius = 0.0f;
        bool isTrigger = false;
        uint64_t collisionGroup = DEFAULT_COLLISION_GROUP;
        uint64_t collisionMask = DEFAULT_COLLISION_MASK;
    };

    struct SpriteConfig
    {
        bool enabled = false;
        uint64_t id = 0;
        std::string path;
        SDL_FlipMode flip = SDL_FLIP_NONE;
        float width = 0.0f;
        float height = 0.0f;
        SDL_FRect sourceRect{0.0f, 0.0f, 0.0f, 0.0f};
        bool hasSourceRect = false;
    };

    struct AnimatorConfig
    {
        bool enabled = false;
        uint64_t id = 0;
        float playbackSpeed = 1.0f;
        std::string clipPath;
        bool playing = true;
    };

    struct AudioConfig
    {
        bool enabled = false;
        uint64_t id = 0;
        std::string path;
        bool autoplay = false;
        int loops = 0;
        float gain = 1.0f;
    };

    struct ScriptComponentConfig
    {
        bool enabled = false;
        uint64_t id = 0;
        std::vector<BehaviorSpec> behaviors;
    };

    struct ObjectConfig
    {
        uint64_t id = 0;
        std::string name;
        std::string tag;
        bool active = true;
        Eigen::Vector2f position = Eigen::Vector2f::Zero();
        float rotation = 0.0f;
        Eigen::Vector2f scale = Eigen::Vector2f::Ones();
        std::vector<uint64_t> children;
        CameraConfig camera;
        RigidbodyConfig rigidbody;
        BoxColliderConfig boxCollider;
        CircleColliderConfig circleCollider;
        SpriteConfig sprite;
        AnimatorConfig animator;
        AudioConfig audio;
        ScriptComponentConfig script;
    };

    struct AnimationClipFrameConfig
    {
        std::string path;
        float duration = 0.0f;
        SDL_FRect sourceRect{0.0f, 0.0f, 0.0f, 0.0f};
        bool hasSourceRect = false;
    };

    struct AnimationClipConfig
    {
        std::string name;
        float fps = 12.0f;
        bool loop = true;
        float width = 0.0f;
        float height = 0.0f;
        std::vector<AnimationClipFrameConfig> frames;
    };

    std::string toLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    std::string requireString(const toml::table &table, std::string_view key, const std::string &context)
    {
        if (const toml::node *node = table.get(key))
        {
            if (node->is_string())
            {
                return node->value_exact<std::string>().value_or("");
            }

            throw std::runtime_error(context + " must be a string.");
        }

        return "";
    }

    bool readBool(const toml::table &table, std::string_view key, bool fallback, const std::string &context)
    {
        if (const toml::node *node = table.get(key))
        {
            if (node->is_boolean())
            {
                return node->value_exact<bool>().value_or(fallback);
            }

            throw std::runtime_error(context + " must be a bool.");
        }

        return fallback;
    }

    double readNumber(const toml::node &node, const std::string &context)
    {
        if (node.is_floating_point())
        {
            return node.value_exact<double>().value_or(0.0);
        }

        if (node.is_integer())
        {
            return static_cast<double>(node.value_exact<int64_t>().value_or(0));
        }

        throw std::runtime_error(context + " must be numeric.");
    }

    int64_t readIntegerNode(const toml::node &node, const std::string &context)
    {
        if (!node.is_integer())
        {
            throw std::runtime_error(context + " must be an integer.");
        }

        return node.value_exact<int64_t>().value_or(0);
    }

    uint64_t readUnsignedIdNode(const toml::node &node, const std::string &context)
    {
        const int64_t value = readIntegerNode(node, context);
        if (value <= 0)
        {
            throw std::runtime_error(context + " must be positive.");
        }

        return static_cast<uint64_t>(value);
    }

    float readFloat(const toml::table &table, std::string_view key, float fallback, const std::string &context)
    {
        if (const toml::node *node = table.get(key))
        {
            return static_cast<float>(readNumber(*node, context));
        }

        return fallback;
    }

    int64_t readInteger(const toml::table &table, std::string_view key, int64_t fallback, const std::string &context)
    {
        if (const toml::node *node = table.get(key))
        {
            return readIntegerNode(*node, context);
        }

        return fallback;
    }

    uint64_t readUnsignedId(const toml::table &table, std::string_view key, uint64_t fallback, const std::string &context)
    {
        if (const toml::node *node = table.get(key))
        {
            return readUnsignedIdNode(*node, context);
        }

        return fallback;
    }

    const toml::array *readArray(const toml::table &table, std::string_view key, const std::string &context)
    {
        if (const toml::node *node = table.get(key))
        {
            if (const toml::array *array = node->as_array())
            {
                return array;
            }

            throw std::runtime_error(context + " must be an array.");
        }

        return nullptr;
    }

    Eigen::Vector2f readVector2(const toml::table &table, std::string_view key, const Eigen::Vector2f &fallback, const std::string &context)
    {
        const toml::array *array = readArray(table, key, context);
        if (array == nullptr)
        {
            return fallback;
        }

        if (array->size() != 2)
        {
            throw std::runtime_error(context + " must contain exactly two numeric values.");
        }

        const toml::node *xNode = array->get(0);
        const toml::node *yNode = array->get(1);
        if (xNode == nullptr || yNode == nullptr)
        {
            throw std::runtime_error(context + " must contain exactly two numeric values.");
        }

        return Eigen::Vector2f(static_cast<float>(readNumber(*xNode, context)), static_cast<float>(readNumber(*yNode, context)));
    }

    SDL_FRect readRect4(const toml::table &table, std::string_view key, const SDL_FRect &fallback, const std::string &context)
    {
        const toml::array *array = readArray(table, key, context);
        if (array == nullptr)
        {
            return fallback;
        }

        if (array->size() != 4)
        {
            throw std::runtime_error(context + " must contain exactly four numeric values.");
        }

        const toml::node *xNode = array->get(0);
        const toml::node *yNode = array->get(1);
        const toml::node *wNode = array->get(2);
        const toml::node *hNode = array->get(3);
        if (xNode == nullptr || yNode == nullptr || wNode == nullptr || hNode == nullptr)
        {
            throw std::runtime_error(context + " must contain exactly four numeric values.");
        }

        return SDL_FRect{static_cast<float>(readNumber(*xNode, context)),
                         static_cast<float>(readNumber(*yNode, context)),
                         static_cast<float>(readNumber(*wNode, context)),
                         static_cast<float>(readNumber(*hNode, context))};
    }

    std::vector<uint64_t> readIdArray(const toml::table &table, std::string_view key, const std::string &context)
    {
        const toml::array *array = readArray(table, key, context);
        if (array == nullptr)
        {
            return {};
        }

        std::vector<uint64_t> ids;
        ids.reserve(array->size());
        for (size_t index = 0; index < array->size(); ++index)
        {
            const toml::node *node = array->get(index);
            if (node == nullptr)
            {
                throw std::runtime_error(context + " contains an invalid id entry.");
            }

            ids.push_back(readUnsignedIdNode(*node, context + "[" + std::to_string(index) + "]"));
        }

        return ids;
    }

    void recordSceneId(std::unordered_map<uint64_t, std::string> &seenIds, uint64_t id, const std::string &context)
    {
        if (id == 0)
        {
            return;
        }

        auto [it, inserted] = seenIds.emplace(id, context);
        if (!inserted)
        {
            throw std::runtime_error(context + " duplicates id " + std::to_string(id) + " already used by " + it->second + ".");
        }
    }

    void validateSceneIds(const std::vector<ObjectConfig> &configs)
    {
        std::unordered_map<uint64_t, std::string> seenIds;
        std::unordered_set<uint64_t> objectIds;

        for (size_t index = 0; index < configs.size(); ++index)
        {
            const ObjectConfig &config = configs[index];
            const std::string objectContext = "objects[" + std::to_string(index) + "]";

            recordSceneId(seenIds, config.id, objectContext + ".id");
            if (config.id != 0)
            {
                objectIds.insert(config.id);
            }

            recordSceneId(seenIds, config.camera.id, objectContext + ".camera.id");
            recordSceneId(seenIds, config.rigidbody.id, objectContext + ".rigidbody.id");
            recordSceneId(seenIds, config.boxCollider.id, objectContext + ".boxCollider.id");
            recordSceneId(seenIds, config.circleCollider.id, objectContext + ".circleCollider.id");
            recordSceneId(seenIds, config.sprite.id, objectContext + ".sprite.id");
            recordSceneId(seenIds, config.animator.id, objectContext + ".animator.id");
            recordSceneId(seenIds, config.audio.id, objectContext + ".audio.id");
            recordSceneId(seenIds, config.script.id, objectContext + ".script.id");
        }

        for (size_t index = 0; index < configs.size(); ++index)
        {
            const ObjectConfig &config = configs[index];
            const std::string childrenContext = "objects[" + std::to_string(index) + "].children";
            for (size_t childIndex = 0; childIndex < config.children.size(); ++childIndex)
            {
                const uint64_t childId = config.children[childIndex];
                if (!objectIds.contains(childId))
                {
                    throw std::runtime_error(childrenContext + "[" + std::to_string(childIndex) + "] references unknown object id " + std::to_string(childId) + ".");
                }
            }
        }
    }

    std::string resolveResourcePath(const std::string &sourcePath, const std::string &resourcePath)
    {
        if (resourcePath.empty())
        {
            return "";
        }

        const std::filesystem::path path(resourcePath);
        if (path.is_absolute() || sourcePath.empty())
        {
            return path.lexically_normal().string();
        }

        return (std::filesystem::path(sourcePath).parent_path() / path).lexically_normal().string();
    }

    std::string makeSceneRelativePath(const std::string &sourcePath, const std::string &resourcePath)
    {
        if (resourcePath.empty())
        {
            return "";
        }

        std::error_code errorCode;
        const std::filesystem::path sourceDirectory = std::filesystem::absolute(std::filesystem::path(sourcePath), errorCode).parent_path();
        const std::filesystem::path assetPath = std::filesystem::absolute(std::filesystem::path(resourcePath), errorCode);
        if (errorCode)
        {
            return std::filesystem::path(resourcePath).lexically_normal().string();
        }

        const std::filesystem::path relativePath = std::filesystem::relative(assetPath, sourceDirectory, errorCode);
        if (errorCode || relativePath.empty())
        {
            return assetPath.lexically_normal().string();
        }

        return relativePath.lexically_normal().string();
    }

    BodyType parseBodyType(const std::string &value, const std::string &context)
    {
        const std::string lowered = toLower(value);
        if (lowered == "dynamic")
        {
            return BodyType::DYNAMIC;
        }
        if (lowered == "static")
        {
            return BodyType::STATIC;
        }
        if (lowered == "kinematic")
        {
            return BodyType::KINEMATIC;
        }

        throw std::runtime_error(context + " must be one of dynamic, static, or kinematic.");
    }

    std::string bodyTypeToToken(BodyType bodyType)
    {
        switch (bodyType)
        {
        case BodyType::DYNAMIC:
            return "dynamic";
        case BodyType::STATIC:
            return "static";
        case BodyType::KINEMATIC:
            return "kinematic";
        default:
            return "dynamic";
        }
    }

    SDL_FlipMode parseFlipMode(const std::string &value, const std::string &context)
    {
        const std::string lowered = toLower(value);
        if (lowered.empty() || lowered == "none")
        {
            return SDL_FLIP_NONE;
        }
        if (lowered == "horizontal")
        {
            return SDL_FLIP_HORIZONTAL;
        }
        if (lowered == "vertical")
        {
            return SDL_FLIP_VERTICAL;
        }
        if (lowered == "both")
        {
            return static_cast<SDL_FlipMode>(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL);
        }

        throw std::runtime_error(context + " must be one of none, horizontal, vertical, or both.");
    }

    std::string flipModeToToken(SDL_FlipMode flip)
    {
        if (flip == SDL_FLIP_HORIZONTAL)
        {
            return "horizontal";
        }
        if (flip == SDL_FLIP_VERTICAL)
        {
            return "vertical";
        }
        if (flip == static_cast<SDL_FlipMode>(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL))
        {
            return "both";
        }

        return "none";
    }

    BehaviorProperty parseBehaviorProperty(const std::string &key, const toml::node &node, const std::string &context)
    {
        if (node.is_string())
        {
            return BehaviorProperty{key, node.value_exact<std::string>().value_or("")};
        }

        if (node.is_integer())
        {
            return BehaviorProperty{key, node.value_exact<int64_t>().value_or(0)};
        }

        if (node.is_floating_point())
        {
            return BehaviorProperty{key, node.value_exact<double>().value_or(0.0)};
        }

        if (node.is_boolean())
        {
            return BehaviorProperty{key, node.value_exact<bool>().value_or(false)};
        }

        if (const toml::array *array = node.as_array())
        {
            if (array->size() != 2)
            {
                throw std::runtime_error(context + " array properties must contain exactly two numeric values.");
            }

            const toml::node *xNode = array->get(0);
            const toml::node *yNode = array->get(1);
            if (xNode == nullptr || yNode == nullptr)
            {
                throw std::runtime_error(context + " array properties must contain exactly two numeric values.");
            }

            return BehaviorProperty{key,
                                    Eigen::Vector2f(static_cast<float>(readNumber(*xNode, context)),
                                                    static_cast<float>(readNumber(*yNode, context)))};
        }

        if (const toml::table *referenceTable = node.as_table())
        {
            if (const toml::node *gameObjectNode = referenceTable->get("gameObject"))
            {
                if (referenceTable->size() != 1)
                {
                    throw std::runtime_error(context + " game object references must only contain a gameObject id.");
                }

                return BehaviorProperty{key, platformator_behavior_detail::GameObjectIdReference{readUnsignedIdNode(*gameObjectNode, context + ".gameObject")}};
            }

            if (const toml::node *componentNode = referenceTable->get("component"))
            {
                if (referenceTable->size() != 1)
                {
                    throw std::runtime_error(context + " component references must only contain a component id.");
                }

                return BehaviorProperty{key, platformator_behavior_detail::ComponentIdReference{readUnsignedIdNode(*componentNode, context + ".component")}};
            }
        }

        throw std::runtime_error(context + " uses an unsupported TOML value type.");
    }

    void insertBehaviorProperty(toml::table &table, const BehaviorProperty &property)
    {
        std::visit(
            [&table, &property](const auto &typedValue)
            {
                using ValueType = std::decay_t<decltype(typedValue)>;
                if constexpr (std::is_same_v<ValueType, Eigen::Vector2f>)
                {
                    table.insert_or_assign(property.name, toml::array{static_cast<double>(typedValue.x()), static_cast<double>(typedValue.y())});
                }
                else if constexpr (std::is_same_v<ValueType, platformator_behavior_detail::GameObjectIdReference>)
                {
                    toml::table referenceTable;
                    referenceTable.is_inline(true);
                    referenceTable.insert_or_assign("gameObject", static_cast<int64_t>(typedValue.id));
                    table.insert_or_assign(property.name, std::move(referenceTable));
                }
                else if constexpr (std::is_same_v<ValueType, platformator_behavior_detail::ComponentIdReference>)
                {
                    toml::table referenceTable;
                    referenceTable.is_inline(true);
                    referenceTable.insert_or_assign("component", static_cast<int64_t>(typedValue.id));
                    table.insert_or_assign(property.name, std::move(referenceTable));
                }
                else
                {
                    table.insert_or_assign(property.name, typedValue);
                }
            },
            property.value);
    }

    CameraConfig parseCameraConfig(const toml::table &table, bool allowIds)
    {
        CameraConfig config;
        config.enabled = true;
        if (allowIds)
        {
            config.id = readUnsignedId(table, "id", 0, "camera.id");
        }

        const SDL_FRect viewport = readRect4(table, "viewport", SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f}, "camera.viewport");
        config.x = readFloat(table, "x", viewport.x, "camera.x");
        config.y = readFloat(table, "y", viewport.y, "camera.y");
        config.width = readFloat(table, "width", viewport.w, "camera.width");
        config.height = readFloat(table, "height", viewport.h, "camera.height");
        return config;
    }

    RigidbodyConfig parseRigidbodyConfig(const toml::table &table, bool allowIds)
    {
        RigidbodyConfig config;
        config.enabled = true;
        if (allowIds)
        {
            config.id = readUnsignedId(table, "id", 0, "rigidbody.id");
        }

        const std::string bodyType = requireString(table, "bodyType", "rigidbody.bodyType");
        if (!bodyType.empty())
        {
            config.bodyType = parseBodyType(bodyType, "rigidbody.bodyType");
        }

        config.gravity = readBool(table, "gravity", config.gravity, "rigidbody.gravity");
        config.mass = readFloat(table, "mass", config.mass, "rigidbody.mass");
        config.velocity = readVector2(table, "velocity", config.velocity, "rigidbody.velocity");
        config.force = readVector2(table, "force", config.force, "rigidbody.force");
        config.angularVelocity = readFloat(table, "angularVelocity", config.angularVelocity, "rigidbody.angularVelocity");
        config.torque = readFloat(table, "torque", config.torque, "rigidbody.torque");
        config.friction = readFloat(table, "friction", config.friction, "rigidbody.friction");
        config.restitution = readFloat(table, "restitution", config.restitution, "rigidbody.restitution");
        return config;
    }

    BoxColliderConfig parseBoxColliderConfig(const toml::table &table, bool allowIds)
    {
        BoxColliderConfig config;
        config.enabled = true;
        if (allowIds)
        {
            config.id = readUnsignedId(table, "id", 0, "boxCollider.id");
        }

        const Eigen::Vector2f size = readVector2(table, "size", Eigen::Vector2f(config.width, config.height), "boxCollider.size");
        config.width = readFloat(table, "width", size.x(), "boxCollider.width");
        config.height = readFloat(table, "height", size.y(), "boxCollider.height");
        config.isTrigger = readBool(table, "trigger", config.isTrigger, "boxCollider.trigger");
        config.collisionGroup = readUnsignedId(table, "collisionGroup", config.collisionGroup, "boxCollider.collisionGroup");
        config.collisionMask = readUnsignedId(table, "collisionMask", config.collisionMask, "boxCollider.collisionMask");
        return config;
    }

    CircleColliderConfig parseCircleColliderConfig(const toml::table &table, bool allowIds)
    {
        CircleColliderConfig config;
        config.enabled = true;
        if (allowIds)
        {
            config.id = readUnsignedId(table, "id", 0, "circleCollider.id");
        }

        config.radius = readFloat(table, "radius", config.radius, "circleCollider.radius");
        config.isTrigger = readBool(table, "trigger", config.isTrigger, "circleCollider.trigger");
        config.collisionGroup = readUnsignedId(table, "collisionGroup", config.collisionGroup, "circleCollider.collisionGroup");
        config.collisionMask = readUnsignedId(table, "collisionMask", config.collisionMask, "circleCollider.collisionMask");
        return config;
    }

    SpriteConfig parseSpriteConfig(const toml::table &table, bool allowIds)
    {
        SpriteConfig config;
        config.enabled = true;
        if (allowIds)
        {
            config.id = readUnsignedId(table, "id", 0, "sprite.id");
        }

        config.path = requireString(table, "path", "sprite.path");
        const std::string flipToken = requireString(table, "flip", "sprite.flip");
        if (!flipToken.empty())
        {
            config.flip = parseFlipMode(flipToken, "sprite.flip");
        }

        const Eigen::Vector2f size = readVector2(table, "size", Eigen::Vector2f(config.width, config.height), "sprite.size");
        config.width = readFloat(table, "width", size.x(), "sprite.width");
        config.height = readFloat(table, "height", size.y(), "sprite.height");

        if (table.contains("sourceRect"))
        {
            config.sourceRect = readRect4(table, "sourceRect", SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f}, "sprite.sourceRect");
            config.hasSourceRect = true;
        }
        else if (table.contains("rect"))
        {
            config.sourceRect = readRect4(table, "rect", SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f}, "sprite.rect");
            config.hasSourceRect = true;
        }

        return config;
    }

    AnimatorConfig parseAnimatorConfig(const toml::table &table, bool allowIds)
    {
        AnimatorConfig config;
        config.enabled = true;
        if (allowIds)
        {
            config.id = readUnsignedId(table, "id", 0, "animator.id");
        }

        config.playbackSpeed = readFloat(table, "playbackSpeed", config.playbackSpeed, "animator.playbackSpeed");
        config.clipPath = requireString(table, "clip", "animator.clip");
        if (config.clipPath.empty())
        {
            config.clipPath = requireString(table, "asset", "animator.asset");
        }
        config.playing = readBool(table, "playing", config.playing, "animator.playing");
        if (table.contains("play"))
        {
            config.playing = readBool(table, "play", config.playing, "animator.play");
        }
        return config;
    }

    AudioConfig parseAudioConfig(const toml::table &table, bool allowIds)
    {
        AudioConfig config;
        config.enabled = true;
        if (allowIds)
        {
            config.id = readUnsignedId(table, "id", 0, "audio.id");
        }

        config.path = requireString(table, "path", "audio.path");
        config.autoplay = readBool(table, "autoplay", config.autoplay, "audio.autoplay");
        config.loops = static_cast<int>(readInteger(table, "loops", config.loops, "audio.loops"));
        config.gain = readFloat(table, "gain", config.gain, "audio.gain");
        return config;
    }

    BehaviorSpec parseBehaviorSpec(const toml::table &table, const std::string &sceneFilePath, const std::string &context)
    {
        BehaviorSpec spec;
        spec.sourcePath = sceneFilePath;
        spec.type = requireString(table, "type", context + ".type");
        spec.enabled = readBool(table, "enabled", true, context + ".enabled");
        if (spec.type.empty())
        {
            throw std::runtime_error(context + ".type is required.");
        }

        for (const auto &[key, node] : table)
        {
            const std::string propertyName = std::string(key.str());
            if (propertyName == "type" || propertyName == "enabled")
            {
                continue;
            }

            const BehaviorProperty property = parseBehaviorProperty(propertyName, node, context + "." + propertyName);
            spec.setProperty(property.name, property.value);
        }

        return spec;
    }

    ScriptComponentConfig parseScriptComponentConfig(const toml::table &table, const std::string &sceneFilePath, const std::string &context, bool allowIds)
    {
        ScriptComponentConfig config;
        config.enabled = true;
        if (allowIds)
        {
            config.id = readUnsignedId(table, "id", 0, context + ".id");
        }

        if (const toml::array *behaviorsArray = readArray(table, "behaviors", context + ".behaviors"))
        {
            config.behaviors.reserve(behaviorsArray->size());
            for (size_t index = 0; index < behaviorsArray->size(); ++index)
            {
                const toml::node *behaviorNode = behaviorsArray->get(index);
                if (behaviorNode == nullptr)
                {
                    throw std::runtime_error(context + ".behaviors contains an invalid entry.");
                }

                const toml::table *behaviorTable = behaviorNode->as_table();
                if (behaviorTable == nullptr)
                {
                    throw std::runtime_error(context + ".behaviors entries must be tables.");
                }

                config.behaviors.push_back(parseBehaviorSpec(*behaviorTable, sceneFilePath, context + ".behaviors[" + std::to_string(index) + "]"));
            }
        }

        return config;
    }

    ObjectConfig parseObject(const toml::table &table, const std::string &sceneFilePath, size_t index, int64_t version)
    {
        ObjectConfig config;
        const bool allowIds = version >= SCENE_VERSION_CURRENT;
        const std::string context = "objects[" + std::to_string(index) + "]";

        if (allowIds)
        {
            config.id = readUnsignedId(table, "id", 0, context + ".id");
            config.children = readIdArray(table, "children", context + ".children");
        }

        config.name = requireString(table, "name", context + ".name");
        config.tag = requireString(table, "tag", context + ".tag");
        config.active = readBool(table, "active", config.active, context + ".active");
        config.position = readVector2(table, "position", config.position, context + ".position");
        config.rotation = readFloat(table, "rotation", config.rotation, context + ".rotation");
        config.scale = readVector2(table, "scale", config.scale, context + ".scale");

        if (const toml::table *cameraTable = table.get_as<toml::table>("camera"))
        {
            config.camera = parseCameraConfig(*cameraTable, allowIds);
        }
        if (const toml::table *rigidbodyTable = table.get_as<toml::table>("rigidbody"))
        {
            config.rigidbody = parseRigidbodyConfig(*rigidbodyTable, allowIds);
        }
        if (const toml::table *boxColliderTable = table.get_as<toml::table>("boxCollider"))
        {
            config.boxCollider = parseBoxColliderConfig(*boxColliderTable, allowIds);
        }
        if (const toml::table *circleColliderTable = table.get_as<toml::table>("circleCollider"))
        {
            config.circleCollider = parseCircleColliderConfig(*circleColliderTable, allowIds);
        }
        if (const toml::table *spriteTable = table.get_as<toml::table>("sprite"))
        {
            config.sprite = parseSpriteConfig(*spriteTable, allowIds);
        }
        if (const toml::table *animatorTable = table.get_as<toml::table>("animator"))
        {
            config.animator = parseAnimatorConfig(*animatorTable, allowIds);
        }
        if (const toml::table *audioTable = table.get_as<toml::table>("audio"))
        {
            config.audio = parseAudioConfig(*audioTable, allowIds);
        }

        if (allowIds)
        {
            if (const toml::table *scriptTable = table.get_as<toml::table>("script"))
            {
                config.script = parseScriptComponentConfig(*scriptTable, sceneFilePath, context + ".script", true);
            }
        }
        else if (const toml::array *scriptsArray = readArray(table, "scripts", context + ".scripts"))
        {
            config.script.enabled = true;
            config.script.behaviors.reserve(scriptsArray->size());
            for (size_t scriptIndex = 0; scriptIndex < scriptsArray->size(); ++scriptIndex)
            {
                const toml::node *scriptNode = scriptsArray->get(scriptIndex);
                if (scriptNode == nullptr)
                {
                    throw std::runtime_error(context + ".scripts contains an invalid entry.");
                }

                const toml::table *scriptTable = scriptNode->as_table();
                if (scriptTable == nullptr)
                {
                    throw std::runtime_error(context + ".scripts entries must be tables.");
                }

                config.script.behaviors.push_back(parseBehaviorSpec(*scriptTable, sceneFilePath, context + ".scripts[" + std::to_string(scriptIndex) + "]"));
            }
        }

        return config;
    }

    AnimationClipFrameConfig parseAnimationClipFrameConfig(const toml::table &table, const std::string &context)
    {
        AnimationClipFrameConfig config;
        config.path = requireString(table, "path", context + ".path");
        if (config.path.empty())
        {
            throw std::runtime_error(context + ".path is required.");
        }

        config.duration = readFloat(table, "duration", 0.0f, context + ".duration");
        if (table.contains("rect"))
        {
            config.sourceRect = readRect4(table, "rect", SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f}, context + ".rect");
            config.hasSourceRect = true;
        }
        else if (table.contains("sourceRect"))
        {
            config.sourceRect = readRect4(table, "sourceRect", SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f}, context + ".sourceRect");
            config.hasSourceRect = true;
        }

        return config;
    }

    AnimationClipConfig parseAnimationClipConfig(const std::string &filePath)
    {
        toml::table document;
        try
        {
            document = toml::parse_file(filePath);
        }
        catch (const toml::parse_error &error)
        {
            throw std::runtime_error("Failed to parse animation clip file '" + filePath + "': " + std::string(error.description()));
        }

        const std::string format = requireString(document, "format", "animation clip format");
        if (!format.empty() && format != ANIMATION_CLIP_FORMAT)
        {
            throw std::runtime_error("Animation clip file '" + filePath + "' has unsupported format '" + format + "'.");
        }

        const int64_t version = readInteger(document, "version", 1, "animation clip version");
        if (document.contains("version") && version != 1)
        {
            throw std::runtime_error("Animation clip file '" + filePath + "' has unsupported version '" + std::to_string(version) + "'.");
        }

        AnimationClipConfig config;
        config.name = requireString(document, "name", "animation clip name");
        config.fps = readFloat(document, "fps", config.fps, "animation clip fps");
        config.loop = readBool(document, "loop", config.loop, "animation clip loop");

        const toml::array *size = readArray(document, "size", "animation clip size");
        if (size != nullptr)
        {
            if (size->size() != 2)
            {
                throw std::runtime_error("animation clip size must contain exactly two numeric values.");
            }

            const toml::node *widthNode = size->get(0);
            const toml::node *heightNode = size->get(1);
            if (widthNode == nullptr || heightNode == nullptr)
            {
                throw std::runtime_error("animation clip size must contain exactly two numeric values.");
            }

            config.width = static_cast<float>(readNumber(*widthNode, "animation clip size"));
            config.height = static_cast<float>(readNumber(*heightNode, "animation clip size"));
        }

        config.width = readFloat(document, "width", config.width, "animation clip width");
        config.height = readFloat(document, "height", config.height, "animation clip height");

        const toml::array *frames = readArray(document, "frames", "animation clip frames");
        if (frames == nullptr || frames->empty())
        {
            throw std::runtime_error("Animation clip file '" + filePath + "' must define at least one frame.");
        }

        config.frames.reserve(frames->size());
        for (size_t index = 0; index < frames->size(); ++index)
        {
            const toml::node *frameNode = frames->get(index);
            if (frameNode == nullptr)
            {
                throw std::runtime_error("Animation clip file '" + filePath + "' contains an invalid frame entry.");
            }

            const std::string context = "frames[" + std::to_string(index) + "]";
            if (frameNode->is_string())
            {
                config.frames.push_back(AnimationClipFrameConfig{frameNode->value_exact<std::string>().value_or(""), 0.0f, SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f}, false});
            }
            else if (const toml::table *frameTable = frameNode->as_table())
            {
                config.frames.push_back(parseAnimationClipFrameConfig(*frameTable, context));
            }
            else
            {
                throw std::runtime_error("Animation clip file '" + filePath + "' " + context + " must be either a string path or a frame table.");
            }
        }

        return config;
    }

    bool requiresFrameTable(const AnimationFrame &frame)
    {
        return frame.hasSourceRect || std::abs(frame.duration) > 1e-6f;
    }

    std::vector<ObjectConfig> parseSceneObjects(const toml::table &document, const std::string &sceneFilePath, int64_t version)
    {
        const toml::node *objectsNode = document.get("objects");
        if (objectsNode == nullptr)
        {
            return {};
        }

        const toml::array *objectsArray = objectsNode->as_array();
        if (objectsArray == nullptr)
        {
            throw std::runtime_error("Scene 'objects' must be an array of tables.");
        }

        std::vector<ObjectConfig> objects;
        objects.reserve(objectsArray->size());
        for (size_t objectIndex = 0; objectIndex < objectsArray->size(); ++objectIndex)
        {
            const toml::node *objectNode = objectsArray->get(objectIndex);
            if (objectNode == nullptr)
            {
                throw std::runtime_error("Scene objects contains an invalid entry.");
            }

            const toml::table *objectTable = objectNode->as_table();
            if (objectTable == nullptr)
            {
                throw std::runtime_error("Scene objects must be tables.");
            }

            objects.push_back(parseObject(*objectTable, sceneFilePath, objectIndex, version));
        }

        return objects;
    }

    toml::table makeAnimationClipDocument(const AnimationClip &animationClip, const std::string &filePath)
    {
        if (animationClip.getFrames().empty())
        {
            throw std::runtime_error("Animation clip file '" + filePath + "' cannot be written without at least one frame.");
        }

        toml::table root;
        root.insert_or_assign("format", std::string(ANIMATION_CLIP_FORMAT));
        root.insert_or_assign("version", 1);
        root.insert_or_assign("name", animationClip.getName().empty() ? std::filesystem::path(filePath).stem().string() : animationClip.getName());
        root.insert_or_assign("fps", static_cast<double>(animationClip.getFramesPerSecond()));
        root.insert_or_assign("loop", animationClip.getLoop());
        root.insert_or_assign("size", toml::array{static_cast<double>(animationClip.getWidth()), static_cast<double>(animationClip.getHeight())});

        toml::array framesArray;
        for (const AnimationFrame &frame : animationClip.getFrames())
        {
            if (frame.textureWrapper == nullptr)
            {
                throw std::runtime_error("Animation clip file '" + filePath + "' contains a frame without a texture.");
            }

            const std::string texturePath = makeSceneRelativePath(filePath, frame.textureWrapper->getFilePath());
            if (!requiresFrameTable(frame))
            {
                framesArray.push_back(texturePath);
            }
            else
            {
                toml::table frameTable;
                frameTable.is_inline(true);
                frameTable.insert_or_assign("path", texturePath);
                if (std::abs(frame.duration) > 1e-6f)
                {
                    frameTable.insert_or_assign("duration", static_cast<double>(frame.duration));
                }
                if (frame.hasSourceRect)
                {
                    frameTable.insert_or_assign("rect", toml::array{static_cast<double>(frame.sourceRect.x),
                                                                    static_cast<double>(frame.sourceRect.y),
                                                                    static_cast<double>(frame.sourceRect.w),
                                                                    static_cast<double>(frame.sourceRect.h)});
                }

                framesArray.push_back(std::move(frameTable));
            }
        }

        root.insert_or_assign("frames", std::move(framesArray));
        return root;
    }

    toml::table makeSceneDocument(const std::vector<GameObject *> &gameObjects, const std::string &sceneFilePath)
    {
        toml::table root;
        root.insert_or_assign("format", std::string(SCENE_FORMAT));
        root.insert_or_assign("version", SCENE_VERSION_CURRENT);

        toml::array objectsArray;
        for (GameObject *gameObject : gameObjects)
        {
            if (gameObject == nullptr)
            {
                continue;
            }

            toml::table objectTable;
            objectTable.insert_or_assign("id", static_cast<int64_t>(gameObject->getId()));
            if (!gameObject->getName().empty())
            {
                objectTable.insert_or_assign("name", gameObject->getName());
            }
            if (!gameObject->getTag().empty())
            {
                objectTable.insert_or_assign("tag", gameObject->getTag());
            }
            objectTable.insert_or_assign("active", gameObject->getActive());
            objectTable.insert_or_assign("position", toml::array{static_cast<double>(gameObject->getPosition().x()), static_cast<double>(gameObject->getPosition().y())});
            objectTable.insert_or_assign("rotation", static_cast<double>(gameObject->getRotation()));
            objectTable.insert_or_assign("scale", toml::array{static_cast<double>(gameObject->getScale().x()), static_cast<double>(gameObject->getScale().y())});

            toml::array childrenArray;
            for (GameObject *child : gameObject->getChildren())
            {
                if (child != nullptr)
                {
                    childrenArray.push_back(static_cast<int64_t>(child->getId()));
                }
            }
            if (!childrenArray.empty())
            {
                objectTable.insert_or_assign("children", std::move(childrenArray));
            }

            if (Camera *camera = gameObject->getComponent<Camera>())
            {
                toml::table cameraTable;
                cameraTable.insert_or_assign("id", static_cast<int64_t>(camera->getId()));
                const SDL_FRect &cameraRect = camera->getCamera();
                cameraTable.insert_or_assign("viewport", toml::array{static_cast<double>(cameraRect.x),
                                                                     static_cast<double>(cameraRect.y),
                                                                     static_cast<double>(cameraRect.w),
                                                                     static_cast<double>(cameraRect.h)});
                objectTable.insert_or_assign("camera", std::move(cameraTable));
            }

            if (Rigidbody *rigidbody = gameObject->getComponent<Rigidbody>())
            {
                toml::table rigidbodyTable;
                rigidbodyTable.insert_or_assign("id", static_cast<int64_t>(rigidbody->getId()));
                rigidbodyTable.insert_or_assign("bodyType", bodyTypeToToken(rigidbody->getBodyType()));
                rigidbodyTable.insert_or_assign("gravity", rigidbody->getGravity());
                rigidbodyTable.insert_or_assign("mass", static_cast<double>(rigidbody->getMass()));
                rigidbodyTable.insert_or_assign("velocity", toml::array{static_cast<double>(rigidbody->getVelocity().x()), static_cast<double>(rigidbody->getVelocity().y())});
                rigidbodyTable.insert_or_assign("force", toml::array{static_cast<double>(rigidbody->getForce().x()), static_cast<double>(rigidbody->getForce().y())});
                rigidbodyTable.insert_or_assign("angularVelocity", static_cast<double>(rigidbody->getAngularVelocity()));
                rigidbodyTable.insert_or_assign("torque", static_cast<double>(rigidbody->getTorque()));
                rigidbodyTable.insert_or_assign("friction", static_cast<double>(rigidbody->getFriction()));
                rigidbodyTable.insert_or_assign("restitution", static_cast<double>(rigidbody->getRestitution()));
                objectTable.insert_or_assign("rigidbody", std::move(rigidbodyTable));
            }

            if (Collider *collider = static_cast<Collider *>(gameObject->getComponent(ComponentType::COLLIDER)))
            {
                if (collider->getColliderType() == ColliderType::BoxCollider)
                {
                    BoxCollider *boxCollider = static_cast<BoxCollider *>(collider);
                    const float scaleX = gameObject->getScale().x();
                    const float scaleY = gameObject->getScale().y();
                    const float unscaledWidth = std::abs(scaleX) > 1e-6f ? boxCollider->getWidth() / scaleX : boxCollider->getWidth();
                    const float unscaledHeight = std::abs(scaleY) > 1e-6f ? boxCollider->getHeight() / scaleY : boxCollider->getHeight();

                    toml::table boxColliderTable;
                    boxColliderTable.insert_or_assign("id", static_cast<int64_t>(boxCollider->getId()));
                    boxColliderTable.insert_or_assign("size", toml::array{static_cast<double>(unscaledWidth), static_cast<double>(unscaledHeight)});
                    boxColliderTable.insert_or_assign("trigger", boxCollider->getIsTrigger());
                    boxColliderTable.insert_or_assign("collisionGroup", static_cast<int64_t>(boxCollider->getCollisionGroup()));
                    boxColliderTable.insert_or_assign("collisionMask", static_cast<int64_t>(boxCollider->getCollisionMask()));
                    objectTable.insert_or_assign("boxCollider", std::move(boxColliderTable));
                }
                else if (collider->getColliderType() == ColliderType::CircleCollider)
                {
                    CircleCollider *circleCollider = static_cast<CircleCollider *>(collider);
                    const float scaleX = std::abs(gameObject->getScale().x());
                    const float scaleY = std::abs(gameObject->getScale().y());
                    const float maxScale = std::max(scaleX, scaleY);
                    const float unscaledRadius = maxScale > 1e-6f ? circleCollider->getRadius() / maxScale : circleCollider->getRadius();

                    toml::table circleColliderTable;
                    circleColliderTable.insert_or_assign("id", static_cast<int64_t>(circleCollider->getId()));
                    circleColliderTable.insert_or_assign("radius", static_cast<double>(unscaledRadius));
                    circleColliderTable.insert_or_assign("trigger", circleCollider->getIsTrigger());
                    circleColliderTable.insert_or_assign("collisionGroup", static_cast<int64_t>(circleCollider->getCollisionGroup()));
                    circleColliderTable.insert_or_assign("collisionMask", static_cast<int64_t>(circleCollider->getCollisionMask()));
                    objectTable.insert_or_assign("circleCollider", std::move(circleColliderTable));
                }
            }

            if (Sprite *sprite = gameObject->getComponent<Sprite>())
            {
                toml::table spriteTable;
                spriteTable.insert_or_assign("id", static_cast<int64_t>(sprite->getId()));
                if (TextureWrapper *textureWrapper = sprite->getTextureWrapper())
                {
                    spriteTable.insert_or_assign("path", makeSceneRelativePath(sceneFilePath, textureWrapper->getFilePath()));
                }
                spriteTable.insert_or_assign("flip", flipModeToToken(sprite->getFlip()));
                spriteTable.insert_or_assign("size", toml::array{static_cast<double>(sprite->getWidth()), static_cast<double>(sprite->getHeight())});
                if (sprite->hasSourceRect())
                {
                    const SDL_FRect *sourceRect = sprite->getSourceRect();
                    spriteTable.insert_or_assign("sourceRect", toml::array{static_cast<double>(sourceRect->x),
                                                                           static_cast<double>(sourceRect->y),
                                                                           static_cast<double>(sourceRect->w),
                                                                           static_cast<double>(sourceRect->h)});
                }
                objectTable.insert_or_assign("sprite", std::move(spriteTable));
            }

            if (Animator *animator = gameObject->getComponent<Animator>())
            {
                toml::table animatorTable;
                animatorTable.insert_or_assign("id", static_cast<int64_t>(animator->getId()));
                animatorTable.insert_or_assign("playbackSpeed", static_cast<double>(animator->getPlaybackSpeed()));
                if (const AnimationClip *clip = animator->getCurrentAnimationClip())
                {
                    if (!clip->getFilePath().empty())
                    {
                        animatorTable.insert_or_assign("clip", makeSceneRelativePath(sceneFilePath, clip->getFilePath()));
                        animatorTable.insert_or_assign("playing", animator->getIsPlaying());
                    }
                }
                objectTable.insert_or_assign("animator", std::move(animatorTable));
            }

            if (Audio *audio = gameObject->getComponent<Audio>())
            {
                toml::table audioTable;
                audioTable.insert_or_assign("id", static_cast<int64_t>(audio->getId()));
                if (!audio->getFilePath().empty())
                {
                    audioTable.insert_or_assign("path", makeSceneRelativePath(sceneFilePath, audio->getFilePath()));
                }
                audioTable.insert_or_assign("autoplay", audio->getAutoPlay());
                audioTable.insert_or_assign("loops", static_cast<int64_t>(audio->getLoopCount()));
                audioTable.insert_or_assign("gain", static_cast<double>(audio->getGain()));
                objectTable.insert_or_assign("audio", std::move(audioTable));
            }

            if (ScriptComponent *scriptComponent = gameObject->getComponent<ScriptComponent>())
            {
                toml::table scriptTable;
                scriptTable.insert_or_assign("id", static_cast<int64_t>(scriptComponent->getId()));

                toml::array behaviorArray;
                for (const Behavior *behavior : scriptComponent->getBehaviors())
                {
                    BehaviorSpec spec;
                    spec.type = behavior->getTypeName();
                    spec.enabled = behavior->getEnabled();
                    if (spec.type.empty())
                    {
                        continue;
                    }

                    behavior->serialize(spec);

                    toml::table behaviorTable;
                    behaviorTable.insert_or_assign("type", spec.type);
                    behaviorTable.insert_or_assign("enabled", spec.enabled);
                    for (const BehaviorProperty &property : spec.properties)
                    {
                        insertBehaviorProperty(behaviorTable, property);
                    }

                    behaviorArray.push_back(std::move(behaviorTable));
                }

                if (!behaviorArray.empty())
                {
                    scriptTable.insert_or_assign("behaviors", std::move(behaviorArray));
                }

                objectTable.insert_or_assign("script", std::move(scriptTable));
            }

            objectsArray.push_back(std::move(objectTable));
        }

        root.insert_or_assign("objects", std::move(objectsArray));
        return root;
    }
}

Scene::Scene(std::string filepath) : filePath(std::move(filepath))
{
}

Scene::~Scene()
{
}

AnimationClip Scene::loadAnimationClipFile(const std::string &filePath)
{
    const AnimationClipConfig config = parseAnimationClipConfig(filePath);

    GameManager &gameManager = GameManager::getInstance();
    std::vector<AnimationFrame> frames;
    frames.reserve(config.frames.size());
    for (const AnimationClipFrameConfig &frameConfig : config.frames)
    {
        TextureWrapper *textureWrapper = gameManager.loadTexture(resolveResourcePath(filePath, frameConfig.path));
        if (frameConfig.hasSourceRect)
        {
            frames.emplace_back(textureWrapper, frameConfig.sourceRect, frameConfig.duration);
        }
        else
        {
            frames.emplace_back(textureWrapper, frameConfig.duration);
        }
    }

    const std::string clipName = config.name.empty() ? std::filesystem::path(filePath).stem().string() : config.name;
    return AnimationClip(std::move(frames), config.fps, config.loop, config.width, config.height, clipName, filePath);
}

void Scene::saveAnimationClipFile(const AnimationClip &animationClip, const std::string &filePath)
{
    const std::filesystem::path outputPath(filePath);
    if (outputPath.has_parent_path())
    {
        std::filesystem::create_directories(outputPath.parent_path());
    }

    std::ofstream clipFile(filePath);
    if (!clipFile.is_open())
    {
        throw std::runtime_error("Failed to open animation clip file '" + filePath + "' for writing.");
    }

    clipFile << makeAnimationClipDocument(animationClip, filePath) << '\n';
}

std::vector<GameObject *> Scene::loadScene()
{
    toml::table document;
    try
    {
        document = toml::parse_file(filePath);
    }
    catch (const toml::parse_error &error)
    {
        throw std::runtime_error("Failed to parse scene file '" + filePath + "': " + std::string(error.description()));
    }

    const std::string format = requireString(document, "format", "scene format");
    if (!format.empty() && format != SCENE_FORMAT)
    {
        throw std::runtime_error("Scene file '" + filePath + "' has unsupported format '" + format + "'.");
    }

    const int64_t version = readInteger(document, "version", SCENE_VERSION_LEGACY, "scene version");
    if (version != SCENE_VERSION_LEGACY && version != SCENE_VERSION_CURRENT)
    {
        throw std::runtime_error("Scene file '" + filePath + "' has unsupported version '" + std::to_string(version) + "'.");
    }

    const std::vector<ObjectConfig> configs = parseSceneObjects(document, filePath, version);
    validateSceneIds(configs);
    GameManager &gameManager = GameManager::getInstance();
    std::vector<GameObject *> objects;
    objects.reserve(configs.size());
    std::unordered_map<uint64_t, GameObject *> objectsById;

    try
    {
        for (const ObjectConfig &config : configs)
        {
            if (config.boxCollider.enabled && config.circleCollider.enabled)
            {
                throw std::runtime_error("A scene object cannot define both boxCollider and circleCollider.");
            }

            GameObject *gameObject = new GameObject(config.rotation, config.active, config.position, config.scale, config.name, config.tag);
            if (config.id != 0)
            {
                gameObject->setId(config.id);
            }

            try
            {
                if (config.camera.enabled)
                {
                    Camera *camera = new Camera(gameObject, config.camera.x, config.camera.y, config.camera.width, config.camera.height);
                    if (config.camera.id != 0)
                    {
                        camera->setId(config.camera.id);
                    }
                    gameObject->addComponentInternal(camera);
                }

                if (config.rigidbody.enabled)
                {
                    Rigidbody *rigidbody = new Rigidbody(gameObject, config.rigidbody.bodyType, config.rigidbody.gravity);
                    if (config.rigidbody.id != 0)
                    {
                        rigidbody->setId(config.rigidbody.id);
                    }
                    rigidbody->setMass(config.rigidbody.mass);
                    rigidbody->setVelocity(config.rigidbody.velocity);
                    rigidbody->setForce(config.rigidbody.force);
                    rigidbody->setAngularVelocity(config.rigidbody.angularVelocity);
                    rigidbody->setTorque(config.rigidbody.torque);
                    rigidbody->setFriction(config.rigidbody.friction);
                    rigidbody->setRestitution(config.rigidbody.restitution);
                    gameObject->addComponentInternal(rigidbody);
                }

                if (config.boxCollider.enabled)
                {
                    if (config.boxCollider.width <= 0.0f || config.boxCollider.height <= 0.0f)
                    {
                        throw std::runtime_error("boxCollider requires positive width and height.");
                    }

                    BoxCollider *boxCollider = new BoxCollider(gameObject, config.boxCollider.width, config.boxCollider.height);
                    if (config.boxCollider.id != 0)
                    {
                        boxCollider->setId(config.boxCollider.id);
                    }
                    boxCollider->setIsTrigger(config.boxCollider.isTrigger);
                    boxCollider->setCollisionGroup(config.boxCollider.collisionGroup);
                    boxCollider->setCollisionMask(config.boxCollider.collisionMask);
                    gameObject->addComponentInternal(boxCollider);
                }

                if (config.circleCollider.enabled)
                {
                    if (config.circleCollider.radius <= 0.0f)
                    {
                        throw std::runtime_error("circleCollider requires a positive radius.");
                    }

                    CircleCollider *circleCollider = new CircleCollider(gameObject, config.circleCollider.radius);
                    if (config.circleCollider.id != 0)
                    {
                        circleCollider->setId(config.circleCollider.id);
                    }
                    circleCollider->setIsTrigger(config.circleCollider.isTrigger);
                    circleCollider->setCollisionGroup(config.circleCollider.collisionGroup);
                    circleCollider->setCollisionMask(config.circleCollider.collisionMask);
                    gameObject->addComponentInternal(circleCollider);
                }

                if (config.sprite.enabled)
                {
                    Sprite *sprite = nullptr;
                    if (config.sprite.path.empty())
                    {
                        sprite = new Sprite(gameObject);
                    }
                    else
                    {
                        const std::string resolvedPath = resolveResourcePath(filePath, config.sprite.path);
                        sprite = new Sprite(gameObject, resolvedPath.c_str(), config.sprite.flip, config.sprite.width, config.sprite.height);
                    }

                    if (config.sprite.id != 0)
                    {
                        sprite->setId(config.sprite.id);
                    }
                    sprite->setFlip(config.sprite.flip);
                    sprite->setWidth(config.sprite.width);
                    sprite->setHeight(config.sprite.height);
                    if (config.sprite.hasSourceRect)
                    {
                        sprite->setSourceRect(config.sprite.sourceRect);
                    }
                    gameObject->addComponentInternal(sprite);
                }

                if (config.animator.enabled)
                {
                    Animator *animator = new Animator(gameObject);
                    if (config.animator.id != 0)
                    {
                        animator->setId(config.animator.id);
                    }
                    animator->setPlaybackSpeed(config.animator.playbackSpeed);
                    if (!config.animator.clipPath.empty())
                    {
                        AnimationClip *clip = gameManager.loadAnimationClip(resolveResourcePath(filePath, config.animator.clipPath));
                        if (clip != nullptr)
                        {
                            animator->play(clip);
                            if (!config.animator.playing)
                            {
                                animator->pause();
                            }
                        }
                    }
                    gameObject->addComponentInternal(animator);
                }

                if (config.audio.enabled)
                {
                    Audio *audio = nullptr;
                    if (config.audio.path.empty())
                    {
                        audio = new Audio(gameObject);
                        audio->setGain(config.audio.gain);
                        audio->setLoopCount(static_cast<float>(config.audio.loops));
                    }
                    else
                    {
                        audio = new Audio(gameObject,
                                          resolveResourcePath(filePath, config.audio.path).c_str(),
                                          config.audio.gain,
                                          config.audio.autoplay,
                                          static_cast<float>(config.audio.loops));
                    }

                    if (config.audio.id != 0)
                    {
                        audio->setId(config.audio.id);
                    }
                    gameObject->addComponentInternal(audio);
                }

                if (config.script.enabled || !config.script.behaviors.empty())
                {
                    ScriptComponent *scriptComponent = new ScriptComponent(gameObject);
                    if (config.script.id != 0)
                    {
                        scriptComponent->setId(config.script.id);
                    }

                    for (const BehaviorSpec &behaviorSpec : config.script.behaviors)
                    {
                        Behavior *behavior = BehaviorFactoryRegistry::getInstance().instantiateBehavior(scriptComponent, behaviorSpec);
                        if (behavior == nullptr)
                        {
                            delete scriptComponent;
                            throw std::runtime_error("Unknown behavior type '" + behaviorSpec.type + "'.");
                        }
                    }

                    gameObject->addComponentInternal(scriptComponent);
                }

                objects.push_back(gameObject);
                objectsById[gameObject->getId()] = gameObject;
            }
            catch (const std::exception &)
            {
                delete gameObject;
                throw;
            }
        }

        for (size_t index = 0; index < configs.size(); ++index)
        {
            GameObject *gameObject = objects[index];
            for (uint64_t childId : configs[index].children)
            {
                auto childIt = objectsById.find(childId);
                if (childIt != objectsById.end() && childIt->second != nullptr)
                {
                    gameObject->addChild(childIt->second);
                }
            }
        }

        return objects;
    }
    catch (const std::exception &)
    {
        for (GameObject *gameObject : objects)
        {
            delete gameObject;
        }

        throw;
    }
}

void Scene::saveScene(const std::vector<GameObject *> &gameObjects) const
{
    const std::filesystem::path outputPath(filePath);
    if (outputPath.has_parent_path())
    {
        std::filesystem::create_directories(outputPath.parent_path());
    }

    std::ofstream sceneFile(filePath);
    if (!sceneFile.is_open())
    {
        throw std::runtime_error("Failed to open scene file '" + filePath + "' for writing.");
    }

    sceneFile << makeSceneDocument(gameObjects, filePath) << '\n';
}
