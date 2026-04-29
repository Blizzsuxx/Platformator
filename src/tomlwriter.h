#pragma once

#include <Eigen/Dense>
#include <SDL3/SDL.h>

#include <toml++/toml.hpp>

#include <cstdint>
#include <iosfwd>
#include <string>

#include "behaviorspec.h"

class TomlWriter
{
public:
    static toml::array makeVector2Array(const Eigen::Vector2f &value);
    static toml::array makeRectArray(const SDL_FRect &value);
    static void insertScriptValue(toml::table &table, const std::string &key, const ScriptValue &value);
    static void writeDocument(std::ostream &stream, const toml::table &table);
    static std::string formatFloat(double value);

private:
    static std::string quoteString(const std::string &value);
    static std::string escapeTomlString(const std::string &value);
    static void writeTable(std::ostream &stream, const toml::table &table, const std::string &path, bool arrayElement);
    static void writeTableBody(std::ostream &stream, const toml::table &table, const std::string &path);
    static void writeKeyValue(std::ostream &stream, const std::string &key, const toml::node &value);
    static void writeValue(std::ostream &stream, const toml::node &value);
    static void writeArray(std::ostream &stream, const toml::array &array);
    static void writeInlineTable(std::ostream &stream, const toml::table &table);
    static bool isInlineValue(const toml::node &value);
    static bool isArrayOfTables(const toml::array &array);
};