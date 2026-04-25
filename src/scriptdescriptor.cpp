#include "scriptdescriptor.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
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

    template <typename Numeric>
    std::string formatFloatingPropertyValue(Numeric value, int precision)
    {
        std::ostringstream stream;
        stream << std::setprecision(precision) << value;
        return stream.str();
    }

    Eigen::Vector2f parseVector2fValue(const std::string &name, const std::string &value)
    {
        const size_t separatorIndex = value.find(',');
        if (separatorIndex == std::string::npos || value.find(',', separatorIndex + 1) != std::string::npos)
        {
            throw std::runtime_error("Script property '" + name + "' expected a Vector2f in the form x,y but found '" + value + "'.");
        }

        const std::string xToken = value.substr(0, separatorIndex);
        const std::string yToken = value.substr(separatorIndex + 1);
        if (xToken.empty() || yToken.empty())
        {
            throw std::runtime_error("Script property '" + name + "' expected a Vector2f in the form x,y but found '" + value + "'.");
        }

        try
        {
            size_t parsedLength = 0;
            const float x = std::stof(xToken, &parsedLength);
            if (parsedLength != xToken.size())
            {
                throw std::runtime_error("");
            }

            parsedLength = 0;
            const float y = std::stof(yToken, &parsedLength);
            if (parsedLength != yToken.size())
            {
                throw std::runtime_error("");
            }

            return Eigen::Vector2f(x, y);
        }
        catch (const std::exception &)
        {
            throw std::runtime_error("Script property '" + name + "' expected a Vector2f in the form x,y but found '" + value + "'.");
        }
    }
}

const std::string *ScriptDescriptor::findProperty(const std::string &name) const
{
    const std::string normalizedName = normalizePropertyName(name);
    for (auto it = properties.rbegin(); it != properties.rend(); ++it)
    {
        if (it->name == normalizedName)
        {
            return &it->value;
        }
    }

    return nullptr;
}

bool ScriptDescriptor::hasProperty(const std::string &name) const
{
    return findProperty(name) != nullptr;
}

std::string ScriptDescriptor::getString(const std::string &name, const std::string &fallback) const
{
    const std::string *value = findProperty(name);
    return value != nullptr ? *value : fallback;
}

float ScriptDescriptor::getFloat(const std::string &name, float fallback) const
{
    const std::string *value = findProperty(name);
    if (value == nullptr)
    {
        return fallback;
    }

    try
    {
        size_t parsedLength = 0;
        float parsedValue = std::stof(*value, &parsedLength);
        if (parsedLength != value->size())
        {
            throw std::runtime_error("");
        }
        return parsedValue;
    }
    catch (const std::exception &)
    {
        throw std::runtime_error("Script property '" + name + "' expected a float but found '" + *value + "'.");
    }
}

double ScriptDescriptor::getDouble(const std::string &name, double fallback) const
{
    const std::string *value = findProperty(name);
    if (value == nullptr)
    {
        return fallback;
    }

    try
    {
        size_t parsedLength = 0;
        const double parsedValue = std::stod(*value, &parsedLength);
        if (parsedLength != value->size())
        {
            throw std::runtime_error("");
        }
        return parsedValue;
    }
    catch (const std::exception &)
    {
        throw std::runtime_error("Script property '" + name + "' expected a double but found '" + *value + "'.");
    }
}

int ScriptDescriptor::getInt(const std::string &name, int fallback) const
{
    const std::string *value = findProperty(name);
    if (value == nullptr)
    {
        return fallback;
    }

    try
    {
        size_t parsedLength = 0;
        int parsedValue = std::stoi(*value, &parsedLength);
        if (parsedLength != value->size())
        {
            throw std::runtime_error("");
        }
        return parsedValue;
    }
    catch (const std::exception &)
    {
        throw std::runtime_error("Script property '" + name + "' expected an int but found '" + *value + "'.");
    }
}

bool ScriptDescriptor::getBool(const std::string &name, bool fallback) const
{
    const std::string *value = findProperty(name);
    if (value == nullptr)
    {
        return fallback;
    }

    const std::string loweredValue = toLower(*value);
    if (loweredValue == "true" || loweredValue == "1")
    {
        return true;
    }

    if (loweredValue == "false" || loweredValue == "0")
    {
        return false;
    }

    throw std::runtime_error("Script property '" + name + "' expected a bool but found '" + *value + "'.");
}

Eigen::Vector2f ScriptDescriptor::getVector2f(const std::string &name, const Eigen::Vector2f &fallback) const
{
    const std::string *value = findProperty(name);
    if (value == nullptr)
    {
        return fallback;
    }

    return parseVector2fValue(name, *value);
}

void ScriptDescriptor::setProperty(const std::string &name, std::string value, ScriptPropertyValueKind valueKind)
{
    const std::string normalizedName = normalizePropertyName(name);
    for (ScriptProperty &property : properties)
    {
        if (property.name == normalizedName)
        {
            property.value = std::move(value);
            property.valueKind = valueKind;
            return;
        }
    }

    properties.push_back(ScriptProperty{normalizedName, std::move(value), valueKind});
}

void ScriptDescriptor::setStringProperty(const std::string &name, std::string value)
{
    setProperty(name, std::move(value), ScriptPropertyValueKind::String);
}

void ScriptDescriptor::setFloatProperty(const std::string &name, float value)
{
    setProperty(name, formatFloatingPropertyValue(value, 6), ScriptPropertyValueKind::Raw);
}

void ScriptDescriptor::setDoubleProperty(const std::string &name, double value)
{
    setProperty(name, formatFloatingPropertyValue(value, std::numeric_limits<double>::digits10), ScriptPropertyValueKind::Raw);
}

void ScriptDescriptor::setIntProperty(const std::string &name, int value)
{
    setProperty(name, std::to_string(value), ScriptPropertyValueKind::Raw);
}

void ScriptDescriptor::setBoolProperty(const std::string &name, bool value)
{
    setProperty(name, value ? "true" : "false", ScriptPropertyValueKind::Raw);
}

void ScriptDescriptor::setVector2fProperty(const std::string &name, const Eigen::Vector2f &value)
{
    setProperty(name,
                formatFloatingPropertyValue(value.x(), 6) + "," + formatFloatingPropertyValue(value.y(), 6),
                ScriptPropertyValueKind::Raw);
}