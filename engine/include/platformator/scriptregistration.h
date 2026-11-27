#pragma once

#include <string>
#include <string_view>
#include <stdexcept>
#include <type_traits>

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
void registerScriptInspectionProvider(std::string_view typeName, ScriptInspectionProvider provider);
ScriptInspectionResult inspectRegisteredScriptFields(std::string_view typeName, const Behavior &instance,
                                                     ScriptInspectionLimits limits = {});
Behavior *createRegisteredBehavior(std::string_view typeName);
nlohmann::json exportRegisteredBehaviorCatalog();
} // namespace platformator

namespace platformator_json_detail
{
template <typename T>
void resolveSerializedField(T &value, platformator::ReferenceResolver &resolver, const char *field,
                            bool templatesOnly = false)
{
    if constexpr (requires { value.resolve(resolver); })
    {
        if (templatesOnly)
        {
            if constexpr (!(requires { value.get(); } && std::is_same_v<decltype(value.get()), Template *>))
                return;
        }
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
#define PLATFORMATOR_FOR_EACH_17(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_16(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_18(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_17(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_19(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_18(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_20(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_19(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_21(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_20(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_22(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_21(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_23(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_22(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_24(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_23(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_25(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_24(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_26(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_25(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_27(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_26(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_28(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_27(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_29(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_28(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_30(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_29(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_31(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_30(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_32(macro, value1, ...) macro(value1) PLATFORMATOR_FOR_EACH_31(macro, __VA_ARGS__)
#define PLATFORMATOR_FOR_EACH_COUNT(                                                                                   \
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16,                                             \
    _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, count, ...)                        \
    count
#define PLATFORMATOR_FOR_EACH_SELECT(...)                                                                              \
    PLATFORMATOR_FOR_EACH_COUNT(                                                                                       \
        __VA_ARGS__, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17,                                   \
        16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define PLATFORMATOR_FOR_EACH(macro, ...)                                                                              \
    PLATFORMATOR_CONCAT(PLATFORMATOR_FOR_EACH_, PLATFORMATOR_FOR_EACH_SELECT(__VA_ARGS__))(macro, __VA_ARGS__)

#define PLATFORMATOR_METADATA_FIELD_ITEM(field) platformator::describe_script_field(#field, &SelfType::field),
#define PLATFORMATOR_INSPECT_SCRIPT_FIELD(field)                                                                       \
    platformator::metadata_detail::append_script_field(result, #field, instance.field, limits);
#define PLATFORMATOR_HAS_FIELDS_COUNT_IMPL(                                                                            \
    _0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16,                                        \
    _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, flag, ...)                         \
    flag
#define PLATFORMATOR_HAS_FIELDS_FLAG(...)                                                                              \
    PLATFORMATOR_HAS_FIELDS_COUNT_IMPL(                                                                                \
        0 __VA_OPT__(, __VA_ARGS__),                                                                                   \
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,                                                                \
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0)
#define PLATFORMATOR_SERIALIZABLE_SCRIPT_SELECT(flag) PLATFORMATOR_CONCAT(PLATFORMATOR_SERIALIZABLE_SCRIPT_, flag)
#define PLATFORMATOR_RESOLVE_SERIALIZED_FIELD(field)                                                                   \
    platformator_json_detail::resolveSerializedField(field, resolver, #field);
#define PLATFORMATOR_RESOLVE_TEMPLATE_FIELD(field)                                                                     \
    platformator_json_detail::resolveSerializedField(field, resolver, #field, true);

#define PLATFORMATOR_SERIALIZABLE_SCRIPT_REGISTRATION(Type)                                                            \
  public:                                                                                                              \
    static bool registerScriptType()                                                                                   \
    {                                                                                                                  \
        platformator::registerScriptFactory(#Type, []() -> Behavior * { return new Type(); });                         \
        platformator::registerScriptDescriptorProvider(#Type, &Type::platformatorDescribeScriptMetadata);              \
        platformator::registerScriptInspectionProvider(#Type, &Type::platformatorInspectScriptInstance);               \
        return true;                                                                                                   \
    }                                                                                                                  \
    inline static const bool platformatorRegisteredBehavior = registerScriptType();                                    \
    static platformator::ScriptInspectionResult platformatorInspectScriptInstance(                                      \
        const Behavior &instance, const platformator::ScriptInspectionLimits &limits)                                    \
    {                                                                                                                  \
        const auto *typed = dynamic_cast<const Type *>(&instance);                                                     \
        if (typed == nullptr)                                                                                          \
            return {};                                                                                                 \
        return platformatorInspectScriptFields(*typed, limits);                                                       \
    }                                                                                                                  \
    uint8_t getUpdateMask() const noexcept override                                                                    \
    {                                                                                                                  \
        return platformator_detail::detectBehaviorUpdateMask<Type>();                                                  \
    }

#define PLATFORMATOR_SERIALIZABLE_SCRIPT_OVERRIDES(Type)                                                               \
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
    }                                                                                                                  \
    Behavior *clone() const override                                                                                   \
    {                                                                                                                  \
        return new Type(*this);                                                                                        \
    }                                                                                                                  \
    void resolveReferences(platformator::ReferenceResolver &resolver) override                                         \
    {                                                                                                                  \
        platformatorResolveScriptReferences(resolver);                                                                 \
    }

#define PLATFORMATOR_SERIALIZABLE_SCRIPT_0(Type)                                                                       \
    PLATFORMATOR_SERIALIZABLE_SCRIPT_REGISTRATION(Type)                                                                \
    static platformator::ScriptDescriptor platformatorDescribeScriptMetadata()                                         \
    {                                                                                                                  \
        return platformator::ScriptDescriptor{#Type, true, {}};                                                        \
    }                                                                                                                  \
    static platformator::ScriptInspectionResult platformatorInspectScriptFields(                                       \
        const Type &, const platformator::ScriptInspectionLimits &)                                                    \
    {                                                                                                                  \
        platformator::ScriptInspectionResult result;                                                                   \
        result.providerAvailable = true;                                                                               \
        return result;                                                                                                 \
    }                                                                                                                  \
    friend void to_json(nlohmann::json &j, const Type &)                                                               \
    {                                                                                                                  \
        j = nlohmann::json::object();                                                                                  \
    }                                                                                                                  \
    friend void from_json(const nlohmann::json &, Type &)                                                              \
    {                                                                                                                  \
    }                                                                                                                  \
    void platformatorResolveScriptReferences(platformator::ReferenceResolver &)                                        \
    {                                                                                                                  \
    }                                                                                                                  \
    PLATFORMATOR_SERIALIZABLE_SCRIPT_OVERRIDES(Type)

#define PLATFORMATOR_SERIALIZABLE_SCRIPT_1(Type, ...)                                                                  \
    PLATFORMATOR_SERIALIZABLE_SCRIPT_REGISTRATION(Type)                                                                \
    static platformator::ScriptDescriptor platformatorDescribeScriptMetadata()                                         \
    {                                                                                                                  \
        using SelfType = Type;                                                                                         \
        return platformator::ScriptDescriptor{                                                                         \
            #Type, true, {PLATFORMATOR_FOR_EACH(PLATFORMATOR_METADATA_FIELD_ITEM, __VA_ARGS__)}};                      \
    }                                                                                                                  \
    static platformator::ScriptInspectionResult platformatorInspectScriptFields(                                       \
        const Type &instance, const platformator::ScriptInspectionLimits &requestedLimits)                              \
    {                                                                                                                  \
        platformator::ScriptInspectionResult result;                                                                   \
        result.providerAvailable = true;                                                                               \
        const auto limits = platformator::bounded_script_inspection_limits(requestedLimits);                           \
        PLATFORMATOR_FOR_EACH(PLATFORMATOR_INSPECT_SCRIPT_FIELD, __VA_ARGS__)                                           \
        return result;                                                                                                 \
    }                                                                                                                  \
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Type, __VA_ARGS__)                                                     \
    void resolveTemplateAssets(platformator::ReferenceResolver &resolver) override                                     \
    {                                                                                                                  \
        PLATFORMATOR_FOR_EACH(PLATFORMATOR_RESOLVE_TEMPLATE_FIELD, __VA_ARGS__)                                        \
    }                                                                                                                  \
    void platformatorResolveScriptReferences(platformator::ReferenceResolver &resolver)                                \
    {                                                                                                                  \
        PLATFORMATOR_FOR_EACH(PLATFORMATOR_RESOLVE_SERIALIZED_FIELD, __VA_ARGS__)                                      \
    }                                                                                                                  \
    PLATFORMATOR_SERIALIZABLE_SCRIPT_OVERRIDES(Type)

#define SERIALIZABLE_SCRIPT(Type, ...)                                                                                 \
    PLATFORMATOR_SERIALIZABLE_SCRIPT_SELECT(PLATFORMATOR_HAS_FIELDS_FLAG(__VA_ARGS__))(Type __VA_OPT__(, __VA_ARGS__))

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
