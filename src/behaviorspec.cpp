#include "behaviorspec.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{
    bool equalsIgnoreCase(std::string_view left, std::string_view right)
    {
        if (left.size() != right.size())
        {
            return false;
        }

        for (size_t index = 0; index < left.size(); ++index)
        {
            if (std::tolower(static_cast<unsigned char>(left[index])) != std::tolower(static_cast<unsigned char>(right[index])))
            {
                return false;
            }
        }

        return true;
    }

    std::string formatScriptValue(const ScriptValue &value)
    {
        return std::visit(
            [](const auto &typedValue) -> std::string
            {
                using ValueType = std::decay_t<decltype(typedValue)>;
                if constexpr (std::is_same_v<ValueType, std::string>)
                {
                    return '"' + typedValue + '"';
                }
                else if constexpr (std::is_same_v<ValueType, bool>)
                {
                    return typedValue ? "true" : "false";
                }
                else if constexpr (std::is_same_v<ValueType, int64_t>)
                {
                    return std::to_string(typedValue);
                }
                else if constexpr (std::is_same_v<ValueType, double>)
                {
                    std::ostringstream stream;
                    stream << typedValue;
                    return stream.str();
                }
                else if constexpr (std::is_same_v<ValueType, Eigen::Vector2f>)
                {
                    std::ostringstream stream;
                    stream << '[' << typedValue.x() << ", " << typedValue.y() << ']';
                    return stream.str();
                }
                else if constexpr (std::is_same_v<ValueType, platformator_behavior_detail::GameObjectIdReference>)
                {
                    return "gameObject#" + std::to_string(typedValue.id);
                }
                else
                {
                    return "component#" + std::to_string(typedValue.id);
                }
            },
            value);
    }
}

const BehaviorProperty *BehaviorSpec::findProperty(const std::string &name) const
{
    for (auto it = properties.rbegin(); it != properties.rend(); ++it)
    {
        if (it->name == name)
        {
            return &*it;
        }
    }

    for (auto it = properties.rbegin(); it != properties.rend(); ++it)
    {
        if (equalsIgnoreCase(it->name, name))
        {
            return &*it;
        }
    }

    return nullptr;
}

bool BehaviorSpec::hasProperty(const std::string &name) const
{
    return findProperty(name) != nullptr;
}

std::string BehaviorSpec::getString(const std::string &name, const std::string &fallback) const
{
    const BehaviorProperty *property = findProperty(name);
    if (property == nullptr)
    {
        return fallback;
    }

    if (const auto *value = std::get_if<std::string>(&property->value))
    {
        return *value;
    }

    throw std::runtime_error("Script property '" + name + "' expected a string but found " + formatScriptValue(property->value) + '.');
}

float BehaviorSpec::getFloat(const std::string &name, float fallback) const
{
    const BehaviorProperty *property = findProperty(name);
    if (property == nullptr)
    {
        return fallback;
    }

    if (const auto *value = std::get_if<int64_t>(&property->value))
    {
        return static_cast<float>(*value);
    }

    if (const auto *value = std::get_if<double>(&property->value))
    {
        return static_cast<float>(*value);
    }

    throw std::runtime_error("Script property '" + name + "' expected a float but found " + formatScriptValue(property->value) + '.');
}

double BehaviorSpec::getDouble(const std::string &name, double fallback) const
{
    const BehaviorProperty *property = findProperty(name);
    if (property == nullptr)
    {
        return fallback;
    }

    if (const auto *value = std::get_if<int64_t>(&property->value))
    {
        return static_cast<double>(*value);
    }

    if (const auto *value = std::get_if<double>(&property->value))
    {
        return *value;
    }

    throw std::runtime_error("Script property '" + name + "' expected a double but found " + formatScriptValue(property->value) + '.');
}

int BehaviorSpec::getInt(const std::string &name, int fallback) const
{
    const BehaviorProperty *property = findProperty(name);
    if (property == nullptr)
    {
        return fallback;
    }

    if (const auto *value = std::get_if<int64_t>(&property->value))
    {
        if (*value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max())
        {
            throw std::runtime_error("Script property '" + name + "' is out of range for int: " + std::to_string(*value) + '.');
        }

        return static_cast<int>(*value);
    }

    throw std::runtime_error("Script property '" + name + "' expected an int but found " + formatScriptValue(property->value) + '.');
}

bool BehaviorSpec::getBool(const std::string &name, bool fallback) const
{
    const BehaviorProperty *property = findProperty(name);
    if (property == nullptr)
    {
        return fallback;
    }

    if (const auto *value = std::get_if<bool>(&property->value))
    {
        return *value;
    }

    throw std::runtime_error("Script property '" + name + "' expected a bool but found " + formatScriptValue(property->value) + '.');
}

Eigen::Vector2f BehaviorSpec::getVector2f(const std::string &name, const Eigen::Vector2f &fallback) const
{
    const BehaviorProperty *property = findProperty(name);
    if (property == nullptr)
    {
        return fallback;
    }

    if (const auto *value = std::get_if<Eigen::Vector2f>(&property->value))
    {
        return *value;
    }

    throw std::runtime_error("Script property '" + name + "' expected a Vector2f but found " + formatScriptValue(property->value) + '.');
}

uint64_t BehaviorSpec::getGameObjectId(const std::string &name, uint64_t fallback) const
{
    const BehaviorProperty *property = findProperty(name);
    if (property == nullptr)
    {
        return fallback;
    }

    if (const auto *value = std::get_if<platformator_behavior_detail::GameObjectIdReference>(&property->value))
    {
        return value->id;
    }

    throw std::runtime_error("Script property '" + name + "' expected a game object reference but found " + formatScriptValue(property->value) + '.');
}

uint64_t BehaviorSpec::getComponentId(const std::string &name, uint64_t fallback) const
{
    const BehaviorProperty *property = findProperty(name);
    if (property == nullptr)
    {
        return fallback;
    }

    if (const auto *value = std::get_if<platformator_behavior_detail::ComponentIdReference>(&property->value))
    {
        return value->id;
    }

    throw std::runtime_error("Script property '" + name + "' expected a component reference but found " + formatScriptValue(property->value) + '.');
}

void BehaviorSpec::setProperty(const std::string &name, ScriptValue value)
{
    for (BehaviorProperty &property : properties)
    {
        if (property.name == name)
        {
            property.value = std::move(value);
            return;
        }
    }

    for (BehaviorProperty &property : properties)
    {
        if (equalsIgnoreCase(property.name, name))
        {
            property.name = name;
            property.value = std::move(value);
            return;
        }
    }

    properties.push_back(BehaviorProperty{name, std::move(value)});
}

void BehaviorSpec::setStringProperty(const std::string &name, std::string value)
{
    setProperty(name, std::move(value));
}

void BehaviorSpec::setFloatProperty(const std::string &name, float value)
{
    setProperty(name, static_cast<double>(value));
}

void BehaviorSpec::setDoubleProperty(const std::string &name, double value)
{
    setProperty(name, value);
}

void BehaviorSpec::setIntProperty(const std::string &name, int value)
{
    setProperty(name, static_cast<int64_t>(value));
}

void BehaviorSpec::setBoolProperty(const std::string &name, bool value)
{
    setProperty(name, value);
}

void BehaviorSpec::setVector2fProperty(const std::string &name, const Eigen::Vector2f &value)
{
    setProperty(name, value);
}

void BehaviorSpec::setGameObjectReferenceProperty(const std::string &name, uint64_t id)
{
    setProperty(name, platformator_behavior_detail::GameObjectIdReference{id});
}

void BehaviorSpec::setComponentReferenceProperty(const std::string &name, uint64_t id)
{
    setProperty(name, platformator_behavior_detail::ComponentIdReference{id});
}