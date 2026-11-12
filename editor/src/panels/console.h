#pragma once

#include "../accessibility.h"
#include "../diagnostics.h"
#include "../process.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace platformator_editor
{
const char *severity_icon(DiagnosticSeverity severity) noexcept;
ImVec4 severity_color(DiagnosticSeverity severity) noexcept;

struct ConsoleFilterState
{
    bool showInfo = true;
    bool showWarnings = true;
    bool showErrors = true;
    std::size_t infoCount = 0;
    std::size_t warningCount = 0;
    std::size_t errorCount = 0;
};

class ConsolePanel
{
  public:
    void draw(const PipelineStatus &status, BoundedConsole &console);
    std::vector<SemanticNode> semantic_nodes(const PipelineStatus &status,
                                              const BoundedConsole &console,
                                              const std::string &parentId) const;
    bool dispatch_semantic_action(const SemanticAction &action,
                                  const PipelineStatus &status,
                                  BoundedConsole &console);

    const ConsoleFilterState &filter_state() const noexcept { return filterState_; }
    ConsoleFilterState &filter_state() noexcept { return filterState_; }

  private:
    void remember_item(std::string id);

    ConsoleFilterState filterState_;
    ConsoleProjection projection_;
    std::array<char, 256> search_{};
    std::optional<std::size_t> selectedRow_;
    std::uint64_t projectedRevision_ = static_cast<std::uint64_t>(-1);
    int severityFilter_ = 0;
    bool collapseRepeats_ = true;
    std::string feedback_;
    std::map<std::string, SemanticBounds> itemBounds_;
    std::vector<std::size_t> drawnRows_;
    std::string focusedItem_;
};
} // namespace platformator_editor
