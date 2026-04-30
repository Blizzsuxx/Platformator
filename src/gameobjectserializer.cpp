#include "gameobject.h"

void to_json(nlohmann::json &j, const GameObject &gameObject)
{
    j = nlohmann::json{{"rotation", gameObject.getRotation()},
                       {"active", gameObject.getActive()},
                       {"position", gameObject.getPosition()},
                       {"scale", gameObject.getScale()},
                       {"name", gameObject.getName()},
                       {"tag", gameObject.getTag()}};
}

void from_json(const nlohmann::json &j, GameObject &gameObject)
{
    gameObject.setRotation(j.at("rotation").get<float>());
    gameObject.setActive(j.at("active").get<bool>());
    gameObject.setPosition(j.at("position").get<Eigen::Vector2f>());
    gameObject.setScale(j.at("scale").get<Eigen::Vector2f>());
    gameObject.setName(j.at("name").get<std::string>());
    gameObject.setTag(j.at("tag").get<std::string>());
}