#pragma once

#include "../accessibility.h"
#include "../diagnostics.h"
#include "../process.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace platformator_editor
{
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

  private:
    void remember_item(std::string id);

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
