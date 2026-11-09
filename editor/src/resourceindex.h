#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "sessionids.h"

namespace platformator_editor
{
enum class ResourceKind
{
    Unknown,
    Texture,
    Animation,
    Audio,
    Script,
    Scene
};

enum class ResourceStatus
{
    Resolved,
    Missing,
    BrokenSymlink,
    SymlinkCycle,
    OutsideRoot,
    Unreadable
};

enum class ResourcePreparationState
{
    Ready,
    Dirty,
    Preparing,
    AwaitingOwnerCommit,
    RetryPending,
    Failed
};

struct ResourceConsumer
{
    DocumentIdentity document;
    int objectId = 0;
    std::optional<int> componentId;
    std::string role;

    bool operator==(const ResourceConsumer &) const = default;
};

struct ResourceResolution
{
    std::filesystem::path logicalPath;
    std::filesystem::path canonicalPath;
    std::filesystem::path lastGoodCanonicalPath;
    ResourceKind kind = ResourceKind::Unknown;
    ResourceStatus status = ResourceStatus::Missing;
    std::uint64_t logicalVersion = 0;
    std::uint64_t canonicalVersion = 0;
    std::optional<std::uintmax_t> size;
    std::optional<std::filesystem::file_time_type> modified;
    ResourcePreparationState preparation = ResourcePreparationState::Ready;
    std::string preparationError;
};

struct ResourceRequestToken
{
    ProjectGeneration projectGeneration;
    std::filesystem::path logicalPath;
    std::uint64_t requestId = 0;
    std::uint64_t invalidationRevision = 0;
    std::uint64_t logicalVersion = 0;
    std::filesystem::path canonicalPath;
    std::uint64_t canonicalVersion = 0;
    std::uint64_t uncertaintyEpoch = 0;
    ResourceKind kind = ResourceKind::Unknown;

    bool operator==(const ResourceRequestToken &) const = default;
};

struct ResourceFallbackChange
{
    std::filesystem::path logicalPath;
    std::vector<ResourceConsumer> consumers;
};

class ResourceIndex
{
  public:
    void set_project(ProjectGeneration generation, std::filesystem::path assetRoot);
    ResourceResolution resolve(const std::filesystem::path &logicalPath,
                               ResourceKind kind = ResourceKind::Unknown);
    void register_consumer(const std::filesystem::path &logicalPath, ResourceKind kind,
                           ResourceConsumer consumer);
    void begin_visible_reference_frame();
    void register_visible_reference(const std::filesystem::path &logicalPath, ResourceKind kind,
                                    ResourceConsumer consumer);
    void register_visible_animation_texture(const std::filesystem::path &logicalPath,
                                            ResourceConsumer consumer);
    void end_visible_reference_frame();
    void unregister_consumer(const std::filesystem::path &logicalPath,
                             const ResourceConsumer &consumer);
    void unregister_document(const DocumentIdentity &document);
    std::vector<ResourceConsumer> affected_consumers(const std::filesystem::path &changedPath,
                                                      bool includeSubtree = false) const;
    std::vector<ResourceFallbackChange> inspect_fallback_paths(std::size_t maxPathStats);
    std::uint64_t invalidate(const std::filesystem::path &changedPath);
    void invalidate_subtree(const std::filesystem::path &directory, std::uint64_t uncertaintyEpoch);
    ResourceRequestToken begin_request(const std::filesystem::path &logicalPath);
    ResourceRequestToken join_request(const std::filesystem::path &logicalPath);
    std::optional<ResourceRequestToken> current_request(const std::filesystem::path &logicalPath) const;
    bool accepts(const ResourceRequestToken &token) const;
    bool is_ready(const ResourceRequestToken &token) const;
    bool set_preparation_state(const ResourceRequestToken &token, ResourcePreparationState state,
                               std::string error = {});

    ProjectGeneration project_generation() const noexcept;
    const std::filesystem::path &asset_root() const noexcept;
    std::uint64_t directory_revision(const std::filesystem::path &directory) const;
    std::uint64_t uncertainty_epoch() const noexcept;
    std::size_t size() const noexcept;

  private:
    struct Entry
    {
        ResourceResolution resolution;
        std::vector<ResourceConsumer> consumers;
        std::uint64_t requestId = 0;
        std::uint64_t invalidationRevision = 0;
        ResourceRequestToken requestToken;
        std::filesystem::path canonicalLinkPath;
        std::vector<std::string> logicalAliasAncestors;
        std::vector<std::string> logicalUnresolvedTargets;
        std::uint64_t appliedUncertaintyEpoch = 0;
        bool resolutionCacheValid = false;
        ResourceKind cachedKind = ResourceKind::Unknown;
        std::uint64_t cachedLogicalVersion = 0;
        std::uint64_t cachedUncertaintyEpoch = 0;
    };

    struct VisibleReferenceDemand
    {
        std::filesystem::path logicalPath;
        ResourceKind kind = ResourceKind::Unknown;
        ResourceConsumer consumer;
    };

    std::optional<std::filesystem::path> logical_absolute(const std::filesystem::path &path) const;
    std::string logical_key(const std::filesystem::path &path) const;
    static bool contains_path(const std::filesystem::path &root, const std::filesystem::path &candidate);
    std::uint64_t revision_for(const std::filesystem::path &directory) const;
    std::uint64_t uncertainty_for(const std::filesystem::path &path) const;
    static std::string visible_consumer_key(const ResourceConsumer &consumer);
    ResourceResolution resolve_impl(const std::filesystem::path &logicalPath,
                                    ResourceKind kind, bool forceFilesystemRefresh);
    void track_logical_alias_ancestors(const std::filesystem::path &logicalPath, Entry &entry);
    void update_canonical_link(const std::string &logicalKey, Entry &entry,
                               const std::filesystem::path &canonicalPath);

    ProjectGeneration projectGeneration_;
    std::filesystem::path assetRoot_;
    std::map<std::string, Entry> entries_;
    std::map<std::string, std::vector<std::string>> logicalByCanonical_;
    // Retain the logical symlink ancestors observed for resolved resources. A
    // directory-link removal/retarget can arrive as a non-directory event, and
    // its old canonical target may no longer be resolvable when the event lands.
    std::map<std::string, std::vector<std::string>> logicalByAliasAncestor_;
    // A demanded alias can be broken before its target is ever resolved. Keep
    // contained target prefixes so creation/repair notifications can find it.
    std::map<std::string, std::vector<std::string>> logicalByUnresolvedTarget_;
    std::map<std::string, std::uint64_t> logicalVersions_;
    std::map<std::string, std::uint64_t> canonicalVersions_;
    std::map<std::string, ResourceRequestToken> canonicalRequests_;
    std::map<std::string, std::uint64_t> preparedCanonicalVersions_;
    std::map<std::string, std::uint64_t> directoryRevisions_;
    std::map<std::string, std::uint64_t> subtreeUncertainty_;
    std::map<std::string, VisibleReferenceDemand> visibleReferenceDemands_;
    std::set<std::string> visibleReferenceSeen_;
    bool visibleReferenceFrameActive_ = false;
    std::uint64_t uncertaintyEpoch_ = 0;
    std::uint64_t nextRequestId_ = 1;
    std::string fallbackCursor_;
};
} // namespace platformator_editor
