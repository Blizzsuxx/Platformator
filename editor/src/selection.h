#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "document.h"

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

    bool operator==(const Selection &) const = default;

    void clear()
    {
        objectId.reset();
        componentId.reset();
        behaviorOrdinal.reset();
    }
};

struct SelectionNotification
{
    std::uint64_t documentGeneration = 0;
    Selection selection;
};

class SelectionModel
{
  public:
    using Observer = std::function<void(const SelectionNotification &)>;

    const Selection &current() const noexcept;
    void set(const DocumentSession &document, Selection selection);
    void repair(const DocumentSession &document);
    void subscribe(Observer observer);

  private:
    void publish(std::uint64_t documentGeneration);

    Selection selection_;
    std::vector<Observer> observers_;
};

std::optional<ObjectLocation> find_object_location(const Json &root, int objectId);
} // namespace platformator_editor
