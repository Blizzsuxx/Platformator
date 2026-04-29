#include "behaviorspec.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{
    std::string toLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    std::string normalizePropertyName(const std::string &name)
    {
        return toLower(name);
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
                else
                {
                    std::ostringstream stream;
                    stream << '[' << typedValue.x() << ", " << typedValue.y() << ']';
                    return stream.str();
                }
            },
            value);
    }
}

const BehaviorProperty *BehaviorSpec::findProperty(const std::string &name) const
{
    const std::string normalizedName = normalizePropertyName(name);
    for (auto it = properties.rbegin(); it != properties.rend(); ++it)
    {
        if (it->name == normalizedName)
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

void BehaviorSpec::setProperty(const std::string &name, ScriptValue value)
{
    const std::string normalizedName = normalizePropertyName(name);
    for (BehaviorProperty &property : properties)
    {
        if (property.name == normalizedName)
        {
            property.value = std::move(value);
            return;
        }
    }

    properties.push_back(BehaviorProperty{normalizedName, std::move(value)});
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