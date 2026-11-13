#include "project.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <sys/stat.h>

#include <json.hpp>

namespace platformator_editor
{
bool should_skip_directory(std::string_view name) noexcept
{
    if (name.empty() || name == "." || name == "..")
        return false;
    if (name.starts_with('.'))
        return true;
    return name == "bin" || name == "build" || name == "dist" ||
           name == "vendored" || name == "vendor" || name == "node_modules" ||
           name == "specs" || name == "benchmark" || name == "diplomski" ||
           name == "paper" || name == "docs";
}

struct SourceHashCacheKey
{
    std::string canonicalPath;
    std::uintmax_t size = 0;
    long long lastWriteTime = 0;
    long long changeTimeSec = 0;
    long long changeTimeNsec = 0;

    bool operator<(const SourceHashCacheKey &other) const
    {
        if (size != other.size)
            return size < other.size;
        if (lastWriteTime != other.lastWriteTime)
            return lastWriteTime < other.lastWriteTime;
        if (changeTimeSec != other.changeTimeSec)
            return changeTimeSec < other.changeTimeSec;
        if (changeTimeNsec != other.changeTimeNsec)
            return changeTimeNsec < other.changeTimeNsec;
        return canonicalPath < other.canonicalPath;
    }
};

static std::mutex s_sourceHashCacheMutex;
static std::map<SourceHashCacheKey, std::uint64_t> s_sourceHashCache;

void clear_source_hash_cache() noexcept
{
    std::lock_guard lock(s_sourceHashCacheMutex);
    s_sourceHashCache.clear();
}

std::size_t source_hash_cache_size() noexcept
{
    std::lock_guard lock(s_sourceHashCacheMutex);
    return s_sourceHashCache.size();
}

struct ProjectScanTicketState
{
    std::uint64_t requestId = 0;
    std::uint64_t projectGeneration = 0;
    std::atomic<bool> cancelled{false};
};

struct ProjectScanSharedState
{
    struct QueuedResult
    {
        ProjectScanResult result;
        std::weak_ptr<ProjectScanTicketState> ticket;
    };

    ProjectScanSharedState(std::size_t publicationLimit, std::size_t outstandingLimit)
        : maxPendingPublications(publicationLimit), maxOutstandingScans(outstandingLimit)
    {
    }

    std::mutex mutex;
    const std::size_t maxPendingPublications;
    const std::size_t maxOutstandingScans;
    std::size_t outstandingScans = 0;
    bool alive = true;
    std::vector<QueuedResult> ready;
};

namespace
{
std::filesystem::path canonical_directory(const std::filesystem::path &path, const char *label)
{
    std::error_code error;
    auto canonical = std::filesystem::canonical(path, error);
    if (error || !std::filesystem::is_directory(canonical))
        throw std::invalid_argument(std::string(label) + " is not an existing directory: " + path.string());
    return canonical;
}

bool path_is_within(const std::filesystem::path &root, const std::filesystem::path &candidate)
{
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty())
        return candidate == root;
    return *relative.begin() != ".." && !relative.is_absolute();
}

bool register_file_api_query(const std::filesystem::path &binaryDirectory,
                             const std::filesystem::path &projectRoot,
                             std::string &errorMessage)
{
    std::error_code error;
    const auto canonicalBinary = std::filesystem::canonical(binaryDirectory, error);
    if (error || !std::filesystem::is_directory(canonicalBinary, error) || error ||
        !path_is_within(projectRoot, canonicalBinary))
    {
        errorMessage = "The selected CMake build directory is unavailable or outside the project.";
        return false;
    }

    auto directory = canonicalBinary;
    for (const auto *component : {".cmake", "api", "v1", "query", "client-platformator"})
    {
        directory /= component;
        error.clear();
        const auto status = std::filesystem::symlink_status(directory, error);
        if (!error && std::filesystem::is_symlink(status))
        {
            errorMessage = "The CMake File API query path contains a symbolic link.";
            return false;
        }
        if (error && error != std::errc::no_such_file_or_directory)
        {
            errorMessage = "The CMake File API query path could not be inspected.";
            return false;
        }
        if (error || !std::filesystem::exists(status))
        {
            error.clear();
            if (!std::filesystem::create_directory(directory, error) && error)
            {
                errorMessage = "The CMake File API query directory could not be created: " + error.message();
                return false;
            }
        }
        else if (!std::filesystem::is_directory(status))
        {
            errorMessage = "The CMake File API query path contains a non-directory entry.";
            return false;
        }

        error.clear();
        const auto canonicalDirectory = std::filesystem::canonical(directory, error);
        if (error || !path_is_within(canonicalBinary, canonicalDirectory))
        {
            errorMessage = "The CMake File API query path resolves outside the build directory.";
            return false;
        }
        directory = canonicalDirectory;
    }

    const auto queryFile = directory / "codemodel-v2";
    error.clear();
    const auto queryStatus = std::filesystem::symlink_status(queryFile, error);
    if (!error && std::filesystem::is_symlink(queryStatus))
    {
        errorMessage = "The CMake File API query file is a symbolic link.";
        return false;
    }
    if (error && error != std::errc::no_such_file_or_directory)
    {
        errorMessage = "The CMake File API query file could not be inspected.";
        return false;
    }
    if (!error && std::filesystem::exists(queryStatus))
    {
        if (!std::filesystem::is_regular_file(queryStatus))
        {
            errorMessage = "The CMake File API query path is not a regular file.";
            return false;
        }
        return true;
    }

    std::ofstream query(queryFile, std::ios::binary | std::ios::out);
    if (!query)
    {
        errorMessage = "The CMake File API query file could not be created.";
        return false;
    }
    query.close();
    if (!query)
    {
        errorMessage = "The CMake File API query file could not be closed cleanly.";
        return false;
    }
    return true;
}

std::filesystem::path path_from_utf8(std::string_view value)
{
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value)
        encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(std::move(encoded));
}

std::string path_to_generic_utf8(const std::filesystem::path &path)
{
    const auto encoded = path.generic_u8string();
    return {reinterpret_cast<const char *>(encoded.data()), encoded.size()};
}

void publish(const std::shared_ptr<ProjectScanSharedState> &state, ProjectScanResult result,
             const std::shared_ptr<ProjectScanTicketState> &ticket)
{
    std::lock_guard lock(state->mutex);
    --state->outstandingScans;
    if (!state->alive)
        return;
    // Every accepted scan reserves a publication slot until the owner consumes it.
    assert(state->ready.size() < state->maxPendingPublications);
    state->ready.push_back(ProjectScanSharedState::QueuedResult{std::move(result), ticket});
}

ProjectScanResult perform_scan(const std::filesystem::path &root,
                               const std::filesystem::path &logicalRoot,
                               const std::filesystem::path &assetRoot,
                               ProjectScanOptions options,
                               const std::shared_ptr<ProjectScanTicketState> &ticket)
{
    ProjectScanResult result;
    result.requestId = ticket->requestId;
    result.projectGeneration = ticket->projectGeneration;
    result.root = logicalRoot;
    result.canonicalRoot = root;

    const auto display_path = [&](const std::filesystem::path &path) {
        return (logicalRoot / path.lexically_relative(root)).lexically_normal();
    };

    std::error_code error;
    const auto directoryOptions = std::filesystem::directory_options::none;
    std::set<std::string> identityEntries;
    std::set<std::string> filesystemIdentityEntries;
    std::size_t entriesVisited = 0;
    std::uintmax_t sourceBytesRead = 0;
    const auto add_entry = [&](const std::filesystem::directory_entry &entry, bool recursive) {
        std::error_code typeError;
        const bool symlink = entry.is_symlink(typeError);
        if (typeError)
        {
            result.error = "could not inspect entry type: " + entry.path().string() + ": " + typeError.message();
            return;
        }
        if (symlink)
        {
            std::error_code targetError;
            const auto target = std::filesystem::canonical(entry.path(), targetError);
            if (targetError)
            {
                // Broken aliases stay visible in a browsed folder so the user can repair
                // them. Cycles, unreadable targets and escaping links are never traversed.
                std::error_code linkError;
                const auto linkTarget = std::filesystem::read_symlink(entry.path(), linkError);
                if (linkError)
                {
                    result.error = "could not read symbolic-link target: " + entry.path().string() +
                                   ": " + linkError.message();
                    return;
                }
                if (options.trackFileMetadata)
                    result.inventoryEntries.emplace_back(display_path(entry.path()),
                        "l:" + linkTarget.generic_string());
                if (targetError == std::errc::permission_denied ||
                    targetError == std::errc::io_error)
                {
                    result.error = "could not inspect symbolic-link target: " + entry.path().string() +
                                   ": " + targetError.message();
                    return;
                }
                if (!recursive && targetError == std::errc::no_such_file_or_directory &&
                    result.files.size() < options.maxFiles)
                    result.files.push_back(display_path(entry.path()));
                return;
            }
            if (!path_is_within(assetRoot, target))
            {
                if (options.trackFileMetadata)
                {
                    std::error_code linkError;
                    const auto linkTarget = std::filesystem::read_symlink(entry.path(), linkError);
                    if (linkError)
                        result.error = "could not read symbolic-link target: " + entry.path().string() +
                                       ": " + linkError.message();
                    else
                        result.inventoryEntries.emplace_back(display_path(entry.path()),
                            "l:outside:" + linkTarget.generic_string());
                }
                return;
            }
            std::error_code targetTypeError;
            if (std::filesystem::is_directory(target, targetTypeError) && !targetTypeError)
            {
                const auto name = entry.path().filename().string();
                if (options.sourceInputsOnly && should_skip_directory(name))
                    return;
                if (options.trackFileMetadata)
                {
                    result.inventoryEntries.emplace_back(display_path(entry.path()),
                        "d:" + target.generic_string());
                    filesystemIdentityEntries.insert("d:" + display_path(entry.path()).lexically_relative(logicalRoot).generic_string() +
                                                     ":" + target.generic_string());
                }
                if (options.includeDirectories)
                {
                    if (result.directories.size() == options.maxDirectories)
                        result.truncated = true;
                    else
                        result.directories.push_back(display_path(entry.path()));
                }
                return;
            }
            if (targetTypeError)
            {
                result.error = "could not inspect symbolic-link target: " + entry.path().string() +
                               ": " + targetTypeError.message();
                return;
            }
        }
        const bool directory = entry.is_directory(typeError);
        if (typeError)
        {
            result.error = "could not inspect directory entry: " + entry.path().string() + ": " + typeError.message();
            return;
        }
        if (directory)
        {
            const auto name = entry.path().filename().string();
            if (options.sourceInputsOnly && should_skip_directory(name))
                return;
            if (options.trackFileMetadata)
            {
                std::error_code canonicalError;
                auto canonical = std::filesystem::canonical(entry.path(), canonicalError);
                if (canonicalError)
                {
                    result.error = "could not resolve directory identity: " + entry.path().string() +
                                   ": " + canonicalError.message();
                    return;
                }
                result.inventoryEntries.emplace_back(display_path(entry.path()), "d:" + canonical.generic_string());
                filesystemIdentityEntries.insert("d:" + display_path(entry.path()).lexically_relative(logicalRoot).generic_string() +
                                                 ":" + canonical.generic_string());
            }
            if (options.includeDirectories)
            {
                if (result.directories.size() == options.maxDirectories)
                    result.truncated = true;
                else
                    result.directories.push_back(display_path(entry.path()));
            }
            if (recursive && options.sourceInputsOnly && result.directories.size() == options.maxDirectories)
                return;
            return;
        }
        const bool regularFile = entry.is_regular_file(typeError);
        if (typeError)
        {
            result.error = "could not inspect file entry: " + entry.path().string() + ": " + typeError.message();
            return;
        }
        if (!regularFile)
            return;

        if (options.sourceInputsOnly)
        {
            const auto filename = entry.path().filename().string();
            const auto extension = entry.path().extension().string();
            static const std::set<std::string> sourceExtensions{
                ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".cmake", ".in"};
            if (filename != "CMakeLists.txt" && filename != "CMakePresets.json" &&
                filename != "CMakeUserPresets.json" && !sourceExtensions.contains(extension))
                return;
        }
        if (result.files.size() == options.maxFiles)
        {
            result.truncated = true;
            return;
        }
        const auto visiblePath = display_path(entry.path());
        result.files.push_back(visiblePath);
        if (options.trackFileMetadata)
        {
            std::error_code metadataError;
            const auto size = entry.file_size(metadataError);
            if (metadataError)
            {
                result.error = "could not read file size: " + entry.path().string();
                return;
            }
            const auto modified = entry.last_write_time(metadataError);
            if (metadataError)
            {
                result.error = "could not read modification time: " + entry.path().string();
                return;
            }
            std::error_code canonicalError;
            const auto canonical = std::filesystem::canonical(entry.path(), canonicalError);
            if (canonicalError)
            {
                result.error = "could not resolve file identity: " + entry.path().string() +
                               ": " + canonicalError.message();
                return;
            }
            const auto fileIdentity = canonical.generic_string() + ":" + std::to_string(size) + ":" +
                                      std::to_string(modified.time_since_epoch().count());
            result.inventoryEntries.emplace_back(visiblePath, "f:" + fileIdentity);
            filesystemIdentityEntries.insert("f:" + visiblePath.lexically_relative(logicalRoot).generic_string() +
                                             ":" + fileIdentity);
            result.fileMetadata.push_back(ProjectScannedFile{visiblePath, size, modified});
        }
        if (options.sourceInputsOnly)
        {
            std::error_code metadataError;
            const auto size = entry.file_size(metadataError);
            if (metadataError)
                return;
            const auto modified = entry.last_write_time(metadataError);
            if (metadataError)
                return;
            if (size > options.maxSourceBytes - std::min(sourceBytesRead, options.maxSourceBytes))
            {
                result.truncated = true;
                return;
            }

            std::error_code canonError;
            const auto canonicalPath = std::filesystem::canonical(entry.path(), canonError);
            const std::string canonicalKey = canonError ? entry.path().string() : canonicalPath.string();

            struct stat st{};
            long long ctimeSec = 0;
            long long ctimeNsec = 0;
            if (::stat(entry.path().c_str(), &st) == 0)
            {
                ctimeSec = static_cast<long long>(st.st_ctime);
#if defined(__linux__)
                ctimeNsec = static_cast<long long>(st.st_ctim.tv_nsec);
#elif defined(__APPLE__)
                ctimeNsec = static_cast<long long>(st.st_ctimespec.tv_nsec);
#endif
            }

            const SourceHashCacheKey cacheKey{
                canonicalKey,
                size,
                static_cast<long long>(modified.time_since_epoch().count()),
                ctimeSec,
                ctimeNsec
            };

            std::uint64_t contentHash = 0;
            bool foundInCache = false;
            {
                std::lock_guard lock(s_sourceHashCacheMutex);
                auto it = s_sourceHashCache.find(cacheKey);
                if (it != s_sourceHashCache.end())
                {
                    contentHash = it->second;
                    foundInCache = true;
                }
            }

            if (!foundInCache)
            {
                std::ifstream source(entry.path(), std::ios::binary);
                if (!source)
                {
                    result.error = "could not read source input: " + entry.path().string();
                    return;
                }
                contentHash = 14695981039346656037ull;
                std::array<char, 16U * 1024U> buffer{};
                while (source)
                {
                    if (ticket->cancelled.load(std::memory_order_acquire))
                    {
                        result.cancelled = true;
                        break;
                    }
                    source.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                    const auto count = source.gcount();
                    for (std::streamsize index = 0; index < count; ++index)
                    {
                        contentHash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
                        contentHash *= 1099511628211ull;
                    }
                }
                if (source.bad())
                {
                    result.error = "could not finish reading source input: " + entry.path().string();
                    return;
                }
                if (!result.cancelled)
                {
                    std::lock_guard lock(s_sourceHashCacheMutex);
                    s_sourceHashCache[cacheKey] = contentHash;
                }
            }

            sourceBytesRead += size;
            auto relative = entry.path().lexically_relative(root).generic_string();
            identityEntries.insert(relative + ":" + std::to_string(size) + ":" +
                                   std::to_string(modified.time_since_epoch().count()) + ":" +
                                   std::to_string(contentHash));
        }
    };
    if (options.recursive)
    {
        std::filesystem::recursive_directory_iterator current(root, directoryOptions, error);
        const std::filesystem::recursive_directory_iterator end;
        if (error)
            result.error = error.message();
        while (!error && current != end)
        {
            if (ticket->cancelled.load(std::memory_order_acquire))
            {
                result.cancelled = true;
                break;
            }
            if (++entriesVisited > options.maxEntries)
            {
                result.truncated = true;
                break;
            }
            const auto before = result.files.size();
            add_entry(*current, true);
            if (result.truncated && result.files.size() == before &&
                result.files.size() >= options.maxFiles)
                break;
            if (options.sourceInputsOnly && current->is_directory(error) && !error)
            {
                const auto name = current->path().filename().string();
                if (should_skip_directory(name))
                    current.disable_recursion_pending();
            }
            error.clear();
            current.increment(error);
            if (error)
            {
                result.error = error.message();
                error.clear();
            }
        }
    }
    else
    {
        std::filesystem::directory_iterator current(root, directoryOptions, error);
        const std::filesystem::directory_iterator end;
        if (error)
            result.error = error.message();
        while (!error && current != end)
        {
            if (ticket->cancelled.load(std::memory_order_acquire))
            {
                result.cancelled = true;
                break;
            }
            if (++entriesVisited > options.maxEntries)
            {
                result.truncated = true;
                break;
            }
            const auto before = result.files.size();
            add_entry(*current, false);
            if (result.truncated && result.files.size() == before &&
                result.files.size() >= options.maxFiles)
                break;
            error.clear();
            current.increment(error);
            if (error)
            {
                result.error = error.message();
                error.clear();
            }
        }
    }

    if (ticket->cancelled.load(std::memory_order_acquire))
        result.cancelled = true;
    std::sort(result.files.begin(), result.files.end());
    std::sort(result.directories.begin(), result.directories.end());
    std::sort(result.inventoryEntries.begin(), result.inventoryEntries.end(), [](const auto &left, const auto &right) {
        return left.first.generic_string() < right.first.generic_string();
    });
    const auto make_identity = [](const std::set<std::string> &entries) {
        std::uint64_t hash = 14695981039346656037ull;
        for (const auto &entry : entries)
        {
            for (const unsigned char byte : entry)
            {
                hash ^= byte;
                hash *= 1099511628211ull;
            }
            hash ^= static_cast<unsigned char>('\n');
            hash *= 1099511628211ull;
        }
        std::ostringstream identity;
        identity << std::hex << std::setfill('0') << std::setw(16) << hash;
        return identity.str();
    };
    if (options.sourceInputsOnly)
    {
        result.sourceInputIdentity = make_identity(identityEntries);
    }
    if (options.trackFileMetadata)
        result.fileIdentity = make_identity(filesystemIdentityEntries);
    return result;
}
} // namespace

ProjectRootCandidate validate_project_folder(const std::filesystem::path &projectRoot)
{
    auto canonicalProject = canonical_directory(projectRoot, "project root");
    std::error_code error;
    if (!std::filesystem::is_regular_file(canonicalProject / "CMakeLists.txt", error) || error)
        throw std::invalid_argument("project root must contain CMakeLists.txt: " + canonicalProject.string());
    const auto canonicalAssets = canonical_directory(canonicalProject / "assets", "asset root");
    return {std::move(canonicalProject), canonicalAssets};
}

ProjectRunDiscovery discover_project_run_options(const std::filesystem::path &projectRoot,
                                                 bool requestFileApiQuery)
{
    constexpr std::uintmax_t maximumPresetBytes = 1024U * 1024U;
    constexpr std::uintmax_t maximumFileApiBytes = 4U * 1024U * 1024U;
    constexpr std::size_t maximumPresets = 64;
    constexpr std::size_t maximumBinaryDirectories = 16;
    constexpr std::size_t maximumIndexFiles = 64;
    constexpr std::size_t maximumTargets = 256;
    ProjectRunDiscovery result;
    std::error_code error;
    const auto canonicalProject = std::filesystem::canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(canonicalProject, error) || error)
    {
        result.status = "Project folder is unavailable for preset and target discovery.";
        return result;
    }

    std::vector<std::filesystem::path> binaryDirectories;
    bool fileApiQueryRegistered = false;
    std::string fileApiQueryFailure;
    const auto add_binary_directory = [&](std::filesystem::path path) {
        if (path.is_relative())
            path = canonicalProject / path;
        path = path.lexically_normal();
        if (!path_is_within(canonicalProject, path))
        {
            result.truncated = true;
            return;
        }
        if (requestFileApiQuery)
        {
            std::error_code directoryError;
            if (std::filesystem::is_directory(path, directoryError) && !directoryError)
            {
                std::string queryError;
                if (register_file_api_query(path, canonicalProject, queryError))
                    fileApiQueryRegistered = true;
                else if (fileApiQueryFailure.empty())
                    fileApiQueryFailure = std::move(queryError);
            }
        }
        if (std::find(binaryDirectories.begin(), binaryDirectories.end(), path) == binaryDirectories.end() &&
            binaryDirectories.size() < maximumBinaryDirectories)
            binaryDirectories.push_back(std::move(path));
        else if (binaryDirectories.size() >= maximumBinaryDirectories)
            result.truncated = true;
    };

    const auto presetsPath = canonicalProject / "CMakePresets.json";
    if (std::filesystem::is_regular_file(presetsPath, error) && !error)
    {
        const auto canonicalPresets = std::filesystem::canonical(presetsPath, error);
        if (error || !path_is_within(canonicalProject, canonicalPresets))
        {
            result.status = "CMakePresets.json resolves outside the project folder.";
            result.truncated = true;
        }
        else
        {
            const auto size = std::filesystem::file_size(canonicalPresets, error);
            if (error || size > maximumPresetBytes)
            {
                result.status = "CMakePresets.json exceeds the 1 MiB discovery limit.";
                result.truncated = true;
            }
            else
            {
                std::ifstream input(canonicalPresets, std::ios::binary);
                nlohmann::json presets;
                try
                {
                    input >> presets;
                }
                catch (const std::exception &exception)
                {
                    result.status = std::string("CMakePresets.json could not be parsed: ") + exception.what();
                }
                if (input.bad())
                    result.status = "CMakePresets.json could not be read completely.";
                if (presets.is_object())
                {
                const auto parse_preset_names = [&](std::string_view key,
                                                    std::vector<std::string> &destination,
                                                    bool configure) {
                    const auto entries = presets.find(std::string(key));
                    if (entries == presets.end() || !entries->is_array())
                        return;
                    for (const auto &entry : *entries)
                    {
                        if (destination.size() >= maximumPresets)
                        {
                            result.truncated = true;
                            break;
                        }
                        if (!entry.is_object() ||
                            (entry.contains("hidden") &&
                             (!entry["hidden"].is_boolean() || entry["hidden"].get<bool>())))
                            continue;
                        const auto name = entry.find("name");
                        if (name == entry.end() || !name->is_string())
                            continue;
                        const auto value = name->get<std::string>();
                        if (value.empty() || value.size() > 256 ||
                            std::find(destination.begin(), destination.end(), value) != destination.end())
                            continue;
                        destination.push_back(value);
                        if (!configure)
                            continue;

                        std::filesystem::path binaryDirectory = canonicalProject / "build";
                        const auto configuredDirectory = entry.find("binaryDir");
                        if (configuredDirectory != entry.end() && configuredDirectory->is_string())
                        {
                            std::string expanded = configuredDirectory->get<std::string>();
                            const auto replace_all = [&](std::string_view token, std::string_view replacement) {
                                std::size_t offset = 0;
                                while ((offset = expanded.find(token, offset)) != std::string::npos)
                                {
                                    expanded.replace(offset, token.size(), replacement);
                                    offset += replacement.size();
                                }
                            };
                            replace_all("${sourceDir}", path_to_generic_utf8(canonicalProject));
                            replace_all("${sourceDirName}", path_to_generic_utf8(canonicalProject.filename()));
                            replace_all("${sourceParentDir}", path_to_generic_utf8(canonicalProject.parent_path()));
                            replace_all("${presetName}", value);
                            if (expanded.find("${") == std::string::npos && expanded.find("$env{") == std::string::npos)
                                binaryDirectory = path_from_utf8(expanded);
                            else
                                result.truncated = true;
                        }
                        add_binary_directory(std::move(binaryDirectory));
                    }
                };
                parse_preset_names("configurePresets", result.configurePresets, true);
                parse_preset_names("buildPresets", result.buildPresets, false);
                }
                else if (result.status.empty())
                    result.status = "CMakePresets.json must contain an object.";
            }
        }
    }
    error.clear();
    if (binaryDirectories.empty())
        add_binary_directory(canonicalProject / "build");

    std::uintmax_t bytesRead = 0;
    const auto read_json = [&](const std::filesystem::path &path, nlohmann::json &document,
                               std::uintmax_t perFileLimit) {
        std::error_code sizeError;
        const auto canonicalPath = std::filesystem::canonical(path, sizeError);
        if (sizeError || !path_is_within(canonicalProject, canonicalPath))
        {
            result.truncated = true;
            return false;
        }
        const auto size = std::filesystem::file_size(canonicalPath, sizeError);
        if (sizeError || size > perFileLimit || size > maximumFileApiBytes - bytesRead)
        {
            result.truncated = true;
            return false;
        }
        bytesRead += size;
        std::ifstream input(canonicalPath, std::ios::binary);
        try
        {
            input >> document;
        }
        catch (...)
        {
            return false;
        }
        return !input.bad();
    };
    for (const auto &binaryDirectory : binaryDirectories)
    {
        const auto replyDirectory = binaryDirectory / ".cmake" / "api" / "v1" / "reply";
        error.clear();
        const auto canonicalReply = std::filesystem::canonical(replyDirectory, error);
        if (error || !path_is_within(binaryDirectory, canonicalReply))
            continue;
        std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> indexes;
        std::size_t visitedReplyEntries = 0;
        for (std::filesystem::directory_iterator entry(canonicalReply, error), end;
             !error && entry != end; entry.increment(error))
        {
            if (++visitedReplyEntries > 1024)
            {
                result.truncated = true;
                break;
            }
            if (indexes.size() >= maximumIndexFiles)
            {
                result.truncated = true;
                break;
            }
            const auto filename = entry->path().filename().string();
            if (!filename.starts_with("index-") || entry->path().extension() != ".json")
                continue;
            std::error_code timeError;
            const auto modified = entry->last_write_time(timeError);
            if (!timeError)
                indexes.emplace_back(modified, entry->path());
        }
        std::sort(indexes.begin(), indexes.end(), [](const auto &left, const auto &right) {
            return left.first > right.first;
        });
        if (indexes.empty())
            continue;

        nlohmann::json index;
        if (!read_json(indexes.front().second, index, 1024U * 1024U))
            continue;
        const auto reply = index.find("reply");
        if (reply == index.end() || !reply->is_object())
            continue;
        const auto codemodelRef = reply->find("codemodel-v2");
        if (codemodelRef == reply->end() || !codemodelRef->is_object() ||
            !codemodelRef->contains("jsonFile") || !(*codemodelRef)["jsonFile"].is_string())
            continue;
        const auto codemodelPath = (canonicalReply /
            path_from_utf8((*codemodelRef)["jsonFile"].get<std::string>())).lexically_normal();
        if (!path_is_within(canonicalReply, codemodelPath))
            continue;
        nlohmann::json codemodel;
        if (!read_json(codemodelPath, codemodel, 2U * 1024U * 1024U))
            continue;
        const auto configurations = codemodel.find("configurations");
        if (configurations == codemodel.end() || !configurations->is_array())
            continue;
        for (const auto &configuration : *configurations)
        {
            const auto targets = configuration.find("targets");
            if (targets == configuration.end() || !targets->is_array())
                continue;
            for (const auto &targetRef : *targets)
            {
                if (result.executableTargets.size() >= maximumTargets)
                {
                    result.truncated = true;
                    break;
                }
                if (!targetRef.is_object() || !targetRef.contains("name") ||
                    !targetRef["name"].is_string() || !targetRef.contains("jsonFile") ||
                    !targetRef["jsonFile"].is_string())
                    continue;
                const auto targetPath = (canonicalReply /
                    path_from_utf8(targetRef["jsonFile"].get<std::string>())).lexically_normal();
                if (!path_is_within(canonicalReply, targetPath))
                    continue;
                nlohmann::json target;
                if (!read_json(targetPath, target, 1024U * 1024U) ||
                    target.value("type", std::string{}) != "EXECUTABLE")
                    continue;
                const auto artifacts = target.find("artifacts");
                if (artifacts == target.end() || !artifacts->is_array())
                    continue;
                for (const auto &artifact : *artifacts)
                {
                    if (!artifact.is_object() || !artifact.contains("path") || !artifact["path"].is_string())
                        continue;
                    auto executable = binaryDirectory / path_from_utf8(artifact["path"].get<std::string>());
                    executable = executable.lexically_normal();
                    std::error_code executableError;
                    if (!path_is_within(binaryDirectory, executable) ||
                        !std::filesystem::is_regular_file(executable, executableError) || executableError)
                        continue;
                    const std::string targetName = targetRef["name"].get<std::string>();
                    const bool duplicate = std::any_of(result.executableTargets.begin(),
                        result.executableTargets.end(), [&](const ProjectExecutableTarget &candidate) {
                            return candidate.targetName == targetName && candidate.executablePath == executable;
                        });
                    if (!duplicate)
                        result.executableTargets.push_back({targetName, std::move(executable)});
                    break;
                }
            }
        }
    }
    if (result.status.empty())
    {
        if (result.executableTargets.empty())
            result.status = "No build targets discovered. Run Configure to publish target choices.";
        else
            result.status = std::to_string(result.configurePresets.size()) + " configure preset(s), " +
                std::to_string(result.buildPresets.size()) + " build preset(s), " +
                std::to_string(result.executableTargets.size()) + " executable target(s).";
    }
    if (result.truncated)
        result.status += " Discovery stopped at a documented size or entry limit; use advanced overrides for missing choices.";
    if (requestFileApiQuery)
    {
        if (fileApiQueryRegistered)
            result.status += " CMake File API target discovery is queued for the next configure.";
        else if (!fileApiQueryFailure.empty())
            result.status += " CMake File API target discovery could not be queued: " + fileApiQueryFailure;
        else
            result.status += " No existing in-project CMake build directory is available for target discovery; configure the project first.";
    }
    return result;
}

void ProjectContext::set_roots(const std::filesystem::path &projectRoot,
                               const std::filesystem::path &assetRoot)
{
    commit_roots(prepare_roots(projectRoot, assetRoot));
}

ProjectSnapshot ProjectContext::prepare_roots(const std::filesystem::path &projectRoot,
                                              const std::filesystem::path &assetRoot) const
{
    auto canonicalProject = canonical_directory(projectRoot, "project root");
    auto canonicalAssets = canonical_directory(assetRoot, "asset root");
    if (canonicalProject == snapshot_.projectRoot && canonicalAssets == snapshot_.assetRoot)
        return snapshot_;
    if (snapshot_.generation == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("project generation space exhausted");
    return ProjectSnapshot{std::move(canonicalProject), std::move(canonicalAssets), snapshot_.generation + 1};
}

void ProjectContext::commit_roots(ProjectSnapshot snapshot) noexcept
{
    snapshot_ = std::move(snapshot);
}

const ProjectSnapshot &ProjectContext::snapshot() const noexcept
{
    return snapshot_;
}

ProjectScanToken::ProjectScanToken(std::shared_ptr<ProjectScanTicketState> state)
    : state_(std::move(state))
{
}

ProjectScanToken::operator bool() const noexcept
{
    return state_ != nullptr;
}

std::uint64_t ProjectScanToken::request_id() const noexcept
{
    return state_ ? state_->requestId : 0;
}

void ProjectScanToken::cancel() const noexcept
{
    if (state_)
        state_->cancelled.store(true, std::memory_order_release);
}

ProjectScanService::ProjectScanService(std::size_t maxPendingPublications, std::size_t maxOutstandingScans)
    : state_(std::make_shared<ProjectScanSharedState>(maxPendingPublications, maxOutstandingScans))
{
}

ProjectScanService::~ProjectScanService()
{
    cancel_all();
    wait_for_idle();
    std::lock_guard lock(state_->mutex);
    state_->alive = false;
    state_->ready.clear();
}

void ProjectScanService::set_wake_callback(std::function<void()> callback)
{
    wakeCallback_ = std::move(callback);
}

ProjectScanToken ProjectScanService::request_scan(const ProjectSnapshot &project,
                                                  const std::filesystem::path &scanRoot,
                                                  ProjectScanOptions options)
{
    if (shuttingDown_)
        return {};
    if (nextRequestId_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("project scan request ID space exhausted");
    std::error_code absoluteError;
    auto logicalRoot = std::filesystem::absolute(scanRoot, absoluteError).lexically_normal();
    if (absoluteError)
        throw std::invalid_argument("scan root could not be made absolute: " + scanRoot.string());
    const auto canonicalRoot = canonical_directory(logicalRoot, "scan root");
    const bool assetLogicalPath = path_is_within(project.assetRoot, logicalRoot);
    if (!path_is_within(project.projectRoot, canonicalRoot) ||
        (assetLogicalPath && !path_is_within(project.assetRoot, canonicalRoot)))
        throw std::invalid_argument("scan root resolves outside the project boundary: " + scanRoot.string());
    auto ticket = std::make_shared<ProjectScanTicketState>();
    ticket->requestId = nextRequestId_++;
    ticket->projectGeneration = project.generation;
    tickets_.erase(std::remove_if(tickets_.begin(), tickets_.end(), [](const auto &candidate) {
                       return candidate.expired();
                   }),
                   tickets_.end());
    tickets_.push_back(ticket);

    const auto state = state_;
    const auto wakeCallback = wakeCallback_;
    {
        std::lock_guard lock(state->mutex);
        if (!state->alive || state->outstandingScans >= state->maxOutstandingScans ||
            state->outstandingScans + state->ready.size() >= state->maxPendingPublications)
            return {};
        ++state->outstandingScans;
    }
    try
    {
        tasks_.run([state, ticket, canonicalRoot, logicalRoot, assetRoot = project.assetRoot,
                    options, wakeCallback] {
            ProjectScanResult result;
            try
            {
                result = perform_scan(canonicalRoot, logicalRoot, assetRoot, options, ticket);
            }
            catch (const std::exception &error)
            {
                result.requestId = ticket->requestId;
                result.projectGeneration = ticket->projectGeneration;
                result.root = logicalRoot;
                result.canonicalRoot = canonicalRoot;
                result.error = error.what();
            }
            catch (...)
            {
                result.requestId = ticket->requestId;
                result.projectGeneration = ticket->projectGeneration;
                result.root = logicalRoot;
                result.canonicalRoot = canonicalRoot;
                result.error = "unknown filesystem scan failure";
            }
            publish(state, std::move(result), ticket);
            if (wakeCallback)
            {
                try { wakeCallback(); }
                catch (...) {}
            }
        });
    }
    catch (...)
    {
        std::lock_guard lock(state->mutex);
        --state->outstandingScans;
        throw;
    }
    return ProjectScanToken(std::move(ticket));
}

std::vector<ProjectScanResult> ProjectScanService::take_ready(std::uint64_t currentProjectGeneration)
{
    std::vector<ProjectScanResult> results;
    std::lock_guard lock(state_->mutex);
    if (!state_->alive)
        return results;
    results.reserve(state_->ready.size());
    for (auto &queued : state_->ready)
    {
        if (queued.result.projectGeneration == currentProjectGeneration)
        {
            if (const auto ticket = queued.ticket.lock(); ticket &&
                ticket->cancelled.load(std::memory_order_acquire))
                queued.result.cancelled = true;
            results.push_back(std::move(queued.result));
        }
    }
    state_->ready.clear();
    return results;
}

void ProjectScanService::cancel_all() noexcept
{
    shuttingDown_ = true;
    for (const auto &weakTicket : tickets_)
    {
        if (const auto ticket = weakTicket.lock())
            ticket->cancelled.store(true, std::memory_order_release);
    }
}

void ProjectScanService::cancel_pending_and_wait()
{
    for (const auto &weakTicket : tickets_)
    {
        if (const auto ticket = weakTicket.lock())
            ticket->cancelled.store(true, std::memory_order_release);
    }
    tasks_.wait();
    {
        std::lock_guard lock(state_->mutex);
        state_->ready.clear();
    }
    tickets_.clear();
}

void ProjectScanService::wait_for_idle()
{
    tasks_.wait();
}
} // namespace platformator_editor
