#pragma once

#include <string>

#include <json.hpp>
#include <SDL3/SDL.h>
#include <Eigen/Dense>

#include "behaviorfactoryregistry.h"

namespace platformator_json_detail
{
    template <typename T>
    void resolveSerializedField(T &value)
    {
        if constexpr (requires { value.resolve(); })
        {
            value.resolve();
        }
    }
} // namespace platformator_json_detail

#define PLATFORMATOR_CONCAT_INNER(left, right) left##right
#define PLATFORMATOR_CONCAT(left, right) PLATFORMATOR_CONCAT_INNER(left, right)
#define PLATFORMATOR_UNIQUE_NAME(prefix) PLATFORMATOR_CONCAT(prefix, __COUNTER__)
#define PLATFORMATOR_FOR_EACH_1(macro, value1) macro(value1)
#define PLATFORMATOR_FOR_EACH_2(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_1(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_3(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_2(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_4(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_3(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_5(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_4(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_6(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_5(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_7(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_6(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_8(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_7(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_9(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_8(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_10(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_9(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_11(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_10(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_12(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_11(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_13(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_12(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_14(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_13(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_15(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_14(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_16(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_15(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_COUNT(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, count, ...) count
#define PLATFORMATOR_FOR_EACH_SELECT(...) PLATFORMATOR_FOR_EACH_COUNT(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define PLATFORMATOR_FOR_EACH(macro, ...) PLATFORMATOR_CONCAT(PLATFORMATOR_FOR_EACH_, PLATFORMATOR_FOR_EACH_SELECT(__VA_ARGS__))(macro, __VA_ARGS__)
#define PLATFORMATOR_RESOLVE_SERIALIZED_FIELD(field) platformator_json_detail::resolveSerializedField(field);

#define SERIALIZABLE_SCRIPT(Type, ...)                                            \
public:                                                                           \
    std::string getTypeName() const override { return #Type; }                    \
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Type, __VA_ARGS__)                \
    void serialize(nlohmann::json &j) const override                              \
    {                                                                             \
        to_json(j, *this);                                                        \
    }                                                                             \
    void deserialize(const nlohmann::json &j) override                            \
    {                                                                             \
        from_json(j, *this);                                                      \
    }                                                                             \
    void resolveReferences() override                                             \
    {                                                                             \
        PLATFORMATOR_FOR_EACH(PLATFORMATOR_RESOLVE_SERIALIZED_FIELD, __VA_ARGS__) \
    }

#define REGISTER_SCRIPT(Type) \
    inline const bool PLATFORMATOR_UNIQUE_NAME(platformatorRegisteredBehavior_) = []() { \
        BehaviorFactoryRegistry::getInstance().registerFactory(#Type, []() -> Behavior * { return new Type(); }); \
        return true; }();

inline void to_json(nlohmann::json &j, const SDL_FRect &rect)
{
    j = nlohmann::json{{"x", rect.x}, {"y", rect.y}, {"w", rect.w}, {"h", rect.h}};
}

inline void from_json(const nlohmann::json &j, SDL_FRect &rect)
{
    rect.x = j.at("x").get<float>();
    rect.y = j.at("y").get<float>();
    rect.w = j.at("w").get<float>();
    rect.h = j.at("h").get<float>();
}

NLOHMANN_JSON_NAMESPACE_BEGIN

template <>
struct adl_serializer<Eigen::Vector2f>
{
    static void to_json(json &j, const Eigen::Vector2f &vec)
    {
        j = json{{"x", vec.x()}, {"y", vec.y()}};
    }

    static void from_json(const json &j, Eigen::Vector2f &vec)
    {
        vec.x() = j.at("x").get<float>();
        vec.y() = j.at("y").get<float>();
    }
};

NLOHMANN_JSON_NAMESPACE_END

inline void to_json(nlohmann::json &j, const SDL_FlipMode &flipMode)
{
    j = static_cast<int>(flipMode);
}

inline void from_json(const nlohmann::json &j, SDL_FlipMode &flipMode)
{
    flipMode = static_cast<SDL_FlipMode>(j.get<int>());
}