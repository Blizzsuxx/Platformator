#include "scenecomponentio.h"
#include "referenceresolver.h"
#include "platformator/sprite.h"

#include "platformator/scriptregistration.h"

#include "runtimeaccess.h"
#include "platformator/texturewrapper.h"

#include <cstdint>
#include <limits>
#include <stdexcept>

void to_json(nlohmann::json &j, const Sprite &sprite)
{
    const SDL_Color color = sprite.getColor();
    j["id"] = sprite.getId();
    j["textureFilePath"] = sprite.getTextureWrapper() ? sprite.getTextureWrapper()->getFilePath() : "";
    j["flip"] = sprite.getFlip();
    j["width"] = sprite.getWidthWithoutScale();
    j["height"] = sprite.getHeightWithoutScale();
    j["zOrder"] = sprite.getZOrder();
    j["color"] = {{"r", color.r}, {"g", color.g}, {"b", color.b}, {"a", color.a}};
    j["sourceRectEnabled"] = sprite.hasSourceRect();
    if (sprite.hasSourceRect())
    {
        j["sourceRect"] = *sprite.getSourceRect();
    }
    j["type"] = ComponentType::SPRITE;
}

void deserialize(const nlohmann::json &j, Sprite &sprite, platformator::ReferenceResolver &resolver)
{
    std::int32_t zOrder = 0;
    if (const auto found = j.find("zOrder"); found != j.end())
    {
        if (found->is_boolean() || !found->is_number_integer())
        {
            throw std::invalid_argument("Sprite zOrder must be an integer in [-2147483648, 2147483647]");
        }
        if (found->is_number_unsigned())
        {
            const auto unsignedValue = found->get<std::uint64_t>();
            if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
            {
                throw std::out_of_range("Sprite zOrder is outside [-2147483648, 2147483647]");
            }
            zOrder = static_cast<std::int32_t>(unsignedValue);
        }
        else
        {
            const auto signedValue = found->get<std::int64_t>();
            if (signedValue < std::numeric_limits<std::int32_t>::min() ||
                signedValue > std::numeric_limits<std::int32_t>::max())
            {
                throw std::out_of_range("Sprite zOrder is outside [-2147483648, 2147483647]");
            }
            zOrder = static_cast<std::int32_t>(signedValue);
        }
    }

    SDL_Color color{255, 255, 255, 255};
    if (const auto foundColor = j.find("color"); foundColor != j.end())
    {
        if (!foundColor->is_object())
        {
            throw std::invalid_argument("Sprite color must be a JSON object with r, g, b, and a in [0, 255]");
        }
        const auto parseChannel = [&](const char *channelName) -> std::uint8_t
        {
            const auto ch = foundColor->find(channelName);
            if (ch == foundColor->end() || ch->is_boolean() || !ch->is_number_integer())
            {
                throw std::invalid_argument(std::string("Sprite color.") + channelName +
                                            " must be an integer in [0, 255]");
            }
            if (ch->is_number_unsigned())
            {
                const auto u = ch->get<std::uint64_t>();
                if (u > 255ULL)
                {
                    throw std::out_of_range(std::string("Sprite color.") + channelName + " is outside [0, 255]");
                }
                return static_cast<std::uint8_t>(u);
            }
            const auto s = ch->get<std::int64_t>();
            if (s < 0 || s > 255)
            {
                throw std::out_of_range(std::string("Sprite color.") + channelName + " is outside [0, 255]");
            }
            return static_cast<std::uint8_t>(s);
        };
        color.r = parseChannel("r");
        color.g = parseChannel("g");
        color.b = parseChannel("b");
        color.a = parseChannel("a");
    }

    sprite.setId(j.at("id").get<int>());

    std::string filePath = j.at("textureFilePath").get<std::string>();
    if (!filePath.empty())
    {
        sprite.setTextureWrapper(resolver.loadTexture(filePath));
    }

    sprite.setFlip(j.at("flip").get<SDL_FlipMode>());
    sprite.setWidth(j.at("width").get<float>());
    sprite.setHeight(j.at("height").get<float>());
    if (j.at("sourceRectEnabled").get<bool>())
    {
        sprite.setSourceRect(j.at("sourceRect").get<SDL_FRect>());
    }
    sprite.setZOrder(zOrder);
    sprite.setColor(color);
}
void from_json(const nlohmann::json &j, Sprite &sprite)
{
    platformator_detail::ActiveReferenceResolver resolver(platformator::Runtime::current());
    deserialize(j, sprite, resolver);
}
