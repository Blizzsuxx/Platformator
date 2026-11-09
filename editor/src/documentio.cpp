#include "documentio.h"

#include "editorwork.h"

#include "platform/nativefile.h"

#include <fstream>
#include <system_error>
#include <utility>

namespace platformator_editor
{
namespace
{
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
        auto identity = native_file_identity(absolute, identityError);
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
    return native_write_file_atomically(path, contents, allowReplaceExisting,
                                        std::move(expectedDestination), verifyDestinationIdentity);
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
    const auto initialIdentity = native_file_identity(target, identityError);
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
    work::record(work::Counter::DocumentWrites);
    auto atomic = write_file_atomically(target, contents, mayReplace, initialIdentity, !explicitOverwrite);
    if (!atomic.succeeded())
    {
        if (atomic.status == AtomicWriteStatus::Conflict)
            return save_conflict(std::move(atomic.message));
        return save_error(std::move(atomic.message));
    }

    document.sourcePath = target;
    std::string savedIdentityError;
    document.sourceIdentity = native_file_identity(target, savedIdentityError);
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
