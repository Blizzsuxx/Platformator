#include "workspace.h"

#include "editcommands.h"
#include "documentio.h"
#include "project.h"
#include "validation.h"
#include "platform/nativefile.h"

#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace platformator_editor
{
namespace
{
std::filesystem::path canonical_directory(const std::filesystem::path &path, const char *label)
{
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error || !std::filesystem::is_directory(canonical))
        throw std::invalid_argument(std::string(label) + " is not an existing directory: " + path.string());
    return canonical;
}

WorkspaceActionResult completed(std::string message = {})
{
    return {WorkspaceActionStatus::Completed, std::move(message)};
}

WorkspaceActionResult failed(std::string message)
{
    return {WorkspaceActionStatus::Failed, std::move(message)};
}
} // namespace

DocumentWorkspace::DocumentWorkspace(std::filesystem::path projectRoot,
                                     std::filesystem::path assetRoot,
                                     std::uint64_t projectGeneration, TabId tabId)
    : projectRoot_(canonical_directory(projectRoot, "project root")),
      assetRoot_(canonical_directory(assetRoot, "asset root")),
      document_(make_default_document(projectRoot_, assetRoot_, nextDocumentGeneration_)),
      projectGeneration_(projectGeneration), tabId_(tabId)
{
    if (projectGeneration == 0)
        throw std::invalid_argument("project generation must be nonzero");
    document_.projectGeneration = projectGeneration;
    document_.tabId = tabId_;
}

const DocumentSession &DocumentWorkspace::document() const noexcept
{
    return document_;
}

DocumentSession &DocumentWorkspace::document_for_edit() noexcept
{
    return document_;
}

bool DocumentWorkspace::dirty() const noexcept
{
    if (history_ != nullptr)
        return history_->dirty();
    return document_.structureRevision != savedStructureRevision_ ||
           document_.valueRevision != savedValueRevision_;
}

void DocumentWorkspace::bind_history(EditCommandService &history) noexcept
{
    history_ = &history;
    history_->reset_history(document_.documentGeneration);
    history_->mark_saved();
}

ValidationResult DocumentWorkspace::validate() const
{
    return validate_document(document_);
}

WorkspaceActionResult DocumentWorkspace::resolve_unsaved(UnsavedDecision decision)
{
    if (!dirty())
        return completed();
    switch (decision)
    {
    case UnsavedDecision::Save:
        return save();
    case UnsavedDecision::Discard:
        return completed();
    case UnsavedDecision::Cancel:
        return {WorkspaceActionStatus::Cancelled, {}};
    }
    return failed("invalid unsaved-work decision");
}

WorkspaceActionResult DocumentWorkspace::new_document(UnsavedDecision decision)
{
    auto resolution = resolve_unsaved(decision);
    if (resolution.status != WorkspaceActionStatus::Completed)
        return resolution;
    if (nextDocumentGeneration_ == std::numeric_limits<std::uint64_t>::max())
        return failed("document generation space exhausted");

    const auto nextGeneration = nextDocumentGeneration_ + 1;
    DocumentSession candidate = make_default_document(projectRoot_, assetRoot_, nextGeneration);
    candidate.projectGeneration = projectGeneration_;
    candidate.tabId = tabId_;
    document_ = std::move(candidate);
    nextDocumentGeneration_ = nextGeneration;
    if (history_ != nullptr)
        history_->reset_history(document_.documentGeneration);
    mark_saved();
    return completed();
}

WorkspaceActionResult DocumentWorkspace::open_document(
    std::optional<std::filesystem::path> selectedPath, UnsavedDecision decision)
{
    if (!selectedPath)
        return {WorkspaceActionStatus::Cancelled, {}};
    auto resolution = resolve_unsaved(decision);
    if (resolution.status != WorkspaceActionStatus::Completed)
        return resolution;
    if (nextDocumentGeneration_ == std::numeric_limits<std::uint64_t>::max())
        return failed("document generation space exhausted");

    const auto candidateGeneration = nextDocumentGeneration_ + 1;
    auto candidate = load_document_file(*selectedPath, candidateGeneration, projectRoot_, assetRoot_);
    if (!candidate)
        return failed(std::move(candidate.error));
    const auto validation = validate_document(*candidate.document);
    if (!validation.structurallyValid)
        return failed("invalid scene structure: " + validation.issues.front().message);
    candidate.document->projectGeneration = projectGeneration_;
    candidate.document->tabId = tabId_;
    document_ = std::move(*candidate.document);
    nextDocumentGeneration_ = candidateGeneration;
    if (history_ != nullptr)
        history_->reset_history(document_.documentGeneration);
    mark_saved();
    return completed();
}

WorkspaceActionResult DocumentWorkspace::save(bool allowOverwrite)
{
    if (!document_.sourcePath)
        return failed("document has no source path; use Save As");
    const auto saved = save_document(document_, *document_.sourcePath,
                                     SaveOptions{false, allowOverwrite ? OverwriteDecision::Overwrite
                                                                       : OverwriteDecision::Ask});
    if (!saved.succeeded())
        return failed(saved.message);
    mark_saved();
    return completed(saved.message);
}

WorkspaceActionResult DocumentWorkspace::save_as(const std::filesystem::path &path,
                                                 bool allowOverwrite)
{
    const auto saved = save_document(document_, path,
                                     SaveOptions{true, allowOverwrite ? OverwriteDecision::Overwrite
                                                                     : OverwriteDecision::Ask});
    if (!saved.succeeded())
        return failed(saved.message);
    mark_saved();
    return completed(saved.message);
}

bool source_file_allows_run(const SourceFileStatus &status, bool externalChangeAcknowledged,
                            std::optional<FileIdentity> acknowledgedIdentity) noexcept
{
    if (status.state == SourceFileState::NoSource || status.state == SourceFileState::Current)
        return true;
    return externalChangeAcknowledged && acknowledgedIdentity == status.diskIdentity;
}

SourceFileStatus DocumentWorkspace::source_file_status() const
{
    if (!document_.sourcePath)
        return {SourceFileState::NoSource, std::nullopt, {}};
    std::string identityError;
    const auto diskIdentity = native_file_identity(*document_.sourcePath, identityError);
    if (!identityError.empty())
        return {SourceFileState::Unavailable, std::nullopt, std::move(identityError)};
    if (!diskIdentity)
        return {SourceFileState::Missing, std::nullopt, "the saved scene file is missing"};
    if (document_.sourceIdentity != diskIdentity)
        return {SourceFileState::Changed, diskIdentity, "the saved scene changed outside the editor"};
    return {SourceFileState::Current, diskIdentity, {}};
}

WorkspaceActionResult DocumentWorkspace::reload_source_file()
{
    if (!document_.sourcePath)
        return failed("document has no source path to reload");
    return open_document(document_.sourcePath, UnsavedDecision::Discard);
}

WorkspaceActionResult DocumentWorkspace::set_project_roots(const std::filesystem::path &projectRoot,
                                                           const std::filesystem::path &assetRoot,
                                                           std::uint64_t projectGeneration)
{
    try
    {
        const auto project = canonical_directory(projectRoot, "project root");
        const auto assets = canonical_directory(assetRoot, "asset root");
        std::error_code error;
        if (!std::filesystem::is_regular_file(project / "CMakeLists.txt", error) || error)
            return failed("project root must contain CMakeLists.txt: " + project.string());
        if (project == projectRoot_ && assets == assetRoot_)
            return completed();
        if (projectGeneration <= document_.projectGeneration)
            return failed("project generation must advance when roots change");

        if (history_ != nullptr)
        {
            const DocumentIdentity from{ProjectGeneration{document_.projectGeneration}, tabId_,
                                         DocumentGeneration{document_.documentGeneration}};
            const DocumentIdentity to{ProjectGeneration{projectGeneration}, tabId_,
                                       DocumentGeneration{document_.documentGeneration}};
            if (!history_->can_migrate_project_identity(from, to))
                return failed("retained document history does not belong to the current project identity");
        }

        auto documentProject = project;
        auto documentAssets = assets;
        commit_project_roots(std::move(project), std::move(assets),
                             std::move(documentProject), std::move(documentAssets),
                             projectGeneration);
        return completed();
    }
    catch (const std::exception &error)
    {
        return failed(error.what());
    }
}

void DocumentWorkspace::commit_project_roots(std::filesystem::path projectRoot,
                                             std::filesystem::path assetRoot,
                                             std::filesystem::path documentProjectRoot,
                                             std::filesystem::path documentAssetRoot,
                                             std::uint64_t projectGeneration) noexcept
{
    if (history_ != nullptr)
    {
        const DocumentIdentity from{ProjectGeneration{document_.projectGeneration}, tabId_,
                                     DocumentGeneration{document_.documentGeneration}};
        const DocumentIdentity to{ProjectGeneration{projectGeneration}, tabId_,
                                   DocumentGeneration{document_.documentGeneration}};
        history_->migrate_project_identity(from, to);
    }
    projectRoot_ = std::move(projectRoot);
    assetRoot_ = std::move(assetRoot);
    document_.projectRoot = std::move(documentProjectRoot);
    document_.assetRoot = std::move(documentAssetRoot);
    document_.projectGeneration = projectGeneration;
    projectGeneration_ = projectGeneration;
}

void DocumentWorkspace::mark_saved() noexcept
{
    savedStructureRevision_ = document_.structureRevision;
    savedValueRevision_ = document_.valueRevision;
    if (history_ != nullptr)
        history_->mark_saved();
}
} // namespace platformator_editor
