#include "scriptdescriptor.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
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

    std::string formatFloatPropertyValue(float value)
    {
        std::ostringstream stream;
        stream << std::setprecision(6) << value;
        return stream.str();
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
    setProperty(name, formatFloatPropertyValue(value), ScriptPropertyValueKind::Raw);
}

void ScriptDescriptor::setIntProperty(const std::string &name, int value)
{
    setProperty(name, std::to_string(value), ScriptPropertyValueKind::Raw);
}

void ScriptDescriptor::setBoolProperty(const std::string &name, bool value)
{
    setProperty(name, value ? "true" : "false", ScriptPropertyValueKind::Raw);
}