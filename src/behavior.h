#pragma once

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "gameobject.h"
#include "scriptdescriptor.h"

class Collider;
class ScriptComponent;
class Behavior;

namespace platformator_behavior_detail
{
    struct NamedGameObjectReference
    {
        std::string name;

        NamedGameObjectReference() = default;
        explicit NamedGameObjectReference(std::string name) : name(std::move(name))
        {
        }
    };

    struct BehaviorFieldDescriptor
    {
        const char *name;
        void (*deserialize)(Behavior &behavior, const char *name, const ScriptDescriptor &descriptor);
        void (*serialize)(const Behavior &behavior, const char *name, ScriptDescriptor &descriptor);
    };

    template <typename T>
    inline constexpr bool unsupported_behavior_field_v = false;

    template <typename T>
    struct ScriptFieldTraits
    {
        static T get(const ScriptDescriptor &, const char *, const T &)
        {
            static_assert(unsupported_behavior_field_v<T>, "Unsupported behavior field type");
        }

        static void set(ScriptDescriptor &, const char *, const T &)
        {
            static_assert(unsupported_behavior_field_v<T>, "Unsupported behavior field type");
        }
    };

    template <>
    struct ScriptFieldTraits<std::string>
    {
        static std::string get(const ScriptDescriptor &descriptor, const char *name, const std::string &fallback)
        {
            return descriptor.getString(name, fallback);
        }

        static void set(ScriptDescriptor &descriptor, const char *name, const std::string &value)
        {
            descriptor.setStringProperty(name, value);
        }
    };

    template <>
    struct ScriptFieldTraits<float>
    {
        static float get(const ScriptDescriptor &descriptor, const char *name, float fallback)
        {
            return descriptor.getFloat(name, fallback);
        }

        static void set(ScriptDescriptor &descriptor, const char *name, float value)
        {
            descriptor.setFloatProperty(name, value);
        }
    };

    template <>
    struct ScriptFieldTraits<double>
    {
        static double get(const ScriptDescriptor &descriptor, const char *name, double fallback)
        {
            return descriptor.getDouble(name, fallback);
        }

        static void set(ScriptDescriptor &descriptor, const char *name, double value)
        {
            descriptor.setDoubleProperty(name, value);
        }
    };

    template <>
    struct ScriptFieldTraits<int>
    {
        static int get(const ScriptDescriptor &descriptor, const char *name, int fallback)
        {
            return descriptor.getInt(name, fallback);
        }

        static void set(ScriptDescriptor &descriptor, const char *name, int value)
        {
            descriptor.setIntProperty(name, value);
        }
    };

    template <>
    struct ScriptFieldTraits<bool>
    {
        static bool get(const ScriptDescriptor &descriptor, const char *name, bool fallback)
        {
            return descriptor.getBool(name, fallback);
        }

        static void set(ScriptDescriptor &descriptor, const char *name, bool value)
        {
            descriptor.setBoolProperty(name, value);
        }
    };

    template <>
    struct ScriptFieldTraits<Eigen::Vector2f>
    {
        static Eigen::Vector2f get(const ScriptDescriptor &descriptor, const char *name, const Eigen::Vector2f &fallback)
        {
            return descriptor.getVector2f(name, fallback);
        }

        static void set(ScriptDescriptor &descriptor, const char *name, const Eigen::Vector2f &value)
        {
            descriptor.setVector2fProperty(name, value);
        }
    };

    template <>
    struct ScriptFieldTraits<NamedGameObjectReference>
    {
        static NamedGameObjectReference get(const ScriptDescriptor &descriptor, const char *name, const NamedGameObjectReference &fallback)
        {
            return NamedGameObjectReference(descriptor.getString(name, fallback.name));
        }

        static void set(ScriptDescriptor &descriptor, const char *name, const NamedGameObjectReference &value)
        {
            descriptor.setStringProperty(name, value.name);
        }
    };

    template <typename Owner, typename Field, Field Owner::*Member>
    struct BehaviorFieldBinding
    {
        using StoredField = std::remove_cv_t<std::remove_reference_t<Field>>;

        static void deserialize(Behavior &behavior, const char *name, const ScriptDescriptor &descriptor)
        {
            Owner &owner = static_cast<Owner &>(behavior);
            owner.*Member = ScriptFieldTraits<StoredField>::get(descriptor, name, owner.*Member);
        }

        static void serialize(const Behavior &behavior, const char *name, ScriptDescriptor &descriptor)
        {
            const Owner &owner = static_cast<const Owner &>(behavior);
            ScriptFieldTraits<StoredField>::set(descriptor, name, owner.*Member);
        }
    };

    template <typename Owner, typename Field, Field Owner::*Member>
    BehaviorFieldDescriptor makeFieldDescriptor(const char *name)
    {
        return BehaviorFieldDescriptor{
            name,
            &BehaviorFieldBinding<Owner, Field, Member>::deserialize,
            &BehaviorFieldBinding<Owner, Field, Member>::serialize};
    }
}

class Behavior
{
    friend class ScriptComponent;
    friend class BehaviorFactoryRegistry;

public:
    Behavior();
    virtual ~Behavior() = default;

    GameObject *getGameObject() const;
    bool getEnabled() const;
    void setEnabled(bool enabled);
    virtual std::string getTypeName() const;
    virtual void deserialize(const ScriptDescriptor &descriptor);
    virtual void serialize(ScriptDescriptor &descriptor) const;

    virtual void start();
    virtual void update(double timeDelta);
    virtual void fixedUpdate(double timeDelta);
    virtual void lateUpdate(double timeDelta);
    virtual void onDestroy();
    virtual void onCollisionEnter(Collider *other, double timeDelta);
    virtual void onCollisionExit(Collider *other, double timeDelta);
    virtual void onCollisionStay(Collider *other, double timeDelta);

    void setGameObject(GameObject *gameObject);
    GameObject *getGameObject();

protected:
    virtual const std::vector<platformator_behavior_detail::BehaviorFieldDescriptor> &getBehaviorFieldDescriptors() const;

private:
    GameObject *gameObject;
    bool enabled;
};

#define BEHAVIOR_FIELDS(TYPE, ...)                                                                                           \
    const std::vector<::platformator_behavior_detail::BehaviorFieldDescriptor> &getBehaviorFieldDescriptors() const override \
    {                                                                                                                        \
        using PlatformatorBehaviorFieldOwner = TYPE;                                                                         \
        static const std::vector<::platformator_behavior_detail::BehaviorFieldDescriptor> fields = {__VA_ARGS__};            \
        return fields;                                                                                                       \
    }

#define BEHAVIOR_FIELD(member) BEHAVIOR_FIELD_NAMED(member, #member)

#define BEHAVIOR_FIELD_NAMED(member, name) \
    ::platformator_behavior_detail::makeFieldDescriptor<PlatformatorBehaviorFieldOwner, decltype(PlatformatorBehaviorFieldOwner::member), &PlatformatorBehaviorFieldOwner::member>(name)
