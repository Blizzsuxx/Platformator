#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "behaviorspec.h"
#include "gameobject.h"

class Collider;
class ScriptComponent;
class Behavior;
class Collision;
class GameManager;
class AudioWrapper;
class TextureWrapper;
class AnimationClip;

namespace platformator_behavior_detail
{
    GameObject *findGameObjectByName(const std::string &name);
    GameObject *findGameObjectById(uint64_t id);
    Component *findComponentById(uint64_t id);
    std::string resolveAssetPath(const std::string &sourcePath, const std::string &path);

    AudioWrapper *resolveAudioAssetByPath(const std::string &resolvedPath);
    TextureWrapper *resolveTextureAssetByPath(const std::string &resolvedPath);
    AnimationClip *resolveAnimationClipByPath(const std::string &resolvedPath);

    struct GameObjectReference
    {
        uint64_t id;
        GameObject *gameObject;

        GameObjectReference() : id(0), gameObject(nullptr)
        {
        }

        explicit GameObjectReference(uint64_t id) : id(id), gameObject(nullptr)
        {
        }

        explicit GameObjectReference(GameObject *gameObject) : id(gameObject != nullptr ? gameObject->getId() : 0), gameObject(gameObject)
        {
        }

        GameObject *get() const
        {
            return gameObject;
        }

        GameObject *operator->() const
        {
            return gameObject;
        }

        GameObject &operator*() const
        {
            return *gameObject;
        }

        operator GameObject *() const
        {
            return gameObject;
        }

        explicit operator bool() const
        {
            return gameObject != nullptr;
        }

        void set(GameObject *value)
        {
            gameObject = value;
            id = value != nullptr ? value->getId() : 0;
        }

        void resolve();
    };

    template <typename T>
    struct ComponentReference
    {
        uint64_t id;
        T *component;

        ComponentReference() : id(0), component(nullptr)
        {
        }

        explicit ComponentReference(uint64_t id) : id(id), component(nullptr)
        {
        }

        explicit ComponentReference(T *component) : id(component != nullptr ? component->getId() : 0), component(component)
        {
        }

        T *get() const
        {
            return component;
        }

        T *operator->() const
        {
            return component;
        }

        T &operator*() const
        {
            return *component;
        }

        operator T *() const
        {
            return component;
        }

        explicit operator bool() const
        {
            return component != nullptr;
        }

        void set(T *value)
        {
            component = value;
            id = value != nullptr ? value->getId() : 0;
        }

        void resolve()
        {
            component = id == 0 ? nullptr : dynamic_cast<T *>(findComponentById(id));
        }
    };

    struct NamedGameObjectReference
    {
        std::string name;
        GameObject *gameObject;

        NamedGameObjectReference() : name(), gameObject(nullptr)
        {
        }

        explicit NamedGameObjectReference(std::string name) : name(std::move(name)), gameObject(nullptr)
        {
        }

        GameObject *get() const
        {
            return gameObject;
        }

        GameObject *operator->() const
        {
            return gameObject;
        }

        GameObject &operator*() const
        {
            return *gameObject;
        }

        operator GameObject *() const
        {
            return gameObject;
        }

        explicit operator bool() const
        {
            return gameObject != nullptr;
        }

        void resolve()
        {
            gameObject = name.empty() ? nullptr : findGameObjectByName(name);
        }
    };

    template <typename Asset>
    struct AssetReference
    {
        using Resolver = Asset *(*)(const std::string &resolvedPath);

        std::string path;
        std::string resolvedPath;
        Asset *asset;
        Resolver resolver;

        AssetReference() : path(), resolvedPath(), asset(nullptr), resolver(nullptr)
        {
        }

        explicit AssetReference(std::string path, Resolver resolver = nullptr)
            : path(std::move(path)), resolvedPath(this->path), asset(nullptr), resolver(resolver)
        {
        }

        AssetReference(std::string path, std::string resolvedPath, Resolver resolver)
            : path(std::move(path)), resolvedPath(std::move(resolvedPath)), asset(nullptr), resolver(resolver)
        {
        }

        Asset *get() const
        {
            return asset;
        }

        Asset *operator->() const
        {
            return asset;
        }

        Asset &operator*() const
        {
            return *asset;
        }

        operator Asset *() const
        {
            return asset;
        }

        explicit operator bool() const
        {
            return asset != nullptr;
        }

        void resolve()
        {
            asset = (resolvedPath.empty() || resolver == nullptr) ? nullptr : resolver(resolvedPath);
        }
    };

    using AudioAssetReference = AssetReference<AudioWrapper>;
    using TextureAssetReference = AssetReference<TextureWrapper>;
    using AnimationClipReference = AssetReference<AnimationClip>;

    template <typename T>
    struct NamedComponentReference
    {
        std::string name;
        T *component;

        NamedComponentReference() : name(), component(nullptr)
        {
        }

        explicit NamedComponentReference(std::string name) : name(std::move(name)), component(nullptr)
        {
        }

        T *get() const
        {
            return component;
        }

        T *operator->() const
        {
            return component;
        }

        T &operator*() const
        {
            return *component;
        }

        operator T *() const
        {
            return component;
        }

        explicit operator bool() const
        {
            return component != nullptr;
        }

        void resolve()
        {
            GameObject *gameObject = name.empty() ? nullptr : findGameObjectByName(name);
            component = gameObject != nullptr ? gameObject->getComponent<T>() : nullptr;
        }
    };

    struct BehaviorFieldDescriptor
    {
        const char *name;
        void (*deserialize)(Behavior &behavior, const char *name, const BehaviorSpec &spec);
        void (*serialize)(const Behavior &behavior, const char *name, BehaviorSpec &spec);
        void (*resolve)(Behavior &behavior);
    };

    template <typename T>
    inline constexpr bool unsupported_behavior_field_v = false;

    template <typename Owner>
    std::vector<BehaviorFieldDescriptor> &annotatedFieldDescriptorsMutable()
    {
        static std::vector<BehaviorFieldDescriptor> descriptors;
        return descriptors;
    }

    template <typename Owner>
    const std::vector<BehaviorFieldDescriptor> &annotatedFieldDescriptors()
    {
        return annotatedFieldDescriptorsMutable<Owner>();
    }

    template <typename T>
    struct ScriptFieldTraits
    {
        static T get(const BehaviorSpec &, const char *, const T &)
        {
            static_assert(unsupported_behavior_field_v<T>, "Unsupported behavior field type");
        }

        static void set(BehaviorSpec &, const char *, const T &)
        {
            static_assert(unsupported_behavior_field_v<T>, "Unsupported behavior field type");
        }
    };

    template <>
    struct ScriptFieldTraits<std::string>
    {
        static std::string get(const BehaviorSpec &spec, const char *name, const std::string &fallback)
        {
            return spec.getString(name, fallback);
        }

        static void set(BehaviorSpec &spec, const char *name, const std::string &value)
        {
            spec.setStringProperty(name, value);
        }
    };

    template <>
    struct ScriptFieldTraits<float>
    {
        static float get(const BehaviorSpec &spec, const char *name, float fallback)
        {
            return spec.getFloat(name, fallback);
        }

        static void set(BehaviorSpec &spec, const char *name, float value)
        {
            spec.setFloatProperty(name, value);
        }
    };

    template <>
    struct ScriptFieldTraits<double>
    {
        static double get(const BehaviorSpec &spec, const char *name, double fallback)
        {
            return spec.getDouble(name, fallback);
        }

        static void set(BehaviorSpec &spec, const char *name, double value)
        {
            spec.setDoubleProperty(name, value);
        }
    };

    template <>
    struct ScriptFieldTraits<int>
    {
        static int get(const BehaviorSpec &spec, const char *name, int fallback)
        {
            return spec.getInt(name, fallback);
        }

        static void set(BehaviorSpec &spec, const char *name, int value)
        {
            spec.setIntProperty(name, value);
        }
    };

    template <>
    struct ScriptFieldTraits<bool>
    {
        static bool get(const BehaviorSpec &spec, const char *name, bool fallback)
        {
            return spec.getBool(name, fallback);
        }

        static void set(BehaviorSpec &spec, const char *name, bool value)
        {
            spec.setBoolProperty(name, value);
        }
    };

    template <>
    struct ScriptFieldTraits<Eigen::Vector2f>
    {
        static Eigen::Vector2f get(const BehaviorSpec &spec, const char *name, const Eigen::Vector2f &fallback)
        {
            return spec.getVector2f(name, fallback);
        }

        static void set(BehaviorSpec &spec, const char *name, const Eigen::Vector2f &value)
        {
            spec.setVector2fProperty(name, value);
        }
    };

    template <>
    struct ScriptFieldTraits<NamedGameObjectReference>
    {
        static NamedGameObjectReference get(const BehaviorSpec &spec, const char *name, const NamedGameObjectReference &fallback)
        {
            return NamedGameObjectReference(spec.getString(name, fallback.name));
        }

        static void set(BehaviorSpec &spec, const char *name, const NamedGameObjectReference &value)
        {
            spec.setStringProperty(name, value.name);
        }
    };

    template <typename T>
    struct ScriptFieldTraits<NamedComponentReference<T>>
    {
        static NamedComponentReference<T> get(const BehaviorSpec &spec, const char *name, const NamedComponentReference<T> &fallback)
        {
            return NamedComponentReference<T>(spec.getString(name, fallback.name));
        }

        static void set(BehaviorSpec &spec, const char *name, const NamedComponentReference<T> &value)
        {
            spec.setStringProperty(name, value.name);
        }
    };

    template <>
    struct ScriptFieldTraits<GameObjectReference>
    {
        static GameObjectReference get(const BehaviorSpec &spec, const char *name, const GameObjectReference &fallback)
        {
            return GameObjectReference(spec.getGameObjectId(name, fallback.id));
        }

        static void set(BehaviorSpec &spec, const char *name, const GameObjectReference &value)
        {
            spec.setGameObjectReferenceProperty(name, value.gameObject != nullptr ? value.gameObject->getId() : value.id);
        }
    };

    template <typename T>
    struct ScriptFieldTraits<ComponentReference<T>>
    {
        static ComponentReference<T> get(const BehaviorSpec &spec, const char *name, const ComponentReference<T> &fallback)
        {
            return ComponentReference<T>(spec.getComponentId(name, fallback.id));
        }

        static void set(BehaviorSpec &spec, const char *name, const ComponentReference<T> &value)
        {
            spec.setComponentReferenceProperty(name, value.component != nullptr ? value.component->getId() : value.id);
        }
    };

    template <>
    struct ScriptFieldTraits<AudioAssetReference>
    {
        static AudioAssetReference get(const BehaviorSpec &spec, const char *name, const AudioAssetReference &fallback)
        {
            const std::string path = spec.getString(name, fallback.path);
            const std::string resolvedPath = resolveAssetPath(spec.sourcePath, path);
            return AudioAssetReference(path, resolvedPath, &resolveAudioAssetByPath);
        }

        static void set(BehaviorSpec &spec, const char *name, const AudioAssetReference &value)
        {
            spec.setStringProperty(name, value.path);
        }
    };

    template <>
    struct ScriptFieldTraits<TextureAssetReference>
    {
        static TextureAssetReference get(const BehaviorSpec &spec, const char *name, const TextureAssetReference &fallback)
        {
            const std::string path = spec.getString(name, fallback.path);
            const std::string resolvedPath = resolveAssetPath(spec.sourcePath, path);
            return TextureAssetReference(path, resolvedPath, &resolveTextureAssetByPath);
        }

        static void set(BehaviorSpec &spec, const char *name, const TextureAssetReference &value)
        {
            spec.setStringProperty(name, value.path);
        }
    };

    template <>
    struct ScriptFieldTraits<AnimationClipReference>
    {
        static AnimationClipReference get(const BehaviorSpec &spec, const char *name, const AnimationClipReference &fallback)
        {
            const std::string path = spec.getString(name, fallback.path);
            const std::string resolvedPath = resolveAssetPath(spec.sourcePath, path);
            return AnimationClipReference(path, resolvedPath, &resolveAnimationClipByPath);
        }

        static void set(BehaviorSpec &spec, const char *name, const AnimationClipReference &value)
        {
            spec.setStringProperty(name, value.path);
        }
    };

    template <typename Owner, typename Field, Field Owner::*Member>
    struct BehaviorFieldBinding
    {
        using StoredField = std::remove_cv_t<std::remove_reference_t<Field>>;

        static void deserialize(Behavior &behavior, const char *name, const BehaviorSpec &spec)
        {
            Owner &owner = static_cast<Owner &>(behavior);
            owner.*Member = ScriptFieldTraits<StoredField>::get(spec, name, owner.*Member);
        }

        static void serialize(const Behavior &behavior, const char *name, BehaviorSpec &spec)
        {
            const Owner &owner = static_cast<const Owner &>(behavior);
            ScriptFieldTraits<StoredField>::set(spec, name, owner.*Member);
        }

        static void resolve(Behavior &behavior)
        {
            Owner &owner = static_cast<Owner &>(behavior);
            if constexpr (requires(StoredField &field) { field.resolve(); })
            {
                (owner.*Member).resolve();
            }
        }
    };

    template <typename Owner, typename Field, Field Owner::*Member>
    BehaviorFieldDescriptor makeFieldDescriptor(const char *name)
    {
        return BehaviorFieldDescriptor{
            name,
            &BehaviorFieldBinding<Owner, Field, Member>::deserialize,
            &BehaviorFieldBinding<Owner, Field, Member>::serialize,
            &BehaviorFieldBinding<Owner, Field, Member>::resolve};
    }

    template <typename Owner, typename Field, Field Owner::*Member>
    void registerAnnotatedField(const char *name)
    {
        std::vector<BehaviorFieldDescriptor> &fields = annotatedFieldDescriptorsMutable<Owner>();
        for (const BehaviorFieldDescriptor &field : fields)
        {
            if (std::string_view(field.name) == name)
            {
                return;
            }
        }

        fields.push_back(makeFieldDescriptor<Owner, Field, Member>(name));
    }

    template <typename Owner, typename Field, Field Owner::*Member>
    struct BehaviorFieldRegistrar
    {
        explicit BehaviorFieldRegistrar(const char *name)
        {
            registerAnnotatedField<Owner, Field, Member>(name);
        }
    };
}

namespace platformator
{
    using GameObjectRef = platformator_behavior_detail::GameObjectReference;
    template <typename T>
    using ComponentRef = platformator_behavior_detail::ComponentReference<T>;
    using AudioAssetRef = platformator_behavior_detail::AudioAssetReference;
    using TextureAssetRef = platformator_behavior_detail::TextureAssetReference;
    using AnimationClipRef = platformator_behavior_detail::AnimationClipReference;
}

class Behavior
{
    friend class ScriptComponent;
    friend class BehaviorFactoryRegistry;
    friend class GameManager;

public:
    Behavior();
    virtual ~Behavior() = default;

    GameObject *getGameObject() const;
    bool getEnabled() const;
    void setEnabled(bool enabled);
    virtual std::string getTypeName() const;
    virtual void deserialize(const BehaviorSpec &spec);
    virtual void serialize(BehaviorSpec &spec) const;

    virtual void start();
    virtual void update(double timeDelta);
    virtual void fixedUpdate(double timeDelta);
    virtual void lateUpdate(double timeDelta);
    virtual void onDestroy();
    virtual void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta);
    virtual void onCollisionExit(Collider *other, double timeDelta);
    virtual void onCollisionStay(const Collision *collision, Collider *other, double timeDelta);

    void setGameObject(GameObject *gameObject);
    GameObject *getGameObject();

protected:
    virtual const std::vector<platformator_behavior_detail::BehaviorFieldDescriptor> &getBehaviorFieldDescriptors() const;

private:
    GameObject *gameObject;
    bool enabled;
    std::string registeredTypeName;

    void resolveFieldBindings();
    void setRegisteredTypeName(std::string typeName);
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

#define PLATFORMATOR_SERIALIZABLE_BEHAVIOR(TYPE)                                                                             \
    using PlatformatorSerializedBehaviorOwner = TYPE;                                                                        \
    const std::vector<::platformator_behavior_detail::BehaviorFieldDescriptor> &getBehaviorFieldDescriptors() const override \
    {                                                                                                                        \
        return ::platformator_behavior_detail::annotatedFieldDescriptors<TYPE>();                                            \
    }

#define PLATFORMATOR_SERIALIZED_FIELD(TYPE, member)                                                                                                                                      \
    TYPE member{};                                                                                                                                                                       \
    [[maybe_unused]] inline static const ::platformator_behavior_detail::BehaviorFieldRegistrar<PlatformatorSerializedBehaviorOwner, TYPE, &PlatformatorSerializedBehaviorOwner::member> \
        platformatorSerializedFieldRegistrar_##member { #member }

#define PLATFORMATOR_SERIALIZED_FIELD_NAMED(TYPE, member, name)                                                                                                                          \
    TYPE member{};                                                                                                                                                                       \
    [[maybe_unused]] inline static const ::platformator_behavior_detail::BehaviorFieldRegistrar<PlatformatorSerializedBehaviorOwner, TYPE, &PlatformatorSerializedBehaviorOwner::member> \
        platformatorSerializedFieldRegistrar_##member { name }

#define SERIALIZABLE_BEHAVIOR(TYPE) PLATFORMATOR_SERIALIZABLE_BEHAVIOR(TYPE)
#define SERIALIZED_FIELD(TYPE, member) PLATFORMATOR_SERIALIZED_FIELD(TYPE, member)
#define SERIALIZED_FIELD_NAMED(TYPE, member, name) PLATFORMATOR_SERIALIZED_FIELD_NAMED(TYPE, member, name)
