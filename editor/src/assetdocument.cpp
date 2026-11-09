#include "assetdocument.h"

#include "platform/nativefile.h"

#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

namespace platformator_editor
{
namespace
{
bool same_source_path(const std::optional<std::filesystem::path> &source,
                      const std::filesystem::path &destination)
{
    return source && source->lexically_normal() == destination.lexically_normal();
}

std::optional<Json> value_at(const Json &root, const Json::json_pointer &pointer,
                             std::string &error)
{
    try
    {
        const auto parent = pointer.parent_pointer();
        if (!root.contains(parent))
        {
            error = "JSON pointer parent does not exist";
            return std::nullopt;
        }
        const Json &container = root.at(parent);
        const std::string &leaf = pointer.back();
        if (container.is_object())
        {
            const auto found = container.find(leaf);
            return found == container.end() ? std::nullopt : std::optional<Json>(*found);
        }
        if (container.is_array())
        {
            std::size_t index = 0;
            const auto parsed = std::from_chars(leaf.data(), leaf.data() + leaf.size(), index);
            if (parsed.ec != std::errc{} || parsed.ptr != leaf.data() + leaf.size() ||
                std::to_string(index) != leaf || index > container.size())
            {
                error = "JSON pointer array index is invalid or out of range";
                return std::nullopt;
            }
            return index == container.size() ? std::nullopt : std::optional<Json>(container[index]);
        }
        error = "JSON pointer parent is not an object or array";
    }
    catch (const std::exception &exception)
    {
        error = exception.what();
    }
    return std::nullopt;
}
} // namespace

AssetDocument::AssetDocument(Json object, std::uint64_t generation)
    : root_(std::move(object)), generation_(generation == 0 ? 1 : generation)
{
    if (!root_.is_object())
        throw std::invalid_argument("asset document root must be a JSON object");
}

bool AssetDocument::dirty() const noexcept
{
    return !savedHistoryToken_ || *savedHistoryToken_ != currentHistoryToken_;
}

SourceFileStatus AssetDocument::source_file_status() const
{
    if (!sourcePath_)
        return {SourceFileState::NoSource, std::nullopt, {}};
    std::string identityError;
    const auto diskIdentity = native_file_identity(*sourcePath_, identityError);
    if (!identityError.empty())
        return {SourceFileState::Unavailable, std::nullopt, std::move(identityError)};
    if (!diskIdentity)
        return {SourceFileState::Missing, std::nullopt, "the saved animation file is missing"};
    if (sourceIdentity_ != diskIdentity)
        return {SourceFileState::Changed, diskIdentity,
                "the saved animation changed outside the editor"};
    return {SourceFileState::Current, diskIdentity, {}};
}

AssetEditResult AssetDocument::set(std::string_view jsonPointer, Json value)
{
    return apply_edit(jsonPointer, std::move(value));
}

AssetEditResult AssetDocument::erase(std::string_view jsonPointer)
{
    return apply_edit(jsonPointer, std::nullopt);
}

AssetEditResult AssetDocument::apply_edit(std::string_view jsonPointer, std::optional<Json> value)
{
    static_assert(std::is_nothrow_move_constructible_v<PathEdit>);
    if (jsonPointer.empty() || jsonPointer.front() != '/')
        return {false, "asset edits require a non-root JSON pointer"};
    if (nextHistoryToken_ == std::numeric_limits<std::uint64_t>::max())
        return {false, "asset history token space is exhausted"};

    std::string path(jsonPointer);
    Json::json_pointer pointer;
    try
    {
        pointer = Json::json_pointer(path);
    }
    catch (const std::exception &exception)
    {
        return {false, std::string("invalid JSON pointer: ") + exception.what()};
    }
    std::string error;
    const auto before = value_at(root_, pointer, error);
    if (!error.empty())
        return {false, std::move(error)};
    if (!value && !before)
        return {false, "cannot erase a missing asset property"};
    if (before == value)
        return {};

    PathEdit edit{std::move(path), before, std::move(value), currentHistoryToken_, nextHistoryToken_};
    try
    {
        if (history_.capacity() < historyCursor_ + 1)
        {
            const std::size_t capacity = history_.capacity();
            const std::size_t doubled = capacity == 0 || capacity > history_.max_size() / 2
                                            ? historyCursor_ + 1
                                            : capacity * 2;
            history_.reserve(doubled < historyCursor_ + 1 ? historyCursor_ + 1 : doubled);
        }
    }
    catch (const std::exception &exception)
    {
        return {false, std::string("could not retain asset edit: ") + exception.what()};
    }
    if (!apply_value(root_, edit.path, edit.after, error))
        return {false, std::move(error)};
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(historyCursor_), history_.end());
    history_.push_back(std::move(edit));
    historyCursor_ = history_.size();
    currentHistoryToken_ = nextHistoryToken_++;
    return {true, {}};
}

bool AssetDocument::apply_value(Json &root, const std::string &path,
                                std::optional<Json> value, std::string &error,
                                bool insertArrayElement) const
{
    try
    {
        const Json::json_pointer pointer(path);
        const Json::json_pointer parentPointer = pointer.parent_pointer();
        if (!root.contains(parentPointer))
        {
            error = "JSON pointer parent does not exist";
            return false;
        }
        Json &parent = root.at(parentPointer);
        const std::string &leaf = pointer.back();
        if (parent.is_object())
        {
            if (value)
            {
                auto found = parent.find(leaf);
                if (found == parent.end())
                    parent.emplace(leaf, std::move(*value));
                else
                    found->swap(*value);
            }
            else if (parent.erase(leaf) == 0)
            {
                error = "asset property disappeared before the edit";
                return false;
            }
            return true;
        }
        if (parent.is_array())
        {
            std::size_t index = 0;
            const auto parsed = std::from_chars(leaf.data(), leaf.data() + leaf.size(), index);
            if (parsed.ec != std::errc{} || parsed.ptr != leaf.data() + leaf.size() ||
                std::to_string(index) != leaf || index > parent.size() ||
                (!value && index == parent.size()))
            {
                error = "JSON pointer array index is invalid or out of range";
                return false;
            }
            if (value && insertArrayElement)
                parent.insert(parent.begin() + static_cast<Json::difference_type>(index), std::move(*value));
            else if (value && index == parent.size())
                parent.push_back(std::move(*value));
            else if (value)
                parent[index].swap(*value);
            else
                parent.erase(parent.begin() + static_cast<Json::difference_type>(index));
            return true;
        }
        error = "JSON pointer parent is not an object or array";
    }
    catch (const std::exception &exception)
    {
        error = exception.what();
    }
    return false;
}

AssetEditResult AssetDocument::undo()
{
    if (!can_undo())
        return {false, "there is no asset edit to undo"};
    const PathEdit &edit = history_[historyCursor_ - 1];
    std::string error;
    if (!apply_value(root_, edit.path, edit.before, error,
                     edit.before.has_value() && !edit.after.has_value()))
        return {false, std::move(error)};
    --historyCursor_;
    currentHistoryToken_ = edit.beforeToken;
    return {true, {}};
}

AssetEditResult AssetDocument::redo()
{
    if (!can_redo())
        return {false, "there is no asset edit to redo"};
    const PathEdit &edit = history_[historyCursor_];
    std::string error;
    if (!apply_value(root_, edit.path, edit.after, error))
        return {false, std::move(error)};
    ++historyCursor_;
    currentHistoryToken_ = edit.afterToken;
    return {true, {}};
}

AssetSaveResult AssetDocument::save(
    const std::filesystem::path &path,
    std::optional<AssetOverwriteObservation> overwrite)
{
    if (path.empty())
        return {AssetSaveStatus::Failed, "asset destination path is empty"};
    const bool sameSource = same_source_path(sourcePath_, path);
    const bool mayReplace = sameSource || overwrite.has_value();
    if (sameSource && !sourceIdentity_ && !overwrite)
        return {AssetSaveStatus::Conflict, "the source file identity is unavailable; use Save As"};
    const std::optional<FileIdentity> expected = overwrite
        ? overwrite->destinationIdentity
        : (sameSource ? sourceIdentity_ : std::nullopt);
    const std::string contents = root_.dump(2) + "\n";
    const auto write = native_write_file_atomically(path, contents, mayReplace, expected, true);
    if (write.status == AtomicWriteStatus::Conflict)
        return {AssetSaveStatus::Conflict, write.message};
    if (!write.succeeded())
        return {AssetSaveStatus::Failed, write.message};

    sourcePath_ = path;
    std::string identityError;
    sourceIdentity_ = native_file_identity(path, identityError);
    savedHistoryToken_ = currentHistoryToken_;
    if (write.status == AtomicWriteStatus::DurabilityWarning || !sourceIdentity_)
    {
        const std::string message = !write.message.empty() ? write.message :
            std::string("Asset was committed, but its new file identity could not be read: ") + identityError;
        return {AssetSaveStatus::SavedWithDurabilityWarning, message};
    }
    return {AssetSaveStatus::Saved, write.message};
}

AssetDocumentLoadResult load_asset_document(const std::filesystem::path &path,
                                           std::uint64_t generation)
{
    std::string identityError;
    const auto before = native_file_identity(path, identityError);
    if (!before)
        return {std::nullopt, std::string("could not identify asset document: ") + identityError};

    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {std::nullopt, "could not open asset document"};
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.good() && !input.eof())
        return {std::nullopt, "could not read complete asset document"};
    std::string text = contents.str();
    std::optional<Json> parsed;
    try
    {
        parsed = Json::parse(text);
    }
    catch (const std::exception &exception)
    {
        return {std::nullopt, std::string("asset document is not valid JSON: ") + exception.what()};
    }
    if (!parsed->is_object())
        return {std::nullopt, "asset document must have a JSON object root"};

    const auto after = native_file_identity(path, identityError);
    if (!after || *after != *before)
        return {std::nullopt, "asset document changed while it was being read; retry the load"};
    AssetDocument document(std::move(*parsed), generation);
    document.sourcePath_ = path;
    document.sourceIdentity_ = after;
    document.savedHistoryToken_ = document.currentHistoryToken_;
    return {std::move(document), {}};
}
} // namespace platformator_editor
