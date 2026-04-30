#pragma once

#include <json.hpp>
#include <SDL3/SDL.h>
#include <Eigen/Dense>

#define REGISTER_SCRIPT(Type, ...)                                 \
public:                                                            \
    std::string getTypeName() const override { return #Type; }     \
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Type, __VA_ARGS__) \
    void serialize(nlohmann::json &j) const override               \
    {                                                              \
        to_json(j, *this);                                         \
    }                                                              \
    void deserialize(const nlohmann::json &j) override             \
    {                                                              \
        from_json(j, *this);                                       \
    }                                                              \
                                                                   \
private:                                                           \
    /* This line auto-registers with your factory */               \
    inline static bool _registered = []() { \
            BehaviorFactoryRegistry::registerBehavior(#Type, []() { return new Type(); }); \
            return true; }();

void to_json(nlohmann::json &j, const SDL_FRect &rect)
{
    j = nlohmann::json{{"x", rect.x}, {"y", rect.y}, {"w", rect.w}, {"h", rect.h}};
}

void from_json(const nlohmann::json &j, SDL_FRect &rect)
{
    rect.x = j.at("x").get<float>();
    rect.y = j.at("y").get<float>();
    rect.w = j.at("w").get<float>();
    rect.h = j.at("h").get<float>();
}

void to_json(nlohmann::json &j, const Eigen::Vector2f &vec)
{
    j = nlohmann::json{{"x", vec.x()}, {"y", vec.y()}};
}

void from_json(const nlohmann::json &j, Eigen::Vector2f &vec)
{
    vec.x() = j.at("x").get<float>();
    vec.y() = j.at("y").get<float>();
}

void to_json(nlohmann::json &j, const SDL_FlipMode &flipMode)
{
    j = static_cast<int>(flipMode);
}

void from_json(const nlohmann::json &j, SDL_FlipMode &flipMode)
{
    flipMode = static_cast<SDL_FlipMode>(j.get<int>());
}