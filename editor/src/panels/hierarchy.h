#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "editcommands.h"
#include "selection.h"

namespace platformator_editor
{
enum class HierarchyRowKind
{
    Object,
    Component
};

struct HierarchyRow
{
    HierarchyRowKind kind = HierarchyRowKind::Object;
    int id = 0;
    int ownerObjectId = 0;
    std::size_t depth = 0;
    std::string label;
    bool hasChildren = false;
};

class HierarchyPanel
{
  public:
    const std::vector<HierarchyRow> &visible_rows(const DocumentSession &document);
    void draw(DocumentSession &document, SelectionModel &selection, EditCommandService &commands);
    void invalidate() noexcept;

  private:
    void rebuild(const DocumentSession &document);
    void refresh_labels(const DocumentSession &document);

    std::uint64_t cachedGeneration_ = 0;
    std::uint64_t cachedStructureRevision_ = 0;
    bool cacheValid_ = false;
    std::unordered_set<int> expandedObjects_;
    bool initializedExpansion_ = false;
    std::vector<HierarchyRow> rows_;
};
} // namespace platformator_editor
