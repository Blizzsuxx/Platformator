#pragma once

#include <string>
#include <string_view>
#include <stdexcept>

#include <Eigen/Dense>
#include <SDL3/SDL.h>
#include <json.hpp>
#include "platformator/referenceresolver.h"
#include "platformator/scriptmetadata.h"

class Behavior;

namespace platformator
{
void registerScriptFactory(std::string_view typeName, Behavior *(*factory)());
void registerScriptDescriptorProvider(std::string_view typeName, ScriptDescriptorProvider provider);
Behavior *createRegisteredBehavior(std::string_view typeName);
nlohmann::json exportRegisteredBehaviorCatalog();
} // namespace platformator

namespace platformator_json_detail
{
template <typename T>
void resolveSerializedField(T &value, platformator::ReferenceResolver &resolver, const char *field)
{
    if constexpr (requires { value.resolve(resolver); })
    {
        try
        {
            value.resolve(resolver);
        }
        catch (const std::exception &error)
        {
            throw std::runtime_error(std::string("Field '") + field + "': " + error.what());
        }
    }
}
} // namespace platformator_json_detail

#define PLATFORMATOR_CONCAT_INNER(left, right) left##right
#define PLATFORMATOR_CONCAT(left, right) PLATFORMATOR_CONCAT_INNER(left, right)
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
#define PLATFORMATOR_FOR_EACH_COUNT(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, count, ...) \
    count
#define PLATFORMATOR_FOR_EACH_SELECT(...)                                                                              \
    PLATFORMATOR_FOR_EACH_COUNT(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define PLATFORMATOR_FOR_EACH(macro, ...)                                                                              \
    PLATFORMATOR_CONCAT(PLATFORMATOR_FOR_EACH_, PLATFORMATOR_FOR_EACH_SELECT(__VA_ARGS__))(macro, __VA_ARGS__)

#define PLATFORMATOR_METADATA_FIELDS_1(macro, Type, field1) macro(Type, field1)
#define PLATFORMATOR_METADATA_FIELDS_2(macro, Type, field1, ...)                                                        \
    macro(Type, field1), PLATFORMATOR_METADATA_FIELDS_1(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELDS_3(macro, Type, field1, ...)                                                        \
    macro(Type, field1), PLATFORMATOR_METADATA_FIELDS_2(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELDS_4(macro, Type, field1, ...)                                                        \
    macro(Type, field1), PLATFORMATOR_METADATA_FIELDS_3(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELDS_5(macro, Type, field1, ...)                                                        \
    macro(Type, field1), PLATFORMATOR_METADATA_FIELDS_4(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELDS_6(macro, Type, field1, ...)                                                        \
    macro(Type, field1), PLATFORMATOR_METADATA_FIELDS_5(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELDS_7(macro, Type, field1, ...)                                                        \
    macro(Type, field1), PLATFORMATOR_METADATA_FIELDS_6(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELDS_8(macro, Type, field1, ...)                                                        \
    macro(Type, field1), PLATFORMATOR_METADATA_FIELDS_7(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELDS_9(macro, Type, field1, ...)                                                        \
    macro(Type, field1), PLATFORMATOR_METADATA_FIELDS_8(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELDS_10(macro, Type, field1, ...)                                                       \
    macro(Type, field1), PLATFORMATOR_METADATA_FIELDS_9(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELDS_11(macro, Type, field1, ...)                                                       \
    macro(Type, field1), PLATFORMATOR_METADATA_FIELDS_10(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELDS_12(macro, Type, field1, ...)                                                       \
    macro(Type, field1), PLATFORMATOR_METADATA_FIELDS_11(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELDS_13(macro, Type, field1, ...)                                                       \
    macro(Type, field1), PLATFORMATOR_METADATA_FIELDS_12(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELDS_14(macro, Type, field1, ...)                                                       \
    macro(Type, field1), PLATFORMATOR_METADATA_FIELDS_13(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELDS_15(macro, Type, field1, ...)                                                       \
    macro(Type, field1), PLATFORMATOR_METADATA_FIELDS_14(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELDS_16(macro, Type, field1, ...)                                                       \
    macro(Type, field1), PLATFORMATOR_METADATA_FIELDS_15(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELDS_COUNT(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, count, ...) \
    count
#define PLATFORMATOR_METADATA_FIELDS_SELECT(...)                                                                        \
    PLATFORMATOR_METADATA_FIELDS_COUNT(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define PLATFORMATOR_METADATA_FIELDS(macro, Type, ...)                                                                  \
    PLATFORMATOR_CONCAT(PLATFORMATOR_METADATA_FIELDS_, PLATFORMATOR_METADATA_FIELDS_SELECT(__VA_ARGS__))(macro, Type, __VA_ARGS__)
#define PLATFORMATOR_METADATA_FIELD(Type, field) platformator::describe_script_field(#field, &Type::field)
#define PLATFORMATOR_ARGUMENT_COUNT_IMPL(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16,    \
                                         count, ...)                                                                   \
    count
#define PLATFORMATOR_ARGUMENT_COUNT(...)                                                                               \
    PLATFORMATOR_ARGUMENT_COUNT_IMPL(0 __VA_OPT__(, __VA_ARGS__), 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2,  \
                                     1, 0)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_SELECT(count) PLATFORMATOR_CONCAT(PLATFORMATOR_SERIALIZABLE_SCRIPT_, count)
#define PLATFORMATOR_RESOLVE_SERIALIZED_FIELD(field)                                                                   \
    platformator_json_detail::resolveSerializedField(field, resolver, #field);

#define PLATFORMATOR_SERIALIZABLE_SCRIPT_COMMON(Type)                                                                  \
  public:                                                                                                              \
    static bool registerScriptType()                                                                                   \
    {                                                                                                                  \
        platformator::registerScriptFactory(#Type, []() -> Behavior * { return new Type(); });                         \
        platformator::registerScriptDescriptorProvider(#Type, &Type::platformatorDescribeScriptMetadata);              \
        return true;                                                                                                   \
    }                                                                                                                  \
    inline static const bool platformatorRegisteredBehavior = registerScriptType();                                    \
    std::string getTypeName() const override                                                                           \
    {                                                                                                                  \
        return #Type;                                                                                                  \
    }                                                                                                                  \
    void serialize(nlohmann::json &j) const override                                                                   \
    {                                                                                                                  \
        to_json(j, *this);                                                                                             \
        j["type"] = getTypeName();                                                                                     \
    }                                                                                                                  \
    void deserialize(const nlohmann::json &j) override                                                                 \
    {                                                                                                                  \
        from_json(j, *this);                                                                                           \
    }

#define PLATFORMATOR_SERIALIZABLE_SCRIPT_0(Type)                                                                       \
    PLATFORMATOR_SERIALIZABLE_SCRIPT_COMMON(Type)                                                                      \
    static platformator::ScriptDescriptor platformatorDescribeScriptMetadata()                                         \
    {                                                                                                                  \
        return platformator::ScriptDescriptor{#Type, true, {}};                                                        \
    }                                                                                                                  \
    friend void to_json(nlohmann::json &j, const Type &)                                                               \
    {                                                                                                                  \
        j = nlohmann::json::object();                                                                                  \
    }                                                                                                                  \
    friend void from_json(const nlohmann::json &, Type &)                                                              \
    {                                                                                                                  \
    }                                                                                                                  \
    void resolveReferences(platformator::ReferenceResolver &resolver) override                                         \
    {                                                                                                                  \
    }

#define PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, ...)                                                        \
    PLATFORMATOR_SERIALIZABLE_SCRIPT_COMMON(Type)                                                                      \
    static platformator::ScriptDescriptor platformatorDescribeScriptMetadata()                                         \
    {                                                                                                                  \
        return platformator::ScriptDescriptor{                                                                        \
            #Type, true, {PLATFORMATOR_METADATA_FIELDS(PLATFORMATOR_METADATA_FIELD, Type, __VA_ARGS__)}};             \
    }                                                                                                                  \
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Type, __VA_ARGS__)                                                     \
    void resolveReferences(platformator::ReferenceResolver &resolver) override                                         \
    {                                                                                                                  \
        PLATFORMATOR_FOR_EACH(PLATFORMATOR_RESOLVE_SERIALIZED_FIELD, __VA_ARGS__)                                      \
    }

#define PLATFORMATOR_SERIALIZABLE_SCRIPT_1(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_2(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_3(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_4(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_5(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_6(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_7(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_8(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_9(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_10(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_11(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_12(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_13(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_14(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_15(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_16(Type, ...) PLATFORMATOR_SERIALIZABLE_SCRIPT_WITH_FIELDS(Type, __VA_ARGS__)

#define SERIALIZABLE_SCRIPT(Type, ...)                                                                                 \
    PLATFORMATOR_SERIALIZABLE_SCRIPT_SELECT(PLATFORMATOR_ARGUMENT_COUNT(__VA_ARGS__))(Type __VA_OPT__(, __VA_ARGS__))

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

template <> struct adl_serializer<Eigen::Vector2f>
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
