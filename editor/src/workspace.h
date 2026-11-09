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
    bool conflict = false;

    explicit operator bool() const noexcept
    {
        return status == WorkspaceActionStatus::Completed;
    }
};

bool source_file_allows_run(const SourceFileStatus &status, bool externalChangeAcknowledged = false,
                            std::optional<FileIdentity> acknowledgedIdentity = std::nullopt) noexcept;

class DocumentWorkspace
{
  public:
    DocumentWorkspace(std::filesystem::path projectRoot, std::filesystem::path assetRoot,
                      std::uint64_t projectGeneration = 1, TabId tabId = {});

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
    SourceFileStatus source_file_status() const;
    WorkspaceActionResult reload_source_file();
    WorkspaceActionResult set_project_roots(const std::filesystem::path &projectRoot,
                                            const std::filesystem::path &assetRoot,
                                            std::uint64_t projectGeneration);

  private:
    friend class DocumentManager;
    WorkspaceActionResult resolve_unsaved(UnsavedDecision decision);
    void mark_saved() noexcept;
    void commit_project_roots(std::filesystem::path projectRoot,
                              std::filesystem::path assetRoot,
                              std::filesystem::path documentProjectRoot,
                              std::filesystem::path documentAssetRoot,
                              std::uint64_t projectGeneration) noexcept;

    std::filesystem::path projectRoot_;
    std::filesystem::path assetRoot_;
    std::uint64_t nextDocumentGeneration_ = 1;
    DocumentSession document_;
    std::uint64_t projectGeneration_ = 1;
    TabId tabId_;
    std::uint64_t savedStructureRevision_ = 0;
    std::uint64_t savedValueRevision_ = 0;
    EditCommandService *history_ = nullptr;
};
} // namespace platformator_editor
