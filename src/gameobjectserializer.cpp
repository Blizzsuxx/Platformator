#include "gameobject.h"

#include "jsonhelpers.h"
#include "collider.h"
#include "boxcollider.h"
#include "circlecollider.h"
#include "rigidbody.h"
#include "sprite.h"
#include "animator.h"
#include "scriptcomponent.h"
#include "camera.h"
#include "audio.h"

void to_json(nlohmann::json &j, const GameObject &gameObject)
{
    j = nlohmann::json{{"id", gameObject.getId()},
                       {"rotation", gameObject.getRotation()},
                       {"active", gameObject.getActive()},
                       {"scale", gameObject.getScale()},
                       {"name", gameObject.getName()},
                       {"tag", gameObject.getTag()}};

    j["children"] = nlohmann::json::array();
    for (const GameObject *child : gameObject.getChildren())
    {
        if (child != nullptr)
        {
            j["children"].push_back(*child);
        }
    }

    j["components"] = nlohmann::json::array();

    Collider *colliderComponent = gameObject.getComponent<Collider>();
    if (colliderComponent != nullptr)
    {
        if (colliderComponent->getColliderType() == ColliderType::BoxCollider)
        {
            BoxCollider *boxCollider = static_cast<BoxCollider *>(colliderComponent);
            j["components"].push_back(*boxCollider);
        }
        else if (colliderComponent->getColliderType() == ColliderType::CircleCollider)
        {
            CircleCollider *circleCollider = static_cast<CircleCollider *>(colliderComponent);
            j["components"].push_back(*circleCollider);
        }
    }

    Rigidbody *rigidbodyComponent = gameObject.getComponent<Rigidbody>();
    if (rigidbodyComponent != nullptr)
    {
        j["components"].push_back(*rigidbodyComponent);
    }

    Sprite *spriteComponent = gameObject.getComponent<Sprite>();
    if (spriteComponent != nullptr)
    {
        j["components"].push_back(*spriteComponent);
    }

    Animator *animatorComponent = gameObject.getComponent<Animator>();
    if (animatorComponent != nullptr)
    {
        j["components"].push_back(*animatorComponent);
    }

    ScriptComponent *scriptComponent = gameObject.getComponent<ScriptComponent>();
    if (scriptComponent != nullptr)
    {
        j["components"].push_back(*scriptComponent);
    }

    Camera *cameraComponent = gameObject.getComponent<Camera>();
    if (cameraComponent != nullptr)
    {
        j["components"].push_back(*cameraComponent);
    }

    Audio *audioComponent = gameObject.getComponent<Audio>();
    if (audioComponent != nullptr)
    {
        j["components"].push_back(*audioComponent);
    }
}

void from_json(const nlohmann::json &j, GameObject &gameObject)
{
    gameObject.setId(j.at("id").get<int>());

    gameObject.setRotation(j.at("rotation").get<float>());
    gameObject.setActive(j.at("active").get<bool>());
    gameObject.setScale(j.at("scale").get<Eigen::Vector2f>());
    gameObject.setName(j.at("name").get<std::string>());
    gameObject.setTag(j.at("tag").get<std::string>());

    for (const nlohmann::json &childJson : j.at("children"))
    {
        GameObject *child = new GameObject();
        gameObject.addChild(child);
        from_json(childJson, *child);
    }

    for (const nlohmann::json &componentJson : j.at("components"))
    {
        ComponentType componentType = componentJson.at("type").get<ComponentType>();

        switch (componentType)
        {

        case ComponentType::COLLIDER:
        {
            ColliderType colliderType = static_cast<ColliderType>(componentJson.at("colliderType").get<int>());
            if (colliderType == ColliderType::BoxCollider)
            {
                BoxCollider *boxCollider = new BoxCollider();
                from_json(componentJson, *boxCollider);
                gameObject.addComponent(boxCollider);
            }
            else if (colliderType == ColliderType::CircleCollider)
            {
                CircleCollider *circleCollider = new CircleCollider();
                from_json(componentJson, *circleCollider);
                gameObject.addComponent(circleCollider);
            }
            break;
        }
        case ComponentType::RIGID_BODY:
        {
            Rigidbody *rigidbody = new Rigidbody();
            from_json(componentJson, *rigidbody);
            gameObject.addComponent(rigidbody);
            break;
        }
        case ComponentType::SPRITE:
        {
            Sprite *sprite = new Sprite();
            from_json(componentJson, *sprite);
            gameObject.addComponent(sprite);
            break;
        }
        case ComponentType::ANIMATOR:
        {
            Animator *animator = new Animator();
            from_json(componentJson, *animator);
            gameObject.addComponent(animator);
            break;
        }
        case ComponentType::SCRIPT:
        {
            ScriptComponent *scriptComponent = new ScriptComponent();
            from_json(componentJson, *scriptComponent);
            gameObject.addComponent(scriptComponent);
            break;
        }
        case ComponentType::CAMERA:
        {
            Camera *camera = new Camera();
            from_json(componentJson, *camera);
            gameObject.addComponent(camera);
            break;
        }
        case ComponentType::AUDIO:
        {
            Audio *audio = new Audio();
            from_json(componentJson, *audio);
            gameObject.addComponent(audio);
            break;
        }
        default:
            throw std::runtime_error("Unknown component type: " + std::to_string(static_cast<int>(componentType)));
        }
    }
}