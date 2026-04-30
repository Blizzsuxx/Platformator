#include "rigidbody.h"

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

    rigidbody.setVelocity(j.at("velocity").get<Eigen::Vector2f>());
    rigidbody.setForce(j.at("force").get<Eigen::Vector2f>());
    rigidbody.setMass(j.at("mass").get<float>());
    rigidbody.setAngularVelocity(j.at("angularVelocity").get<float>());
    rigidbody.setTorque(j.at("torque").get<float>());
    rigidbody.setFriction(j.at("friction").get<float>());
    rigidbody.setRestitution(j.at("restitution").get<float>());
    rigidbody.setBodyType(j.at("bodyType").get<BodyType>());
    rigidbody.setGravity(j.at("gravity").get<bool>());
}