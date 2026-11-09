#pragma once

#include "documentio.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace platformator_editor
{
struct AssetEditResult
{
    bool changed = false;
    std::string error;
};

enum class AssetSaveStatus
{
    Saved,
    SavedWithDurabilityWarning,
    Conflict,
    Failed
};

struct AssetSaveResult
{
    AssetSaveStatus status = AssetSaveStatus::Failed;
    std::string message;

    bool succeeded() const noexcept
    {
        return status == AssetSaveStatus::Saved ||
               status == AssetSaveStatus::SavedWithDurabilityWarning;
    }
};

// Captured when the user explicitly approves replacing a destination. A null
// identity records that the destination was observed to be missing.
struct AssetOverwriteObservation
{
    std::optional<FileIdentity> destinationIdentity;
};

struct AssetDocumentLoadResult;

// Asset documents are mutated sequentially on the editor owner thread. Ordinary
// edits touch one JSON path and retain only that path's before/after values; no
// worker accesses the document or its history.
class AssetDocument
{
  public:
    explicit AssetDocument(Json object, std::uint64_t generation = 1);

    const Json &root() const noexcept { return root_; }
    const std::optional<std::filesystem::path> &source_path() const noexcept { return sourcePath_; }
    const std::optional<FileIdentity> &source_identity() const noexcept { return sourceIdentity_; }
    std::uint64_t generation() const noexcept { return generation_; }
    std::uint64_t history_token() const noexcept { return currentHistoryToken_; }
    std::optional<std::uint64_t> saved_history_token() const noexcept { return savedHistoryToken_; }
    bool dirty() const noexcept;
    bool can_undo() const noexcept { return historyCursor_ != 0; }
    bool can_redo() const noexcept { return historyCursor_ < history_.size(); }

    AssetEditResult set(std::string_view jsonPointer, Json value);
    AssetEditResult erase(std::string_view jsonPointer);
    AssetEditResult undo();
    AssetEditResult redo();
    SourceFileStatus source_file_status() const;
    AssetSaveResult save(const std::filesystem::path &path,
                         std::optional<AssetOverwriteObservation> overwrite = std::nullopt);

  private:
    struct PathEdit
    {
        std::string path;
        std::optional<Json> before;
        std::optional<Json> after;
        std::uint64_t beforeToken = 0;
        std::uint64_t afterToken = 0;
    };

    AssetEditResult apply_edit(std::string_view jsonPointer, std::optional<Json> value);
    bool apply_value(Json &root, const std::string &path, std::optional<Json> value,
                     std::string &error, bool insertArrayElement = false) const;
    friend AssetDocumentLoadResult load_asset_document(const std::filesystem::path &, std::uint64_t);

    Json root_;
    std::optional<std::filesystem::path> sourcePath_;
    std::optional<FileIdentity> sourceIdentity_;
    std::uint64_t generation_ = 1;
    std::uint64_t currentHistoryToken_ = 1;
    std::uint64_t nextHistoryToken_ = 2;
    std::optional<std::uint64_t> savedHistoryToken_;
    std::vector<PathEdit> history_;
    std::size_t historyCursor_ = 0;
};

struct AssetDocumentLoadResult
{
    std::optional<AssetDocument> document;
    std::string error;

    explicit operator bool() const noexcept { return document.has_value(); }
};

AssetDocumentLoadResult load_asset_document(const std::filesystem::path &path,
                                            std::uint64_t generation = 1);
} // namespace platformator_editor
