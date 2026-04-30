#include "sprite.h"

#include "gamemanager.h"
#include "texturewrapper.h"

void to_json(nlohmann::json &j, const Sprite &sprite)
{
    j["textureFilePath"] = sprite.getTextureWrapper() ? sprite.getTextureWrapper()->getFilePath() : "";
    j["flip"] = sprite.getFlip();
    j["width"] = sprite.getWidth();
    j["height"] = sprite.getHeight();
    j["sourceRectEnabled"] = sprite.hasSourceRect();
    if (sprite.hasSourceRect())
    {
        j["sourceRect"] = *sprite.getSourceRect();
    }
}

void from_json(const nlohmann::json &j, Sprite &sprite)
{
    std::string filePath = j.at("textureFilePath").get<std::string>();
    if (!filePath.empty())
    {
        sprite.setTextureWrapper(GameManager::getInstance().loadTexture(filePath));
    }

    sprite.setFlip(j.at("flip").get<SDL_FlipMode>());
    sprite.setWidth(j.at("width").get<float>());
    sprite.setHeight(j.at("height").get<float>());
    if (j.at("sourceRectEnabled").get<bool>())
    {
        sprite.setSourceRect(j.at("sourceRect").get<SDL_FRect>());
    }
}