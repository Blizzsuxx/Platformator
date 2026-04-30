#include "camera.h"

void to_json(nlohmann::json &j, const Camera &camera)
{
    j = nlohmann::json{{"camera", camera.getCamera()}};
}

void from_json(const nlohmann::json &j, Camera &camera)
{
    camera.setCamera(j.at("camera").get<SDL_FRect>());
}