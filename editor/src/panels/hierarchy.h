#pragma once

#include <cstdint>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "editcommands.h"
#include "selection.h"
#include "../templates.h"
#include "../accessibility.h"

namespace platformator_editor
{
enum class HierarchyRowKind
{
    Object
};

struct HierarchyRow
{
    HierarchyRowKind kind = HierarchyRowKind::Object;
    int id = 0;
    std::size_t depth = 0;
    std::string label;
    bool hasChildren = false;
    int parentRowIndex = -1;
};

class HierarchyPanel
{
    friend struct HierarchyPanelTestProbe;

  public:
    const std::vector<HierarchyRow> &visible_rows(const DocumentSession &document);
    void set_search_query(std::string query);
    void draw(DocumentSession &document, SelectionModel &selection, EditCommandService &commands,
              bool editingAllowed = true,
              std::vector<int> *hiddenObjectIds = nullptr,
              std::vector<int> *lockedObjectIds = nullptr,
              std::string *duplicationReferencePolicy = nullptr,
              const Json *behaviorCatalog = nullptr,
              const std::vector<Json> &authorReferenceBindings = {},
              const std::vector<TemplateAuthorSchemaDeclaration> &authorSchemaDeclarations = {},
              bool deleteKeyPressed = false,
              bool mouseButtonEventThisFrame = false);
    void reveal_object(const DocumentSession &document, int objectId);
    std::vector<int> expanded_object_ids() const;
    std::vector<SemanticNode> semantic_nodes(const DocumentSession &document,
                                             const Selection &selection,
                                             std::string_view scope) const;
    bool dispatch_semantic_action(DocumentSession &document, SelectionModel &selection,
                                  const SemanticAction &action, std::string_view scope,
                                  WindowHostGeneration hostGeneration);
    void restore_expanded_object_ids(const DocumentSession &document,
                                     const std::vector<int> &objectIds);
    void reset_expansion_for_document() noexcept;
    void apply_change(const DocumentSession &document, const DocumentChange &change);
    void invalidate() noexcept;
    void set_on_frame_selected(std::function<void()> callback) { onFrameSelected_ = std::move(callback); }

  private:
    std::function<void()> onFrameSelected_;
    void rebuild(const DocumentSession &document);
    void refresh_labels(const DocumentSession &document);
    void update_search_matches(const DocumentSession &document);
    void update_search_visibility(const DocumentSession &document,
                                  std::optional<int> selectedObjectId);

    std::uint64_t cachedGeneration_ = 0;
    std::uint64_t cachedStructureRevision_ = 0;
    std::uint64_t cachedValueRevision_ = 0;
    bool cacheValid_ = false;
    std::unordered_set<int> expandedObjects_;
    std::unordered_set<int> knownObjectIds_;
    std::unordered_map<int, std::size_t> objectRowIndexes_;
    std::vector<int> displayedObjectOrder_;
    std::vector<char> renameBuffer_;
    int renameTargetObjectId_ = 0;
    bool renameFocusPending_ = false;
    int lastMouseInteractionFrame_ = -2;
    std::optional<int> lastRevealedObjectId_;
    std::optional<int> pendingScrollObjectId_;
    bool initializedExpansion_ = false;
    std::vector<HierarchyRow> rows_;
    struct SemanticVisibleRow
    {
        int rowIndex = -1;
        SemanticBounds bounds;
    };
    std::vector<SemanticVisibleRow> semanticVisibleRows_;
    std::vector<int> firstChildRows_;
    std::vector<int> lastChildRows_;
    std::vector<std::uint64_t> connectorVisitMarks_;
    std::vector<int> connectorCandidates_;
    std::uint64_t connectorVisitGeneration_ = 0;
    std::string searchQuery_;
    std::string cachedSearchQuery_;
    std::string searchExpansionQuery_;
    std::uint64_t cachedSearchStructureRevision_ = 0;
    std::uint64_t cachedSearchValueRevision_ = 0;
    std::uint64_t searchVisibilityStructureRevision_ = 0;
    std::uint64_t searchVisibilityValueRevision_ = 0;
    std::string searchVisibilityQuery_;
    std::vector<std::uint8_t> searchVisibleRows_;
    std::unordered_set<int> searchMatchedObjectIds_;
    std::unordered_set<int> searchContextObjectIds_;
    std::unordered_set<int> searchMatchedComponentIds_;
    std::unordered_set<int> searchComponentOwnerIds_;
    std::vector<int> searchResultObjectOrder_;
    std::unordered_set<int> searchExpansionBackup_;
    bool hasSearchExpansionBackup_ = false;
    std::string duplicatePolicyStatus_;
};
} // namespace platformator_editor
