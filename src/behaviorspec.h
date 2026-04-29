#pragma once

#include <Eigen/Dense>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace platformator_behavior_detail
{
    struct GameObjectIdReference
    {
        uint64_t id = 0;
    };

    struct ComponentIdReference
    {
        uint64_t id = 0;
    };
}

using ScriptValue = std::variant<std::string,
                                 bool,
                                 int64_t,
                                 double,
                                 Eigen::Vector2f,
                                 platformator_behavior_detail::GameObjectIdReference,
                                 platformator_behavior_detail::ComponentIdReference>;

struct BehaviorProperty
{
    std::string name;
    ScriptValue value;
};

struct BehaviorSpec
{
    std::string type;
    std::string sourcePath;
    bool enabled = true;
    std::vector<BehaviorProperty> properties;

    const BehaviorProperty *findProperty(const std::string &name) const;
    bool hasProperty(const std::string &name) const;
    std::string getString(const std::string &name, const std::string &fallback = "") const;
    float getFloat(const std::string &name, float fallback = 0.0f) const;
    double getDouble(const std::string &name, double fallback = 0.0) const;
    int getInt(const std::string &name, int fallback = 0) const;
    bool getBool(const std::string &name, bool fallback = false) const;
    Eigen::Vector2f getVector2f(const std::string &name, const Eigen::Vector2f &fallback = Eigen::Vector2f::Zero()) const;
    uint64_t getGameObjectId(const std::string &name, uint64_t fallback = 0) const;
    uint64_t getComponentId(const std::string &name, uint64_t fallback = 0) const;

    void setProperty(const std::string &name, ScriptValue value);
    void setStringProperty(const std::string &name, std::string value);
    void setFloatProperty(const std::string &name, float value);
    void setDoubleProperty(const std::string &name, double value);
    void setIntProperty(const std::string &name, int value);
    void setBoolProperty(const std::string &name, bool value);
    void setVector2fProperty(const std::string &name, const Eigen::Vector2f &value);
    void setGameObjectReferenceProperty(const std::string &name, uint64_t id);
    void setComponentReferenceProperty(const std::string &name, uint64_t id);
};