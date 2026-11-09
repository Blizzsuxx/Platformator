#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <deque>
#include <optional>
#include <string>
#include <utility>
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
    DocumentIdentity identity;
};

inline PropertyAddress address_for_document(const DocumentSession &document, int objectId,
                                            std::optional<int> componentId,
                                            std::optional<std::size_t> behaviorOrdinal,
                                            std::vector<FieldPathSegment> fieldPath)
{
    return PropertyAddress{document.documentGeneration, objectId, componentId, behaviorOrdinal,
                           std::move(fieldPath),
                           DocumentIdentity{ProjectGeneration{document.projectGeneration},
                                            document.tabId,
                                            DocumentGeneration{document.documentGeneration}}};
}

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

struct BatchPropertyEdit
{
    PropertyAddress address;
    PropertyValue value;
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

enum class DuplicateReferencePolicy
{
    PreserveExistingTargets,
    RemapInternalTargets
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
    DocumentIdentity identity;
};

struct MoveOperation
{
    DocumentIdentity identity;
    int objectId = 0;
    std::optional<int> beforeParentObjectId;
    std::optional<int> afterParentObjectId;
    std::size_t beforeIndex = 0;
    std::size_t afterIndex = 0;
    Json beforePayload;
    Json afterPayload;
};

using BatchOperation = std::variant<PropertyCommand, StructuralCommand>;

struct BatchCommand
{
    std::vector<BatchOperation> operations;
    SelectionState selection;
    DocumentIdentity identity;
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
class DocumentManager;
class DocumentWorkspace;

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

struct EditCommandWorkStats
{
    std::size_t propertyBatchTargetsResolved = 0;
    std::size_t propertyBatchPathSegmentsResolved = 0;
    std::size_t propertyBatchPatchesPrepared = 0;
    std::size_t propertyBatchPatchesCommitted = 0;
    std::size_t propertyBatchIndexRebuildAttempts = 0;
    std::size_t propertyBatchDocumentCopies = 0;
    std::size_t propertyBatchOverlapChecks = 0;
    std::size_t retainedHistoryEntriesCopied = 0;
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
    PropertyGesture(EditCommandService &service, DocumentSession &document, PropertyCommand command);

    EditCommandService *service_ = nullptr;
    DocumentSession *document_ = nullptr;
    std::uint64_t startingValueRevision_ = 0;
    PropertyCommand command_;
    bool active_ = false;
    bool dirtyCounted_ = false;
};

class BatchPropertyGesture
{
  public:
    BatchPropertyGesture() = default;
    BatchPropertyGesture(const BatchPropertyGesture &) = delete;
    BatchPropertyGesture &operator=(const BatchPropertyGesture &) = delete;
    BatchPropertyGesture(BatchPropertyGesture &&other) noexcept;
    BatchPropertyGesture &operator=(BatchPropertyGesture &&other) noexcept;
    ~BatchPropertyGesture();

    bool active() const noexcept;
    CommandResult update(EditCommandService &service, DocumentSession &document,
                         const std::vector<Json> &values);
    bool commit();
    CommandResult cancel(EditCommandService &service, DocumentSession &document);

  private:
    friend class EditCommandService;
    BatchPropertyGesture(EditCommandService &service, DocumentSession &document, BatchCommand command);

    EditCommandService *service_ = nullptr;
    DocumentSession *document_ = nullptr;
    std::uint64_t startingValueRevision_ = 0;
    BatchCommand command_;
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
    CommandResult apply_batch(DocumentSession &document, const std::vector<BatchPropertyEdit> &edits,
                              SelectionState selection = {});
    CommandResult apply_batch(DocumentSession &document, const std::vector<BatchOperation> &operations,
                              SelectionState selection = {});
    StructuralCommandResult create_object(DocumentSession &document, std::optional<int> parentObjectId,
                                          std::string name,
                                          std::size_t insertionIndex = std::numeric_limits<std::size_t>::max(),
                                          SelectionState selection = {});
    StructuralCommandResult duplicate_object(DocumentSession &document, int objectId,
                                             SelectionState selection = {},
                                             DuplicateReferencePolicy referencePolicy =
                                                 DuplicateReferencePolicy::PreserveExistingTargets,
                                             const Json &typedReferenceBindings = Json::array(),
                                             bool bindingCoverageAssured = false);
    CommandResult duplicate_objects(DocumentSession &document, const std::vector<int> &objectIds,
                                    SelectionState selection = {},
                                    DuplicateReferencePolicy referencePolicy =
                                        DuplicateReferencePolicy::PreserveExistingTargets,
                                    const Json &typedReferenceBindings = Json::array(),
                                    bool bindingCoverageAssured = false);
    CommandResult move_object(DocumentSession &document, int objectId,
                              std::optional<int> destinationParentObjectId,
                              std::size_t insertionIndex = std::numeric_limits<std::size_t>::max(),
                              SelectionState selection = {});
    CommandResult move_objects(DocumentSession &document, const std::vector<int> &objectIds,
                               std::optional<int> destinationParentObjectId,
                               std::size_t insertionIndex = std::numeric_limits<std::size_t>::max(),
                               SelectionState selection = {});
    StructuralCommandResult delete_object(DocumentSession &document, int objectId,
                                          SelectionState selection = {});
    CommandResult delete_objects(DocumentSession &document, const std::vector<int> &objectIds,
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
    std::optional<PropertyGesture> begin_gesture(DocumentSession &document,
                                                 const PropertyAddress &address,
                                                 SelectionState selection = {});
    std::optional<BatchPropertyGesture> begin_batch_gesture(
        DocumentSession &document, const std::vector<PropertyAddress> &addresses,
        SelectionState selection = {});
    void inject_history_retention_failure_once_for_testing() noexcept;
    const std::string &last_error() const noexcept;
    const EditCommandWorkStats &last_work_stats() const noexcept;

  private:
    friend class DocumentManager;
    friend class DocumentWorkspace;
    friend class PropertyGesture;
    friend class BatchPropertyGesture;
    struct HistoryEntry
    {
        std::variant<PropertyCommand, StructuralCommand, BatchCommand> command;
        std::uint64_t beforeToken = 0;
        std::uint64_t afterToken = 0;
        std::size_t retainedBytes = 0;
    };

    struct PreparedHistory
    {
        std::size_t priorCursor = 0;
        std::size_t payloadBytes = 0;
        std::uint64_t afterToken = 0;
        bool staged = false;
    };

    SelectionState capture_selection(SelectionState selection) const;
    bool can_migrate_project_identity(const DocumentIdentity &from,
                                     const DocumentIdentity &to) const noexcept;
    void migrate_project_identity(const DocumentIdentity &from,
                                  const DocumentIdentity &to) noexcept;
    PreparedHistory prepare_history(std::variant<PropertyCommand, StructuralCommand, BatchCommand> command);
    void commit_history(PreparedHistory &&prepared) noexcept;
    void rollback_history(PreparedHistory &prepared) noexcept;
    void append_history(std::variant<PropertyCommand, StructuralCommand, BatchCommand> command);
    CommandResult apply_property_batch(DocumentSession &document, BatchCommand &&command,
                                       CommandDirection direction, bool recordHistory,
                                       bool deriveBefore = false);
    CommandResult apply_batch_entry(DocumentSession &document, const BatchCommand &command,
                                    CommandDirection direction);
    CommandResult commit_batch_candidate(DocumentSession &document, DocumentSession &&candidate,
                                         BatchCommand &&command, DocumentChange change,
                                         bool recordHistory);
    void prune_history();
    bool token_reachable(std::uint64_t token) const noexcept;
    std::uint64_t current_token() const noexcept;
    void update_gesture_dirty(PropertyGesture &gesture, bool changed) noexcept;
    bool finish_gesture(PropertyGesture &gesture, bool commit) noexcept;
    void rollback_gesture(PropertyGesture &gesture) noexcept;
    void update_batch_gesture_dirty(BatchPropertyGesture &gesture, bool changed) noexcept;
    bool finish_batch_gesture(BatchPropertyGesture &gesture, bool commit) noexcept;
    void rollback_batch_gesture(BatchPropertyGesture &gesture) noexcept;

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
    EditCommandWorkStats lastWorkStats_;
    std::string lastError_;
    bool failNextHistoryRetentionForTesting_ = false;
};
} // namespace platformator_editor
