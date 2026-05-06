#include "sprite.h"

#include "runtimeaccess.h"
#include "texturewrapper.h"

void to_json(nlohmann::json &j, const Sprite &sprite)
{
    j["id"] = sprite.getId();
    j["textureFilePath"] = sprite.getTextureWrapper() ? sprite.getTextureWrapper()->getFilePath() : "";
    j["flip"] = sprite.getFlip();
    j["width"] = sprite.getWidthWithoutScale();
    j["height"] = sprite.getHeightWithoutScale();
    j["sourceRectEnabled"] = sprite.hasSourceRect();
    if (sprite.hasSourceRect())
    {
        j["sourceRect"] = *sprite.getSourceRect();
    }
    j["type"] = ComponentType::SPRITE;
}

void from_json(const nlohmann::json &j, Sprite &sprite)
{
    sprite.setId(j.at("id").get<int>());

    std::string filePath = j.at("textureFilePath").get<std::string>();
    if (!filePath.empty())
    {
        sprite.setTextureWrapper(platformator_detail::RuntimeAccess::loadTexture(filePath));
    }

    sprite.setFlip(j.at("flip").get<SDL_FlipMode>());
    sprite.setWidth(j.at("width").get<float>());
    sprite.setHeight(j.at("height").get<float>());
    if (j.at("sourceRectEnabled").get<bool>())
    {
        sprite.setSourceRect(j.at("sourceRect").get<SDL_FRect>());
    }
}