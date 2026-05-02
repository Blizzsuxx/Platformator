#include "camera.h"

void to_json(nlohmann::json &j, const Camera &camera)
{
    j = nlohmann::json{{"id", camera.getId()}, {"width", camera.getWidth()}, {"height", camera.getHeight()}, {"type", ComponentType::CAMERA}};
}

void from_json(const nlohmann::json &j, Camera &camera)
{
    camera.setId(j.at("id").get<int>());

    camera.setWidth(j.at("width").get<float>());
    camera.setHeight(j.at("height").get<float>());
}