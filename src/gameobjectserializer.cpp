#include "platformator/gameobject.h"

#include "platformator/scriptregistration.h"
#include "platformator/collider.h"
#include "platformator/boxcollider.h"
#include "platformator/circlecollider.h"
#include "platformator/rigidbody.h"
#include "platformator/sprite.h"
#include "platformator/animator.h"
#include "platformator/scriptcomponent.h"
#include "platformator/camera.h"
#include "platformator/audio.h"

void to_json(nlohmann::json &j, const GameObject &gameObject)
{
    j = nlohmann::json{{"id", gameObject.getId()},         {"rotation", gameObject.getRotation()},
                       {"active", gameObject.getActive()}, {"position", gameObject.getPosition()},
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
    throw std::logic_error("Deserialize GameObjects through Runtime::loadScene so their graph has an owner");
}