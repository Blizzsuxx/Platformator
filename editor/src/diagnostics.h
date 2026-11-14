#pragma once

#include "process.h"
#include "sessionids.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace platformator_editor
{
enum class DiagnosticSeverity
{
    Info,
    Warning,
    Error
};

struct DiagnosticTarget
{
    DocumentIdentity document;
    std::optional<int> objectId;
    std::optional<int> componentId;
    std::optional<std::size_t> behaviorOrdinal;
    std::string fieldPath;
};

struct DiagnosticSourceLocation
{
    std::filesystem::path path;
    std::size_t line = 0;
    std::optional<std::size_t> column;
};

struct Diagnostic
{
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    std::string message;
    std::size_t repeatCount = 1;
    std::chrono::system_clock::time_point time = std::chrono::system_clock::now();
    std::optional<DiagnosticTarget> target;
    std::optional<DiagnosticSourceLocation> source;
    bool blocksRun = false;
};

struct ConsoleRow
{
    std::size_t byteOffset = 0;
    std::size_t byteLength = 0;
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    std::size_t repeatCount = 1;
    std::optional<DiagnosticSourceLocation> source;
};

// A bounded presentation index over BoundedConsole's exact retained bytes.
// Rows refer to byte ranges and never own or rewrite the source transcript.
class ConsoleProjection
{
  public:
    static constexpr std::size_t maximumRows = 16384;

    void rebuild(const BoundedConsole &console);
    void clear() noexcept;
    void set_search(std::string search);
    void set_severity_mask(bool showInfo, bool showWarnings, bool showErrors) noexcept;
    void set_collapse_repeats(bool enabled);
    void set_auto_follow(bool enabled) noexcept;
    void user_scrolled_away() noexcept;
    void return_to_bottom() noexcept;

    const std::vector<ConsoleRow> &rows() const noexcept;
    std::vector<std::size_t> visible_rows(std::string_view rawText) const;
    std::string copy_all(const BoundedConsole &console) const;
    std::string copy_row(const BoundedConsole &console, std::size_t rowIndex) const;
    bool auto_follow() const noexcept;
    std::size_t omitted_rows() const noexcept;

  private:
    std::vector<ConsoleRow> rows_;
    std::string search_;
    bool showInfo_ = true;
    bool showWarnings_ = true;
    bool showErrors_ = true;
    bool useSeverityMask_ = false;
    bool collapseRepeats_ = true;
    bool autoFollow_ = true;
    std::size_t omittedRows_ = 0;
};

std::optional<DiagnosticSourceLocation> parse_source_location(std::string_view line);
} // namespace platformator_editor
