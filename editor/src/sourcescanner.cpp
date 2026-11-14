#include "sourcescanner.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_set>

#include "project.h"

namespace platformator_editor
{
namespace
{
bool is_ident_start(char ch) noexcept
{
    const unsigned char u = static_cast<unsigned char>(ch);
    return std::isalpha(u) != 0 || ch == '_';
}

bool is_ident_char(char ch) noexcept
{
    const unsigned char u = static_cast<unsigned char>(ch);
    return std::isalnum(u) != 0 || ch == '_';
}

std::string trim(std::string_view sv)
{
    std::size_t start = 0;
    while (start < sv.size() && std::isspace(static_cast<unsigned char>(sv[start])) != 0)
        ++start;
    std::size_t end = sv.size();
    while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1])) != 0)
        --end;
    return std::string(sv.substr(start, end - start));
}

// Preserves line numbers (`\n`) while blanking comments and string/char literal bodies.
std::string strip_comments_preserving_lines(std::string_view src, bool preserveStringLiterals)
{
    std::string out;
    out.reserve(src.size());
    std::size_t i = 0;
    while (i < src.size())
    {
        // Raw string literal R"delim(...)delim"
        if (src[i] == 'R' && i + 1 < src.size() && src[i + 1] == '"')
        {
            std::size_t delimEnd = i + 2;
            while (delimEnd < src.size() && src[delimEnd] != '(' && src[delimEnd] != '\n')
                ++delimEnd;
            if (delimEnd < src.size() && src[delimEnd] == '(')
            {
                const std::string_view delim = src.substr(i + 2, delimEnd - (i + 2));
                const std::string closing = ")" + std::string(delim) + "\"";
                const std::size_t endPos = src.find(closing, delimEnd + 1);
                const std::size_t finish = (endPos == std::string_view::npos) ? src.size() : endPos + closing.size();
                if (preserveStringLiterals)
                {
                    out.append(src.substr(i, finish - i));
                }
                else
                {
                    for (std::size_t k = i; k < finish; ++k)
                        out.push_back(src[k] == '\n' ? '\n' : ' ');
                }
                i = finish;
                continue;
            }
        }
        if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '/')
        {
            i += 2;
            while (i < src.size() && src[i] != '\n')
                ++i;
            continue;
        }
        if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/'))
            {
                if (src[i] == '\n')
                    out.push_back('\n');
                ++i;
            }
            if (i + 1 < src.size())
                i += 2;
            else
                i = src.size();
            out.push_back(' ');
            continue;
        }
        if (src[i] == '"' || src[i] == '\'')
        {
            const char quote = src[i];
            const std::size_t start = i++;
            while (i < src.size())
            {
                if (src[i] == '\\' && i + 1 < src.size())
                {
                    i += 2;
                    continue;
                }
                if (src[i] == quote)
                {
                    ++i;
                    break;
                }
                if (src[i] == '\n')
                    break;
                ++i;
            }
            if (preserveStringLiterals)
                out.append(src.substr(start, i - start));
            else
            {
                out.push_back(quote);
                for (std::size_t k = start + 1; k + 1 < i; ++k)
                    out.push_back(' ');
                if (i > start + 1 && src[i - 1] == quote)
                    out.push_back(quote);
            }
            continue;
        }
        out.push_back(src[i++]);
    }
    return out;
}

int line_at_offset(std::string_view text, std::size_t offset) noexcept
{
    int line = 1;
    const std::size_t limit = std::min(offset, text.size());
    for (std::size_t i = 0; i < limit; ++i)
    {
        if (text[i] == '\n')
            ++line;
    }
    return line;
}

struct ParsedMemberField
{
    std::string name;
    std::string cppType;
    std::string initializer;
    int line = 0;
};

struct ParsedClassInfo
{
    std::string name;
    std::vector<std::string> baseClasses;
    std::filesystem::path filePath;
    int line = 0;
    std::vector<ParsedMemberField> members;
};

struct ParsedMacroCall
{
    std::string typeName;
    std::vector<std::string> fieldNames;
    std::filesystem::path filePath;
    int line = 0;
};

struct RawFileParse
{
    std::map<std::string, std::vector<SourceEnumValue>> enums;
    std::vector<ParsedClassInfo> classes;
    std::vector<ParsedMacroCall> macros;
    std::vector<SourceScannerDiagnostic> diagnostics;
};

std::map<std::string, std::vector<SourceEnumValue>> builtin_enums()
{
    return {
        {"BodyType", {{"DYNAMIC", 0}, {"KINEMATIC", 1}, {"STATIC", 2}}},
        {"SDL_FlipMode",
         {{"SDL_FLIP_NONE", 0}, {"SDL_FLIP_HORIZONTAL", 1}, {"SDL_FLIP_VERTICAL", 2}}},
    };
}

std::optional<std::int64_t> parse_integer_literal(std::string_view text)
{
    std::string s = trim(text);
    if (s.empty())
        return std::nullopt;
    while (!s.empty() && (s.back() == 'u' || s.back() == 'U' || s.back() == 'l' || s.back() == 'L'))
        s.pop_back();
    if (s.empty())
        return std::nullopt;
    try
    {
        std::size_t consumed = 0;
        const auto val = std::stoll(s, &consumed, 0);
        if (consumed == s.size())
            return static_cast<std::int64_t>(val);
    }
    catch (...)
    {
    }
    return std::nullopt;
}

std::optional<double> parse_float_literal(std::string_view text)
{
    std::string s = trim(text);
    if (s.empty())
        return std::nullopt;
    if (s.back() == 'f' || s.back() == 'F')
        s.pop_back();
    if (s.empty())
        return std::nullopt;
    try
    {
        std::size_t consumed = 0;
        const double val = std::stod(s, &consumed);
        if (consumed == s.size())
            return val;
    }
    catch (...)
    {
    }
    return std::nullopt;
}

std::string strip_namespace_qualifiers(std::string_view typeName)
{
    const auto pos = typeName.rfind("::");
    return pos == std::string_view::npos ? std::string(typeName) : std::string(typeName.substr(pos + 2));
}

std::vector<std::string> split_top_level(std::string_view text, char delimiter)
{
    std::vector<std::string> parts;
    int angleDepth = 0;
    int parenDepth = 0;
    int braceDepth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char ch = text[i];
        if (ch == '<')
            ++angleDepth;
        else if (ch == '>' && angleDepth > 0)
            --angleDepth;
        else if (ch == '(')
            ++parenDepth;
        else if (ch == ')' && parenDepth > 0)
            --parenDepth;
        else if (ch == '{')
            ++braceDepth;
        else if (ch == '}' && braceDepth > 0)
            --braceDepth;
        else if (ch == delimiter && angleDepth == 0 && parenDepth == 0 && braceDepth == 0)
        {
            parts.push_back(trim(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    parts.push_back(trim(text.substr(start)));
    return parts;
}

std::size_t find_matching_brace(std::string_view text, std::size_t openPos) noexcept
{
    int depth = 0;
    for (std::size_t i = openPos; i < text.size(); ++i)
    {
        if (text[i] == '{')
            ++depth;
        else if (text[i] == '}')
        {
            --depth;
            if (depth == 0)
                return i;
        }
    }
    return std::string_view::npos;
}

std::size_t find_matching_paren(std::string_view text, std::size_t openPos) noexcept
{
    int depth = 0;
    for (std::size_t i = openPos; i < text.size(); ++i)
    {
        if (text[i] == '(')
            ++depth;
        else if (text[i] == ')')
        {
            --depth;
            if (depth == 0)
                return i;
        }
    }
    return std::string_view::npos;
}

void parse_enums_in_source(std::string_view stripped, RawFileParse &out)
{
    std::size_t pos = 0;
    while (pos < stripped.size())
    {
        const std::size_t found = stripped.find("enum", pos);
        if (found == std::string_view::npos)
            break;
        if (found > 0 && is_ident_char(stripped[found - 1]))
        {
            pos = found + 4;
            continue;
        }
        std::size_t cur = found + 4;
        if (cur < stripped.size() && is_ident_char(stripped[cur]))
        {
            pos = cur;
            continue;
        }
        auto skip_ws = [&]() {
            while (cur < stripped.size() && std::isspace(static_cast<unsigned char>(stripped[cur])) != 0)
                ++cur;
        };
        skip_ws();
        if (stripped.substr(cur, 5) == "class" &&
            (cur + 5 == stripped.size() || !is_ident_char(stripped[cur + 5])))
        {
            cur += 5;
            skip_ws();
        }
        else if (stripped.substr(cur, 6) == "struct" &&
                 (cur + 6 == stripped.size() || !is_ident_char(stripped[cur + 6])))
        {
            cur += 6;
            skip_ws();
        }
        const std::size_t nameStart = cur;
        while (cur < stripped.size() && is_ident_char(stripped[cur]))
            ++cur;
        const std::string enumName(stripped.substr(nameStart, cur - nameStart));
        if (enumName.empty())
        {
            pos = cur;
            continue;
        }
        const std::size_t bracePos = stripped.find('{', cur);
        const std::size_t semiBeforeBrace = stripped.find(';', cur);
        if (bracePos == std::string_view::npos ||
            (semiBeforeBrace != std::string_view::npos && semiBeforeBrace < bracePos))
        {
            pos = cur;
            continue;
        }
        const std::size_t closePos = find_matching_brace(stripped, bracePos);
        if (closePos == std::string_view::npos)
            break;

        const std::string_view body = stripped.substr(bracePos + 1, closePos - bracePos - 1);
        std::vector<SourceEnumValue> values;
        std::int64_t nextValue = 0;
        for (const std::string &entry : split_top_level(body, ','))
        {
            if (entry.empty())
                continue;
            const auto eqPos = entry.find('=');
            const std::string id = trim(eqPos == std::string::npos ? entry : entry.substr(0, eqPos));
            if (id.empty() || !is_ident_start(id.front()))
                continue;
            if (eqPos != std::string::npos)
            {
                if (const auto parsed = parse_integer_literal(entry.substr(eqPos + 1)))
                    nextValue = *parsed;
            }
            values.push_back({id, nextValue});
            ++nextValue;
        }
        if (!values.empty())
            out.enums[enumName] = std::move(values);
        pos = closePos + 1;
    }
}

std::vector<ParsedMemberField> parse_class_members(std::string_view rawWithStrings,
                                                   std::size_t bodyStartOffset,
                                                   std::string_view strippedBody)
{
    std::vector<ParsedMemberField> members;
    std::size_t i = 0;
    int braceDepth = 0;
    std::size_t stmtStart = 0;
    bool topLevelBraceInitializer = false;

    while (i < strippedBody.size())
    {
        const char ch = strippedBody[i];
        if (ch == '{')
        {
            if (braceDepth == 0)
            {
                const std::string prefix = trim(strippedBody.substr(stmtStart, i - stmtStart));
                int angle = 0;
                bool hasTopLevelParen = false;
                for (const char prefixChar : prefix)
                {
                    if (prefixChar == '<')
                        ++angle;
                    else if (prefixChar == '>' && angle > 0)
                        --angle;
                    else if (prefixChar == '(' && angle == 0)
                    {
                        hasTopLevelParen = true;
                        break;
                    }
                }
                const bool declarationBlock = prefix.starts_with("struct ") ||
                    prefix.starts_with("class ") || prefix.starts_with("union ") ||
                    prefix.starts_with("enum ");
                topLevelBraceInitializer = !prefix.empty() && !hasTopLevelParen &&
                    !declarationBlock;
            }
            ++braceDepth;
        }
        else if (ch == '}')
        {
            if (braceDepth > 0)
                --braceDepth;
            if (braceDepth == 0)
            {
                if (!topLevelBraceInitializer)
                    stmtStart = i + 1;
                topLevelBraceInitializer = false;
            }
        }
        else if (ch == ';' && braceDepth == 0)
        {
            std::string_view rawStmt = rawWithStrings.substr(bodyStartOffset + stmtStart, i - stmtStart);
            std::string stmt = trim(strippedBody.substr(stmtStart, i - stmtStart));
            const int stmtLine = line_at_offset(rawWithStrings, bodyStartOffset + stmtStart);
            stmtStart = i + 1;

            // Strip leading access specifiers (public:, protected:, private:)
            for (const char *spec : {"public:", "protected:", "private:"})
            {
                const auto pos = stmt.rfind(spec);
                if (pos != std::string::npos && pos < 16)
                {
                    stmt = trim(stmt.substr(pos + std::strlen(spec)));
                    if (const auto rawSpec = rawStmt.rfind(spec); rawSpec != std::string_view::npos)
                        rawStmt = rawStmt.substr(rawSpec + std::strlen(spec));
                }
            }
            if (stmt.empty() || stmt.front() == '#' ||
                stmt.starts_with("using ") || stmt.starts_with("typedef ") ||
                stmt.starts_with("friend ") || stmt.starts_with("static ") ||
                stmt.starts_with("virtual ") || stmt.starts_with("constexpr "))
            {
                ++i;
                continue;
            }

            // Skip function declarations / method signatures (has top-level '(' before '=' or '{')
            bool hasTopLevelParen = false;
            int angle = 0;
            for (std::size_t k = 0; k < stmt.size(); ++k)
            {
                if (stmt[k] == '<')
                    ++angle;
                else if (stmt[k] == '>' && angle > 0)
                    --angle;
                else if ((stmt[k] == '=' || stmt[k] == '{') && angle == 0)
                    break;
                else if (stmt[k] == '(' && angle == 0)
                {
                    hasTopLevelParen = true;
                    break;
                }
            }
            if (hasTopLevelParen)
            {
                ++i;
                continue;
            }

            // Split initializer on first top-level '=' or '{'
            std::size_t initPos = std::string::npos;
            bool braceInit = false;
            angle = 0;
            for (std::size_t k = 0; k < stmt.size(); ++k)
            {
                if (stmt[k] == '<')
                    ++angle;
                else if (stmt[k] == '>' && angle > 0)
                    --angle;
                else if (angle == 0 && (stmt[k] == '=' || stmt[k] == '{'))
                {
                    initPos = k;
                    braceInit = (stmt[k] == '{');
                    break;
                }
            }

            const std::string declPart = trim(initPos == std::string::npos ? stmt : stmt.substr(0, initPos));
            std::string initPart;
            if (initPos != std::string::npos)
            {
                // Extract initializer from rawStmt so string defaults are preserved!
                std::size_t rawInitPos = rawStmt.find(braceInit ? '{' : '=');
                if (rawInitPos != std::string_view::npos)
                {
                    if (!braceInit)
                        ++rawInitPos;
                    initPart = trim(rawStmt.substr(rawInitPos));
                }
            }

            // Last identifier in declPart is field name; everything before is cppType
            if (declPart.empty())
            {
                ++i;
                continue;
            }
            std::size_t end = declPart.size();
            while (end > 0 && std::isspace(static_cast<unsigned char>(declPart[end - 1])) != 0)
                --end;
            std::size_t start = end;
            while (start > 0 && is_ident_char(declPart[start - 1]))
                --start;
            if (start == 0 || start == end)
            {
                ++i;
                continue;
            }
            const std::string fieldName = declPart.substr(start, end - start);
            const std::string cppType = trim(declPart.substr(0, start));
            if (!cppType.empty() && is_ident_start(fieldName.front()))
            {
                members.push_back({fieldName, cppType, initPart, stmtLine});
            }
        }
        ++i;
    }
    return members;
}

void parse_classes_in_source(const std::filesystem::path &filePath,
                             std::string_view rawWithStrings,
                             std::string_view stripped,
                             RawFileParse &out)
{
    std::size_t pos = 0;
    while (pos < stripped.size())
    {
        const std::size_t classPos = stripped.find("class", pos);
        const std::size_t structPos = stripped.find("struct", pos);
        std::size_t kwPos = std::string_view::npos;
        std::size_t kwLen = 0;
        if (classPos != std::string_view::npos &&
            (structPos == std::string_view::npos || classPos < structPos))
        {
            kwPos = classPos;
            kwLen = 5;
        }
        else if (structPos != std::string_view::npos)
        {
            kwPos = structPos;
            kwLen = 6;
        }
        if (kwPos == std::string_view::npos)
            break;
        if (kwPos > 0 && is_ident_char(stripped[kwPos - 1]))
        {
            pos = kwPos + kwLen;
            continue;
        }
        if (kwPos + kwLen < stripped.size() && is_ident_char(stripped[kwPos + kwLen]))
        {
            pos = kwPos + kwLen;
            continue;
        }

        // Ignore `enum class` / `enum struct`
        std::size_t back = kwPos;
        while (back > 0 && std::isspace(static_cast<unsigned char>(stripped[back - 1])) != 0)
            --back;
        if (back >= 4 && stripped.substr(back - 4, 4) == "enum" &&
            (back == 4 || !is_ident_char(stripped[back - 5])))
        {
            pos = kwPos + kwLen;
            continue;
        }

        std::size_t cur = kwPos + kwLen;
        while (cur < stripped.size() && std::isspace(static_cast<unsigned char>(stripped[cur])) != 0)
            ++cur;
        const std::size_t nameStart = cur;
        while (cur < stripped.size() && is_ident_char(stripped[cur]))
            ++cur;
        const std::string className(stripped.substr(nameStart, cur - nameStart));
        if (className.empty())
        {
            pos = cur;
            continue;
        }

        // Check inheritance list before '{', abort if ';' appears first (forward decl)
        const std::size_t bracePos = stripped.find('{', cur);
        const std::size_t semiPos = stripped.find(';', cur);
        if (bracePos == std::string_view::npos ||
            (semiPos != std::string_view::npos && semiPos < bracePos))
        {
            pos = cur;
            continue;
        }
        const std::size_t closePos = find_matching_brace(stripped, bracePos);
        if (closePos == std::string_view::npos)
            break;

        ParsedClassInfo info;
        info.name = className;
        info.filePath = filePath;
        info.line = line_at_offset(rawWithStrings, kwPos);

        const std::string_view headerTail = stripped.substr(cur, bracePos - cur);
        if (const std::size_t colonPos = headerTail.find(':'); colonPos != std::string_view::npos)
        {
            for (const std::string &baseEntry : split_top_level(headerTail.substr(colonPos + 1), ','))
            {
                std::string cleaned = baseEntry;
                for (const char *acc : {"public ", "protected ", "private ", "virtual "})
                {
                    std::size_t p = cleaned.find(acc);
                    while (p != std::string::npos)
                    {
                        cleaned.erase(p, std::strlen(acc));
                        p = cleaned.find(acc);
                    }
                }
                cleaned = strip_namespace_qualifiers(trim(cleaned));
                if (!cleaned.empty())
                    info.baseClasses.push_back(std::move(cleaned));
            }
        }

        info.members = parse_class_members(
            rawWithStrings, bracePos + 1, stripped.substr(bracePos + 1, closePos - bracePos - 1));
        out.classes.push_back(std::move(info));
        pos = bracePos + 1; // continue inside to catch nested structs/classes if any
    }
}

void parse_serializable_script_macros(const std::filesystem::path &filePath,
                                      std::string_view rawWithStrings,
                                      std::string_view stripped,
                                      RawFileParse &out)
{
    constexpr std::string_view Macro = "SERIALIZABLE_SCRIPT";
    std::size_t pos = 0;
    while (pos < stripped.size())
    {
        const std::size_t found = stripped.find(Macro, pos);
        if (found == std::string_view::npos)
            break;
        if (found > 0 && is_ident_char(stripped[found - 1]))
        {
            pos = found + Macro.size();
            continue;
        }
        std::size_t cur = found + Macro.size();
        while (cur < stripped.size() && std::isspace(static_cast<unsigned char>(stripped[cur])) != 0)
            ++cur;
        if (cur >= stripped.size() || stripped[cur] != '(')
        {
            pos = cur;
            continue;
        }
        const std::size_t closeParen = find_matching_paren(stripped, cur);
        if (closeParen == std::string_view::npos)
        {
            const std::size_t argsEnd = stripped.find(',', cur + 1);
            const std::string_view firstArgument = stripped.substr(
                cur + 1, (argsEnd == std::string_view::npos ? stripped.size() : argsEnd) - (cur + 1));
            const std::string typeName = strip_namespace_qualifiers(trim(firstArgument));
            out.diagnostics.push_back({
                typeName, {}, filePath, line_at_offset(rawWithStrings, found),
                "SERIALIZABLE_SCRIPT call is incomplete; waiting for its closing parenthesis."});
            pos = found + Macro.size();
            continue;
        }

        const std::vector<std::string> args = split_top_level(
            stripped.substr(cur + 1, closeParen - cur - 1), ',');
        if (!args.empty() && !args.front().empty())
        {
            ParsedMacroCall call;
            call.typeName = strip_namespace_qualifiers(args.front());
            call.filePath = filePath;
            call.line = line_at_offset(rawWithStrings, found);
            for (std::size_t idx = 1; idx < args.size(); ++idx)
            {
                if (!args[idx].empty())
                    call.fieldNames.push_back(args[idx]);
            }
            out.macros.push_back(std::move(call));
        }
        pos = closeParen + 1;
    }
}

RawFileParse parse_single_file(const std::filesystem::path &path, const std::string &contents)
{
    RawFileParse parsed;
    const std::string rawWithStrings = strip_comments_preserving_lines(contents, true);
    const std::string stripped = strip_comments_preserving_lines(contents, false);
    parse_enums_in_source(stripped, parsed);
    parse_classes_in_source(path, rawWithStrings, stripped, parsed);
    parse_serializable_script_macros(path, rawWithStrings, stripped, parsed);
    return parsed;
}

std::string normalize_cpp_type(std::string_view rawType)
{
    std::string out = trim(rawType);
    for (const char *qual : {"const ", "volatile ", "mutable "})
    {
        if (out.starts_with(qual))
            out = trim(out.substr(std::strlen(qual)));
    }
    return out;
}

std::optional<std::string> extract_template_arg(std::string_view typeStr, std::string_view templateName)
{
    const std::string qualified = "std::" + std::string(templateName);
    std::string_view rest;
    if (typeStr.starts_with(qualified))
        rest = typeStr.substr(qualified.size());
    else if (typeStr.starts_with(templateName))
        rest = typeStr.substr(templateName.size());
    else
        return std::nullopt;

    std::size_t open = 0;
    while (open < rest.size() && std::isspace(static_cast<unsigned char>(rest[open])) != 0)
        ++open;
    if (open >= rest.size() || rest[open] != '<')
        return std::nullopt;
    const std::size_t close = rest.rfind('>');
    if (close == std::string_view::npos || close <= open)
        return std::nullopt;
    return trim(rest.substr(open + 1, close - open - 1));
}

std::optional<Json> parse_default_initializer(const SourceBehaviorFieldDescriptor &field,
                                              std::string_view rawInit)
{
    std::string s = trim(rawInit);
    if (s.size() >= 2 && s.front() == '{' && s.back() == '}')
        s = trim(s.substr(1, s.size() - 2));

    if (s.empty())
    {
        if (field.kind == "optional")
            return Json(nullptr);
        if (field.kind == "array")
            return Json::array();
        return std::nullopt;
    }

    if (field.kind == "boolean")
    {
        if (s == "true")
            return Json(true);
        if (s == "false")
            return Json(false);
    }
    else if (field.kind == "signed_integer")
    {
        if (const auto val = parse_integer_literal(s))
            return Json(*val);
    }
    else if (field.kind == "unsigned_integer")
    {
        if (const auto val = parse_integer_literal(s))
            return Json(static_cast<std::uint64_t>(std::max<std::int64_t>(0, *val)));
    }
    else if (field.kind == "number")
    {
        if (const auto val = parse_float_literal(s))
            return Json(*val);
    }
    else if (field.kind == "string")
    {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return Json(s.substr(1, s.size() - 2));
    }
    else if (field.kind == "vector2")
    {
        const auto parts = split_top_level(s, ',');
        if (parts.size() == 2)
        {
            const auto x = parse_float_literal(parts[0]);
            const auto y = parse_float_literal(parts[1]);
            if (x && y)
                return Json{{"x", *x}, {"y", *y}};
        }
    }
    else if (field.kind == "rectangle")
    {
        const auto parts = split_top_level(s, ',');
        if (parts.size() == 4)
        {
            const auto x = parse_float_literal(parts[0]);
            const auto y = parse_float_literal(parts[1]);
            const auto w = parse_float_literal(parts[2]);
            const auto h = parse_float_literal(parts[3]);
            if (x && y && w && h)
                return Json{{"x", *x}, {"y", *y}, {"w", *w}, {"h", *h}};
        }
    }
    else if (field.kind == "object_reference")
    {
        if (s == "nullptr" || s == "null")
            return Json(nullptr);
        if (const auto value = parse_integer_literal(s); value && *value >= 0)
            return Json(*value);
    }
    else if (field.kind == "asset_reference")
    {
        if (s == "nullptr" || s == "null")
            return Json(nullptr);
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return Json(s.substr(1, s.size() - 2));
    }
    else if (field.kind == "array" && field.element)
    {
        Json result = Json::array();
        for (const auto &part : split_top_level(s, ','))
        {
            const auto element = parse_default_initializer(*field.element, part);
            if (!element)
                return std::nullopt;
            result.push_back(*element);
        }
        return result;
    }
    else if (field.kind == "optional" && field.element)
    {
        if (s == "std::nullopt" || s == "nullopt" || s == "nullptr")
            return Json(nullptr);
        const auto value = parse_default_initializer(*field.element, s);
        if (value)
            return value;
    }
    else if (field.kind == "enum")
    {
        const std::string shortName = strip_namespace_qualifiers(s);
        for (const auto &ev : field.enumValues)
        {
            if (ev.name == shortName)
                return Json(ev.value);
        }
        if (const auto val = parse_integer_literal(s))
            return Json(*val);
    }
    return std::nullopt;
}

void resolve_field_type(const std::string &rawCppType,
                        const std::map<std::string, std::vector<SourceEnumValue>> &enums,
                        SourceBehaviorFieldDescriptor &out)
{
    const std::string t = normalize_cpp_type(rawCppType);
    out.cppType = t;
    const std::string unqualified = strip_namespace_qualifiers(t);

    if (t == "bool")
    {
        out.kind = "boolean";
        return;
    }
    if (t == "int" || t == "short" || t == "long" || t == "long long" ||
        t == "int8_t" || t == "int16_t" || t == "int32_t" || t == "int64_t" ||
        t == "std::int8_t" || t == "std::int16_t" || t == "std::int32_t" || t == "std::int64_t")
    {
        out.kind = "signed_integer";
        return;
    }
    if (t == "unsigned" || t == "unsigned int" || t == "unsigned long" || t == "unsigned long long" ||
        t == "size_t" || t == "std::size_t" ||
        t == "uint8_t" || t == "uint16_t" || t == "uint32_t" || t == "uint64_t" ||
        t == "std::uint8_t" || t == "std::uint16_t" || t == "std::uint32_t" || t == "std::uint64_t")
    {
        out.kind = "unsigned_integer";
        return;
    }
    if (t == "float" || t == "double")
    {
        out.kind = "number";
        return;
    }
    if (t == "std::string" || t == "string")
    {
        out.kind = "string";
        return;
    }
    if (t == "Eigen::Vector2f" || t == "Vector2f")
    {
        out.kind = "vector2";
        return;
    }
    if (t == "SDL_FRect")
    {
        out.kind = "rectangle";
        return;
    }
    if (t == "AnimationClipRef" || t == "platformator::AnimationClipRef")
    {
        out.kind = "asset_reference";
        out.targetType = "AnimationClip";
        out.nullable = true;
        return;
    }
    if (t == "AudioAssetRef" || t == "platformator::AudioAssetRef")
    {
        out.kind = "asset_reference";
        out.targetType = "Audio";
        out.nullable = true;
        return;
    }
    if (t == "TextureAssetRef" || t == "platformator::TextureAssetRef")
    {
        out.kind = "asset_reference";
        out.targetType = "Texture";
        out.nullable = true;
        return;
    }
    if (const auto assetArg = extract_template_arg(t, "AssetRef"))
    {
        out.kind = "asset_reference";
        out.targetType = strip_namespace_qualifiers(*assetArg);
        out.nullable = true;
        return;
    }
    auto objectReferenceArg = extract_template_arg(t, "ObjectReference");
    if (!objectReferenceArg)
        objectReferenceArg = extract_template_arg(t, "ObjectRef");
    if (objectReferenceArg)
    {
        out.kind = "object_reference";
        out.targetType = strip_namespace_qualifiers(*objectReferenceArg);
        out.nullable = true;
        return;
    }
    if (t.ends_with('*'))
    {
        const std::string ptrTarget = strip_namespace_qualifiers(trim(t.substr(0, t.size() - 1)));
        out.kind = "object_reference";
        out.targetType = ptrTarget.empty() ? "GameObject" : ptrTarget;
        out.nullable = true;
        return;
    }
    if (const auto vecArg = extract_template_arg(t, "vector"))
    {
        out.kind = "array";
        out.element = std::make_shared<SourceBehaviorFieldDescriptor>();
        resolve_field_type(*vecArg, enums, *out.element);
        if (!out.element->resolved)
        {
            out.resolved = false;
            out.diagnostic = out.element->diagnostic;
        }
        return;
    }
    if (const auto optArg = extract_template_arg(t, "optional"))
    {
        out.kind = "optional";
        out.nullable = true;
        out.element = std::make_shared<SourceBehaviorFieldDescriptor>();
        resolve_field_type(*optArg, enums, *out.element);
        if (!out.element->resolved)
        {
            out.resolved = false;
            out.diagnostic = out.element->diagnostic;
        }
        return;
    }
    if (const auto enumIt = enums.find(unqualified); enumIt != enums.end())
    {
        out.kind = "enum";
        out.targetType = unqualified;
        out.enumValues = enumIt->second;
        return;
    }

    out.kind = "opaque_json";
    out.resolved = false;
    out.diagnostic = "Unrecognized field C++ type '" + t + "'";
}

Json field_to_catalog_json(const SourceBehaviorFieldDescriptor &field)
{
    Json result{{"name", field.name.empty() ? "element" : field.name}, {"kind", field.kind}};
    if (!field.targetType.empty())
        result["target"] = field.targetType;
    if (field.nullable)
        result["nullable"] = true;
    if (!field.enumValues.empty())
    {
        result["enum_values"] = Json::array();
        for (const auto &ev : field.enumValues)
            result["enum_values"].push_back({{"name", ev.name}, {"value", ev.value}});
    }
    if (field.defaultValue)
        result["default_value"] = *field.defaultValue;
    if (field.element)
        result["element"] = field_to_catalog_json(*field.element);
    if (!field.resolved && !field.diagnostic.empty())
        result["scanner_warning"] = field.diagnostic;
    return result;
}
} // namespace

struct CachedFileParse
{
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified{};
    RawFileParse parsed;
    std::string diagnostic;
};

struct SourceBehaviorScanner::Impl
{
    std::unordered_map<std::string, CachedFileParse> fileCache;
};

Json SourceScanResult::to_catalog_json() const
{
    Json behaviorsJson = Json::array();
    for (const auto &[typeName, desc] : behaviors)
    {
        Json fieldsJson = Json::array();
        for (const auto &field : desc.fields)
            fieldsJson.push_back(field_to_catalog_json(field));

        Json entry{{"name", desc.typeName},
                   {"field_schema_available", true},
                   {"fields", std::move(fieldsJson)}};
        if (!desc.headerPath.empty())
            entry["header_path"] = desc.headerPath.generic_string();
        if (!desc.sourcePath.empty())
            entry["source_path"] = desc.sourcePath.generic_string();
        if (desc.declarationLine > 0)
            entry["declaration_line"] = desc.declarationLine;
        behaviorsJson.push_back(std::move(entry));
    }
    return {{"schema_version", 1}, {"behaviors", std::move(behaviorsJson)}};
}

SourceBehaviorScanner::SourceBehaviorScanner() : impl_(std::make_unique<Impl>())
{
}

SourceBehaviorScanner::~SourceBehaviorScanner() = default;

void SourceBehaviorScanner::clear_cache() noexcept
{
    impl_->fileCache.clear();
}

std::size_t SourceBehaviorScanner::cache_size() const noexcept
{
    return impl_->fileCache.size();
}

SourceScanResult SourceBehaviorScanner::scan_sources(
    const std::vector<std::pair<std::filesystem::path, std::string>> &files)
{
    SourceScanResult result;
    result.enums = builtin_enums();

    std::map<std::string, ParsedClassInfo> classesByName;
    std::vector<ParsedMacroCall> allMacros;

    for (const auto &[path, contents] : files)
    {
        ++result.scannedFileCount;
        RawFileParse parsed = parse_single_file(path, contents);
        result.diagnostics.insert(result.diagnostics.end(), parsed.diagnostics.begin(),
                                  parsed.diagnostics.end());
        for (auto &[enumName, enumVals] : parsed.enums)
            result.enums[enumName] = std::move(enumVals);
        for (auto &cls : parsed.classes)
            classesByName[cls.name] = std::move(cls);
        allMacros.insert(allMacros.end(), parsed.macros.begin(), parsed.macros.end());
    }

    // Also include ScriptComponent subclasses that didn't emit SERIALIZABLE_SCRIPT (zero fields)
    auto is_script_subclass = [&](const std::string &startName) -> std::string {
        std::unordered_set<std::string> visited;
        std::vector<std::string> queue{startName};
        while (!queue.empty())
        {
            const std::string cur = std::move(queue.back());
            queue.pop_back();
            if (!visited.insert(cur).second)
                continue;
            const auto it = classesByName.find(cur);
            if (it == classesByName.end())
                continue;
            for (const auto &base : it->second.baseClasses)
            {
                if (base == "ScriptComponent" || base == "Behavior")
                    return base;
                queue.push_back(base);
            }
        }
        return {};
    };

    std::unordered_map<std::string, ParsedMacroCall> macrosByType;
    for (auto &macro : allMacros)
        macrosByType[macro.typeName] = std::move(macro);

    for (const auto &[typeName, cls] : classesByName)
    {
        const std::string scriptBase = is_script_subclass(typeName);
        if (scriptBase.empty() && macrosByType.find(typeName) == macrosByType.end())
            continue;

        SourceBehaviorDescriptor desc;
        desc.typeName = typeName;
        desc.baseClass = scriptBase.empty() ? "ScriptComponent" : scriptBase;
        desc.headerPath = cls.filePath;
        desc.declarationLine = cls.line;

        if (const auto macroIt = macrosByType.find(typeName); macroIt != macrosByType.end())
        {
            desc.sourcePath = macroIt->second.filePath;
            for (const std::string &fieldName : macroIt->second.fieldNames)
            {
                const ParsedMemberField *matchedMember = nullptr;
                for (const auto &member : cls.members)
                {
                    if (member.name == fieldName)
                    {
                        matchedMember = &member;
                        break;
                    }
                }
                SourceBehaviorFieldDescriptor field;
                field.name = fieldName;
                if (matchedMember == nullptr)
                {
                    field.resolved = false;
                    field.diagnostic = "Field '" + fieldName + "' listed in SERIALIZABLE_SCRIPT was not found on " + typeName;
                    desc.diagnostics.push_back(
                        {typeName, fieldName, macroIt->second.filePath, macroIt->second.line, field.diagnostic});
                    result.diagnostics.push_back(desc.diagnostics.back());
                }
                else
                {
                    field.lineNumber = matchedMember->line;
                    resolve_field_type(matchedMember->cppType, result.enums, field);
                    field.defaultValue = parse_default_initializer(field, matchedMember->initializer);
                    if (!field.resolved)
                    {
                        desc.diagnostics.push_back(
                            {typeName, fieldName, cls.filePath, matchedMember->line, field.diagnostic});
                        result.diagnostics.push_back(desc.diagnostics.back());
                    }
                }
                desc.fields.push_back(std::move(field));
            }
        }

        result.behaviors[typeName] = std::move(desc);
    }

    // Handle SERIALIZABLE_SCRIPT macros whose class declaration was not found
    for (const auto &[typeName, macro] : macrosByType)
    {
        if (result.behaviors.find(typeName) != result.behaviors.end())
            continue;
        SourceBehaviorDescriptor desc;
        desc.typeName = typeName;
        desc.sourcePath = macro.filePath;
        desc.declarationLine = macro.line;
        for (const std::string &fieldName : macro.fieldNames)
        {
            SourceBehaviorFieldDescriptor field;
            field.name = fieldName;
            field.resolved = false;
            field.diagnostic = "Class declaration for '" + typeName + "' not found in scanned project sources";
            desc.diagnostics.push_back({typeName, fieldName, macro.filePath, macro.line, field.diagnostic});
            result.diagnostics.push_back(desc.diagnostics.back());
            desc.fields.push_back(std::move(field));
        }
        result.behaviors[typeName] = std::move(desc);
    }

    return result;
}

SourceScanResult SourceBehaviorScanner::scan_project(const std::filesystem::path &projectRoot,
                                                     const std::filesystem::path &assetRoot,
                                                     const std::atomic_bool *cancelled)
{
    constexpr std::size_t maximumSourceFiles = 10'000;
    constexpr std::uintmax_t maximumSourceBytes = 256U * 1024U * 1024U;
    std::vector<std::filesystem::path> scanRoots;
    if (!projectRoot.empty())
        scanRoots.push_back(projectRoot);
    if (!assetRoot.empty() && assetRoot != projectRoot)
        scanRoots.push_back(assetRoot);

    std::vector<std::pair<std::filesystem::path, std::string>> files;
    std::unordered_set<std::string> seenCanonical;
    std::size_t cacheHits = 0;
    std::uintmax_t totalSourceBytes = 0;

    auto is_cpp_file = [](const std::filesystem::path &p) {
        const auto ext = p.extension().string();
        return ext == ".h" || ext == ".hpp" || ext == ".hh" ||
               ext == ".cpp" || ext == ".cc" || ext == ".cxx";
    };

    SourceScanResult result;
    result.enums = builtin_enums();
    std::map<std::string, ParsedClassInfo> classesByName;
    std::vector<ParsedMacroCall> allMacros;

    for (const auto &root : scanRoots)
    {
        if (cancelled != nullptr && cancelled->load(std::memory_order_acquire))
        {
            result.cancelled = true;
            return result;
        }
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec) || ec)
            continue;
        std::filesystem::recursive_directory_iterator it(
            root, std::filesystem::directory_options::skip_permission_denied, ec);
        for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        {
            if (cancelled != nullptr && cancelled->load(std::memory_order_acquire))
            {
                result.cancelled = true;
                return result;
            }
            const auto &entry = *it;
            if (entry.is_directory(ec))
            {
                if (should_skip_directory(entry.path().filename().string()))
                    it.disable_recursion_pending();
                continue;
            }
            if (!entry.is_regular_file(ec) || !is_cpp_file(entry.path()))
                continue;

            const std::filesystem::path relPath = entry.path().lexically_relative(root);
            const std::string cacheKey = entry.path().lexically_normal().generic_string();
            if (!seenCanonical.insert(cacheKey).second)
                continue;

            const auto size = entry.file_size(ec);
            const auto mtime = entry.last_write_time(ec);
            if (ec || size > 2U * 1024U * 1024U)
                continue;

            if (result.scannedFileCount >= maximumSourceFiles ||
                size > maximumSourceBytes - std::min(maximumSourceBytes, totalSourceBytes))
            {
                result.truncated = true;
                return result;
            }
            totalSourceBytes += size;

            ++result.scannedFileCount;
            auto cachedIt = impl_->fileCache.find(cacheKey);
            if (cachedIt != impl_->fileCache.end() &&
                cachedIt->second.size == size &&
                cachedIt->second.modified == mtime)
            {
                ++cacheHits;
            }
            else
            {
                std::ifstream stream(entry.path(), std::ios::binary);
                if (!stream)
                    continue;
                std::ostringstream ss;
                ss << stream.rdbuf();
                if (cancelled != nullptr && cancelled->load(std::memory_order_acquire))
                {
                    result.cancelled = true;
                    return result;
                }
                std::string contents = ss.str();
                const std::string stripped = strip_comments_preserving_lines(contents, false);
                int braceDepth = 0;
                bool unbalanced = false;
                for (const char ch : stripped)
                {
                    if (ch == '{')
                        ++braceDepth;
                    else if (ch == '}' && --braceDepth < 0)
                    {
                        unbalanced = true;
                        break;
                    }
                }
                if (braceDepth != 0)
                    unbalanced = true;
                CachedFileParse fresh;
                fresh.size = size;
                fresh.modified = mtime;
                fresh.parsed = parse_single_file(relPath, contents);
                if (unbalanced)
                    fresh.diagnostic = "Source braces are temporarily unbalanced; keeping the last valid behavior schema.";
                cachedIt = impl_->fileCache.insert_or_assign(cacheKey, std::move(fresh)).first;
            }

            const RawFileParse &parsed = cachedIt->second.parsed;
            result.diagnostics.insert(result.diagnostics.end(), parsed.diagnostics.begin(),
                                      parsed.diagnostics.end());
            if (!cachedIt->second.diagnostic.empty())
                result.diagnostics.push_back({{}, {}, relPath, 0, cachedIt->second.diagnostic});
            for (const auto &[enumName, enumVals] : parsed.enums)
                result.enums[enumName] = enumVals;
            for (const auto &cls : parsed.classes)
                classesByName[cls.name] = cls;
            allMacros.insert(allMacros.end(), parsed.macros.begin(), parsed.macros.end());
        }
    }

    if (cancelled != nullptr && cancelled->load(std::memory_order_acquire))
    {
        result.cancelled = true;
        return result;
    }

    // Resolve behaviors identically to scan_sources using merged class/macro/enum tables
    auto is_script_subclass = [&](const std::string &startName) -> std::string {
        std::unordered_set<std::string> visited;
        std::vector<std::string> queue{startName};
        while (!queue.empty())
        {
            const std::string cur = std::move(queue.back());
            queue.pop_back();
            if (!visited.insert(cur).second)
                continue;
            const auto it = classesByName.find(cur);
            if (it == classesByName.end())
                continue;
            for (const auto &base : it->second.baseClasses)
            {
                if (base == "ScriptComponent" || base == "Behavior")
                    return base;
                queue.push_back(base);
            }
        }
        return {};
    };

    std::unordered_map<std::string, ParsedMacroCall> macrosByType;
    for (auto &macro : allMacros)
        macrosByType[macro.typeName] = std::move(macro);

    for (const auto &[typeName, cls] : classesByName)
    {
        const std::string scriptBase = is_script_subclass(typeName);
        if (scriptBase.empty() && macrosByType.find(typeName) == macrosByType.end())
            continue;

        SourceBehaviorDescriptor desc;
        desc.typeName = typeName;
        desc.baseClass = scriptBase.empty() ? "ScriptComponent" : scriptBase;
        desc.headerPath = cls.filePath;
        desc.declarationLine = cls.line;

        if (const auto macroIt = macrosByType.find(typeName); macroIt != macrosByType.end())
        {
            desc.sourcePath = macroIt->second.filePath;
            for (const std::string &fieldName : macroIt->second.fieldNames)
            {
                const ParsedMemberField *matchedMember = nullptr;
                for (const auto &member : cls.members)
                {
                    if (member.name == fieldName)
                    {
                        matchedMember = &member;
                        break;
                    }
                }
                SourceBehaviorFieldDescriptor field;
                field.name = fieldName;
                if (matchedMember == nullptr)
                {
                    field.resolved = false;
                    field.diagnostic = "Field '" + fieldName + "' listed in SERIALIZABLE_SCRIPT was not found on " + typeName;
                    desc.diagnostics.push_back(
                        {typeName, fieldName, macroIt->second.filePath, macroIt->second.line, field.diagnostic});
                    result.diagnostics.push_back(desc.diagnostics.back());
                }
                else
                {
                    field.lineNumber = matchedMember->line;
                    resolve_field_type(matchedMember->cppType, result.enums, field);
                    field.defaultValue = parse_default_initializer(field, matchedMember->initializer);
                    if (!field.resolved)
                    {
                        desc.diagnostics.push_back(
                            {typeName, fieldName, cls.filePath, matchedMember->line, field.diagnostic});
                        result.diagnostics.push_back(desc.diagnostics.back());
                    }
                }
                desc.fields.push_back(std::move(field));
            }
        }
        result.behaviors[typeName] = std::move(desc);
    }

    for (const auto &[typeName, macro] : macrosByType)
    {
        if (result.behaviors.find(typeName) != result.behaviors.end())
            continue;
        SourceBehaviorDescriptor desc;
        desc.typeName = typeName;
        desc.sourcePath = macro.filePath;
        desc.declarationLine = macro.line;
        for (const std::string &fieldName : macro.fieldNames)
        {
            SourceBehaviorFieldDescriptor field;
            field.name = fieldName;
            field.resolved = false;
            field.diagnostic = "Class declaration for '" + typeName + "' not found in scanned project sources";
            desc.diagnostics.push_back({typeName, fieldName, macro.filePath, macro.line, field.diagnostic});
            result.diagnostics.push_back(desc.diagnostics.back());
            desc.fields.push_back(std::move(field));
        }
        result.behaviors[typeName] = std::move(desc);
    }

    result.cachedFileHitCount = cacheHits;
    for (auto it = impl_->fileCache.begin(); it != impl_->fileCache.end();)
    {
        if (!seenCanonical.contains(it->first))
            it = impl_->fileCache.erase(it);
        else
            ++it;
    }
    return result;
}
} // namespace platformator_editor
