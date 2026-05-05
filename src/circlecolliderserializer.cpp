#include "circlecollider.h"

void to_json(nlohmann::json &j, const CircleCollider &circleCollider)
{
    float radius = circleCollider.getRadiusWithoutScale();

    j = nlohmann::json{{"id", circleCollider.getId()},
                       {"radius", radius},
                       {"offset", circleCollider.getOffset()},
                       {"trigger", circleCollider.getIsTrigger()},
                       {"collisionGroup", circleCollider.getCollisionGroup()},
                       {"type", ComponentType::COLLIDER},
                       {"colliderType", circleCollider.getColliderType()},
                       {"collisionMask", circleCollider.getCollisionMask()}};
}

void from_json(const nlohmann::json &j, CircleCollider &circleCollider)
{
    circleCollider.setId(j.at("id").get<int>());

    circleCollider.setRadius(j.at("radius").get<float>());
    circleCollider.setOffset(j.at("offset").get<Eigen::Vector2f>());
    circleCollider.setIsTrigger(j.at("trigger").get<bool>());
    circleCollider.setCollisionGroup(j.at("collisionGroup").get<uint64_t>());
    circleCollider.setCollisionMask(j.at("collisionMask").get<uint64_t>());
}