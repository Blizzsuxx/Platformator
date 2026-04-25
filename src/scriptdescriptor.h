#pragma once

#include <string>
#include <vector>

enum class ScriptPropertyValueKind
{
    Raw,
    String
};

struct ScriptProperty
{
    std::string name;
    std::string value;
    ScriptPropertyValueKind valueKind = ScriptPropertyValueKind::Raw;
};

struct ScriptDescriptor
{
    std::string type;
    std::vector<ScriptProperty> properties;

    const std::string *findProperty(const std::string &name) const;
    bool hasProperty(const std::string &name) const;
    std::string getString(const std::string &name, const std::string &fallback = "") const;
    float getFloat(const std::string &name, float fallback = 0.0f) const;
    int getInt(const std::string &name, int fallback = 0) const;
    bool getBool(const std::string &name, bool fallback = false) const;
    void setProperty(const std::string &name, std::string value, ScriptPropertyValueKind valueKind = ScriptPropertyValueKind::Raw);
    void setStringProperty(const std::string &name, std::string value);
    void setFloatProperty(const std::string &name, float value);
    void setIntProperty(const std::string &name, int value);
    void setBoolProperty(const std::string &name, bool value);
};