#include "boxcollider.h"

void to_json(nlohmann::json &j, const BoxCollider &boxCollider)
{
    j = nlohmann::json{{"id", boxCollider.getId()},
                       {"width", boxCollider.getWidth()},
                       {"height", boxCollider.getHeight()},
                       {"trigger", boxCollider.getIsTrigger()},
                       {"collisionGroup", boxCollider.getCollisionGroup()},
                       {"type", ComponentType::COLLIDER},
                       {"colliderType", boxCollider.getColliderType()},
                       {"collisionMask", boxCollider.getCollisionMask()}};
}

void from_json(const nlohmann::json &j, BoxCollider &boxCollider)
{
    boxCollider.setId(j.at("id").get<int>());

    boxCollider.setWidth(j.at("width").get<float>());
    boxCollider.setHeight(j.at("height").get<float>());
    boxCollider.setIsTrigger(j.at("trigger").get<bool>());
    boxCollider.setCollisionGroup(j.at("collisionGroup").get<uint64_t>());
    boxCollider.setCollisionMask(j.at("collisionMask").get<uint64_t>());
}