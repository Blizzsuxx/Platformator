#pragma once

#include "document.h"

#include <filesystem>
#include <optional>
#include <string>

namespace platformator_editor
{
class EditCommandService;
struct ValidationResult;

enum class UnsavedDecision
{
    Save,
    Discard,
    Cancel
};

enum class WorkspaceActionStatus
{
    Completed,
    Cancelled,
    Failed
};

struct WorkspaceActionResult
{
    WorkspaceActionStatus status = WorkspaceActionStatus::Failed;
    std::string message;

    explicit operator bool() const noexcept
    {
        return status == WorkspaceActionStatus::Completed;
    }
};

class DocumentWorkspace
{
  public:
    DocumentWorkspace(std::filesystem::path projectRoot, std::filesystem::path assetRoot);

    const DocumentSession &document() const noexcept;
    DocumentSession &document_for_edit() noexcept;
    bool dirty() const noexcept;
    ValidationResult validate() const;
    void bind_history(EditCommandService &history) noexcept;

    WorkspaceActionResult new_document(UnsavedDecision decision);
    WorkspaceActionResult open_document(std::optional<std::filesystem::path> selectedPath,
                                        UnsavedDecision decision);
    WorkspaceActionResult save(bool allowOverwrite = false);
    WorkspaceActionResult save_as(const std::filesystem::path &path, bool allowOverwrite = false);
    WorkspaceActionResult set_project_roots(const std::filesystem::path &projectRoot,
                                            const std::filesystem::path &assetRoot,
                                            std::uint64_t projectGeneration);

  private:
    WorkspaceActionResult resolve_unsaved(UnsavedDecision decision);
    void mark_saved() noexcept;

    std::filesystem::path projectRoot_;
    std::filesystem::path assetRoot_;
    DocumentSession document_;
    std::uint64_t nextDocumentGeneration_ = 1;
    std::uint64_t savedStructureRevision_ = 0;
    std::uint64_t savedValueRevision_ = 0;
    EditCommandService *history_ = nullptr;
};
} // namespace platformator_editor
