#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <deque>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "document.h"
#include "selection.h"

namespace platformator_editor
{
using FieldPathSegment = std::variant<std::string, std::size_t>;

struct PropertyAddress
{
    std::uint64_t documentGeneration = 0;
    int objectId = 0;
    std::optional<int> componentId;
    std::optional<std::size_t> behaviorOrdinal;
    std::vector<FieldPathSegment> fieldPath;
};

struct PropertyValue
{
    bool present = false;
    Json value;

    static PropertyValue missing();
    static PropertyValue of(Json value);
    bool operator==(const PropertyValue &) const = default;
};

struct SelectionState
{
    Selection before;
    Selection after;
};

struct PropertyCommand
{
    PropertyAddress address;
    PropertyValue before;
    PropertyValue after;
    SelectionState selection;
};

enum class CommandDirection
{
    Forward,
    Reverse
};

enum class CommandStatus
{
    Changed,
    NoChange,
    StaleAddress,
    Rejected
};

struct CommandResult
{
    CommandStatus status = CommandStatus::NoChange;
    std::string message;
    std::vector<int> affectedObjectIds;
    DocumentChange change;
    std::optional<PropertyCommand> propertyCommand;

    explicit operator bool() const noexcept
    {
        return status == CommandStatus::Changed || status == CommandStatus::NoChange;
    }
};

enum class StructuralCommandKind
{
    InsertObject,
    DeleteObject,
    InsertComponent,
    DeleteComponent,
    InsertBehavior,
    DeleteBehavior
};

struct StructuralCommand
{
    std::uint64_t documentGeneration = 0;
    StructuralCommandKind kind = StructuralCommandKind::InsertObject;
    std::optional<int> parentObjectId;
    int ownerObjectId = 0;
    std::optional<int> componentId;
    std::size_t insertionIndex = 0;
    Json payload;
    SelectionState selection;
};

struct StructuralCommandResult
{
    CommandStatus status = CommandStatus::NoChange;
    std::string message;
    std::vector<int> affectedObjectIds;
    DocumentChange change;
    std::optional<StructuralCommand> command;

    explicit operator bool() const noexcept
    {
        return status == CommandStatus::Changed || status == CommandStatus::NoChange;
    }
};

class EditCommandService;

struct HistoryStatus
{
    bool canUndo = false;
    bool canRedo = false;
    bool dirty = false;
    bool savedTokenReachable = true;
    bool overBudget = false;
    std::size_t retainedBytes = 0;
    std::size_t budgetBytes = 0;
    std::size_t undoCommands = 0;
    std::size_t redoCommands = 0;
    std::uint64_t currentToken = 0;
    std::uint64_t savedToken = 0;
};

class PropertyGesture
{
  public:
    PropertyGesture() = default;
    PropertyGesture(const PropertyGesture &) = delete;
    PropertyGesture &operator=(const PropertyGesture &) = delete;
    PropertyGesture(PropertyGesture &&other) noexcept;
    PropertyGesture &operator=(PropertyGesture &&other) noexcept;
    ~PropertyGesture();

    bool active() const noexcept;
    CommandResult update(EditCommandService &service, DocumentSession &document, Json value);
    std::optional<PropertyCommand> commit();
    CommandResult cancel(EditCommandService &service, DocumentSession &document);

  private:
    friend class EditCommandService;
    PropertyGesture(EditCommandService &service, PropertyCommand command);

    EditCommandService *service_ = nullptr;
    PropertyCommand command_;
    bool active_ = false;
    bool dirtyCounted_ = false;
};

class EditCommandService
{
  public:
    using Observer = std::function<void(const DocumentChange &, const std::vector<int> &)>;
    using SelectionProvider = std::function<Selection()>;

    explicit EditCommandService(std::size_t historyBudgetBytes = 64U * 1024U * 1024U);

    void subscribe(Observer observer);
    void set_selection_provider(SelectionProvider provider);
    void set_last_selection_state(SelectionState selection);
    void reset_history(std::uint64_t documentGeneration);
    void mark_saved() noexcept;
    bool dirty() const noexcept;
    HistoryStatus history_status() const noexcept;
    CommandResult undo(DocumentSession &document, SelectionModel &selection);
    CommandResult redo(DocumentSession &document, SelectionModel &selection);

    CommandResult set_property(DocumentSession &document, const PropertyAddress &address, Json value,
                               SelectionState selection = {});
    CommandResult erase_property(DocumentSession &document, const PropertyAddress &address,
                                 SelectionState selection = {});
    CommandResult apply(DocumentSession &document, const PropertyCommand &command,
                        CommandDirection direction);
    StructuralCommandResult create_object(DocumentSession &document, std::optional<int> parentObjectId,
                                          std::string name,
                                          std::size_t insertionIndex = std::numeric_limits<std::size_t>::max(),
                                          SelectionState selection = {});
    StructuralCommandResult duplicate_object(DocumentSession &document, int objectId,
                                             SelectionState selection = {});
    StructuralCommandResult delete_object(DocumentSession &document, int objectId,
                                          SelectionState selection = {});
    StructuralCommandResult add_component(DocumentSession &document, int objectId, Json component,
                                           std::size_t insertionIndex = std::numeric_limits<std::size_t>::max(),
                                           SelectionState selection = {});
    StructuralCommandResult remove_component(DocumentSession &document, int objectId, int componentId,
                                              SelectionState selection = {});
    StructuralCommandResult add_behavior(DocumentSession &document, int objectId, int scriptComponentId,
                                          Json behavior,
                                          std::size_t insertionIndex = std::numeric_limits<std::size_t>::max(),
                                          SelectionState selection = {});
    StructuralCommandResult remove_behavior(DocumentSession &document, int objectId, int scriptComponentId,
                                             std::size_t behaviorOrdinal,
                                             SelectionState selection = {});
    StructuralCommandResult apply(DocumentSession &document, const StructuralCommand &command,
                                  CommandDirection direction);
    std::optional<PropertyGesture> begin_gesture(const DocumentSession &document,
                                                 const PropertyAddress &address,
                                                 SelectionState selection = {});

  private:
    friend class PropertyGesture;
    struct HistoryEntry
    {
        std::variant<PropertyCommand, StructuralCommand> command;
        std::uint64_t beforeToken = 0;
        std::uint64_t afterToken = 0;
        std::size_t retainedBytes = 0;
    };

    SelectionState capture_selection(SelectionState selection) const;
    void append_history(std::variant<PropertyCommand, StructuralCommand> command);
    void prune_history();
    bool token_reachable(std::uint64_t token) const noexcept;
    std::uint64_t current_token() const noexcept;
    void update_gesture_dirty(PropertyGesture &gesture, bool changed) noexcept;
    void finish_gesture(PropertyGesture &gesture, bool commit) noexcept;

    std::vector<Observer> observers_;
    SelectionProvider selectionProvider_;
    std::deque<HistoryEntry> history_;
    std::size_t cursor_ = 0;
    std::size_t retainedBytes_ = 0;
    std::size_t historyBudgetBytes_;
    std::uint64_t historyGeneration_ = 0;
    std::uint64_t baseToken_ = 1;
    std::uint64_t savedToken_ = 1;
    std::uint64_t nextToken_ = 2;
    std::size_t activeGestures_ = 0;
    std::size_t activeChangedGestures_ = 0;
    std::size_t suppressHistory_ = 0;
};
} // namespace platformator_editor
