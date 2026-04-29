#include "tomlwriter.h"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace
{
    bool nodeIsInlineValue(const toml::node &value)
    {
        return value.is_string() || value.is_boolean() || value.is_integer() || value.is_floating_point() || value.is_array() || value.is_table();
    }
}

toml::array TomlWriter::makeVector2Array(const Eigen::Vector2f &value)
{
    return toml::array{static_cast<double>(value.x()), static_cast<double>(value.y())};
}

toml::array TomlWriter::makeRectArray(const SDL_FRect &value)
{
    return toml::array{static_cast<double>(value.x),
                       static_cast<double>(value.y),
                       static_cast<double>(value.w),
                       static_cast<double>(value.h)};
}

void TomlWriter::insertScriptValue(toml::table &table, const std::string &key, const ScriptValue &value)
{
    std::visit(
        [&table, &key](const auto &typedValue)
        {
            using ValueType = std::decay_t<decltype(typedValue)>;
            if constexpr (std::is_same_v<ValueType, Eigen::Vector2f>)
            {
                table.insert_or_assign(key, makeVector2Array(typedValue));
            }
            else
            {
                table.insert_or_assign(key, typedValue);
            }
        },
        value);
}

void TomlWriter::writeDocument(std::ostream &stream, const toml::table &table)
{
    writeTableBody(stream, table, "");
    stream << '\n';
}

std::string TomlWriter::quoteString(const std::string &value)
{
    return '"' + escapeTomlString(value) + '"';
}

std::string TomlWriter::formatFloat(double value)
{
    if (!std::isfinite(value))
    {
        throw std::runtime_error("TOML does not support non-finite floating-point values.");
    }

    std::ostringstream stream;
    stream.precision(15);
    stream << value;

    std::string text = stream.str();
    const size_t exponentIndex = text.find_first_of("eE");
    if (exponentIndex != std::string::npos)
    {
        return text;
    }

    const size_t decimalIndex = text.find('.');
    if (decimalIndex == std::string::npos)
    {
        return text;
    }

    while (!text.empty() && text.back() == '0')
    {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.')
    {
        text.pop_back();
    }

    return text.empty() ? "0" : text;
}

void TomlWriter::writeTable(std::ostream &stream, const toml::table &table, const std::string &path, bool arrayElement)
{
    stream << (arrayElement ? "[[" : "[") << path << (arrayElement ? "]]\n" : "]\n");
    writeTableBody(stream, table, path);
}

void TomlWriter::writeTableBody(std::ostream &stream, const toml::table &table, const std::string &path)
{
    bool wroteInlineValue = false;
    bool wroteSection = false;

    for (const auto &[key, value] : table)
    {
        if (!isInlineValue(value))
        {
            continue;
        }

        writeKeyValue(stream, std::string(key.str()), value);
        wroteInlineValue = true;
    }

    for (const auto &[key, value] : table)
    {
        if (const toml::table *childTable = value.as_table())
        {
            if (childTable->is_inline())
            {
                continue;
            }

            if (wroteInlineValue || wroteSection)
            {
                stream << '\n';
            }

            const std::string childPath = path.empty() ? std::string(key.str()) : path + "." + std::string(key.str());
            writeTable(stream, *childTable, childPath, false);
            wroteSection = true;
            continue;
        }

        const toml::array *array = value.as_array();
        if (array == nullptr || !isArrayOfTables(*array))
        {
            continue;
        }

        const std::string childPath = path.empty() ? std::string(key.str()) : path + "." + std::string(key.str());
        for (const toml::node &element : *array)
        {
            const toml::table *elementTable = element.as_table();
            if (elementTable == nullptr)
            {
                continue;
            }

            if (wroteInlineValue || wroteSection)
            {
                stream << '\n';
            }

            writeTable(stream, *elementTable, childPath, true);
            wroteSection = true;
        }
    }
}

std::string TomlWriter::escapeTomlString(const std::string &value)
{
    std::string escaped;
    escaped.reserve(value.size());

    for (char character : value)
    {
        switch (character)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\t':
            escaped += "\\t";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\r':
            escaped += "\\r";
            break;
        default:
            escaped += character;
            break;
        }
    }

    return escaped;
}

void TomlWriter::writeKeyValue(std::ostream &stream, const std::string &key, const toml::node &value)
{
    stream << key << " = ";
    writeValue(stream, value);
    stream << '\n';
}

void TomlWriter::writeValue(std::ostream &stream, const toml::node &value)
{
    if (const auto *stringValue = value.as_string())
    {
        stream << quoteString(stringValue->get());
        return;
    }

    if (const auto *boolValue = value.as_boolean())
    {
        stream << (boolValue->get() ? "true" : "false");
        return;
    }

    if (const auto *integerValue = value.as_integer())
    {
        stream << integerValue->get();
        return;
    }

    if (const auto *floatingValue = value.as_floating_point())
    {
        stream << formatFloat(floatingValue->get());
        return;
    }

    if (const auto *arrayValue = value.as_array())
    {
        writeArray(stream, *arrayValue);
        return;
    }

    if (const auto *tableValue = value.as_table())
    {
        writeInlineTable(stream, *tableValue);
        return;
    }

    throw std::runtime_error("Unsupported TOML node in serializer.");
}

void TomlWriter::writeArray(std::ostream &stream, const toml::array &array)
{
    if (isArrayOfTables(array))
    {
        throw std::runtime_error("Array-of-table values must be emitted as sections.");
    }

    bool multiline = false;
    for (const toml::node &element : array)
    {
        if (element.is_table())
        {
            multiline = true;
            break;
        }
    }

    if (!multiline)
    {
        stream << '[';
        bool first = true;
        for (const toml::node &element : array)
        {
            if (!first)
            {
                stream << ", ";
            }

            writeValue(stream, element);
            first = false;
        }
        stream << ']';
        return;
    }

    stream << "[\n";
    for (const toml::node &element : array)
    {
        stream << "    ";
        writeValue(stream, element);
        stream << ",\n";
    }
    stream << ']';
}

void TomlWriter::writeInlineTable(std::ostream &stream, const toml::table &table)
{
    if (!table.is_inline())
    {
        throw std::runtime_error("Non-inline tables must be emitted as sections.");
    }

    stream << "{ ";
    bool first = true;
    for (const auto &[key, value] : table)
    {
        if (!first)
        {
            stream << ", ";
        }

        stream << key.str() << " = ";
        writeValue(stream, value);
        first = false;
    }
    stream << " }";
}

bool TomlWriter::isInlineValue(const toml::node &value)
{
    if (!nodeIsInlineValue(value))
    {
        return false;
    }

    if (const toml::table *table = value.as_table())
    {
        return table->is_inline();
    }

    if (const toml::array *array = value.as_array())
    {
        return !isArrayOfTables(*array);
    }

    return true;
}

bool TomlWriter::isArrayOfTables(const toml::array &array)
{
    if (array.empty())
    {
        return false;
    }

    for (const toml::node &element : array)
    {
        const toml::table *table = element.as_table();
        if (table == nullptr || table->is_inline())
        {
            return false;
        }
    }

    return true;
}