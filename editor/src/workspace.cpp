#include "workspace.h"

#include "editcommands.h"
#include "documentio.h"
#include "project.h"
#include "validation.h"

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
                                     std::filesystem::path assetRoot)
    : projectRoot_(canonical_directory(projectRoot, "project root")),
      assetRoot_(canonical_directory(assetRoot, "asset root")),
      document_(make_default_document(projectRoot_, assetRoot_, nextDocumentGeneration_))
{
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

        projectRoot_ = project;
        assetRoot_ = assets;
        document_.projectRoot = std::move(project);
        document_.assetRoot = std::move(assets);
        document_.projectGeneration = projectGeneration;
        return completed();
    }
    catch (const std::exception &error)
    {
        return failed(error.what());
    }
}

void DocumentWorkspace::mark_saved() noexcept
{
    savedStructureRevision_ = document_.structureRevision;
    savedValueRevision_ = document_.valueRevision;
    if (history_ != nullptr)
        history_->mark_saved();
}
} // namespace platformator_editor
