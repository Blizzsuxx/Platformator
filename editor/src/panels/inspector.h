#pragma once

#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "editcommands.h"
#include "../accessibility.h"
#include "behaviorfields.h"
#include "selection.h"

namespace platformator_editor
{
class InspectorPanel
{
  public:
    void draw(DocumentSession &document, SelectionModel &selection, EditCommandService &commands,
              const BehaviorCatalog &catalog, const AssetBrowseCallback &browse = {},
              ResourceIndex *resourceIndex = nullptr);
    void reveal_field(int objectId, std::optional<int> componentId, std::string fieldPath);
    void set_host_generation(std::uint64_t generation) noexcept;
    const std::string &feedback() const noexcept;
    std::vector<Json> template_author_bindings(const DocumentSession &document) const;
    std::vector<TemplateAuthorSchemaDeclaration> template_author_schemas(
        const DocumentSession &document) const;
    std::vector<SemanticNode> semantic_nodes(const DocumentSession &document,
                                             const Selection &selection,
                                             std::string_view scope,
                                             const BehaviorCatalog *catalog = nullptr);
    bool dispatch_semantic_action(DocumentSession &document, SelectionModel &selection,
                                  const SemanticAction &action, std::string_view scope,
                                  const BehaviorCatalog *catalog = nullptr,
                                  EditCommandService *commands = nullptr);

  private:
    bool draw_boolean(DocumentSession &document, EditCommandService &commands, int objectId,
                      std::optional<int> componentId, std::vector<FieldPathSegment> path,
                      const char *label, bool defaultValue = false);
    bool draw_number(DocumentSession &document, EditCommandService &commands, int objectId,
                     std::optional<int> componentId, std::vector<FieldPathSegment> path,
                     const char *label, float defaultValue = 0.0f,
                     std::optional<float> minimum = std::nullopt,
                     std::optional<float> maximum = std::nullopt);
    bool draw_integer(DocumentSession &document, EditCommandService &commands, int objectId,
                      std::optional<int> componentId, std::vector<FieldPathSegment> path,
                      const char *label, int defaultValue = 0,
                      std::optional<int> minimum = std::nullopt,
                      std::optional<int> maximum = std::nullopt);
    bool draw_string(DocumentSession &document, EditCommandService &commands, int objectId,
                     std::optional<int> componentId, std::vector<FieldPathSegment> path,
                     const char *label, std::string defaultValue = {});
    bool draw_enum(DocumentSession &document, EditCommandService &commands, int objectId,
                   std::optional<int> componentId, std::vector<FieldPathSegment> path,
                   const char *label, int defaultValue,
                   const std::vector<std::pair<int, const char *>> &values);
    bool set(DocumentSession &document, EditCommandService &commands, int objectId,
             std::optional<int> componentId, std::vector<FieldPathSegment> path, Json value);
    bool field_matches_search(const DocumentSession &document, int objectId,
                              std::optional<int> componentId,
                              const std::vector<FieldPathSegment> &path,
                              std::string_view label) const;
    void draw_search_bar();
    void draw_multi_object_fields(DocumentSession &document, SelectionModel &selection,
                                  EditCommandService &commands, const BehaviorCatalog &catalog,
                                  const std::vector<int> &objectIds);
    void save_selected_template(DocumentSession &document, int objectId,
                                const BehaviorCatalog &catalog);
    bool consume_reveal_target(int objectId, std::optional<int> componentId,
                               const std::vector<FieldPathSegment> &path);

    std::string feedback_;
    std::string searchQuery_;
    std::string templateName_;
    std::string templateNameSelectionKey_;
    std::string addPickerQuery_;
    std::string lastSelectionKey_;
    std::string lastObjectSelectionKey_;
    std::optional<Json> componentSettingsClipboard_;
    std::uint64_t hostGeneration_ = 0;
    std::optional<int> revealObjectId_;
    std::optional<int> revealComponentId_;
    std::string revealPath_;
    struct IntegerDraft
    {
        int value = 0;
        bool wasActive = false;
    };
    std::unordered_map<std::uint32_t, IntegerDraft> integerDrafts_;
    BehaviorFieldsPanel behaviorFields_;
    ResourceIndex *resourceIndex_ = nullptr;
    enum class SemanticOperation
    {
        Reveal,
        SelectComponent,
        SelectBehavior,
        CopySettings,
        PasteSettings,
        ResetSettings,
        SaveTemplate
    };
    struct SemanticTarget
    {
        int objectId = 0;
        std::optional<int> componentId;
        std::optional<std::size_t> behaviorOrdinal;
        std::string fieldPath;
        SemanticOperation operation = SemanticOperation::Reveal;
    };
    std::unordered_map<std::string, SemanticTarget> semanticTargets_;
    std::optional<std::string> semanticFocusNode_;
};
} // namespace platformator_editor
