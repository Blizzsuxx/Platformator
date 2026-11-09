#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "document.h"
#include "sessionids.h"

namespace platformator_editor
{
enum class FocusedPanel
{
    None,
    Hierarchy,
    Scene,
    Inspector,
    Behaviors,
    Project,
    Console
};

struct Selection
{
    std::optional<int> objectId;
    std::optional<int> componentId;
    std::optional<std::size_t> behaviorOrdinal;
    FocusedPanel focusedPanel = FocusedPanel::None;
    WindowHostGeneration focusedHost;
    std::vector<int> selectedObjectIds;
    std::optional<int> rangeAnchorObjectId;

    bool operator==(const Selection &) const = default;

    void clear()
    {
        objectId.reset();
        componentId.reset();
        behaviorOrdinal.reset();
        selectedObjectIds.clear();
        rangeAnchorObjectId.reset();
    }
};

struct SelectionNotification
{
    std::uint64_t documentGeneration = 0;
    Selection selection;
    DocumentIdentity identity;
};

class SelectionModel
{
  public:
    using Observer = std::function<void(const SelectionNotification &)>;

    const Selection &current() const noexcept;
    void set(const DocumentSession &document, Selection selection);
    void set_objects(const DocumentSession &document, std::vector<int> orderedObjectIds,
                     std::optional<int> primaryObjectId = std::nullopt,
                     std::optional<int> rangeAnchorObjectId = std::nullopt);
    void toggle_object(const DocumentSession &document, int objectId);
    void select_range(const DocumentSession &document, int objectId,
                      const std::vector<int> &displayedObjectOrder, bool additive = false);
    void repair(const DocumentSession &document);
    void subscribe(Observer observer);

  private:
    void publish(const DocumentSession &document);

    Selection selection_;
    std::vector<Observer> observers_;
};

std::optional<ObjectLocation> find_object_location(const Json &root, int objectId);
std::vector<int> selected_top_level_roots(const DocumentSession &document,
                                          const Selection &selection);
} // namespace platformator_editor
