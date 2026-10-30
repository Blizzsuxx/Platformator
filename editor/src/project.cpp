#include "project.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace platformator_editor
{
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

void publish(const std::shared_ptr<ProjectScanSharedState> &state, ProjectScanResult result,
             const std::shared_ptr<ProjectScanTicketState> &ticket)
{
    std::lock_guard lock(state->mutex);
    if (!state->alive || state->maxPendingPublications == 0)
        return;
    if (state->ready.size() == state->maxPendingPublications)
        state->ready.erase(state->ready.begin());
    state->ready.push_back(ProjectScanSharedState::QueuedResult{std::move(result), ticket});
}

ProjectScanResult perform_scan(const std::filesystem::path &root, ProjectScanOptions options,
                               const std::shared_ptr<ProjectScanTicketState> &ticket)
{
    ProjectScanResult result;
    result.requestId = ticket->requestId;
    result.projectGeneration = ticket->projectGeneration;
    result.root = root;

    std::error_code error;
    const auto directoryOptions = std::filesystem::directory_options::skip_permission_denied;
    std::set<std::string> identityEntries;
    std::size_t entriesVisited = 0;
    const auto add_entry = [&](const std::filesystem::directory_entry &entry, bool recursive) {
        std::error_code typeError;
        if (entry.is_symlink(typeError) || typeError)
            return;
        if (entry.is_directory(typeError))
        {
            if (typeError)
                return;
            const auto name = entry.path().filename().string();
            if (options.sourceInputsOnly &&
                (name == ".git" || name == "bin" || name == "build" || name == "dist" ||
                 name == "vendored" || name == "vendor" || name == "node_modules"))
                return;
            if (options.includeDirectories)
            {
                if (result.directories.size() == options.maxDirectories)
                    result.truncated = true;
                else
                    result.directories.push_back(entry.path());
            }
            if (recursive && options.sourceInputsOnly && result.directories.size() == options.maxDirectories)
                return;
            return;
        }
        if (!entry.is_regular_file(typeError) || typeError)
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
        result.files.push_back(entry.path());
        if (options.sourceInputsOnly)
        {
            std::error_code metadataError;
            const auto size = entry.file_size(metadataError);
            if (metadataError)
                return;
            const auto modified = entry.last_write_time(metadataError);
            if (metadataError)
                return;
            auto relative = entry.path().lexically_relative(root).generic_string();
            identityEntries.insert(relative + ":" + std::to_string(size) + ":" +
                                   std::to_string(modified.time_since_epoch().count()));
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
                if (name == ".git" || name == "bin" || name == "build" || name == "dist" ||
                    name == "vendored" || name == "vendor" || name == "node_modules")
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
    if (options.sourceInputsOnly)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (const auto &entry : identityEntries)
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
        result.sourceInputIdentity = identity.str();
    }
    return result;
}
} // namespace

void ProjectContext::set_roots(const std::filesystem::path &projectRoot,
                               const std::filesystem::path &assetRoot)
{
    auto canonicalProject = canonical_directory(projectRoot, "project root");
    auto canonicalAssets = canonical_directory(assetRoot, "asset root");
    if (canonicalProject == snapshot_.projectRoot && canonicalAssets == snapshot_.assetRoot)
        return;
    if (snapshot_.generation == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("project generation space exhausted");
    snapshot_.projectRoot = std::move(canonicalProject);
    snapshot_.assetRoot = std::move(canonicalAssets);
    ++snapshot_.generation;
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

ProjectScanToken ProjectScanService::request_scan(const ProjectSnapshot &project,
                                                  const std::filesystem::path &scanRoot,
                                                  ProjectScanOptions options)
{
    if (shuttingDown_)
        return {};
    if (nextRequestId_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("project scan request ID space exhausted");
    const auto canonicalRoot = canonical_directory(scanRoot, "scan root");
    auto ticket = std::make_shared<ProjectScanTicketState>();
    ticket->requestId = nextRequestId_++;
    ticket->projectGeneration = project.generation;
    tickets_.erase(std::remove_if(tickets_.begin(), tickets_.end(), [](const auto &candidate) {
                       return candidate.expired();
                   }),
                   tickets_.end());
    tickets_.push_back(ticket);

    const auto state = state_;
    {
        std::lock_guard lock(state->mutex);
        if (!state->alive || state->outstandingScans >= state->maxOutstandingScans)
            return {};
        ++state->outstandingScans;
    }
    try
    {
        tasks_.run([state, ticket, canonicalRoot, options] {
            ProjectScanResult result;
            try
            {
                result = perform_scan(canonicalRoot, options, ticket);
            }
            catch (const std::exception &error)
            {
                result.requestId = ticket->requestId;
                result.projectGeneration = ticket->projectGeneration;
                result.root = canonicalRoot;
                result.error = error.what();
            }
            catch (...)
            {
                result.requestId = ticket->requestId;
                result.projectGeneration = ticket->projectGeneration;
                result.root = canonicalRoot;
                result.error = "unknown filesystem scan failure";
            }
            publish(state, std::move(result), ticket);
            std::lock_guard lock(state->mutex);
            --state->outstandingScans;
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

void ProjectScanService::wait_for_idle()
{
    tasks_.wait();
}
} // namespace platformator_editor
