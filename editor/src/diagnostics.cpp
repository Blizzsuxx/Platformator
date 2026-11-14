#include "diagnostics.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <string>
#include <system_error>

namespace platformator_editor
{
namespace
{
struct ParsedNumber
{
    std::size_t value = 0;
    std::size_t end = 0;
};

std::optional<ParsedNumber> parse_positive_number(std::string_view text, std::size_t start)
{
    if (start >= text.size() || !std::isdigit(static_cast<unsigned char>(text[start])))
        return std::nullopt;
    std::size_t value = 0;
    const char *begin = text.data() + start;
    const char *end = text.data() + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr == begin || value == 0)
        return std::nullopt;
    return ParsedNumber{value, static_cast<std::size_t>(parsed.ptr - text.data())};
}

std::filesystem::path path_from_utf8(std::string_view bytes)
{
    std::u8string encoded;
    encoded.reserve(bytes.size());
    for (const unsigned char byte : bytes)
        encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(std::move(encoded));
}

bool has_severity_word(std::string_view text, std::size_t start)
{
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
        ++start;
    constexpr std::string_view words[] = {"error", "fatal error", "warning", "note"};
    for (const auto word : words)
    {
        if (text.substr(start, word.size()) == word &&
            (start + word.size() == text.size() ||
             !std::isalnum(static_cast<unsigned char>(text[start + word.size()]))))
            return true;
    }
    return false;
}

DiagnosticSeverity severity_for_line(std::string_view line)
{
    const auto contains = [&](std::string_view word) {
        return line.find(word) != std::string_view::npos;
    };
    if (contains("fatal error") || contains(": error") || contains(") : error") ||
        contains("): error"))
        return DiagnosticSeverity::Error;
    if (contains(": warning") || contains("): warning"))
        return DiagnosticSeverity::Warning;
    return DiagnosticSeverity::Info;
}

bool equal_ascii_case_insensitive(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index])))
            return false;
    return true;
}

bool contains_ascii_case_insensitive(std::string_view text, std::string_view needle)
{
    if (needle.empty())
        return true;
    if (needle.size() > text.size())
        return false;
    for (std::size_t start = 0; start + needle.size() <= text.size(); ++start)
        if (equal_ascii_case_insensitive(text.substr(start, needle.size()), needle))
            return true;
    return false;
}
} // namespace

std::optional<DiagnosticSourceLocation> parse_source_location(std::string_view line)
{
    // MSVC: path(line[,column]): error ...
    for (std::size_t open = line.rfind('('); open != std::string_view::npos;
         open = open == 0 ? std::string_view::npos : line.rfind('(', open - 1))
    {
        const auto lineNumber = parse_positive_number(line, open + 1);
        if (!lineNumber)
            continue;
        std::size_t cursor = lineNumber->end;
        std::optional<std::size_t> column;
        if (cursor < line.size() && line[cursor] == ',')
        {
            const auto columnNumber = parse_positive_number(line, cursor + 1);
            if (!columnNumber)
                continue;
            column = columnNumber->value;
            cursor = columnNumber->end;
        }
        if (cursor + 1 >= line.size() || line[cursor] != ')' || line[cursor + 1] != ':' ||
            !has_severity_word(line, cursor + 2))
            continue;
        const auto path = line.substr(0, open);
        if (path.empty() || path.find('\0') != std::string_view::npos)
            continue;
        return DiagnosticSourceLocation{path_from_utf8(path), lineNumber->value, column};
    }

    // GCC/Clang: path:line[:column]: error ... . Greedy path parsing handles
    // drive-letter colons and colons in parent directory names.
    for (std::size_t colon = line.rfind(':'); colon != std::string_view::npos;
         colon = colon == 0 ? std::string_view::npos : line.rfind(':', colon - 1))
    {
        const auto lineNumber = parse_positive_number(line, colon + 1);
        if (!lineNumber || lineNumber->end >= line.size() || line[lineNumber->end] != ':')
            continue;
        const std::size_t separatorAfterLine = lineNumber->end;
        const auto column = parse_positive_number(line, separatorAfterLine + 1);
        if (column && column->end < line.size() && line[column->end] == ':' &&
            has_severity_word(line, column->end + 1))
        {
            const auto path = line.substr(0, colon);
            if (!path.empty() && path.find('\0') == std::string_view::npos)
                return DiagnosticSourceLocation{path_from_utf8(path), lineNumber->value,
                                                column->value};
        }

        if (!has_severity_word(line, separatorAfterLine + 1))
            continue;
        // If the proposed path ends in another :number component, this is the
        // column separator in a path:line:column diagnostic, not the line one.
        const auto previousColon = colon == 0 ? std::string_view::npos : line.rfind(':', colon - 1);
        const auto trailingComponent = previousColon == std::string_view::npos
                                           ? std::string_view{}
                                           : line.substr(previousColon + 1, colon - previousColon - 1);
        const bool pathEndsInNumericComponent = !trailingComponent.empty() &&
            std::all_of(trailingComponent.begin(), trailingComponent.end(), [](unsigned char value) {
                return std::isdigit(value) != 0;
            });
        if (pathEndsInNumericComponent)
            continue;
        const auto path = line.substr(0, colon);
        if (path.empty() || path.find('\0') != std::string_view::npos)
            continue;
        return DiagnosticSourceLocation{path_from_utf8(path), lineNumber->value, std::nullopt};
    }
    return std::nullopt;
}

void ConsoleProjection::rebuild(const BoundedConsole &console)
{
    rows_.clear();
    omittedRows_ = 0;
    const std::string &raw = console.text();
    std::size_t offset = 0;
    while (offset < raw.size())
    {
        const auto newline = raw.find('\n', offset);
        const std::size_t end = newline == std::string::npos ? raw.size() : newline;
        std::size_t contentEnd = end;
        if (contentEnd > offset && raw[contentEnd - 1] == '\r')
            --contentEnd;
        const std::string_view line(raw.data() + offset, contentEnd - offset);
        if (collapseRepeats_ && !rows_.empty())
        {
            const auto &previous = rows_.back();
            const std::string_view previousLine(raw.data() + previous.byteOffset, previous.byteLength);
            if (equal_ascii_case_insensitive(previousLine, line))
            {
                ++rows_.back().repeatCount;
                offset = newline == std::string::npos ? raw.size() : newline + 1;
                continue;
            }
        }
        if (rows_.size() < maximumRows)
        {
            rows_.push_back({offset, contentEnd - offset, severity_for_line(line), 1,
                             parse_source_location(line)});
        }
        else
            ++omittedRows_;
        offset = newline == std::string::npos ? raw.size() : newline + 1;
    }
}

void ConsoleProjection::clear() noexcept
{
    rows_.clear();
    omittedRows_ = 0;
}

void ConsoleProjection::set_search(std::string search)
{
    search_ = std::move(search);
}

void ConsoleProjection::set_severity_mask(bool showInfo, bool showWarnings, bool showErrors) noexcept
{
    showInfo_ = showInfo;
    showWarnings_ = showWarnings;
    showErrors_ = showErrors;
    useSeverityMask_ = true;
}

void ConsoleProjection::set_collapse_repeats(bool enabled)
{
    if (collapseRepeats_ == enabled)
        return;
    collapseRepeats_ = enabled;
}

void ConsoleProjection::set_auto_follow(bool enabled) noexcept
{
    autoFollow_ = enabled;
}

void ConsoleProjection::user_scrolled_away() noexcept
{
    autoFollow_ = false;
}

void ConsoleProjection::return_to_bottom() noexcept
{
    autoFollow_ = true;
}

const std::vector<ConsoleRow> &ConsoleProjection::rows() const noexcept
{
    return rows_;
}

std::vector<std::size_t> ConsoleProjection::visible_rows(std::string_view rawText) const
{
    std::vector<std::size_t> visible;
    visible.reserve(rows_.size());
    for (std::size_t index = 0; index < rows_.size(); ++index)
    {
        const auto &row = rows_[index];
        if (row.byteOffset > rawText.size() || row.byteLength > rawText.size() - row.byteOffset)
            continue;
        if (useSeverityMask_)
        {
            if (row.severity == DiagnosticSeverity::Info && !showInfo_)
                continue;
            if (row.severity == DiagnosticSeverity::Warning && !showWarnings_)
                continue;
            if (row.severity == DiagnosticSeverity::Error && !showErrors_)
                continue;
        }
        const auto line = rawText.substr(row.byteOffset, row.byteLength);
        if (!contains_ascii_case_insensitive(line, search_))
            continue;
        visible.push_back(index);
    }
    return visible;
}

std::string ConsoleProjection::copy_all(const BoundedConsole &console) const
{
    return console.text();
}

std::string ConsoleProjection::copy_row(const BoundedConsole &console, std::size_t rowIndex) const
{
    if (rowIndex >= rows_.size())
        return {};
    const auto &row = rows_[rowIndex];
    const auto &raw = console.text();
    if (row.byteOffset > raw.size() || row.byteLength > raw.size() - row.byteOffset)
        return {};
    return raw.substr(row.byteOffset, row.byteLength);
}

bool ConsoleProjection::auto_follow() const noexcept
{
    return autoFollow_;
}

std::size_t ConsoleProjection::omitted_rows() const noexcept
{
    return omittedRows_;
}
} // namespace platformator_editor
