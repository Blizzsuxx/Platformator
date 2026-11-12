#pragma once

#include "behaviorcatalog.h"
#include "editcommands.h"
#include "referencefields.h"
#include "selection.h"
#include "templates.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace platformator_editor
{
class BehaviorFieldsPanel
{
  public:
    struct ScrubTarget
    {
        int objectId = 0;
        int componentId = 0;
        std::size_t behaviorOrdinal = 0;
        std::vector<FieldPathSegment> path;
        Json originalJson;
        bool hadValue = false;
    };

    struct ScrubState
    {
        bool active = false;
        bool moved = false;
        bool seenThisFrame = false;
        std::uint32_t widgetId = 0;
        double accumDelta = 0.0;
        std::uint64_t projectGeneration = 0;
        std::uint64_t tabId = 0;
        std::uint64_t documentGeneration = 0;
        std::uint64_t hostGeneration = 0;
        std::vector<int> orderedSelection;
        std::vector<ScrubTarget> targets;
    };

    void set_host_generation(std::uint64_t generation, DocumentSession *document = nullptr) noexcept;
    void cancel_active_interactions(DocumentSession *document = nullptr);

    std::vector<Json> template_author_bindings(const DocumentSession &document) const;
    std::vector<TemplateAuthorSchemaDeclaration> template_author_schemas(
        const DocumentSession &document) const;
    void draw(DocumentSession &document, SelectionModel &selection,
              EditCommandService &commands, const BehaviorCatalog &catalog,
              const AssetBrowseCallback &browse = {},
              ResourceIndex *resourceIndex = nullptr,
              std::string_view searchQuery = {});
    void draw_multi_object_fields(DocumentSession &document, const Selection &selection,
                                  EditCommandService &commands, const BehaviorCatalog &catalog,
                                  const std::vector<int> &objectIds,
                                  ResourceIndex *resourceIndex = nullptr,
                                  std::string_view searchQuery = {});

  private:
    struct AuthorReferenceDeclaration
    {
        struct NestedPathDraft
        {
            std::array<char, 256> pointer{};
            std::optional<std::string> targetKind;
        };

        DocumentIdentity identity;
        int ownerId = 0;
        int componentId = 0;
        std::size_t behaviorOrdinal = 0;
        std::string behaviorType;
        std::map<std::string, std::string> fieldKinds;
        std::map<std::string, NestedPathDraft> nestedPathDrafts;
        bool schemaCoverageComplete = false;
    };

    std::map<std::string, AuthorReferenceDeclaration> authorReferenceDeclarations_;
    ScrubState scrubState_;
    std::uint64_t hostGeneration_ = 0;
};
} // namespace platformator_editor
