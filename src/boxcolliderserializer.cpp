#include "boxcollider.h"

void to_json(nlohmann::json &j, const BoxCollider &boxCollider)
{
    j = nlohmann::json{{"width", boxCollider.getWidth()},
                       {"height", boxCollider.getHeight()},
                       {"collisionGroup", boxCollider.getCollisionGroup()},
                       {"collisionMask", boxCollider.getCollisionMask()}};
}

void from_json(const nlohmann::json &j, BoxCollider &boxCollider)
{
    boxCollider.setWidth(j.at("width").get<float>());
    boxCollider.setHeight(j.at("height").get<float>());
    boxCollider.setCollisionGroup(j.at("collisionGroup").get<uint64_t>());
    boxCollider.setCollisionMask(j.at("collisionMask").get<uint64_t>());
}