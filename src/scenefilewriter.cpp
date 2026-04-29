#include "scenefilewriter.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <toml++/toml.hpp>

#include "animator.h"
#include "audio.h"
#include "boxcollider.h"
#include "camera.h"
#include "circlecollider.h"
#include "rigidbody.h"
#include "scriptcomponent.h"
#include "sprite.h"
#include "tomlwriter.h"

namespace
{
    std::string makeSceneResourcePath(const std::string &scenePath, const std::string &resourcePath)
    {
        if (resourcePath.empty())
        {
            return "";
        }

        std::error_code errorCode;
        const std::filesystem::path sceneDirectory = std::filesystem::absolute(std::filesystem::path(scenePath), errorCode).parent_path();
        const std::filesystem::path assetPath = std::filesystem::absolute(std::filesystem::path(resourcePath), errorCode);
        if (errorCode)
        {
            return std::filesystem::path(resourcePath).lexically_normal().string();
        }

        const std::filesystem::path relativePath = std::filesystem::relative(assetPath, sceneDirectory, errorCode);
        if (errorCode || relativePath.empty())
        {
            return assetPath.lexically_normal().string();
        }

        return relativePath.lexically_normal().string();
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
}

SceneFileWriter::SceneFileWriter(std::string scenePath) : scenePath(std::move(scenePath))
{
}

void SceneFileWriter::write(const std::vector<GameObject *> &gameObjects) const
{
    std::ofstream sceneFile(scenePath);
    if (!sceneFile.is_open())
    {
        throw std::runtime_error("Failed to open scene file '" + scenePath + "' for writing.");
    }

    toml::table root;
    root.insert_or_assign("format", "platformator_scene");
    root.insert_or_assign("version", 1);

    toml::array objectsArray;
    for (GameObject *gameObject : gameObjects)
    {
        if (gameObject == nullptr)
        {
            continue;
        }

        Rigidbody *rigidbody = gameObject->getComponent<Rigidbody>();
        Collider *collider = static_cast<Collider *>(gameObject->getComponent(ComponentType::COLLIDER));
        Sprite *sprite = gameObject->getComponent<Sprite>();
        Camera *camera = gameObject->getComponent<Camera>();
        Audio *audio = gameObject->getComponent<Audio>();
        Animator *animator = gameObject->getComponent<Animator>();
        ScriptComponent *scriptComponent = gameObject->getComponent<ScriptComponent>();

        toml::table objectTable;

        if (!gameObject->getName().empty())
        {
            objectTable.insert_or_assign("name", gameObject->getName());
        }
        if (!gameObject->getTag().empty())
        {
            objectTable.insert_or_assign("tag", gameObject->getTag());
        }
        objectTable.insert_or_assign("active", gameObject->getActive());
        objectTable.insert_or_assign("position", TomlWriter::makeVector2Array(gameObject->getPosition()));
        objectTable.insert_or_assign("rotation", static_cast<double>(gameObject->getRotation()));
        objectTable.insert_or_assign("scale", TomlWriter::makeVector2Array(gameObject->getScale()));

        if (camera != nullptr)
        {
            toml::table cameraTable;
            cameraTable.insert_or_assign("viewport", TomlWriter::makeRectArray(camera->getCamera()));
            objectTable.insert_or_assign("camera", std::move(cameraTable));
        }

        if (rigidbody != nullptr)
        {
            toml::table rigidbodyTable;
            rigidbodyTable.insert_or_assign("bodyType", bodyTypeToToken(rigidbody->getBodyType()));
            rigidbodyTable.insert_or_assign("gravity", rigidbody->getGravity());
            rigidbodyTable.insert_or_assign("mass", static_cast<double>(rigidbody->getMass()));
            rigidbodyTable.insert_or_assign("velocity", TomlWriter::makeVector2Array(rigidbody->getVelocity()));
            rigidbodyTable.insert_or_assign("force", TomlWriter::makeVector2Array(rigidbody->getForce()));
            rigidbodyTable.insert_or_assign("angularVelocity", static_cast<double>(rigidbody->getAngularVelocity()));
            rigidbodyTable.insert_or_assign("torque", static_cast<double>(rigidbody->getTorque()));
            rigidbodyTable.insert_or_assign("friction", static_cast<double>(rigidbody->getFriction()));
            rigidbodyTable.insert_or_assign("restitution", static_cast<double>(rigidbody->getRestitution()));
            objectTable.insert_or_assign("rigidbody", std::move(rigidbodyTable));
        }

        if (collider != nullptr)
        {
            if (collider->getColliderType() == ColliderType::BoxCollider)
            {
                BoxCollider *boxCollider = static_cast<BoxCollider *>(collider);
                const float scaleX = gameObject->getScale().x();
                const float scaleY = gameObject->getScale().y();
                const float unscaledWidth = std::abs(scaleX) > 1e-6f ? boxCollider->getWidth() / scaleX : boxCollider->getWidth();
                const float unscaledHeight = std::abs(scaleY) > 1e-6f ? boxCollider->getHeight() / scaleY : boxCollider->getHeight();

                toml::table boxColliderTable;
                boxColliderTable.insert_or_assign("size", TomlWriter::makeVector2Array(Eigen::Vector2f(unscaledWidth, unscaledHeight)));
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
                circleColliderTable.insert_or_assign("radius", static_cast<double>(unscaledRadius));
                circleColliderTable.insert_or_assign("trigger", circleCollider->getIsTrigger());
                circleColliderTable.insert_or_assign("collisionGroup", static_cast<int64_t>(circleCollider->getCollisionGroup()));
                circleColliderTable.insert_or_assign("collisionMask", static_cast<int64_t>(circleCollider->getCollisionMask()));
                objectTable.insert_or_assign("circleCollider", std::move(circleColliderTable));
            }
        }

        if (sprite != nullptr)
        {
            toml::table spriteTable;
            if (TextureWrapper *textureWrapper = sprite->getTextureWrapper())
            {
                spriteTable.insert_or_assign("path", makeSceneResourcePath(scenePath, textureWrapper->getFilePath()));
            }
            spriteTable.insert_or_assign("flip", flipModeToToken(sprite->getFlip()));
            spriteTable.insert_or_assign("size", TomlWriter::makeVector2Array(Eigen::Vector2f(sprite->getWidth(), sprite->getHeight())));
            objectTable.insert_or_assign("sprite", std::move(spriteTable));
        }

        if (animator != nullptr)
        {
            toml::table animatorTable;
            animatorTable.insert_or_assign("playbackSpeed", static_cast<double>(animator->getPlaybackSpeed()));
            objectTable.insert_or_assign("animator", std::move(animatorTable));
        }

        if (audio != nullptr)
        {
            toml::table audioTable;
            if (!audio->getFilePath().empty())
            {
                audioTable.insert_or_assign("path", makeSceneResourcePath(scenePath, audio->getFilePath()));
            }
            audioTable.insert_or_assign("autoplay", audio->getAutoPlay());
            audioTable.insert_or_assign("loops", static_cast<int64_t>(audio->getLoopCount()));
            audioTable.insert_or_assign("gain", static_cast<double>(audio->getGain()));
            objectTable.insert_or_assign("audio", std::move(audioTable));
        }

        if (scriptComponent != nullptr)
        {
            toml::array scriptArray;
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

                toml::table scriptTable;
                scriptTable.insert_or_assign("type", spec.type);
                scriptTable.insert_or_assign("enabled", spec.enabled);
                for (const BehaviorProperty &property : spec.properties)
                {
                    TomlWriter::insertScriptValue(scriptTable, property.name, property.value);
                }

                scriptArray.push_back(std::move(scriptTable));
            }

            if (!scriptArray.empty())
            {
                objectTable.insert_or_assign("scripts", std::move(scriptArray));
            }
        }

        objectsArray.push_back(std::move(objectTable));
    }

    if (!objectsArray.empty())
    {
        root.insert_or_assign("objects", std::move(objectsArray));
    }

    TomlWriter::writeDocument(sceneFile, root);
}