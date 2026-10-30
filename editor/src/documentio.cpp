#include "documentio.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace platformator_editor
{
namespace
{
std::atomic<std::uint64_t> temporarySequence{0};

std::string system_error(const char *operation, int error)
{
    return std::string(operation) + ": " + std::strerror(error);
}

std::optional<FileIdentity> file_identity(const std::filesystem::path &path, std::string &error)
{
    struct stat info
    {
    };
    if (::stat(path.c_str(), &info) != 0)
    {
        if (errno == ENOENT || errno == ENOTDIR)
            return std::nullopt;
        error = system_error("cannot inspect destination", errno);
        return std::nullopt;
    }

    const auto seconds = static_cast<std::int64_t>(info.st_mtim.tv_sec);
    const auto nanoseconds = static_cast<std::int64_t>(info.st_mtim.tv_nsec);
    const auto changeSeconds = static_cast<std::int64_t>(info.st_ctim.tv_sec);
    const auto changeNanoseconds = static_cast<std::int64_t>(info.st_ctim.tv_nsec);
    const auto maxSeconds = std::numeric_limits<std::int64_t>::max() / 1000000000;
    const auto timestamp = seconds > maxSeconds ? std::numeric_limits<std::int64_t>::max()
                                                : seconds * 1000000000 + nanoseconds;
    const auto changeTimestamp = changeSeconds > maxSeconds ? std::numeric_limits<std::int64_t>::max()
                                                            : changeSeconds * 1000000000 + changeNanoseconds;
    return FileIdentity{static_cast<std::uint64_t>(info.st_dev), static_cast<std::uint64_t>(info.st_ino),
                        static_cast<std::uint64_t>(info.st_size), static_cast<std::uint32_t>(info.st_mode),
                        timestamp, changeTimestamp};
}

std::filesystem::path normalized_save_path(const std::filesystem::path &path, std::string &error)
{
    if (path.empty() || path.filename().empty())
    {
        error = "save destination must name a file";
        return {};
    }
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    if (ec)
    {
        error = "cannot resolve save destination: " + ec.message();
        return {};
    }
    const auto parent = absolute.parent_path();
    const auto canonicalParent = std::filesystem::canonical(parent, ec);
    if (ec || !std::filesystem::is_directory(canonicalParent))
    {
        error = "save destination directory does not exist: " + parent.string();
        return {};
    }
    return canonicalParent / absolute.filename();
}

bool write_all(int fd, std::string_view contents, std::string &error)
{
    std::size_t written = 0;
    while (written < contents.size())
    {
        const ssize_t result = ::write(fd, contents.data() + written, contents.size() - written);
        if (result < 0)
        {
            if (errno == EINTR)
                continue;
            error = system_error("cannot write temporary file", errno);
            return false;
        }
        if (result == 0)
        {
            error = "temporary file write made no progress";
            return false;
        }
        written += static_cast<std::size_t>(result);
    }
    return true;
}

SaveResult save_error(std::string message)
{
    return SaveResult{SaveStatus::Failed, std::move(message), false};
}

SaveResult save_conflict(std::string message)
{
    return SaveResult{SaveStatus::Conflict, std::move(message), false};
}
} // namespace

DocumentParseResult load_document_file(const std::filesystem::path &path,
                                       std::uint64_t documentGeneration,
                                       std::filesystem::path projectRoot,
                                       std::filesystem::path assetRoot)
{
    std::error_code ec;
    const auto requested = std::filesystem::absolute(path, ec).lexically_normal();
    if (ec)
        return DocumentParseResult{std::nullopt, "cannot resolve scene path: " + ec.message()};
    const auto absolute = std::filesystem::canonical(requested, ec);
    if (ec)
        return DocumentParseResult{std::nullopt, "cannot resolve scene file: " + requested.string()};
    if (!std::filesystem::is_regular_file(absolute, ec) || ec)
        return DocumentParseResult{std::nullopt, "scene is not a readable regular file: " + absolute.string()};

    try
    {
        std::ifstream stream(absolute, std::ios::binary);
        if (!stream)
            return DocumentParseResult{std::nullopt, "cannot open scene file: " + absolute.string()};
        Json root = Json::parse(stream);
        auto parsed = parse_document(std::move(root), documentGeneration, absolute,
                                     std::move(projectRoot), std::move(assetRoot));
        if (!parsed)
        {
            parsed.error = "invalid scene '" + absolute.string() + "': " + parsed.error;
            return parsed;
        }
        std::string identityError;
        auto identity = file_identity(absolute, identityError);
        if (!identity)
            return DocumentParseResult{std::nullopt,
                                       identityError.empty() ? "scene file disappeared while loading"
                                                             : std::move(identityError)};
        parsed.document->sourceIdentity = *identity;
        return parsed;
    }
    catch (const std::exception &error)
    {
        return DocumentParseResult{std::nullopt, "cannot parse scene '" + absolute.string() + "': " + error.what()};
    }
}

AtomicWriteResult write_file_atomically(const std::filesystem::path &path, std::string_view contents,
                                        bool allowReplaceExisting,
                                        std::optional<FileIdentity> expectedDestination,
                                        bool verifyDestinationIdentity)
{
    std::string error;
    const auto target = normalized_save_path(path, error);
    if (target.empty())
        return {AtomicWriteStatus::Failed, std::move(error), false};
    std::string identityError;
    const auto initialIdentity = file_identity(target, identityError);
    if (!identityError.empty())
        return {AtomicWriteStatus::Failed, std::move(identityError), false};
    if (verifyDestinationIdentity && initialIdentity != expectedDestination)
        return {AtomicWriteStatus::Conflict, "save destination changed before writing", false};

    const int directoryFd = ::open(target.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directoryFd < 0)
        return {AtomicWriteStatus::Failed, system_error("cannot open save directory", errno), false};

    const auto sequence = temporarySequence.fetch_add(1, std::memory_order_relaxed);
    const std::string targetName = target.filename().string();
    const std::string temporaryName = "." + targetName + ".tmp." + std::to_string(::getpid()) + "." +
                                     std::to_string(sequence);
    mode_t mode = 0666;
    if (initialIdentity)
        mode = static_cast<mode_t>(initialIdentity->mode & 0777u);
    const int fileFd = ::openat(directoryFd, temporaryName.c_str(),
                                O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, mode);
    if (fileFd < 0)
    {
        const int openError = errno;
        ::close(directoryFd);
        return {AtomicWriteStatus::Failed,
                system_error("cannot create same-directory temporary file", openError), false};
    }

    bool temporaryExists = true;
    auto cleanup_temporary = [&] {
        if (temporaryExists)
        {
            ::unlinkat(directoryFd, temporaryName.c_str(), 0);
            temporaryExists = false;
        }
    };
    if (initialIdentity && ::fchmod(fileFd, mode) != 0)
        error = system_error("cannot preserve destination permissions", errno);
    if (error.empty())
        write_all(fileFd, contents, error);
    if (error.empty() && ::fsync(fileFd) != 0)
        error = system_error("cannot flush temporary file", errno);
    const int closeResult = ::close(fileFd);
    if (error.empty() && closeResult != 0)
        error = system_error("cannot close temporary file", errno);
    if (!error.empty())
    {
        cleanup_temporary();
        ::close(directoryFd);
        return {AtomicWriteStatus::Failed, std::move(error), false};
    }

    std::string lateIdentityError;
    const auto currentIdentity = file_identity(target, lateIdentityError);
    if (!lateIdentityError.empty())
    {
        cleanup_temporary();
        ::close(directoryFd);
        return {AtomicWriteStatus::Failed, std::move(lateIdentityError), false};
    }
    if (verifyDestinationIdentity && currentIdentity != expectedDestination)
    {
        cleanup_temporary();
        ::close(directoryFd);
        return {AtomicWriteStatus::Conflict, "save destination changed while writing", false};
    }

    bool destinationReplaced = false;
    bool cleanupWarning = false;
    if (allowReplaceExisting)
    {
        if (::renameat(directoryFd, temporaryName.c_str(), directoryFd, targetName.c_str()) != 0)
            error = system_error("cannot atomically replace file", errno);
        else
        {
            temporaryExists = false;
            destinationReplaced = true;
        }
    }
    else
    {
        if (::linkat(directoryFd, temporaryName.c_str(), directoryFd, targetName.c_str(), 0) != 0)
            error = errno == EEXIST ? "save destination appeared while writing"
                                    : system_error("cannot atomically create file", errno);
        else
        {
            destinationReplaced = true;
            if (::unlinkat(directoryFd, temporaryName.c_str(), 0) == 0)
                temporaryExists = false;
            else
                cleanupWarning = true;
        }
    }
    if (!destinationReplaced)
    {
        cleanup_temporary();
        ::close(directoryFd);
        if (error == "save destination appeared while writing")
            return {AtomicWriteStatus::Conflict, std::move(error), false};
        return {AtomicWriteStatus::Failed, std::move(error), false};
    }

    bool durabilityWarning = cleanupWarning;
    if (::fsync(directoryFd) != 0)
    {
        durabilityWarning = true;
        error = system_error("file was replaced but directory durability could not be confirmed", errno);
    }
    ::close(directoryFd);
    if (temporaryExists)
    {
        // If the target was linked successfully but the temporary unlink failed,
        // retry cleanup once while the directory is still available by path.
        if (::unlink((target.parent_path() / temporaryName).c_str()) == 0)
            temporaryExists = false;
    }

    return {durabilityWarning ? AtomicWriteStatus::DurabilityWarning : AtomicWriteStatus::Written,
            durabilityWarning ? (error.empty() ? "file was replaced, but directory durability is uncertain" : error)
                              : std::string{},
            true};
}

SaveResult save_document(DocumentSession &document, const std::filesystem::path &path, SaveOptions options)
{
    std::string error;
    const auto target = normalized_save_path(path, error);
    if (target.empty())
        return save_error(std::move(error));
    std::string contents;
    try
    {
        if (!document.root.is_array())
            return save_error("document root must be a top-level array");
        contents = document.root.dump(2) + "\n";
    }
    catch (const std::exception &serializationError)
    {
        return save_error(std::string("cannot serialize scene: ") + serializationError.what());
    }

    const auto source = document.sourcePath ? document.sourcePath->lexically_normal()
                                            : std::filesystem::path{};
    const bool sameSource = document.sourcePath && source == target;
    std::string identityError;
    const auto initialIdentity = file_identity(target, identityError);
    if (!identityError.empty())
        return save_error(std::move(identityError));
    const bool hasDestination = initialIdentity.has_value();
    const bool sourceIdentityMatches = sameSource && document.sourceIdentity == initialIdentity;
    const bool explicitOverwrite = options.overwrite == OverwriteDecision::Overwrite;

    if (sameSource && !sourceIdentityMatches && !explicitOverwrite)
    {
        const auto message = options.overwrite == OverwriteDecision::SaveElsewhere
                                 ? "scene changed on disk; choose Save As to preserve the external version"
                                 : "scene changed on disk; confirm overwrite or choose Save As";
        return save_conflict(message);
    }
    if (!sameSource && hasDestination && !explicitOverwrite)
    {
        const auto message = options.overwrite == OverwriteDecision::SaveElsewhere
                                 ? "save destination already exists; choose another path"
                                 : "save destination already exists; confirm overwrite";
        return save_conflict(message);
    }

    const bool mayReplace = explicitOverwrite || (sameSource && sourceIdentityMatches);
    auto atomic = write_file_atomically(target, contents, mayReplace, initialIdentity, !explicitOverwrite);
    if (!atomic.succeeded())
    {
        if (atomic.status == AtomicWriteStatus::Conflict)
            return save_conflict(std::move(atomic.message));
        return save_error(std::move(atomic.message));
    }

    document.sourcePath = target;
    std::string savedIdentityError;
    document.sourceIdentity = file_identity(target, savedIdentityError);
    if (!savedIdentityError.empty())
    {
        if (atomic.status == AtomicWriteStatus::Written)
            atomic.status = AtomicWriteStatus::DurabilityWarning;
        if (atomic.message.empty())
            atomic.message = "scene was replaced but its new file identity could not be recorded: " + savedIdentityError;
        document.sourceIdentity.reset();
    }
    const auto status = atomic.status == AtomicWriteStatus::DurabilityWarning
                            ? SaveStatus::SavedWithDurabilityWarning
                            : SaveStatus::Saved;
    return SaveResult{status, std::move(atomic.message), atomic.destinationReplaced};
}
} // namespace platformator_editor
