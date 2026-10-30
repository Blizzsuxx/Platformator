#pragma once

#include "document.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <optional>

namespace platformator_editor
{
enum class OverwriteDecision
{
    Ask,
    Overwrite,
    SaveElsewhere
};

enum class SaveStatus
{
    Saved,
    SavedWithDurabilityWarning,
    Conflict,
    Failed
};

struct SaveOptions
{
    bool saveAs = false;
    OverwriteDecision overwrite = OverwriteDecision::Ask;
};

struct SaveResult
{
    SaveStatus status = SaveStatus::Failed;
    std::string message;
    bool destinationReplaced = false;

    bool succeeded() const noexcept
    {
        return status == SaveStatus::Saved || status == SaveStatus::SavedWithDurabilityWarning;
    }
};

enum class AtomicWriteStatus
{
    Written,
    DurabilityWarning,
    Conflict,
    Failed
};

struct AtomicWriteResult
{
    AtomicWriteStatus status = AtomicWriteStatus::Failed;
    std::string message;
    bool destinationReplaced = false;

    bool succeeded() const noexcept
    {
        return status == AtomicWriteStatus::Written || status == AtomicWriteStatus::DurabilityWarning;
    }
};

DocumentParseResult load_document_file(const std::filesystem::path &path,
                                       std::uint64_t documentGeneration,
                                       std::filesystem::path projectRoot = {},
                                       std::filesystem::path assetRoot = {});
SaveResult save_document(DocumentSession &document, const std::filesystem::path &path,
                         SaveOptions options = {});
AtomicWriteResult write_file_atomically(const std::filesystem::path &path, std::string_view contents,
                                        bool allowReplaceExisting,
                                        std::optional<FileIdentity> expectedDestination = std::nullopt,
                                        bool verifyDestinationIdentity = false);
} // namespace platformator_editor
