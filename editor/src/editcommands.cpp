#include "editcommands.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <platformator/gameobject.h>

namespace platformator_editor
{
namespace
{
template <typename Value>
struct ResolvedTarget
{
    Value *parent = nullptr;
    bool finalKey = false;
    std::string key;
    std::size_t index = 0;
    PropertyValue current;
};

struct ResolveFailure
{
    CommandStatus status = CommandStatus::Rejected;
    std::string message;
};

template <typename Value>
Value *resolve_object_at(Value &root, const ObjectLocation &location)
{
    if (!root.is_array() || location.childIndices.empty())
        return nullptr;
    Value *object = nullptr;
    Value *siblings = &root;
    for (std::size_t depth = 0; depth < location.childIndices.size(); ++depth)
    {
        if (depth > 0)
        {
            if (object == nullptr || !object->is_object())
                return nullptr;
            const auto children = object->find("children");
            if (children == object->end())
                return nullptr;
            siblings = &*children;
        }
        const auto index = location.childIndices[depth];
        if (!siblings->is_array() || index >= siblings->size())
            return nullptr;
        object = &(*siblings)[index];
    }
    return object;
}

template <typename Value>
Value *find_component(Value &object, int componentId)
{
    if (!object.is_object())
        return nullptr;
    auto components = object.find("components");
    if (components == object.end() || !components->is_array())
        return nullptr;
    for (auto &component : *components)
    {
        if (!component.is_object())
            continue;
        const auto id = component.find("id");
        if (id == component.end() || !(id->is_number_integer() || id->is_number_unsigned()))
            continue;
        try
        {
            if (id->template get<std::int64_t>() == componentId)
                return &component;
        }
        catch (...)
        {
        }
    }
    return nullptr;
}

template <typename Value>
Value *find_address_owner(Value &root, const PropertyAddress &address, ResolveFailure &failure)
{
    if (address.behaviorOrdinal && !address.componentId)
    {
        failure = {CommandStatus::Rejected, "behavior address requires a component ID"};
        return nullptr;
    }
    const auto location = find_object_location(root, address.objectId);
    if (!location)
    {
        failure = {CommandStatus::StaleAddress, "object ID is no longer present"};
        return nullptr;
    }
    Value *object = resolve_object_at(root, *location);
    if (object == nullptr)
    {
        failure = {CommandStatus::StaleAddress, "object location is no longer valid"};
        return nullptr;
    }
    if (!address.componentId)
        return object;

    Value *component = find_component(*object, *address.componentId);
    if (component == nullptr)
    {
        failure = {CommandStatus::StaleAddress, "component ID is no longer present on the object"};
        return nullptr;
    }
    if (!address.behaviorOrdinal)
        return component;

    auto behaviors = component->find("behaviors");
    if (behaviors == component->end() || !behaviors->is_array() || *address.behaviorOrdinal >= behaviors->size())
    {
        failure = {CommandStatus::StaleAddress, "behavior ordinal is no longer present"};
        return nullptr;
    }
    return &(*behaviors)[*address.behaviorOrdinal];
}

template <typename Value>
std::optional<ResolvedTarget<Value>> resolve_target(Value &root, const PropertyAddress &address,
                                                     ResolveFailure &failure)
{
    if (address.fieldPath.empty())
    {
        failure = {CommandStatus::Rejected, "property address has an empty field path"};
        return std::nullopt;
    }
    Value *current = find_address_owner(root, address, failure);
    if (current == nullptr)
        return std::nullopt;

    for (std::size_t depth = 0; depth + 1 < address.fieldPath.size(); ++depth)
    {
        const auto &segment = address.fieldPath[depth];
        if (const auto *key = std::get_if<std::string>(&segment))
        {
            if (!current->is_object())
            {
                failure = {CommandStatus::Rejected, "field path expects an object"};
                return std::nullopt;
            }
            auto found = current->find(*key);
            if (found == current->end())
            {
                failure = {CommandStatus::Rejected, "intermediate object key is missing"};
                return std::nullopt;
            }
            current = &*found;
        }
        else
        {
            const auto index = std::get<std::size_t>(segment);
            if (!current->is_array() || index >= current->size())
            {
                failure = {CommandStatus::Rejected, "field path array index is out of range"};
                return std::nullopt;
            }
            current = &(*current)[index];
        }
    }

    ResolvedTarget<Value> target;
    const auto &last = address.fieldPath.back();
    if (const auto *key = std::get_if<std::string>(&last))
    {
        if (!current->is_object())
        {
            failure = {CommandStatus::Rejected, "final field path expects an object"};
            return std::nullopt;
        }
        target.parent = current;
        target.finalKey = true;
        target.key = *key;
        const auto found = current->find(*key);
        if (found != current->end())
            target.current = PropertyValue::of(*found);
        else
            target.current = PropertyValue::missing();
    }
    else
    {
        const auto index = std::get<std::size_t>(last);
        if (!current->is_array() || index >= current->size())
        {
            failure = {CommandStatus::Rejected, "final field path array index is out of range"};
            return std::nullopt;
        }
        target.parent = current;
        target.index = index;
        target.current = PropertyValue::of((*current)[index]);
    }
    return target;
}

CommandResult failed(CommandStatus status, std::string message, const DocumentSession &document)
{
    CommandResult result;
    result.status = status;
    result.message = std::move(message);
    result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                   document.valueRevision, false};
    return result;
}

bool values_equal(const PropertyValue &left, const PropertyValue &right)
{
    return left.present == right.present && (!left.present || left.value == right.value);
}

std::size_t json_payload_bytes(const Json &value)
{
    return value.dump().size();
}

std::size_t command_payload_bytes(const PropertyCommand &command)
{
    std::size_t bytes = sizeof(PropertyCommand) + command.address.fieldPath.size() * sizeof(FieldPathSegment);
    for (const auto &segment : command.address.fieldPath)
        if (const auto *key = std::get_if<std::string>(&segment))
            bytes += key->size();
    if (command.before.present)
        bytes += json_payload_bytes(command.before.value);
    if (command.after.present)
        bytes += json_payload_bytes(command.after.value);
    return bytes;
}

std::size_t command_payload_bytes(const StructuralCommand &command)
{
    return sizeof(StructuralCommand) + json_payload_bytes(command.payload);
}

bool selection_is_empty(const Selection &selection)
{
    return !selection.objectId && !selection.componentId && !selection.behaviorOrdinal &&
           selection.focusedPanel == FocusedPanel::None;
}

Json *mutable_object(DocumentSession &document, int objectId)
{
    const auto found = document.indexes.objectsById.find(objectId);
    if (found == document.indexes.objectsById.end())
        return nullptr;
    return resolve_object_at(document.root, found->second);
}

Json *mutable_siblings(DocumentSession &document, const std::optional<int> &parentObjectId)
{
    if (!parentObjectId)
        return &document.root;
    Json *parent = mutable_object(document, *parentObjectId);
    if (parent == nullptr || !parent->is_object())
        return nullptr;
    auto children = parent->find("children");
    return children != parent->end() && children->is_array() ? &*children : nullptr;
}

std::optional<int> component_type(const Json &component)
{
    const auto type = component.find("type");
    if (type == component.end() || !(type->is_number_integer() || type->is_number_unsigned()))
        return std::nullopt;
    try
    {
        const auto value = type->get<std::int64_t>();
        if (value < 0 || value > std::numeric_limits<int>::max())
            return std::nullopt;
        return static_cast<int>(value);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<int> record_id(const Json &record)
{
    const auto id = record.find("id");
    if (id == record.end() || !(id->is_number_integer() || id->is_number_unsigned()))
        return std::nullopt;
    try
    {
        const auto value = id->get<std::int64_t>();
        if (value < 0 || value > std::numeric_limits<int>::max())
            return std::nullopt;
        return static_cast<int>(value);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool file_id_in_use(const DocumentSession &document, int id)
{
    return document.indexes.objectsById.contains(id) || document.indexes.componentsById.contains(id);
}

std::optional<int> first_unused_id(const DocumentSession &document)
{
    int candidate = 0;
    for (const int id : document.indexes.orderedObjectIds)
    {
        if (id == std::numeric_limits<int>::max())
            return std::nullopt;
        candidate = std::max(candidate, id + 1);
    }
    for (const auto &[id, location] : document.indexes.componentsById)
    {
        (void)location;
        if (id == std::numeric_limits<int>::max())
            return std::nullopt;
        candidate = std::max(candidate, id + 1);
    }
    while (file_id_in_use(document, candidate))
    {
        if (candidate == std::numeric_limits<int>::max())
            return std::nullopt;
        ++candidate;
    }
    return candidate;
}

std::optional<int> next_file_id_from_set(int &candidate, const std::vector<int> &assigned)
{
    while (candidate >= 0 && std::find(assigned.begin(), assigned.end(), candidate) != assigned.end())
    {
        if (candidate == std::numeric_limits<int>::max())
            return std::nullopt;
        ++candidate;
    }
    if (candidate < 0)
        return std::nullopt;
    const int result = candidate;
    if (candidate == std::numeric_limits<int>::max())
        candidate = -1;
    else
        ++candidate;
    return result;
}

void collect_subtree_object_ids(const Json &root, std::vector<int> &ids)
{
    std::vector<const Json *> pending{&root};
    while (!pending.empty())
    {
        const Json *object = pending.back();
        pending.pop_back();
        if (!object->is_object())
            continue;
        if (auto id = record_id(*object))
            ids.push_back(*id);
        const auto components = object->find("components");
        if (components != object->end() && components->is_array())
        {
            for (const auto &component : *components)
                if (auto id = record_id(component))
                    ids.push_back(*id);
        }
        const auto children = object->find("children");
        if (children != object->end() && children->is_array())
            for (const auto &child : *children)
                pending.push_back(&child);
    }
}

void remap_subtree_ids(Json &root, int firstId, std::vector<int> &assigned)
{
    int candidate = firstId;
    std::vector<Json *> pending{&root};
    while (!pending.empty())
    {
        Json *object = pending.back();
        pending.pop_back();
        if (!object->is_object())
            continue;
        auto objectId = next_file_id_from_set(candidate, assigned);
        if (!objectId)
            throw std::overflow_error("no file IDs remain for duplicated subtree");
        (*object)["id"] = *objectId;
        assigned.push_back(*objectId);
        auto components = object->find("components");
        if (components != object->end() && components->is_array())
        {
            for (auto &component : *components)
            {
                auto componentId = next_file_id_from_set(candidate, assigned);
                if (!componentId)
                    throw std::overflow_error("no file IDs remain for duplicated subtree");
                component["id"] = *componentId;
                assigned.push_back(*componentId);
            }
        }
        auto children = object->find("children");
        if (children != object->end() && children->is_array())
            for (auto &child : *children)
                pending.push_back(&child);
    }
}

StructuralCommandResult failed_structural(CommandStatus status, std::string message,
                                          const DocumentSession &document)
{
    StructuralCommandResult result;
    result.status = status;
    result.message = std::move(message);
    result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                   document.valueRevision, false};
    return result;
}

StructuralCommandResult commit_structural(DocumentSession &document, StructuralCommand command,
                                           bool inserting)
{
    if (document.structureRevision == std::numeric_limits<std::uint64_t>::max())
        return failed_structural(CommandStatus::Rejected, "document structure revision space exhausted", document);
    if (document.documentGeneration != command.documentGeneration)
        return failed_structural(CommandStatus::StaleAddress, "structural command belongs to an older document generation", document);
    rebuild_document_indexes(document);

    Json *siblings = nullptr;
    if (command.kind == StructuralCommandKind::InsertObject || command.kind == StructuralCommandKind::DeleteObject)
        siblings = mutable_siblings(document, command.parentObjectId);
    else
    {
        Json *owner = mutable_object(document, command.ownerObjectId);
        if (owner == nullptr)
            return failed_structural(CommandStatus::StaleAddress, "structural command owner is no longer present", document);
        if (command.kind == StructuralCommandKind::InsertBehavior ||
            command.kind == StructuralCommandKind::DeleteBehavior)
        {
            if (!command.componentId)
                return failed_structural(CommandStatus::Rejected, "behavior command requires a script component ID", document);
            Json *script = find_component(*owner, *command.componentId);
            if (script == nullptr || component_type(*script) != SCRIPT)
                return failed_structural(CommandStatus::StaleAddress, "script component is no longer present", document);
            auto collection = script->find("behaviors");
            if (collection == script->end() || !collection->is_array())
                return failed_structural(CommandStatus::Rejected, "script behavior list is malformed", document);
            siblings = &*collection;
        }
        else
        {
            auto collection = owner->find("components");
            if (collection == owner->end() || !collection->is_array())
                return failed_structural(CommandStatus::Rejected, "component collection is missing or malformed", document);
            siblings = &*collection;
        }
    }
    if (siblings == nullptr || !siblings->is_array())
        return failed_structural(CommandStatus::StaleAddress, "structural destination no longer exists", document);

    const std::size_t oldIndex = command.insertionIndex;
    if (inserting)
    {
        if (oldIndex > siblings->size())
            return failed_structural(CommandStatus::Rejected, "structural insertion index is out of range", document);
        try
        {
            siblings->insert(siblings->begin() + static_cast<Json::difference_type>(oldIndex), command.payload);
        }
        catch (const std::exception &error)
        {
            return failed_structural(CommandStatus::Rejected, std::string("cannot insert structural payload: ") + error.what(), document);
        }
    }
    else
    {
        if (oldIndex >= siblings->size() || (*siblings)[oldIndex] != command.payload)
            return failed_structural(CommandStatus::Rejected, "structural command precondition no longer matches", document);
        siblings->erase(siblings->begin() + static_cast<Json::difference_type>(oldIndex));
    }

    const auto previousRevision = document.structureRevision;
    document.structureRevision = previousRevision + 1;
    try
    {
        rebuild_document_indexes(document);
    }
    catch (const std::exception &error)
    {
        document.structureRevision = previousRevision;
        if (inserting)
            siblings->erase(siblings->begin() + static_cast<Json::difference_type>(oldIndex));
        else
            siblings->insert(siblings->begin() + static_cast<Json::difference_type>(oldIndex), std::move(command.payload));
        return failed_structural(CommandStatus::Rejected, std::string("structural payload is invalid: ") + error.what(), document);
    }

    StructuralCommandResult result;
    result.status = CommandStatus::Changed;
    result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                   document.valueRevision, true};
    result.command = std::move(command);
    if (result.command->kind == StructuralCommandKind::InsertObject ||
        result.command->kind == StructuralCommandKind::DeleteObject)
        collect_subtree_object_ids(result.command->payload, result.affectedObjectIds);
    else
        result.affectedObjectIds.push_back(result.command->ownerObjectId);
    // The service-level observer list is attached by the caller after the commit.
    return result;
}
} // namespace

PropertyValue PropertyValue::missing()
{
    return PropertyValue{false, Json{}};
}

PropertyValue PropertyValue::of(Json value)
{
    return PropertyValue{true, std::move(value)};
}

EditCommandService::EditCommandService(std::size_t historyBudgetBytes)
    : historyBudgetBytes_(historyBudgetBytes)
{
}

void EditCommandService::subscribe(Observer observer)
{
    if (observer)
        observers_.push_back(std::move(observer));
}

void EditCommandService::set_selection_provider(SelectionProvider provider)
{
    selectionProvider_ = std::move(provider);
}

SelectionState EditCommandService::capture_selection(SelectionState selection) const
{
    if (selection_is_empty(selection.before) && selection_is_empty(selection.after) && selectionProvider_)
    {
        try
        {
            selection.before = selectionProvider_();
            selection.after = selection.before;
        }
        catch (...)
        {
        }
    }
    return selection;
}

void EditCommandService::reset_history(std::uint64_t documentGeneration)
{
    history_.clear();
    cursor_ = 0;
    retainedBytes_ = 0;
    activeGestures_ = 0;
    activeChangedGestures_ = 0;
    historyGeneration_ = documentGeneration;
    baseToken_ = nextToken_;
    if (nextToken_ != std::numeric_limits<std::uint64_t>::max())
        ++nextToken_;
    savedToken_ = baseToken_;
}

void EditCommandService::mark_saved() noexcept
{
    savedToken_ = current_token();
}

std::uint64_t EditCommandService::current_token() const noexcept
{
    return cursor_ == 0 ? baseToken_ : history_[cursor_ - 1].afterToken;
}

bool EditCommandService::token_reachable(std::uint64_t token) const noexcept
{
    if (token == baseToken_ || token == current_token())
        return true;
    for (const auto &entry : history_)
        if (entry.beforeToken == token || entry.afterToken == token)
            return true;
    return false;
}

bool EditCommandService::dirty() const noexcept
{
    return activeChangedGestures_ > 0 || current_token() != savedToken_ || !token_reachable(savedToken_);
}

HistoryStatus EditCommandService::history_status() const noexcept
{
    HistoryStatus status;
    status.canUndo = cursor_ > 0;
    status.canRedo = cursor_ < history_.size();
    status.dirty = dirty();
    status.savedTokenReachable = token_reachable(savedToken_);
    status.overBudget = retainedBytes_ > historyBudgetBytes_;
    status.retainedBytes = retainedBytes_;
    status.budgetBytes = historyBudgetBytes_;
    status.undoCommands = cursor_;
    status.redoCommands = history_.size() - cursor_;
    status.currentToken = current_token();
    status.savedToken = savedToken_;
    return status;
}

void EditCommandService::set_last_selection_state(SelectionState selection)
{
    if (cursor_ == 0)
        return;
    auto &command = history_[cursor_ - 1].command;
    std::visit([&](auto &stored) { stored.selection = std::move(selection); }, command);
}

void EditCommandService::append_history(std::variant<PropertyCommand, StructuralCommand> command)
{
    if (nextToken_ == std::numeric_limits<std::uint64_t>::max())
        return;
    while (history_.size() > cursor_)
    {
        retainedBytes_ -= history_.back().retainedBytes;
        history_.pop_back();
    }
    const std::uint64_t beforeToken = current_token();
    const std::size_t bytes = std::visit([](const auto &stored) { return command_payload_bytes(stored); }, command);
    const std::uint64_t afterToken = nextToken_++;
    history_.push_back(HistoryEntry{std::move(command), beforeToken, afterToken, bytes});
    retainedBytes_ += bytes;
    ++cursor_;
    prune_history();
}

void EditCommandService::prune_history()
{
    while (retainedBytes_ > historyBudgetBytes_ && history_.size() > 1)
    {
        const auto removed = history_.front();
        retainedBytes_ -= removed.retainedBytes;
        history_.pop_front();
        if (cursor_ > 0)
        {
            --cursor_;
            baseToken_ = removed.afterToken;
        }
        else
            baseToken_ = removed.beforeToken;
    }
}

CommandResult EditCommandService::undo(DocumentSession &document, SelectionModel &selection)
{
    if (cursor_ == 0)
        return failed(CommandStatus::Rejected, "there is no edit to undo", document);
    if (document.documentGeneration != historyGeneration_)
        return failed(CommandStatus::StaleAddress, "history belongs to an older document generation", document);

    auto &entry = history_[cursor_ - 1];
    ++suppressHistory_;
    CommandResult result = std::visit([&](const auto &command) {
        auto applied = apply(document, command, CommandDirection::Reverse);
        if constexpr (std::is_same_v<std::decay_t<decltype(command)>, PropertyCommand>)
            return applied;
        else
        {
            CommandResult converted;
            converted.status = applied.status;
            converted.message = std::move(applied.message);
            converted.affectedObjectIds = std::move(applied.affectedObjectIds);
            converted.change = applied.change;
            return converted;
        }
    }, entry.command);
    --suppressHistory_;
    if (result.status == CommandStatus::Changed || result.status == CommandStatus::NoChange)
    {
        --cursor_;
        const SelectionState stored = std::visit([](const auto &command) { return command.selection; }, entry.command);
        selection.set(document, stored.before);
    }
    return result;
}

CommandResult EditCommandService::redo(DocumentSession &document, SelectionModel &selection)
{
    if (cursor_ >= history_.size())
        return failed(CommandStatus::Rejected, "there is no edit to redo", document);
    if (document.documentGeneration != historyGeneration_)
        return failed(CommandStatus::StaleAddress, "history belongs to an older document generation", document);

    auto &entry = history_[cursor_];
    ++suppressHistory_;
    CommandResult result = std::visit([&](const auto &command) {
        auto applied = apply(document, command, CommandDirection::Forward);
        if constexpr (std::is_same_v<std::decay_t<decltype(command)>, PropertyCommand>)
            return applied;
        else
        {
            CommandResult converted;
            converted.status = applied.status;
            converted.message = std::move(applied.message);
            converted.affectedObjectIds = std::move(applied.affectedObjectIds);
            converted.change = applied.change;
            return converted;
        }
    }, entry.command);
    --suppressHistory_;
    if (result.status == CommandStatus::Changed || result.status == CommandStatus::NoChange)
    {
        ++cursor_;
        const SelectionState stored = std::visit([](const auto &command) { return command.selection; }, entry.command);
        selection.set(document, stored.after);
    }
    return result;
}

void EditCommandService::update_gesture_dirty(PropertyGesture &gesture, bool changed) noexcept
{
    if (gesture.dirtyCounted_ == changed)
        return;
    gesture.dirtyCounted_ = changed;
    if (changed)
        ++activeChangedGestures_;
    else if (activeChangedGestures_ > 0)
        --activeChangedGestures_;
}

void EditCommandService::finish_gesture(PropertyGesture &gesture, bool commit) noexcept
{
    if (!gesture.active_)
        return;
    if (gesture.dirtyCounted_ && activeChangedGestures_ > 0)
        --activeChangedGestures_;
    if (activeGestures_ > 0)
        --activeGestures_;
    const bool validGeneration = gesture.command_.address.documentGeneration == historyGeneration_;
    if (commit && validGeneration && !values_equal(gesture.command_.before, gesture.command_.after))
    {
        try
        {
            append_history(gesture.command_);
        }
        catch (...)
        {
        }
    }
    gesture.active_ = false;
    gesture.dirtyCounted_ = false;
    gesture.service_ = nullptr;
}

CommandResult EditCommandService::set_property(DocumentSession &document, const PropertyAddress &address,
                                               Json value, SelectionState selection)
{
    if (document.documentGeneration != address.documentGeneration)
        return failed(CommandStatus::StaleAddress, "property address belongs to an older document generation", document);
    ResolveFailure failure;
    auto target = resolve_target(document.root, address, failure);
    if (!target)
        return failed(failure.status, std::move(failure.message), document);
    PropertyCommand command{address, target->current, PropertyValue::of(std::move(value)), std::move(selection)};
    if (values_equal(command.before, command.after))
    {
        CommandResult result;
        result.status = CommandStatus::NoChange;
        result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                       document.valueRevision, false};
        return result;
    }
    return apply(document, command, CommandDirection::Forward);
}

CommandResult EditCommandService::erase_property(DocumentSession &document, const PropertyAddress &address,
                                                 SelectionState selection)
{
    if (document.documentGeneration != address.documentGeneration)
        return failed(CommandStatus::StaleAddress, "property address belongs to an older document generation", document);
    ResolveFailure failure;
    auto target = resolve_target(document.root, address, failure);
    if (!target)
        return failed(failure.status, std::move(failure.message), document);
    PropertyCommand command{address, target->current, PropertyValue::missing(), std::move(selection)};
    if (!command.before.present)
    {
        CommandResult result;
        result.status = CommandStatus::NoChange;
        result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                       document.valueRevision, false};
        return result;
    }
    return apply(document, command, CommandDirection::Forward);
}

CommandResult EditCommandService::apply(DocumentSession &document, const PropertyCommand &command,
                                       CommandDirection direction)
{
    if (historyGeneration_ == 0)
        historyGeneration_ = document.documentGeneration;
    if (document.documentGeneration != command.address.documentGeneration)
        return failed(CommandStatus::StaleAddress, "property command belongs to an older document generation", document);
    if (command.address.objectId < 0)
        return failed(CommandStatus::Rejected, "property address has an invalid object ID", document);

    const PropertyValue &expected = direction == CommandDirection::Forward ? command.before : command.after;
    const PropertyValue &replacementValue = direction == CommandDirection::Forward ? command.after : command.before;
    if (values_equal(expected, replacementValue))
    {
        CommandResult result;
        result.status = CommandStatus::NoChange;
        result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                       document.valueRevision, false};
        return result;
    }
    if (document.valueRevision == std::numeric_limits<std::uint64_t>::max())
        return failed(CommandStatus::Rejected, "document value revision space exhausted", document);
    const bool recordHistory = direction == CommandDirection::Forward && suppressHistory_ == 0;
    if (recordHistory && nextToken_ == std::numeric_limits<std::uint64_t>::max())
        return failed(CommandStatus::Rejected, "history state token space exhausted", document);
    PropertyCommand retainedCommand = command;
    if (recordHistory)
        retainedCommand.selection = capture_selection(std::move(retainedCommand.selection));

    ResolveFailure failure;
    auto target = resolve_target(document.root, command.address, failure);
    if (!target)
        return failed(failure.status, std::move(failure.message), document);
    if (!values_equal(target->current, expected))
        return failed(CommandStatus::Rejected, "property command precondition no longer matches", document);
    if (!replacementValue.present && !target->finalKey)
        return failed(CommandStatus::Rejected, "array entries cannot be removed by a property command", document);

    // Allocate all result/notification storage before touching the DOM. Existing
    // values are replaced with a no-throw JSON swap; missing object keys use
    // map insertion's strong exception guarantee.
    CommandResult result;
    result.status = CommandStatus::Changed;
    result.affectedObjectIds.push_back(command.address.objectId);
    result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                   document.valueRevision + 1, false};
    result.propertyCommand = retainedCommand;
    const auto observers = observers_;
    Json replacement = replacementValue.present ? replacementValue.value : Json{};

    if (target->finalKey)
    {
        auto found = target->parent->find(target->key);
        if (!replacementValue.present)
            target->parent->erase(target->key);
        else if (found == target->parent->end())
            target->parent->emplace(target->key, std::move(replacement));
        else
            found.value().swap(replacement);
    }
    else
        (*target->parent)[target->index].swap(replacement);

    ++document.valueRevision;
    result.change.valueRevision = document.valueRevision;
    for (const auto &observer : observers)
    {
        try
        {
            observer(result.change, result.affectedObjectIds);
        }
        catch (...)
        {
            // A notification is advisory; committed document state is retained.
        }
    }
    if (recordHistory)
        append_history(std::move(retainedCommand));
    return result;
}

StructuralCommandResult EditCommandService::apply(DocumentSession &document, const StructuralCommand &command,
                                                   CommandDirection direction)
{
    if (historyGeneration_ == 0)
        historyGeneration_ = document.documentGeneration;
    const bool insertKind = command.kind == StructuralCommandKind::InsertObject ||
                            command.kind == StructuralCommandKind::InsertComponent ||
                            command.kind == StructuralCommandKind::InsertBehavior;
    const bool deleteKind = command.kind == StructuralCommandKind::DeleteObject ||
                            command.kind == StructuralCommandKind::DeleteComponent ||
                            command.kind == StructuralCommandKind::DeleteBehavior;
    if (!insertKind && !deleteKind)
        return failed_structural(CommandStatus::Rejected, "unknown structural command kind", document);
    const bool inserting = (direction == CommandDirection::Forward) == insertKind;
    const bool recordHistory = direction == CommandDirection::Forward && suppressHistory_ == 0;
    if (recordHistory && nextToken_ == std::numeric_limits<std::uint64_t>::max())
        return failed_structural(CommandStatus::Rejected, "history state token space exhausted", document);
    StructuralCommand retainedCommand = command;
    if (recordHistory)
        retainedCommand.selection = capture_selection(std::move(retainedCommand.selection));
    const auto observers = observers_;
    auto result = commit_structural(document, retainedCommand, inserting);
    if (result.status == CommandStatus::Changed)
    {
        if (recordHistory)
            append_history(retainedCommand);
        for (const auto &observer : observers)
        {
            try
            {
                observer(result.change, result.affectedObjectIds);
            }
            catch (...)
            {
                // Notifications are advisory after a committed structural edit.
            }
        }
    }
    return result;
}

StructuralCommandResult EditCommandService::create_object(DocumentSession &document,
                                                           std::optional<int> parentObjectId,
                                                           std::string name, std::size_t insertionIndex,
                                                           SelectionState selection)
{
    rebuild_document_indexes(document);
    if (parentObjectId && !document.indexes.objectsById.contains(*parentObjectId))
        return failed_structural(CommandStatus::StaleAddress, "parent object is no longer present", document);
    Json *siblings = mutable_siblings(document, parentObjectId);
    if (siblings == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "object insertion parent is unavailable", document);
    if (insertionIndex == std::numeric_limits<std::size_t>::max())
        insertionIndex = siblings->size();
    if (insertionIndex > siblings->size())
        return failed_structural(CommandStatus::Rejected, "object insertion index is out of range", document);
    const auto id = first_unused_id(document);
    if (!id)
        return failed_structural(CommandStatus::Rejected, "no file IDs remain for a new object", document);
    Json object = {{"id", *id},
                   {"name", std::move(name)},
                   {"tag", ""},
                   {"active", true},
                   {"position", {{"x", 0.0}, {"y", 0.0}}},
                   {"scale", {{"x", 1.0}, {"y", 1.0}}},
                   {"rotation", 0.0},
                   {"children", Json::array()},
                   {"components", Json::array()}};
    StructuralCommand command{document.documentGeneration, StructuralCommandKind::InsertObject,
                              parentObjectId, *id, std::nullopt, insertionIndex, std::move(object),
                              std::move(selection)};
    return apply(document, command, CommandDirection::Forward);
}

StructuralCommandResult EditCommandService::duplicate_object(DocumentSession &document, int objectId,
                                                              SelectionState selection)
{
    rebuild_document_indexes(document);
    const auto location = document.indexes.objectsById.find(objectId);
    if (location == document.indexes.objectsById.end())
        return failed_structural(CommandStatus::StaleAddress, "object to duplicate is no longer present", document);
    Json *source = resolve_object_at(document.root, location->second);
    if (source == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "object location is no longer valid", document);
    Json duplicate = *source;
    const auto parentEntry = document.indexes.parentById.find(objectId);
    if (parentEntry == document.indexes.parentById.end())
        return failed_structural(CommandStatus::StaleAddress, "object parent index is unavailable", document);
    Json *siblings = mutable_siblings(document, parentEntry->second);
    if (siblings == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "object sibling list is unavailable", document);
    const std::size_t sourceIndex = location->second.childIndices.back();
    if (sourceIndex >= siblings->size())
        return failed_structural(CommandStatus::StaleAddress, "object sibling index is no longer valid", document);
    const auto firstId = first_unused_id(document);
    if (!firstId)
        return failed_structural(CommandStatus::Rejected, "no file IDs remain for duplicated subtree", document);

    std::vector<int> assigned;
    try
    {
        remap_subtree_ids(duplicate, *firstId, assigned);
    }
    catch (const std::exception &error)
    {
        return failed_structural(CommandStatus::Rejected, error.what(), document);
    }

    const std::string sourceName = source->value("name", std::string("GameObject"));
    std::string copiedName = sourceName + " Copy";
    auto nameTaken = [&](const std::string &candidate) {
        return std::any_of(siblings->begin(), siblings->end(), [&](const Json &sibling) {
            return sibling.is_object() && sibling.value("name", std::string{}) == candidate;
        });
    };
    for (std::size_t suffix = 2; nameTaken(copiedName); ++suffix)
        copiedName = sourceName + " Copy " + std::to_string(suffix);
    duplicate["name"] = std::move(copiedName);
    auto position = duplicate.find("position");
    if (position == duplicate.end() || !position->is_object() ||
        !position->contains("x") || !position->contains("y") ||
        !position->at("x").is_number() || !position->at("y").is_number())
        return failed_structural(CommandStatus::Rejected, "object position cannot be offset for duplication", document);
    const double x = position->at("x").get<double>() + 32.0;
    const double y = position->at("y").get<double>() + 32.0;
    if (!std::isfinite(x) || !std::isfinite(y))
        return failed_structural(CommandStatus::Rejected, "duplicated object position is outside the finite range", document);
    (*position)["x"] = x;
    (*position)["y"] = y;

    StructuralCommand command{document.documentGeneration, StructuralCommandKind::InsertObject,
                              parentEntry->second, objectId, std::nullopt, sourceIndex + 1,
                              std::move(duplicate), std::move(selection)};
    return apply(document, command, CommandDirection::Forward);
}

StructuralCommandResult EditCommandService::delete_object(DocumentSession &document, int objectId,
                                                           SelectionState selection)
{
    rebuild_document_indexes(document);
    const auto location = document.indexes.objectsById.find(objectId);
    const auto parent = document.indexes.parentById.find(objectId);
    if (location == document.indexes.objectsById.end() || parent == document.indexes.parentById.end())
        return failed_structural(CommandStatus::StaleAddress, "object to delete is no longer present", document);
    Json *object = resolve_object_at(document.root, location->second);
    if (object == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "object location is no longer valid", document);
    StructuralCommand command{document.documentGeneration, StructuralCommandKind::DeleteObject,
                              parent->second, objectId, std::nullopt,
                              location->second.childIndices.back(), *object, std::move(selection)};
    return apply(document, command, CommandDirection::Forward);
}

StructuralCommandResult EditCommandService::add_component(DocumentSession &document, int objectId,
                                                          Json component, std::size_t insertionIndex,
                                                          SelectionState selection)
{
    rebuild_document_indexes(document);
    Json *owner = mutable_object(document, objectId);
    if (owner == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "component owner is no longer present", document);
    if (!component.is_object())
        return failed_structural(CommandStatus::Rejected, "component payload must be an object", document);
    const auto type = component_type(component);
    if (!type)
        return failed_structural(CommandStatus::Rejected, "component payload requires a valid type discriminator", document);
    auto components = owner->find("components");
    if (components == owner->end() || !components->is_array())
        return failed_structural(CommandStatus::Rejected, "component collection is malformed", document);
    if (std::any_of(components->begin(), components->end(), [&](const Json &existing) {
            return component_type(existing) == type;
        }))
        return failed_structural(CommandStatus::Rejected, "the object already has this component slot", document);
    if (insertionIndex == std::numeric_limits<std::size_t>::max())
        insertionIndex = components->size();
    if (insertionIndex > components->size())
        return failed_structural(CommandStatus::Rejected, "component insertion index is out of range", document);
    const auto id = first_unused_id(document);
    if (!id)
        return failed_structural(CommandStatus::Rejected, "no file IDs remain for a new component", document);
    component["id"] = *id;
    StructuralCommand command{document.documentGeneration, StructuralCommandKind::InsertComponent,
                              std::nullopt, objectId, std::nullopt, insertionIndex,
                              std::move(component), std::move(selection)};
    return apply(document, command, CommandDirection::Forward);
}

StructuralCommandResult EditCommandService::remove_component(DocumentSession &document, int objectId,
                                                             int componentId, SelectionState selection)
{
    rebuild_document_indexes(document);
    Json *owner = mutable_object(document, objectId);
    if (owner == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "component owner is no longer present", document);
    auto components = owner->find("components");
    if (components == owner->end() || !components->is_array())
        return failed_structural(CommandStatus::Rejected, "component collection is malformed", document);
    for (std::size_t index = 0; index < components->size(); ++index)
    {
        if (record_id((*components)[index]) != componentId)
            continue;
        StructuralCommand command{document.documentGeneration, StructuralCommandKind::DeleteComponent,
                                  std::nullopt, objectId, std::nullopt, index,
                                  (*components)[index], std::move(selection)};
        return apply(document, command, CommandDirection::Forward);
    }
    return failed_structural(CommandStatus::StaleAddress, "component ID is no longer present on the object", document);
}

StructuralCommandResult EditCommandService::add_behavior(DocumentSession &document, int objectId,
                                                         int scriptComponentId, Json behavior,
                                                         std::size_t insertionIndex,
                                                         SelectionState selection)
{
    rebuild_document_indexes(document);
    Json *owner = mutable_object(document, objectId);
    if (owner == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "behavior owner is no longer present", document);
    Json *script = find_component(*owner, scriptComponentId);
    if (script == nullptr || component_type(*script) != SCRIPT)
        return failed_structural(CommandStatus::StaleAddress, "script component is no longer present", document);
    if (!behavior.is_object() || !behavior.contains("type") || !behavior.at("type").is_string())
        return failed_structural(CommandStatus::Rejected, "behavior payload requires a string type", document);
    auto behaviors = script->find("behaviors");
    if (behaviors == script->end() || !behaviors->is_array())
        return failed_structural(CommandStatus::Rejected, "script behavior list is malformed", document);
    if (insertionIndex == std::numeric_limits<std::size_t>::max())
        insertionIndex = behaviors->size();
    if (insertionIndex > behaviors->size())
        return failed_structural(CommandStatus::Rejected, "behavior insertion index is out of range", document);
    StructuralCommand command{document.documentGeneration, StructuralCommandKind::InsertBehavior,
                              std::nullopt, objectId, scriptComponentId, insertionIndex,
                              std::move(behavior), std::move(selection)};
    return apply(document, command, CommandDirection::Forward);
}

StructuralCommandResult EditCommandService::remove_behavior(DocumentSession &document, int objectId,
                                                            int scriptComponentId,
                                                            std::size_t behaviorOrdinal,
                                                            SelectionState selection)
{
    rebuild_document_indexes(document);
    Json *owner = mutable_object(document, objectId);
    if (owner == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "behavior owner is no longer present", document);
    Json *script = find_component(*owner, scriptComponentId);
    if (script == nullptr || component_type(*script) != SCRIPT)
        return failed_structural(CommandStatus::StaleAddress, "script component is no longer present", document);
    auto behaviors = script->find("behaviors");
    if (behaviors == script->end() || !behaviors->is_array() || behaviorOrdinal >= behaviors->size())
        return failed_structural(CommandStatus::StaleAddress, "behavior ordinal is no longer present", document);
    StructuralCommand command{document.documentGeneration, StructuralCommandKind::DeleteBehavior,
                              std::nullopt, objectId, scriptComponentId, behaviorOrdinal,
                              (*behaviors)[behaviorOrdinal], std::move(selection)};
    return apply(document, command, CommandDirection::Forward);
}

std::optional<PropertyGesture> EditCommandService::begin_gesture(const DocumentSession &document,
                                                                 const PropertyAddress &address,
                                                                 SelectionState selection)
{
    if (historyGeneration_ == 0)
        historyGeneration_ = document.documentGeneration;
    if (document.documentGeneration != address.documentGeneration)
        return std::nullopt;
    ResolveFailure failure;
    auto target = resolve_target(document.root, address, failure);
    if (!target)
        return std::nullopt;
    PropertyCommand command{address, target->current, target->current,
                            capture_selection(std::move(selection))};
    ++activeGestures_;
    return PropertyGesture(*this, std::move(command));
}

PropertyGesture::PropertyGesture(EditCommandService &service, PropertyCommand command)
    : service_(&service), command_(std::move(command)), active_(true)
{
}

PropertyGesture::PropertyGesture(PropertyGesture &&other) noexcept
    : service_(std::exchange(other.service_, nullptr)), command_(std::move(other.command_)),
      active_(std::exchange(other.active_, false)), dirtyCounted_(std::exchange(other.dirtyCounted_, false))
{
}

PropertyGesture &PropertyGesture::operator=(PropertyGesture &&other) noexcept
{
    if (this == &other)
        return *this;
    if (active_ && service_ != nullptr)
        service_->finish_gesture(*this, true);
    service_ = std::exchange(other.service_, nullptr);
    command_ = std::move(other.command_);
    active_ = std::exchange(other.active_, false);
    dirtyCounted_ = std::exchange(other.dirtyCounted_, false);
    return *this;
}

PropertyGesture::~PropertyGesture()
{
    if (active_ && service_ != nullptr)
        service_->finish_gesture(*this, true);
}

bool PropertyGesture::active() const noexcept
{
    return active_;
}

CommandResult PropertyGesture::update(EditCommandService &service, DocumentSession &document, Json value)
{
    if (!active_)
        return failed(CommandStatus::Rejected, "property gesture is no longer active", document);
    Json retainedValue = value;
    ++service.suppressHistory_;
    auto result = service.set_property(document, command_.address, std::move(value), command_.selection);
    --service.suppressHistory_;
    if (result.status == CommandStatus::Changed || result.status == CommandStatus::NoChange)
    {
        command_.after = PropertyValue::of(std::move(retainedValue));
        service.update_gesture_dirty(*this, !values_equal(command_.before, command_.after));
    }
    return result;
}

std::optional<PropertyCommand> PropertyGesture::commit()
{
    if (!active_)
        return std::nullopt;
    if (values_equal(command_.before, command_.after))
    {
        if (service_ != nullptr)
            service_->finish_gesture(*this, false);
        return std::nullopt;
    }
    std::optional<PropertyCommand> committed = command_;
    if (service_ != nullptr)
        service_->finish_gesture(*this, true);
    else
        active_ = false;
    return committed;
}

CommandResult PropertyGesture::cancel(EditCommandService &service, DocumentSession &document)
{
    if (!active_)
        return failed(CommandStatus::Rejected, "property gesture is no longer active", document);
    if (values_equal(command_.before, command_.after))
    {
        service.finish_gesture(*this, false);
        CommandResult result;
        result.status = CommandStatus::NoChange;
        result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                       document.valueRevision, false};
        return result;
    }
    auto result = service.apply(document, command_, CommandDirection::Reverse);
    service.finish_gesture(*this, false);
    return result;
}
} // namespace platformator_editor
