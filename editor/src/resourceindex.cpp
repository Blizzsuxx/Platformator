#include "resourceindex.h"

#include "editorwork.h"

#include <algorithm>
#include <deque>
#include <limits>
#include <system_error>
#include <utility>

namespace platformator_editor
{
namespace
{
struct SymlinkTrace
{
    std::vector<std::filesystem::path> ancestors;
    std::vector<std::filesystem::path> unresolvedTargets;
    bool allLinksResolve = true;
};

std::string canonical_request_key(const std::filesystem::path &path, ResourceKind kind)
{
    std::string key = path.generic_string();
    key.push_back('\0');
    key += std::to_string(static_cast<int>(kind));
    return key;
}

bool path_is_within_root(const std::filesystem::path &root,
                         const std::filesystem::path &candidate)
{
    const auto relative = candidate.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty())
        return candidate.lexically_normal() == root.lexically_normal();
    if (relative.is_absolute())
        return false;
    for (const auto &part : relative)
        if (part == "..")
            return false;
    return true;
}

SymlinkTrace resolved_symlink_ancestors(
    const std::filesystem::path &logicalPath, const std::filesystem::path &assetRoot)
{
    const auto relative = logicalPath.lexically_relative(assetRoot);
    if (relative.empty() || relative.is_absolute() ||
        (relative.begin() != relative.end() && *relative.begin() == ".."))
        return {.allLinksResolve = false};

    std::deque<std::filesystem::path> pending;
    for (const auto &part : relative)
        pending.push_back(part);

    SymlinkTrace result;
    auto current = assetRoot;
    std::size_t followedLinks = 0;
    std::size_t visitedComponents = 0;
    bool followingLink = false;
    constexpr std::size_t MaxSymlinkHops = 64;
    constexpr std::size_t MaxPathComponents = 512;
    while (!pending.empty() && followedLinks < MaxSymlinkHops &&
           visitedComponents < MaxPathComponents)
    {
        ++visitedComponents;
        const auto part = std::move(pending.front());
        pending.pop_front();
        if (part.empty() || part == ".")
            continue;
        if (part == "..")
        {
            current = current.parent_path();
            if (!path_is_within_root(assetRoot, current))
            {
                result.allLinksResolve = false;
                break;
            }
            continue;
        }

        const auto candidate = (current / part).lexically_normal();
        std::error_code error;
        const auto status = std::filesystem::symlink_status(candidate, error);
        if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory)
        {
            if (followingLink && path_is_within_root(assetRoot, candidate) &&
                std::find(result.unresolvedTargets.begin(), result.unresolvedTargets.end(), candidate) ==
                    result.unresolvedTargets.end())
                result.unresolvedTargets.push_back(candidate);
            if (followingLink)
            {
                result.allLinksResolve = false;
                break;
            }
            current = candidate;
            continue;
        }
        if (error || !std::filesystem::is_symlink(status))
        {
            if (error && followingLink)
            {
                result.allLinksResolve = false;
                break;
            }
            current = candidate;
            continue;
        }

        followingLink = true;
        if (std::find(result.ancestors.begin(), result.ancestors.end(), candidate) ==
            result.ancestors.end())
            result.ancestors.push_back(candidate);
        if (++followedLinks >= MaxSymlinkHops)
        {
            result.allLinksResolve = false;
            break;
        }
        error.clear();
        const auto target = std::filesystem::read_symlink(candidate, error);
        if (error)
        {
            result.allLinksResolve = false;
            break;
        }

        std::filesystem::path targetParts = target;
        if (target.is_absolute())
        {
            targetParts = target.lexically_relative(assetRoot);
            if ((targetParts.empty() && target.lexically_normal() != assetRoot.lexically_normal()) ||
                targetParts.is_absolute() ||
                (targetParts.begin() != targetParts.end() && *targetParts.begin() == ".."))
            {
                // Keep the in-root link dependency, but do not walk or inspect an
                // absolute target outside the configured asset root.
                result.allLinksResolve = false;
                break;
            }
            current = assetRoot;
        }
        else
            current = candidate.parent_path();
        std::vector<std::filesystem::path> components;
        for (const auto &targetPart : targetParts)
            components.push_back(targetPart);
        for (auto component = components.rbegin(); component != components.rend(); ++component)
            pending.push_front(*component);
    }
    if (!pending.empty())
        result.allLinksResolve = false;
    return result;
}
}

void ResourceIndex::set_project(ProjectGeneration generation, std::filesystem::path assetRoot)
{
    std::error_code error;
    auto canonicalRoot = std::filesystem::canonical(assetRoot, error);
    if (error || !std::filesystem::is_directory(canonicalRoot, error) || error)
        canonicalRoot = assetRoot.lexically_normal();
    projectGeneration_ = generation;
    assetRoot_ = std::move(canonicalRoot);
    entries_.clear();
    logicalByCanonical_.clear();
    logicalByAliasAncestor_.clear();
    logicalByUnresolvedTarget_.clear();
    logicalVersions_.clear();
    canonicalVersions_.clear();
    canonicalRequests_.clear();
    preparedCanonicalVersions_.clear();
    directoryRevisions_.clear();
    subtreeUncertainty_.clear();
    visibleReferenceDemands_.clear();
    visibleReferenceSeen_.clear();
    visibleReferenceFrameActive_ = false;
    uncertaintyEpoch_ = 0;
    nextRequestId_ = 1;
    fallbackCursor_.clear();
}

ResourceResolution ResourceIndex::resolve(const std::filesystem::path &logicalPath, ResourceKind kind)
{
    return resolve_impl(logicalPath, kind, false);
}

ResourceResolution ResourceIndex::resolve_impl(const std::filesystem::path &logicalPath, ResourceKind kind,
                                               bool forceFilesystemRefresh)
{
    const auto absolute = logical_absolute(logicalPath);
    ResourceResolution resolution;
    resolution.kind = kind;
    if (!absolute)
    {
        resolution.logicalPath = logicalPath.lexically_normal();
        resolution.status = ResourceStatus::OutsideRoot;
        return resolution;
    }

    const std::string key = logical_key(*absolute);
    auto &entry = entries_[key];
    if (kind == ResourceKind::Unknown)
        kind = entry.resolution.kind;
    resolution.logicalPath = *absolute;
    resolution.kind = kind;
    resolution.logicalVersion = logicalVersions_[key];
    resolution.preparation = entry.resolution.preparation;
    resolution.preparationError = entry.resolution.preparationError;
    if (resolution.logicalVersion == 0)
        resolution.logicalVersion = logicalVersions_[key] = 1;

    const std::uint64_t uncertainty = uncertainty_for(*absolute);
    if (uncertainty > entry.appliedUncertaintyEpoch)
    {
        resolution.preparation = ResourcePreparationState::Dirty;
        resolution.preparationError.clear();
        auto &version = logicalVersions_[key];
        if (version != std::numeric_limits<std::uint64_t>::max())
            ++version;
        const auto oldCanonical = entry.canonicalLinkPath;
        if (!oldCanonical.empty())
        {
            auto &canonicalVersion = canonicalVersions_[logical_key(oldCanonical)];
            if (canonicalVersion != std::numeric_limits<std::uint64_t>::max())
                ++canonicalVersion;
        }
        entry.appliedUncertaintyEpoch = uncertainty;
    }
    resolution.logicalVersion = logicalVersions_[key];

    if (!forceFilesystemRefresh && entry.resolutionCacheValid && entry.cachedKind == kind &&
        entry.cachedLogicalVersion == resolution.logicalVersion &&
        entry.cachedUncertaintyEpoch == uncertainty)
        return entry.resolution;

    work::record(work::Counter::ValidityQueries);
    std::error_code error;
    const auto canonical = std::filesystem::canonical(*absolute, error);
    if (!error)
    {
        if (!contains_path(assetRoot_, canonical))
            resolution.status = ResourceStatus::OutsideRoot;
        else
        {
            std::error_code metadataError;
            const bool regularFile = std::filesystem::is_regular_file(canonical, metadataError);
            if (metadataError)
                resolution.status = ResourceStatus::Unreadable;
            else if (!regularFile)
                resolution.status = ResourceStatus::Missing;
            else
            {
                resolution.status = ResourceStatus::Resolved;
                resolution.canonicalPath = canonical;
                const auto size = std::filesystem::file_size(canonical, metadataError);
                if (!metadataError)
                    resolution.size = size;
                const auto modified = std::filesystem::last_write_time(canonical, metadataError);
                if (!metadataError)
                    resolution.modified = modified;
            }
        }
    }
    else if (error == std::errc::too_many_symbolic_link_levels)
        resolution.status = ResourceStatus::SymlinkCycle;
    else if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted)
        resolution.status = ResourceStatus::Unreadable;
    else
    {
        bool brokenSymlink = false;
        auto prefix = assetRoot_;
        const auto relative = absolute->lexically_relative(assetRoot_);
        for (const auto &part : relative)
        {
            if (part == "." || part.empty())
                continue;
            prefix /= part;
            std::error_code statusError;
            const auto status = std::filesystem::symlink_status(prefix, statusError);
            if (statusError == std::errc::no_such_file_or_directory)
                break;
            if (statusError)
            {
                if (statusError == std::errc::permission_denied)
                    resolution.status = ResourceStatus::Unreadable;
                break;
            }
            if (std::filesystem::is_symlink(status))
            {
                std::error_code targetError;
                const auto target = std::filesystem::canonical(prefix, targetError);
                if (targetError == std::errc::too_many_symbolic_link_levels)
                {
                    resolution.status = ResourceStatus::SymlinkCycle;
                    break;
                }
                if (targetError)
                {
                    brokenSymlink = true;
                    break;
                }
                if (!contains_path(assetRoot_, target))
                {
                    resolution.status = ResourceStatus::OutsideRoot;
                    break;
                }
            }
        }
        if (resolution.status != ResourceStatus::Unreadable &&
            resolution.status != ResourceStatus::SymlinkCycle &&
            resolution.status != ResourceStatus::OutsideRoot)
            resolution.status = brokenSymlink ? ResourceStatus::BrokenSymlink : ResourceStatus::Missing;
    }

    if (resolution.status == ResourceStatus::Resolved)
    {
        auto &canonicalVersion = canonicalVersions_[logical_key(resolution.canonicalPath)];
        if (canonicalVersion == 0)
            canonicalVersion = 1;
        resolution.canonicalVersion = canonicalVersion;
        resolution.lastGoodCanonicalPath = resolution.canonicalPath;
        track_logical_alias_ancestors(*absolute, entry);
        update_canonical_link(key, entry, resolution.canonicalPath);
    }
    else
    {
        resolution.lastGoodCanonicalPath = entry.resolution.lastGoodCanonicalPath;
        if (resolution.status == ResourceStatus::Missing ||
            resolution.status == ResourceStatus::BrokenSymlink ||
            resolution.status == ResourceStatus::OutsideRoot ||
            resolution.status == ResourceStatus::SymlinkCycle ||
            resolution.status == ResourceStatus::Unreadable)
            track_logical_alias_ancestors(*absolute, entry);
        if (resolution.status == ResourceStatus::OutsideRoot ||
            resolution.status == ResourceStatus::SymlinkCycle ||
            resolution.status == ResourceStatus::Unreadable)
            update_canonical_link(key, entry, {});
    }
    entry.resolution = resolution;
    entry.resolutionCacheValid = true;
    entry.cachedKind = kind;
    entry.cachedLogicalVersion = resolution.logicalVersion;
    entry.cachedUncertaintyEpoch = uncertainty;
    return resolution;
}

void ResourceIndex::register_consumer(const std::filesystem::path &logicalPath, ResourceKind kind,
                                     ResourceConsumer consumer)
{
    const auto resolution = resolve(logicalPath, kind);
    const auto absolute = logical_absolute(logicalPath);
    if (!absolute)
        return;
    auto &consumers = entries_[logical_key(*absolute)].consumers;
    if (std::find(consumers.begin(), consumers.end(), consumer) == consumers.end())
        consumers.push_back(std::move(consumer));
    (void)resolution;
}

std::string ResourceIndex::visible_consumer_key(const ResourceConsumer &consumer)
{
    std::string key = std::to_string(consumer.document.projectGeneration.value) + ":" +
        std::to_string(consumer.document.tabId.value) + ":" +
        std::to_string(consumer.document.documentGeneration.value) + ":" +
        std::to_string(consumer.objectId) + ":" +
        (consumer.componentId ? std::to_string(*consumer.componentId) : std::string("-")) + ":" +
        std::to_string(consumer.role.size()) + ":" + consumer.role;
    return key;
}

void ResourceIndex::begin_visible_reference_frame()
{
    visibleReferenceSeen_.clear();
    visibleReferenceFrameActive_ = true;
}

void ResourceIndex::register_visible_reference(const std::filesystem::path &logicalPath,
                                               ResourceKind kind, ResourceConsumer consumer)
{
    if (logicalPath.empty())
        return;
    const std::string key = visible_consumer_key(consumer);
    auto existing = visibleReferenceDemands_.find(key);
    if (existing != visibleReferenceDemands_.end() &&
        (existing->second.logicalPath.lexically_normal() != logicalPath.lexically_normal() ||
         existing->second.kind != kind))
    {
        const auto previousPath = existing->second.logicalPath;
        const auto previousConsumer = existing->second.consumer;
        unregister_consumer(previousPath, previousConsumer);
        existing = visibleReferenceDemands_.end();
    }
    register_consumer(logicalPath, kind, consumer);
    if (existing == visibleReferenceDemands_.end())
        existing = visibleReferenceDemands_.emplace(key,
            VisibleReferenceDemand{logicalPath, kind, consumer}).first;
    if (visibleReferenceFrameActive_)
        visibleReferenceSeen_.insert(key);
}

void ResourceIndex::register_visible_animation_texture(
    const std::filesystem::path &logicalPath, ResourceConsumer consumer)
{
    consumer.role = "animation.preview.frame";
    register_visible_reference(logicalPath, ResourceKind::Texture, std::move(consumer));
}

void ResourceIndex::end_visible_reference_frame()
{
    if (!visibleReferenceFrameActive_)
        return;
    std::vector<std::string> stale;
    stale.reserve(visibleReferenceDemands_.size());
    for (const auto &[key, demand] : visibleReferenceDemands_)
    {
        (void)demand;
        if (!visibleReferenceSeen_.contains(key))
            stale.push_back(key);
    }
    for (const auto &key : stale)
    {
        const auto demand = visibleReferenceDemands_.find(key);
        if (demand == visibleReferenceDemands_.end())
            continue;
        const auto logicalPath = demand->second.logicalPath;
        const auto consumer = demand->second.consumer;
        visibleReferenceDemands_.erase(demand);
        unregister_consumer(logicalPath, consumer);
    }
    visibleReferenceSeen_.clear();
    visibleReferenceFrameActive_ = false;
}

void ResourceIndex::unregister_consumer(const std::filesystem::path &logicalPath,
                                       const ResourceConsumer &consumer)
{
    const auto absolute = logical_absolute(logicalPath);
    if (!absolute)
        return;
    const auto found = entries_.find(logical_key(*absolute));
    if (found != entries_.end())
        std::erase(found->second.consumers, consumer);
    const auto visible = visibleReferenceDemands_.find(visible_consumer_key(consumer));
    const auto visibleAbsolute = visible == visibleReferenceDemands_.end()
        ? std::optional<std::filesystem::path>{}
        : logical_absolute(visible->second.logicalPath);
    if (visible != visibleReferenceDemands_.end() && visibleAbsolute && *visibleAbsolute == *absolute)
    {
        visibleReferenceSeen_.erase(visible->first);
        visibleReferenceDemands_.erase(visible);
    }
}

void ResourceIndex::unregister_document(const DocumentIdentity &document)
{
    for (auto &[key, entry] : entries_)
    {
        (void)key;
        std::erase_if(entry.consumers, [&](const ResourceConsumer &consumer) {
            return consumer.document == document;
        });
    }
    std::vector<std::string> visibleKeys;
    for (const auto &[key, demand] : visibleReferenceDemands_)
        if (demand.consumer.document == document)
            visibleKeys.push_back(key);
    for (const auto &key : visibleKeys)
    {
        visibleReferenceDemands_.erase(key);
        visibleReferenceSeen_.erase(key);
    }
}

std::vector<ResourceConsumer> ResourceIndex::affected_consumers(
    const std::filesystem::path &changedPath, bool includeSubtree) const
{
    std::vector<ResourceConsumer> result;
    const auto absolute = logical_absolute(changedPath);
    if (!absolute)
        return result;
    const std::string directKey = logical_key(*absolute);
    const auto add_entry = [&](const std::string &key) {
        const auto found = entries_.find(key);
        if (found == entries_.end())
            return;
        for (const auto &consumer : found->second.consumers)
            if (std::find(result.begin(), result.end(), consumer) == result.end())
                result.push_back(consumer);
    };
    add_entry(directKey);
    if (const auto aliases = logicalByAliasAncestor_.find(directKey);
        aliases != logicalByAliasAncestor_.end())
        for (const auto &logicalKey : aliases->second)
            add_entry(logicalKey);
    if (const auto unresolved = logicalByUnresolvedTarget_.find(directKey);
        unresolved != logicalByUnresolvedTarget_.end())
        for (const auto &logicalKey : unresolved->second)
            add_entry(logicalKey);
    if (includeSubtree)
    {
        for (const auto &[key, entry] : entries_)
        {
            const auto logicalPath = assetRoot_ / std::filesystem::path(key);
            if (!contains_path(*absolute, logicalPath))
                continue;
            for (const auto &consumer : entry.consumers)
                if (std::find(result.begin(), result.end(), consumer) == result.end())
                    result.push_back(consumer);
        }
        // A canonical directory may already have been renamed or removed, so do not
        // depend on filesystem canonicalization to find its previously resolved aliases.
        for (const auto &[canonicalKey, logicalKeys] : logicalByCanonical_)
        {
            const auto canonicalPath = assetRoot_ / std::filesystem::path(canonicalKey);
            if (!contains_path(*absolute, canonicalPath))
                continue;
            for (const auto &logicalKey : logicalKeys)
                add_entry(logicalKey);
        }
        // An event can name a parent of a symlink hop in a cached logical path.
        // The hop may no longer exist by the time reconciliation runs, so match
        // against the retained lexical hop path instead of resolving it again.
        const std::string aliasPrefix = directKey.empty() ? std::string{} : directKey + "/";
        for (auto alias = logicalByAliasAncestor_.lower_bound(aliasPrefix);
             alias != logicalByAliasAncestor_.end() && alias->first.starts_with(aliasPrefix); ++alias)
        {
            for (const auto &logicalKey : alias->second)
                add_entry(logicalKey);
        }
        for (auto unresolved = logicalByUnresolvedTarget_.lower_bound(aliasPrefix);
             unresolved != logicalByUnresolvedTarget_.end() &&
             unresolved->first.starts_with(aliasPrefix); ++unresolved)
        {
            for (const auto &logicalKey : unresolved->second)
                add_entry(logicalKey);
        }
    }

    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(*absolute, statusError);
    const bool changedSymlink = !statusError && std::filesystem::is_symlink(status);
    if (changedSymlink)
    {
        // A newly created or atomically replaced directory link may not have
        // been part of a prior resolution. Include already cached logical
        // descendants even when the native event classifies the link itself as
        // a file/non-directory event.
        for (const auto &[key, entry] : entries_)
        {
            (void)entry;
            const auto logicalPath = assetRoot_ / std::filesystem::path(key);
            if (contains_path(*absolute, logicalPath))
                add_entry(key);
        }
        return result;
    }

    std::filesystem::path canonical = *absolute;
    std::error_code canonicalError;
    const auto resolved = std::filesystem::canonical(*absolute, canonicalError);
    if (!canonicalError)
        canonical = resolved;
    if (!contains_path(assetRoot_, canonical))
        return result;
    const auto aliases = logicalByCanonical_.find(logical_key(canonical));
    if (aliases != logicalByCanonical_.end())
        for (const auto &key : aliases->second)
            add_entry(key);
    return result;
}

std::vector<ResourceFallbackChange> ResourceIndex::inspect_fallback_paths(std::size_t maxPathStats)
{
    std::vector<ResourceFallbackChange> changes;
    if (maxPathStats == 0 || entries_.empty())
        return changes;
    auto current = entries_.upper_bound(fallbackCursor_);
    if (current == entries_.end())
        current = entries_.begin();
    std::size_t visitedEntries = 0;
    std::size_t pathStats = 0;
    while (visitedEntries < entries_.size() && pathStats < maxPathStats)
    {
        if (current == entries_.end())
            current = entries_.begin();
        const std::string key = current->first;
        auto &entry = current->second;
        ++current;
        ++visitedEntries;
        if (entry.consumers.empty())
            continue;

        const auto logicalPath = assetRoot_ / std::filesystem::path(key);
        const auto previous = entry.resolution;
        auto consumers = affected_consumers(logicalPath);
        const auto latest = resolve_impl(logicalPath, previous.kind, true);
        ++pathStats;
        fallbackCursor_ = key;
        const bool changed = previous.status != latest.status ||
            previous.canonicalPath != latest.canonicalPath || previous.size != latest.size ||
            previous.modified != latest.modified;
        if (!changed)
            continue;
        auto invalidationPath = logicalPath;
        if (previous.status == ResourceStatus::Resolved && latest.status == ResourceStatus::Resolved &&
            previous.canonicalPath == latest.canonicalPath)
        {
            invalidationPath = latest.canonicalPath;
            consumers = affected_consumers(invalidationPath);
        }
        invalidate(invalidationPath);
        if (!consumers.empty())
            changes.push_back(ResourceFallbackChange{logicalPath, consumers});
    }
    return changes;
}

std::uint64_t ResourceIndex::invalidate(const std::filesystem::path &changedPath)
{
    const auto absolute = logical_absolute(changedPath);
    if (!absolute)
        return 0;
    const std::string directKey = logical_key(*absolute);
    std::vector<std::string> affectedKeys{directKey};
    std::filesystem::path canonical = *absolute;
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(*absolute, statusError);
    const bool changedSymlink = !statusError && std::filesystem::is_symlink(status);
    if (const auto aliases = logicalByAliasAncestor_.find(directKey);
        aliases != logicalByAliasAncestor_.end())
        affectedKeys.insert(affectedKeys.end(), aliases->second.begin(), aliases->second.end());
    if (const auto unresolved = logicalByUnresolvedTarget_.find(directKey);
        unresolved != logicalByUnresolvedTarget_.end())
        affectedKeys.insert(affectedKeys.end(), unresolved->second.begin(), unresolved->second.end());
    // Preserve dependency fan-out when a directory containing an intermediate
    // symlink hop is renamed, removed, or replaced. These mappings deliberately
    // survive broken chains until a later successful resolve can rebind them.
    const std::string aliasPrefix = directKey.empty() ? std::string{} : directKey + "/";
    for (auto alias = logicalByAliasAncestor_.lower_bound(aliasPrefix);
         alias != logicalByAliasAncestor_.end() && alias->first.starts_with(aliasPrefix); ++alias)
    {
        affectedKeys.insert(affectedKeys.end(), alias->second.begin(), alias->second.end());
    }
    for (auto unresolved = logicalByUnresolvedTarget_.lower_bound(aliasPrefix);
         unresolved != logicalByUnresolvedTarget_.end() &&
         unresolved->first.starts_with(aliasPrefix); ++unresolved)
    {
        affectedKeys.insert(affectedKeys.end(), unresolved->second.begin(), unresolved->second.end());
    }
    if (changedSymlink)
    {
        for (const auto &[key, entry] : entries_)
        {
            (void)entry;
            const auto logicalPath = assetRoot_ / std::filesystem::path(key);
            if (contains_path(*absolute, logicalPath))
                affectedKeys.push_back(key);
        }
    }
    if (!changedSymlink)
    {
        std::error_code canonicalError;
        const auto resolved = std::filesystem::canonical(*absolute, canonicalError);
        if (!canonicalError)
            canonical = resolved;
        const auto aliases = logicalByCanonical_.find(logical_key(canonical));
        if (aliases != logicalByCanonical_.end())
            affectedKeys.insert(affectedKeys.end(), aliases->second.begin(), aliases->second.end());
    }
    std::sort(affectedKeys.begin(), affectedKeys.end());
    affectedKeys.erase(std::unique(affectedKeys.begin(), affectedKeys.end()), affectedKeys.end());

    std::uint64_t canonicalVersion = 0;
    if (!changedSymlink && contains_path(assetRoot_, canonical))
    {
        auto &version = canonicalVersions_[logical_key(canonical)];
        if (version != std::numeric_limits<std::uint64_t>::max())
            ++version;
        canonicalVersion = version;
    }
    for (const auto &key : affectedKeys)
    {
        auto &version = logicalVersions_[key];
        if (version == 0)
            version = 1;
        else if (version != std::numeric_limits<std::uint64_t>::max())
            ++version;
        const auto found = entries_.find(key);
        if (found != entries_.end())
        {
            found->second.resolutionCacheValid = false;
            found->second.resolution.logicalVersion = version;
            found->second.resolution.preparation = ResourcePreparationState::Dirty;
            found->second.resolution.preparationError.clear();
            if (canonicalVersion != 0)
                found->second.resolution.canonicalVersion = canonicalVersion;
        }
        auto &parentRevision = directoryRevisions_[logical_key(
            (assetRoot_ / std::filesystem::path(key)).parent_path())];
        if (parentRevision != std::numeric_limits<std::uint64_t>::max())
            ++parentRevision;
    }
    for (std::filesystem::path dir = absolute->parent_path().lexically_normal();
         !dir.empty() && contains_path(assetRoot_, dir);)
    {
        auto &directoryRevision = directoryRevisions_[logical_key(dir)];
        if (directoryRevision != std::numeric_limits<std::uint64_t>::max())
            ++directoryRevision;
        if (dir == assetRoot_)
            break;
        const auto parent = dir.parent_path();
        if (parent == dir)
            break;
        dir = parent;
    }
    return affectedKeys.empty() ? 0 : logicalVersions_[directKey];
}

void ResourceIndex::invalidate_subtree(const std::filesystem::path &directory,
                                       std::uint64_t uncertaintyEpoch)
{
    const auto absolute = logical_absolute(directory);
    if (!absolute)
        return;
    uncertaintyEpoch_ = std::max(uncertaintyEpoch_, uncertaintyEpoch);
    const auto invalidate_logical_subtree = [&](const std::filesystem::path &logicalDirectory) {
        const auto key = logical_key(logicalDirectory);
        subtreeUncertainty_[key] = std::max(subtreeUncertainty_[key], uncertaintyEpoch);
        auto &revision = directoryRevisions_[key];
        if (revision != std::numeric_limits<std::uint64_t>::max())
            ++revision;
    };
    invalidate_logical_subtree(*absolute);

    // Preserve the old canonical mapping long enough to invalidate every logical
    // alias consumer, including children whose target directory no longer exists.
    for (const auto &[canonicalKey, logicalKeys] : logicalByCanonical_)
    {
        const auto canonicalPath = assetRoot_ / std::filesystem::path(canonicalKey);
        if (!contains_path(*absolute, canonicalPath))
            continue;
        for (const auto &logicalKey : logicalKeys)
            invalidate_logical_subtree(assetRoot_ / std::filesystem::path(logicalKey));
    }
    const std::string unresolvedPrefix = logical_key(*absolute);
    const std::string unresolvedSearch = unresolvedPrefix.empty()
        ? std::string{} : unresolvedPrefix + "/";
    for (auto unresolved = logicalByUnresolvedTarget_.lower_bound(unresolvedSearch);
         unresolved != logicalByUnresolvedTarget_.end() &&
         (unresolvedPrefix.empty() || unresolved->first.starts_with(unresolvedSearch)); ++unresolved)
    {
        for (const auto &logicalKey : unresolved->second)
            invalidate_logical_subtree(assetRoot_ / std::filesystem::path(logicalKey));
    }
}

ResourceRequestToken ResourceIndex::begin_request(const std::filesystem::path &logicalPath)
{
    const auto resolution = resolve(logicalPath);
    const auto absolute = logical_absolute(logicalPath);
    if (!absolute || nextRequestId_ == 0 || nextRequestId_ == std::numeric_limits<std::uint64_t>::max())
        return {projectGeneration_, {}, 0, 0, 0, {}, 0, 0, ResourceKind::Unknown};
    const auto key = logical_key(*absolute);
    auto &entry = entries_[key];
    const auto invalidationRevision = revision_for(absolute->parent_path());
    const auto uncertaintyEpoch = uncertainty_for(*absolute);
    entry.requestId = nextRequestId_++;
    entry.invalidationRevision = invalidationRevision;
    entry.requestToken = {projectGeneration_, *absolute, entry.requestId, entry.invalidationRevision,
                          resolution.logicalVersion, resolution.canonicalPath,
                          resolution.canonicalVersion, uncertaintyEpoch, resolution.kind};
    entry.resolution.preparation = ResourcePreparationState::Preparing;
    entry.resolution.preparationError.clear();
    if (!resolution.canonicalPath.empty())
    {
        const auto canonicalKey = canonical_request_key(resolution.canonicalPath, resolution.kind);
        canonicalRequests_[canonicalKey] = entry.requestToken;
        preparedCanonicalVersions_.erase(canonicalKey);
    }
    return entry.requestToken;
}

ResourceRequestToken ResourceIndex::join_request(const std::filesystem::path &logicalPath)
{
    const auto resolution = resolve(logicalPath);
    const auto absolute = logical_absolute(logicalPath);
    if (!absolute)
        return {projectGeneration_, {}, 0, 0, 0, {}, 0, 0, ResourceKind::Unknown};
    const auto found = entries_.find(logical_key(*absolute));
    if (found != entries_.end())
    {
        const auto &entry = found->second;
        const auto &current = entry.requestToken;
        const auto state = entry.resolution.preparation;
        const bool joinableState = state == ResourcePreparationState::Preparing ||
                                   state == ResourcePreparationState::AwaitingOwnerCommit ||
                                   state == ResourcePreparationState::RetryPending ||
                                   state == ResourcePreparationState::Ready;
        if (joinableState && current.requestId != 0 &&
            current.projectGeneration == projectGeneration_ && current.logicalPath == *absolute &&
            current.invalidationRevision == revision_for(absolute->parent_path()) &&
            current.logicalVersion == resolution.logicalVersion &&
            current.canonicalPath == resolution.canonicalPath &&
            current.canonicalVersion == resolution.canonicalVersion &&
            current.uncertaintyEpoch == uncertainty_for(*absolute) && current.kind == resolution.kind)
            return current;
    }

    if (!resolution.canonicalPath.empty())
    {
        const auto key = canonical_request_key(resolution.canonicalPath, resolution.kind);
        const auto prepared = preparedCanonicalVersions_.find(key);
        if (prepared != preparedCanonicalVersions_.end() && prepared->second == resolution.canonicalVersion)
        {
            const auto token = begin_request(logicalPath);
            set_preparation_state(token, ResourcePreparationState::Ready);
            return token;
        }
        const auto active = canonicalRequests_.find(key);
        if (active != canonicalRequests_.end() && active->second.projectGeneration == projectGeneration_ &&
            active->second.canonicalVersion == resolution.canonicalVersion)
        {
            const auto activeEntry = entries_.find(logical_key(active->second.logicalPath));
            if (activeEntry != entries_.end() &&
                (activeEntry->second.resolution.preparation == ResourcePreparationState::Preparing ||
                 activeEntry->second.resolution.preparation == ResourcePreparationState::AwaitingOwnerCommit ||
                 activeEntry->second.resolution.preparation == ResourcePreparationState::RetryPending) &&
                accepts(active->second))
                return begin_request(logicalPath);
        }
    }
    return begin_request(logicalPath);
}

std::optional<ResourceRequestToken> ResourceIndex::current_request(
    const std::filesystem::path &logicalPath) const
{
    const auto absolute = logical_absolute(logicalPath);
    if (!absolute)
        return std::nullopt;
    const auto found = entries_.find(logical_key(*absolute));
    if (found == entries_.end() || found->second.requestToken.requestId == 0)
        return std::nullopt;
    return found->second.requestToken;
}

bool ResourceIndex::accepts(const ResourceRequestToken &token) const
{
    if (token.requestId == 0 || token.projectGeneration != projectGeneration_)
        return false;
    const auto absolute = logical_absolute(token.logicalPath);
    if (!absolute)
        return false;
    const auto found = entries_.find(logical_key(*absolute));
    if (found == entries_.end())
        return false;
    return found->second.requestToken == token && found->second.requestId == token.requestId &&
           found->second.invalidationRevision == token.invalidationRevision &&
           revision_for(absolute->parent_path()) == token.invalidationRevision &&
           logicalVersions_.at(logical_key(*absolute)) == token.logicalVersion &&
           found->second.resolution.canonicalPath == token.canonicalPath &&
           found->second.resolution.canonicalVersion == token.canonicalVersion &&
           uncertainty_for(*absolute) == token.uncertaintyEpoch;
}

bool ResourceIndex::is_ready(const ResourceRequestToken &token) const
{
    if (!accepts(token))
        return false;
    const auto found = entries_.find(logical_key(token.logicalPath));
    return found != entries_.end() &&
           found->second.resolution.preparation == ResourcePreparationState::Ready;
}

bool ResourceIndex::set_preparation_state(const ResourceRequestToken &token,
                                          ResourcePreparationState state, std::string error)
{
    if (!accepts(token))
        return false;
    const auto found = entries_.find(logical_key(token.logicalPath));
    if (found == entries_.end())
        return false;
    found->second.resolution.preparation = state;
    found->second.resolution.preparationError = std::move(error);
    if (!token.canonicalPath.empty())
    {
        const auto key = canonical_request_key(token.canonicalPath, token.kind);
        if (state == ResourcePreparationState::Ready)
        {
            preparedCanonicalVersions_[key] = token.canonicalVersion;
            const auto active = canonicalRequests_.find(key);
            if (active != canonicalRequests_.end() && active->second == token)
                canonicalRequests_.erase(active);
        }
        else if (state == ResourcePreparationState::Failed)
        {
            const auto active = canonicalRequests_.find(key);
            if (active != canonicalRequests_.end() && active->second == token)
                canonicalRequests_.erase(active);
        }
    }
    return true;
}

ProjectGeneration ResourceIndex::project_generation() const noexcept { return projectGeneration_; }
const std::filesystem::path &ResourceIndex::asset_root() const noexcept { return assetRoot_; }

std::uint64_t ResourceIndex::directory_revision(const std::filesystem::path &directory) const
{
    const auto absolute = logical_absolute(directory);
    return absolute ? revision_for(*absolute) : 0;
}

std::uint64_t ResourceIndex::uncertainty_epoch() const noexcept { return uncertaintyEpoch_; }
std::size_t ResourceIndex::size() const noexcept { return entries_.size(); }

std::optional<std::filesystem::path> ResourceIndex::logical_absolute(const std::filesystem::path &path) const
{
    if (assetRoot_.empty() || path.empty())
        return std::nullopt;
    auto normalized = path;
    if (!normalized.is_absolute() && normalized.begin() != normalized.end() &&
        *normalized.begin() == "assets")
        normalized = normalized.lexically_relative("assets");
    const auto absolute = (normalized.is_absolute() ? normalized : assetRoot_ / normalized).lexically_normal();
    if (!contains_path(assetRoot_, absolute))
        return std::nullopt;
    return absolute;
}

std::string ResourceIndex::logical_key(const std::filesystem::path &path) const
{
    if (path == assetRoot_)
        return {};
    return path.lexically_relative(assetRoot_).generic_string();
}

bool ResourceIndex::contains_path(const std::filesystem::path &root,
                                  const std::filesystem::path &candidate)
{
    const auto relative = candidate.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty())
        return candidate.lexically_normal() == root.lexically_normal();
    for (const auto &part : relative)
        if (part == "..")
            return false;
    return !relative.is_absolute();
}

std::uint64_t ResourceIndex::revision_for(const std::filesystem::path &directory) const
{
    const auto found = directoryRevisions_.find(logical_key(directory.lexically_normal()));
    return found == directoryRevisions_.end() ? 0 : found->second;
}

std::uint64_t ResourceIndex::uncertainty_for(const std::filesystem::path &path) const
{
    std::uint64_t result = 0;
    auto current = path.lexically_normal();
    while (contains_path(assetRoot_, current))
    {
        const auto found = subtreeUncertainty_.find(logical_key(current));
        if (found != subtreeUncertainty_.end())
            result = std::max(result, found->second);
        if (current == assetRoot_)
            break;
        current = current.parent_path();
    }
    return result;
}

void ResourceIndex::update_canonical_link(const std::string &logicalKey, Entry &entry,
                                          const std::filesystem::path &canonicalPath)
{
    const auto oldCanonical = entry.canonicalLinkPath;
    if (!oldCanonical.empty())
    {
        const auto oldKey = logical_key(oldCanonical);
        auto links = logicalByCanonical_.find(oldKey);
        if (links != logicalByCanonical_.end())
        {
            std::erase(links->second, logicalKey);
            if (links->second.empty())
                logicalByCanonical_.erase(links);
        }
    }
    if (!canonicalPath.empty())
    {
        auto &links = logicalByCanonical_[logical_key(canonicalPath)];
        if (std::find(links.begin(), links.end(), logicalKey) == links.end())
            links.push_back(logicalKey);
    }
    entry.canonicalLinkPath = canonicalPath;
}

void ResourceIndex::track_logical_alias_ancestors(const std::filesystem::path &logicalPath,
                                                   Entry &entry)
{
    const std::string logicalKey = logical_key(logicalPath);
    auto trace = resolved_symlink_ancestors(logicalPath, assetRoot_);
    std::vector<std::string> currentAncestors;
    for (const auto &ancestor : trace.ancestors)
    {
        if (!contains_path(assetRoot_, ancestor))
            continue;
        const auto ancestorKey = logical_key(ancestor);
        if (std::find(currentAncestors.begin(), currentAncestors.end(), ancestorKey) == currentAncestors.end())
            currentAncestors.push_back(ancestorKey);
    }

    std::vector<std::string> nextAncestors = currentAncestors;
    if (!trace.allLinksResolve)
    {
        constexpr std::size_t MaxRetainedAliasAncestors = 128;
        for (const auto &oldAncestor : entry.logicalAliasAncestors)
        {
            if (nextAncestors.size() >= MaxRetainedAliasAncestors)
                break;
            if (std::find(nextAncestors.begin(), nextAncestors.end(), oldAncestor) == nextAncestors.end())
                nextAncestors.push_back(oldAncestor);
        }
    }

    std::vector<std::string> nextUnresolvedTargets;
    constexpr std::size_t MaxTrackedUnresolvedTargets = 512;
    for (const auto &target : trace.unresolvedTargets)
    {
        if (!contains_path(assetRoot_, target) || nextUnresolvedTargets.size() >= MaxTrackedUnresolvedTargets)
            continue;
        const auto targetKey = logical_key(target);
        if (std::find(nextUnresolvedTargets.begin(), nextUnresolvedTargets.end(), targetKey) ==
            nextUnresolvedTargets.end())
            nextUnresolvedTargets.push_back(targetKey);
    }

    const auto update_reverse = [&](const std::vector<std::string> &oldKeys,
                                    const std::vector<std::string> &newKeys,
                                    auto &reverseIndex) {
        for (const auto &oldKey : oldKeys)
        {
            if (std::find(newKeys.begin(), newKeys.end(), oldKey) != newKeys.end())
                continue;
            const auto found = reverseIndex.find(oldKey);
            if (found == reverseIndex.end())
                continue;
            std::erase(found->second, logicalKey);
            if (found->second.empty())
                reverseIndex.erase(found);
        }
        for (const auto &newKey : newKeys)
        {
            auto &logicalKeys = reverseIndex[newKey];
            if (std::find(logicalKeys.begin(), logicalKeys.end(), logicalKey) == logicalKeys.end())
                logicalKeys.push_back(logicalKey);
        }
    };

    update_reverse(entry.logicalAliasAncestors, nextAncestors, logicalByAliasAncestor_);
    update_reverse(entry.logicalUnresolvedTargets, nextUnresolvedTargets, logicalByUnresolvedTarget_);
    entry.logicalAliasAncestors = std::move(nextAncestors);
    entry.logicalUnresolvedTargets = std::move(nextUnresolvedTargets);
}
} // namespace platformator_editor
