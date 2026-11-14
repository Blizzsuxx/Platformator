#pragma once

#include "behaviorcatalog.h"
#include "editcommands.h"
#include "referencefields.h"
#include "selection.h"
#include "templates.h"

#include <cstdint>
#include <optional>
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
    void reveal_field(int objectId, int componentId, std::size_t behaviorOrdinal,
                      std::string fieldPath);

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
    bool consume_reveal_target(int objectId, int componentId, std::size_t behaviorOrdinal,
                               const std::vector<FieldPathSegment> &path);

    std::optional<int> revealObjectId_;
    std::optional<int> revealComponentId_;
    std::optional<std::size_t> revealBehaviorOrdinal_;
    std::string revealPath_;
    std::string feedback_;
    std::optional<DocumentIdentity> feedbackDocument_;
    Selection feedbackSelection_;
    std::vector<int> feedbackObjectIds_;
    std::uint64_t feedbackRevision_ = 0;
    ScrubState scrubState_;
    std::uint64_t hostGeneration_ = 0;
};
} // namespace platformator_editor
