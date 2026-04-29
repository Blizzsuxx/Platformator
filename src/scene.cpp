#include "scene.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <toml++/toml.hpp>

#include "animationclipfilewriter.h"
#include "animationclip.h"
#include "animator.h"
#include "audio.h"
#include "behaviorfactoryregistry.h"
#include "boxcollider.h"
#include "camera.h"
#include "circlecollider.h"
#include "gamemanager.h"
#include "rigidbody.h"
#include "scenefilewriter.h"
#include "scriptcomponent.h"
#include "sprite.h"

namespace
{
    constexpr uint64_t DEFAULT_COLLISION_GROUP = 1;
    constexpr uint64_t DEFAULT_COLLISION_MASK = 1;

    struct CameraConfig
    {
        bool enabled = false;
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct RigidbodyConfig
    {
        bool enabled = false;
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
        float width = 0.0f;
        float height = 0.0f;
        bool isTrigger = false;
        uint64_t collisionGroup = DEFAULT_COLLISION_GROUP;
        uint64_t collisionMask = DEFAULT_COLLISION_MASK;
    };

    struct CircleColliderConfig
    {
        bool enabled = false;
        float radius = 0.0f;
        bool isTrigger = false;
        uint64_t collisionGroup = DEFAULT_COLLISION_GROUP;
        uint64_t collisionMask = DEFAULT_COLLISION_MASK;
    };

    struct SpriteConfig
    {
        bool enabled = false;
        std::string path;
        SDL_FlipMode flip = SDL_FLIP_NONE;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct AnimatorConfig
    {
        bool enabled = false;
        float playbackSpeed = 1.0f;
    };

    struct AudioConfig
    {
        bool enabled = false;
        std::string path;
        bool autoplay = false;
        int loops = 0;
        float gain = 1.0f;
    };

    struct ObjectConfig
    {
        std::string name;
        std::string tag;
        bool active = true;
        Eigen::Vector2f position = Eigen::Vector2f::Zero();
        float rotation = 0.0f;
        Eigen::Vector2f scale = Eigen::Vector2f::Ones();
        CameraConfig camera;
        RigidbodyConfig rigidbody;
        BoxColliderConfig boxCollider;
        CircleColliderConfig circleCollider;
        SpriteConfig sprite;
        AnimatorConfig animator;
        AudioConfig audio;
        std::vector<BehaviorSpec> scripts;
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

    std::string formatFloat(double value)
    {
        if (std::abs(value) < 1e-9)
        {
            value = 0.0;
        }

        std::ostringstream stream;
        stream.setf(std::ios::fixed, std::ios::floatfield);
        stream.precision(6);
        stream << value;

        std::string text = stream.str();
        while (!text.empty() && text.back() == '0')
        {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.')
        {
            text.pop_back();
        }
        if (text.empty() || text == "-0")
        {
            return "0";
        }

        return text;
    }

    std::string boolToToken(bool value)
    {
        return value ? "true" : "false";
    }

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
                const auto value = node->value_exact<std::string>();
                return *value;
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
                const auto value = node->value_exact<bool>();
                return *value;
            }

            throw std::runtime_error(context + " must be a bool.");
        }

        return fallback;
    }

    double readNumber(const toml::node &node, const std::string &context)
    {
        if (node.is_floating_point())
        {
            const auto value = node.value_exact<double>();
            return *value;
        }

        if (node.is_integer())
        {
            const auto value = node.value_exact<int64_t>();
            return static_cast<double>(*value);
        }

        throw std::runtime_error(context + " must be numeric.");
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
            if (node->is_integer())
            {
                const auto value = node->value_exact<int64_t>();
                return *value;
            }

            throw std::runtime_error(context + " must be an integer.");
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

    std::string resolveResourcePath(const std::string &sourcePath, const std::string &resourcePath)
    {
        if (resourcePath.empty())
        {
            return "";
        }

        const std::filesystem::path path(resourcePath);
        if (path.is_absolute())
        {
            return path.lexically_normal().string();
        }

        return (std::filesystem::path(sourcePath).parent_path() / path).lexically_normal().string();
    }

    std::string resolveSceneRelativePath(const std::string &scenePath, const std::string &resourcePath)
    {
        return resolveResourcePath(scenePath, resourcePath);
    }

    AnimationClipFrameConfig parseAnimationClipFrameConfig(const toml::table &table, const std::string &context)
    {
        for (const auto &[key, node] : table)
        {
            const std::string keyString = std::string(key.str());
            if (keyString != "path" && keyString != "duration" && keyString != "rect" && keyString != "sourceRect")
            {
                throw std::runtime_error(context + " contains unsupported key '" + keyString + "'.");
            }
            (void)node;
        }

        AnimationClipFrameConfig config;
        config.path = requireString(table, "path", context + ".path");
        if (config.path.empty())
        {
            throw std::runtime_error(context + ".path is required.");
        }

        config.duration = readFloat(table, "duration", 0.0f, context + ".duration");

        const toml::node *rectNode = table.get("rect");
        const toml::node *sourceRectNode = table.get("sourceRect");
        if (rectNode != nullptr && sourceRectNode != nullptr)
        {
            throw std::runtime_error(context + " cannot define both rect and sourceRect.");
        }
        if (rectNode != nullptr)
        {
            config.sourceRect = readRect4(table, "rect", SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f}, context + ".rect");
            config.hasSourceRect = true;
        }
        else if (sourceRectNode != nullptr)
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

        for (const auto &[key, node] : document)
        {
            const std::string keyString = std::string(key.str());
            if (keyString != "format" && keyString != "version" && keyString != "name" && keyString != "fps" && keyString != "loop" &&
                keyString != "size" && keyString != "width" && keyString != "height" && keyString != "frames")
            {
                throw std::runtime_error("Animation clip file '" + filePath + "' contains unsupported key '" + keyString + "'.");
            }
            (void)node;
        }

        const std::string format = requireString(document, "format", "animation clip format");
        if (!format.empty() && format != "platformator_animset")
        {
            throw std::runtime_error("Animation clip file '" + filePath + "' has unsupported format '" + format + "'.");
        }

        const int64_t version = readInteger(document, "version", 1, "animation clip version");
        if (document.get("version") != nullptr && version != 1)
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
            if (const auto path = frameNode->value<std::string>())
            {
                config.frames.push_back(AnimationClipFrameConfig{*path, 0.0f, SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f}, false});
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

    BehaviorProperty parseScriptProperty(const std::string &key, const toml::node &node, const std::string &context)
    {
        if (node.is_string())
        {
            const auto value = node.value<std::string>();
            return BehaviorProperty{key, *value};
        }

        if (node.is_integer())
        {
            const auto value = node.value<int64_t>();
            return BehaviorProperty{key, *value};
        }

        if (node.is_floating_point())
        {
            const auto value = node.value<double>();
            return BehaviorProperty{key, *value};
        }

        if (node.is_boolean())
        {
            const auto value = node.value<bool>();
            return BehaviorProperty{key, *value};
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

        throw std::runtime_error(context + " uses an unsupported TOML value type.");
    }

    CameraConfig parseCameraConfig(const toml::table &table)
    {
        CameraConfig config;
        config.enabled = true;

        const SDL_FRect viewport = readRect4(table, "viewport", SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f}, "camera.viewport");
        config.x = readFloat(table, "x", viewport.x, "camera.x");
        config.y = readFloat(table, "y", viewport.y, "camera.y");
        config.width = readFloat(table, "width", viewport.w, "camera.width");
        config.height = readFloat(table, "height", viewport.h, "camera.height");
        return config;
    }

    RigidbodyConfig parseRigidbodyConfig(const toml::table &table)
    {
        RigidbodyConfig config;
        config.enabled = true;
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

    BoxColliderConfig parseBoxColliderConfig(const toml::table &table)
    {
        BoxColliderConfig config;
        config.enabled = true;
        const Eigen::Vector2f size = readVector2(table, "size", Eigen::Vector2f(config.width, config.height), "boxCollider.size");
        config.width = readFloat(table, "width", size.x(), "boxCollider.width");
        config.height = readFloat(table, "height", size.y(), "boxCollider.height");
        config.isTrigger = readBool(table, "trigger", config.isTrigger, "boxCollider.trigger");
        const int64_t collisionGroup = readInteger(table, "collisionGroup", static_cast<int64_t>(config.collisionGroup), "boxCollider.collisionGroup");
        const int64_t collisionMask = readInteger(table, "collisionMask", static_cast<int64_t>(config.collisionMask), "boxCollider.collisionMask");
        if (collisionGroup < 0 || collisionMask < 0)
        {
            throw std::runtime_error("Collider collisionGroup and collisionMask must be non-negative.");
        }
        config.collisionGroup = static_cast<uint64_t>(collisionGroup);
        config.collisionMask = static_cast<uint64_t>(collisionMask);
        return config;
    }

    CircleColliderConfig parseCircleColliderConfig(const toml::table &table)
    {
        CircleColliderConfig config;
        config.enabled = true;
        config.radius = readFloat(table, "radius", config.radius, "circleCollider.radius");
        config.isTrigger = readBool(table, "trigger", config.isTrigger, "circleCollider.trigger");
        const int64_t collisionGroup = readInteger(table, "collisionGroup", static_cast<int64_t>(config.collisionGroup), "circleCollider.collisionGroup");
        const int64_t collisionMask = readInteger(table, "collisionMask", static_cast<int64_t>(config.collisionMask), "circleCollider.collisionMask");
        if (collisionGroup < 0 || collisionMask < 0)
        {
            throw std::runtime_error("Collider collisionGroup and collisionMask must be non-negative.");
        }
        config.collisionGroup = static_cast<uint64_t>(collisionGroup);
        config.collisionMask = static_cast<uint64_t>(collisionMask);
        return config;
    }

    SpriteConfig parseSpriteConfig(const toml::table &table)
    {
        SpriteConfig config;
        config.enabled = true;
        config.path = requireString(table, "path", "sprite.path");
        const std::string flipToken = requireString(table, "flip", "sprite.flip");
        if (!flipToken.empty())
        {
            config.flip = parseFlipMode(flipToken, "sprite.flip");
        }
        const Eigen::Vector2f size = readVector2(table, "size", Eigen::Vector2f(config.width, config.height), "sprite.size");
        config.width = readFloat(table, "width", size.x(), "sprite.width");
        config.height = readFloat(table, "height", size.y(), "sprite.height");
        return config;
    }

    AnimatorConfig parseAnimatorConfig(const toml::table &table)
    {
        AnimatorConfig config;
        config.enabled = true;
        config.playbackSpeed = readFloat(table, "playbackSpeed", config.playbackSpeed, "animator.playbackSpeed");
        return config;
    }

    AudioConfig parseAudioConfig(const toml::table &table)
    {
        AudioConfig config;
        config.enabled = true;
        config.path = requireString(table, "path", "audio.path");
        config.autoplay = readBool(table, "autoplay", config.autoplay, "audio.autoplay");
        config.loops = static_cast<int>(readInteger(table, "loops", config.loops, "audio.loops"));
        config.gain = readFloat(table, "gain", config.gain, "audio.gain");
        return config;
    }

    BehaviorSpec parseBehaviorSpec(const toml::table &table, const std::string &sceneFilePath, size_t index)
    {
        BehaviorSpec spec;
        spec.sourcePath = sceneFilePath;
        spec.type = requireString(table, "type", "scripts[" + std::to_string(index) + "].type");
        spec.enabled = readBool(table, "enabled", true, "scripts[" + std::to_string(index) + "].enabled");
        if (spec.type.empty())
        {
            throw std::runtime_error("scripts[" + std::to_string(index) + "].type is required.");
        }

        for (const auto &[key, node] : table)
        {
            const std::string propertyName = std::string(key.str());
            if (propertyName == "type" || propertyName == "enabled")
            {
                continue;
            }

            const BehaviorProperty property = parseScriptProperty(propertyName, node, "scripts[" + std::to_string(index) + "]." + propertyName);
            spec.setProperty(property.name, property.value);
        }

        return spec;
    }

    ObjectConfig parseObject(const toml::table &table, const std::string &sceneFilePath, size_t index)
    {
        ObjectConfig config;
        const std::string objectContext = "objects[" + std::to_string(index) + "]";
        config.name = requireString(table, "name", objectContext + ".name");
        config.tag = requireString(table, "tag", objectContext + ".tag");
        config.active = readBool(table, "active", config.active, objectContext + ".active");
        config.position = readVector2(table, "position", config.position, objectContext + ".position");
        config.rotation = readFloat(table, "rotation", config.rotation, objectContext + ".rotation");
        config.scale = readVector2(table, "scale", config.scale, objectContext + ".scale");

        for (const auto &[key, node] : table)
        {
            const std::string name = std::string(key.str());
            if (name == "name" || name == "tag" || name == "active" || name == "position" || name == "rotation" || name == "scale")
            {
                continue;
            }

            if (name == "camera")
            {
                if (const toml::table *componentTable = node.as_table())
                {
                    config.camera = parseCameraConfig(*componentTable);
                    continue;
                }
            }
            else if (name == "rigidbody")
            {
                if (const toml::table *componentTable = node.as_table())
                {
                    config.rigidbody = parseRigidbodyConfig(*componentTable);
                    continue;
                }
            }
            else if (name == "boxCollider")
            {
                if (const toml::table *componentTable = node.as_table())
                {
                    config.boxCollider = parseBoxColliderConfig(*componentTable);
                    continue;
                }
            }
            else if (name == "circleCollider")
            {
                if (const toml::table *componentTable = node.as_table())
                {
                    config.circleCollider = parseCircleColliderConfig(*componentTable);
                    continue;
                }
            }
            else if (name == "sprite")
            {
                if (const toml::table *componentTable = node.as_table())
                {
                    config.sprite = parseSpriteConfig(*componentTable);
                    continue;
                }
            }
            else if (name == "animator")
            {
                if (const toml::table *componentTable = node.as_table())
                {
                    config.animator = parseAnimatorConfig(*componentTable);
                    continue;
                }
            }
            else if (name == "audio")
            {
                if (const toml::table *componentTable = node.as_table())
                {
                    config.audio = parseAudioConfig(*componentTable);
                    continue;
                }
            }
            else if (name == "scripts")
            {
                if (const toml::array *scriptsArray = node.as_array())
                {
                    size_t scriptIndex = 0;
                    for (const toml::node &scriptNode : *scriptsArray)
                    {
                        const toml::table *scriptTable = scriptNode.as_table();
                        if (scriptTable == nullptr)
                        {
                            throw std::runtime_error(objectContext + ".scripts entries must be tables.");
                        }

                        config.scripts.push_back(parseBehaviorSpec(*scriptTable, sceneFilePath, scriptIndex));
                        scriptIndex++;
                    }
                    continue;
                }
            }

            throw std::runtime_error(objectContext + " contains unsupported key '" + name + "'.");
        }

        return config;
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
    AnimationClipFileWriter(filePath).write(animationClip);
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

    for (const auto &[key, node] : document)
    {
        const std::string rootKey = std::string(key.str());
        if (rootKey != "format" && rootKey != "version" && rootKey != "objects")
        {
            throw std::runtime_error("Unsupported top-level scene key '" + rootKey + "'.");
        }
        (void)node;
    }

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

    std::vector<GameObject *> objects;
    objects.reserve(objectsArray->size());

    try
    {
        size_t objectIndex = 0;
        for (const toml::node &objectNode : *objectsArray)
        {
            const toml::table *objectTable = objectNode.as_table();
            if (objectTable == nullptr)
            {
                throw std::runtime_error("Scene objects must be tables.");
            }

            ObjectConfig config = parseObject(*objectTable, filePath, objectIndex);
            objectIndex++;

            if (config.boxCollider.enabled && config.circleCollider.enabled)
            {
                throw std::runtime_error("A scene object cannot define both boxCollider and circleCollider.");
            }

            GameObject *gameObject = new GameObject(config.rotation, config.active, config.position, config.scale, config.name, config.tag);

            try
            {
                if (config.camera.enabled)
                {
                    gameObject->addComponentInternal(new Camera(gameObject, config.camera.x, config.camera.y, config.camera.width, config.camera.height));
                }

                if (config.rigidbody.enabled)
                {
                    Rigidbody *rigidbody = new Rigidbody(gameObject, config.rigidbody.bodyType, config.rigidbody.gravity);
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
                        sprite->setFlip(config.sprite.flip);
                        sprite->setWidth(config.sprite.width);
                        sprite->setHeight(config.sprite.height);
                    }
                    else
                    {
                        const std::string resolvedPath = resolveSceneRelativePath(filePath, config.sprite.path);
                        sprite = new Sprite(gameObject, resolvedPath.c_str(), config.sprite.flip, config.sprite.width, config.sprite.height);
                    }

                    gameObject->addComponentInternal(sprite);
                }

                if (config.animator.enabled)
                {
                    Animator *animator = new Animator(gameObject);
                    animator->setPlaybackSpeed(config.animator.playbackSpeed);
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
                        const std::string resolvedPath = resolveSceneRelativePath(filePath, config.audio.path);
                        audio = new Audio(gameObject, resolvedPath.c_str(), config.audio.gain, config.audio.autoplay, static_cast<float>(config.audio.loops));
                    }

                    gameObject->addComponentInternal(audio);
                }

                if (!config.scripts.empty())
                {
                    ScriptComponent *scriptComponent = new ScriptComponent(gameObject);
                    for (const BehaviorSpec &behaviorSpec : config.scripts)
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
            }
            catch (const std::exception &)
            {
                delete gameObject;
                throw;
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
    SceneFileWriter(filePath).write(gameObjects);
}
