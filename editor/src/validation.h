#pragma once

#include "document.h"
#include "resourceindex.h"

#include <optional>
#include <memory>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace platformator_editor
{
enum class ValidationSeverity
{
    Warning,
    Error
};

struct ValidationIssue
{
    ValidationSeverity severity = ValidationSeverity::Error;
    std::optional<int> objectId;
    std::optional<int> componentId;
    std::string fieldPath;
    std::string message;
    bool blocksRun = false;
};

struct ValidationAssetDependency
{
    std::filesystem::path logicalPath;
    ResourceKind kind = ResourceKind::Unknown;
    int objectId = 0;
    int componentId = 0;
};

struct ValidationResult
{
    bool structurallyValid = true;
    bool blocksRun = false;
    std::vector<ValidationIssue> issues;
    std::vector<ValidationAssetDependency> assetDependencies;
    DocumentIdentity document;
    std::uint64_t structureRevision = 0;
    std::uint64_t valueRevision = 0;
    std::uint64_t requestId = 0;

    bool valid_for_run() const noexcept
    {
        return structurallyValid && !blocksRun;
    }
};

ValidationResult validate_document(const DocumentSession &document);
std::vector<int> validation_object_ids_for_resource_consumers(
    const std::vector<ResourceConsumer> &consumers, const DocumentIdentity &document);

struct ValidationCompletion
{
    DocumentIdentity document;
    std::uint64_t structureRevision = 0;
    std::uint64_t valueRevision = 0;
    std::uint64_t requestId = 0;
    ValidationResult result;
    bool incremental = false;
    std::vector<int> refreshedObjectIds;
};

// Recovery coverage can remain dirty for many owner frames. Schedule one full
// validation for each new project/uncertainty revision instead of cancelling
// and rebuilding the same snapshot until a filesystem scan succeeds.
class ValidationRecoveryGate
{
  public:
    bool should_request_full(bool rootDirty, ProjectGeneration project,
                             std::uint64_t uncertaintyEpoch) noexcept
    {
        if (!rootDirty)
            return false;
        const auto revision = std::pair{project, uncertaintyEpoch};
        if (lastRequested_ == revision)
            return false;
        lastRequested_ = revision;
        return true;
    }

    void reset() noexcept { lastRequested_.reset(); }

  private:
    std::optional<std::pair<ProjectGeneration, std::uint64_t>> lastRequested_;
};

// Coalesces document validation to one active and one newest queued immutable snapshot.
// Completion is accepted only for the exact document identity and revisions supplied by
// the caller, so stale work can never authorize Run.
class ValidationService
{
  public:
    ValidationService();
    ~ValidationService();
    ValidationService(const ValidationService &) = delete;
    ValidationService &operator=(const ValidationService &) = delete;

    std::uint64_t request(const DocumentSession &document, bool forceFresh = false);
    // Revalidate only the supplied object IDs and merge those results with the
    // last complete result for the same document structure. If no compatible
    // complete base exists, this safely falls back to a full validation.
    std::uint64_t request_changed(const DocumentSession &document,
                                  const std::vector<int> &changedObjectIds);
    void set_wake_callback(std::function<void()> callback);
    std::optional<ValidationCompletion> take_current(const DocumentSession &document);
    void cancel() noexcept;
    bool pending() const noexcept;
    bool is_current(const DocumentSession &document) const noexcept;
    bool is_current(const DocumentSession &document,
                    const ValidationResult &result) const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

bool validation_allows_run(const ValidationService &service,
                           const DocumentSession &document,
                           const ValidationResult &result) noexcept;
} // namespace platformator_editor
