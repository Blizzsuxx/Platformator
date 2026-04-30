#include "circlecollider.h"

void to_json(nlohmann::json &j, const CircleCollider &circleCollider)
{
    j = nlohmann::json{{"radius", circleCollider.getRadius()},
                       {"collisionGroup", circleCollider.getCollisionGroup()},
                       {"collisionMask", circleCollider.getCollisionMask()}};
}

void from_json(const nlohmann::json &j, CircleCollider &circleCollider)
{
    circleCollider.setRadius(j.at("radius").get<float>());
    circleCollider.setCollisionGroup(j.at("collisionGroup").get<uint64_t>());
    circleCollider.setCollisionMask(j.at("collisionMask").get<uint64_t>());
}