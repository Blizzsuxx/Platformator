#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "editcommands.h"
#include "behaviorfields.h"
#include "selection.h"

namespace platformator_editor
{
class InspectorPanel
{
  public:
    void draw(DocumentSession &document, SelectionModel &selection, EditCommandService &commands,
              const BehaviorCatalog &catalog, const AssetBrowseCallback &browse = {});
    const std::string &feedback() const noexcept;

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

    std::string feedback_;
    BehaviorFieldsPanel behaviorFields_;
};
} // namespace platformator_editor
