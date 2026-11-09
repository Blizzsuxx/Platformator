#include "documentio.h"
#include "platform/nativefile.h"
#include "platform/nativefile_testhooks.h"
#include "test_support.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#include <thread>
#include <utility>
#include <vector>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace
{
void require_file(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    input.exceptions(std::ios::badbit | std::ios::failbit);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output << text;
}

class ScopedHooks
{
  public:
    explicit ScopedHooks(NativeFileTestHooks hooks)
    {
        set_native_file_test_hooks(std::move(hooks));
    }
    ~ScopedHooks() { clear_native_file_test_hooks(); }
    ScopedHooks(const ScopedHooks &) = delete;
    ScopedHooks &operator=(const ScopedHooks &) = delete;
};

bool has_staging_file(const std::filesystem::path &target)
{
    const auto prefix = "." + target.filename().string() + ".tmp.";
    for (const auto &entry : std::filesystem::directory_iterator(target.parent_path()))
        if (entry.path().filename().string().starts_with(prefix))
            return true;
    return false;
}

std::optional<std::filesystem::path> find_recovery_file(const std::filesystem::path &target)
{
    const auto prefix = std::filesystem::path(".").native() + target.filename().native();
    const auto posixSuffix = std::filesystem::path(".recovery.").native();
    const auto windowsSuffix = std::filesystem::path(".bak.").native();
    for (const auto &entry : std::filesystem::directory_iterator(target.parent_path()))
    {
        const auto name = entry.path().filename().native();
        if (name.starts_with(prefix + posixSuffix) || name.starts_with(prefix + windowsSuffix))
            return entry.path();
    }
    return std::nullopt;
}

void test_unicode_identity_and_mode_preservation(const std::filesystem::path &root)
{
    const auto directory = root / std::filesystem::path(u8"folder with spaces café");
    std::filesystem::create_directories(directory);
    const auto target = directory / std::filesystem::path(u8"scene 名.scene");
    std::string error;
    require_file(!native_file_identity(target, error) && error.empty(),
                 "missing destinations have an empty identity without an inspection error");

    auto created = write_file_atomically(target, "first", false, std::nullopt, true);
    require_file(created.status == AtomicWriteStatus::Written && created.destinationReplaced,
                 "create-new writes commit a Unicode path with spaces");
    require_file(read_text(target) == "first", "Unicode path contains committed bytes");

    const auto originalIdentity = native_file_identity(target, error);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    write_text(target, "other");
    const auto changedIdentity = native_file_identity(target, error);
    require_file(originalIdentity.has_value() && changedIdentity.has_value() && error.empty() &&
                     originalIdentity->size == changedIdentity->size &&
                     *originalIdentity != *changedIdentity,
                 "native identity detects a same-size rewrite through its timestamp metadata");

#if defined(_WIN32)
    require_file(::SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE),
                 "set source attributes for preservation case");
#else
    require_file(::chmod(target.c_str(), 0640) == 0, "set source mode for preservation case");
#endif
    const auto before = native_file_identity(target, error);
    require_file(before.has_value() && error.empty() && before->size == 5,
                 "native identity reports destination size and metadata");
    auto replaced = write_file_atomically(target, "replacement", true, before, true);
    require_file(replaced.status == AtomicWriteStatus::Written && replaced.destinationReplaced,
                 "identity-matched replacement commits");
#if defined(_WIN32)
    const DWORD attributes = ::GetFileAttributesW(target.c_str());
    require_file(attributes != INVALID_FILE_ATTRIBUTES &&
                     (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE)) ==
                         (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE),
                 "replacement preserves the destination file attributes");
#else
    struct stat info
    {
    };
    require_file(::stat(target.c_str(), &info) == 0 && (info.st_mode & 07777) == 0640,
                 "replacement preserves the destination permission bits");
#endif
    require_file(read_text(target) == "replacement", "replacement content is complete");
    require_file(!find_recovery_file(target), "durable replacement removes its temporary recovery copy");
}

void test_precommit_failures_preserve_destination(const std::filesystem::path &root)
{
    for (const auto &[name, hook] : std::vector<std::pair<std::string, NativeFileTestHooks>>{
             {"staging", NativeFileTestHooks{.failTemporaryCreate = true}},
             {"flush", NativeFileTestHooks{.failFileFlush = true}},
             {"replace", NativeFileTestHooks{.failReplace = true}}})
    {
        const auto target = root / (name + "-failure.scene");
        write_text(target, "original bytes");
        std::string error;
        const auto identity = native_file_identity(target, error);
        require_file(identity.has_value() && error.empty(), name + " fixture identity");
        AtomicWriteResult result;
        {
            ScopedHooks hooks(hook);
            result = write_file_atomically(target, "new bytes", true, identity, true);
        }
        require_file(result.status == AtomicWriteStatus::Failed && !result.destinationReplaced,
                     name + " failure is reported before commit");
        require_file(read_text(target) == "original bytes",
                     name + " failure preserves original destination bytes");
        require_file(!has_staging_file(target), name + " failure removes its staging file");
        require_file(!find_recovery_file(target), name + " failure removes its temporary recovery copy");
    }
}

void test_late_observed_identity_conflict(const std::filesystem::path &root)
{
    const auto target = root / "late-conflict.scene";
    write_text(target, "original");
    std::string error;
    const auto identity = native_file_identity(target, error);
    require_file(identity.has_value() && error.empty(), "late-conflict fixture identity");
    NativeFileTestHooks hooks;
    hooks.afterStagingAndFlush = [&](const auto &, const auto &) { write_text(target, "external edit"); };
    AtomicWriteResult result;
    {
        ScopedHooks scoped(std::move(hooks));
        result = write_file_atomically(target, "editor edit", true, identity, true);
    }
    require_file(result.status == AtomicWriteStatus::Conflict && !result.destinationReplaced,
                 "external edit observed after staging is reported as a conflict");
    require_file(read_text(target) == "external edit", "late conflict preserves competing bytes");
    require_file(!has_staging_file(target), "late conflict removes the staged editor write");
}

#if !defined(_WIN32)
void test_symlink_save_updates_target_and_preserves_link(const std::filesystem::path &root)
{
    const auto target = root / "symlink-target.scene";
    const auto link = root / "symlink-save.scene";
    write_text(target, "original target");
    std::filesystem::create_symlink(target.filename(), link);

    std::string error;
    const auto identity = native_file_identity(link, error);
    require_file(identity.has_value() && error.empty(), "symlink destination resolves to a file identity");
    const auto result = write_file_atomically(link, "updated through link", true, identity, true);
    require_file(result.status == AtomicWriteStatus::Written && result.destinationReplaced,
                 "atomic save through a symlink commits to its resolved target");
    require_file(std::filesystem::is_symlink(std::filesystem::symlink_status(link)) &&
                     read_text(target) == "updated through link" && read_text(link) == "updated through link",
                 "saving through a symlink preserves the link and updates its target");
}

void test_symlink_retarget_during_staging_is_conflict(const std::filesystem::path &root)
{
    const auto original = root / "symlink-original.scene";
    const auto alternate = root / "symlink-alternate.scene";
    const auto link = root / "symlink-race.scene";
    write_text(original, "original");
    write_text(alternate, "alternate");
    std::filesystem::create_symlink(original.filename(), link);

    std::string error;
    const auto identity = native_file_identity(link, error);
    require_file(identity.has_value() && error.empty(), "symlink race fixture identity");
    NativeFileTestHooks hooks;
    hooks.afterStagingAndFlush = [&](const auto &, const auto &) {
        std::filesystem::remove(link);
        std::filesystem::create_symlink(alternate.filename(), link);
    };
    AtomicWriteResult result;
    {
        ScopedHooks scoped(std::move(hooks));
        result = write_file_atomically(link, "editor write", true, identity, true);
    }
    require_file(result.status == AtomicWriteStatus::Conflict && !result.destinationReplaced,
                 "retargeting a destination symlink while staging is reported as a conflict");
    require_file(read_text(original) == "original" && read_text(alternate) == "alternate" &&
                     !has_staging_file(original),
                 "a retarget conflict preserves both targets and removes the staged editor write");
}
#endif

void test_create_if_absent_race(const std::filesystem::path &root)
{
    const auto target = root / "create-race.scene";
    NativeFileTestHooks hooks;
    hooks.beforeCommit = [&](const auto &path) { write_text(path, "racing creator"); };
    AtomicWriteResult result;
    {
        ScopedHooks scoped(std::move(hooks));
        result = write_file_atomically(target, "editor create", false, std::nullopt, true);
    }
    require_file(result.status == AtomicWriteStatus::Conflict && !result.destinationReplaced,
                 "create-if-absent race is reported as a conflict");
    require_file(read_text(target) == "racing creator", "create race preserves the newly created file");
    require_file(!has_staging_file(target), "create race removes the staged file");
}

void test_committed_directory_durability_warning(const std::filesystem::path &root)
{
    const auto target = root / "durability-warning.scene";
    NativeFileTestHooks hooks;
    hooks.failDirectoryFlush = true;
    AtomicWriteResult result;
    {
        ScopedHooks scoped(std::move(hooks));
        result = write_file_atomically(target, "committed", false, std::nullopt, true);
    }
    require_file(result.status == AtomicWriteStatus::DurabilityWarning && result.destinationReplaced,
                 "directory flush failure reports a committed durability warning");
    require_file(read_text(target) == "committed", "durability warning retains committed content");
    require_file(!has_staging_file(target), "committed warning leaves no staging file");
}

void test_replace_durability_warning_retains_prior_content(const std::filesystem::path &root)
{
    const auto target = root / "replace-durability-warning.scene";
    write_text(target, "prior valid content");
    std::string error;
    const auto identity = native_file_identity(target, error);
    require_file(identity.has_value() && error.empty(), "recovery fixture identity");
    NativeFileTestHooks hooks;
    hooks.failDirectoryFlush = true;
    AtomicWriteResult result;
    {
        ScopedHooks scoped(std::move(hooks));
        result = write_file_atomically(target, "new committed content", true, identity, true);
    }
    require_file(result.status == AtomicWriteStatus::DurabilityWarning && result.destinationReplaced,
                 "replacement directory-flush failure reports that new content committed");
    require_file(read_text(target) == "new committed content",
                 "directory-flush warning retains the committed replacement");
    const auto recovery = find_recovery_file(target);
    require_file(recovery && read_text(*recovery) == "prior valid content" &&
                     result.message.find(recovery->string()) != std::string::npos,
                 "directory-flush warning retains and identifies the recoverable prior file");
}
} // namespace

int main()
{
    try
    {
        TemporaryDirectory temporary;
        test_unicode_identity_and_mode_preservation(temporary.path());
        test_precommit_failures_preserve_destination(temporary.path());
        test_late_observed_identity_conflict(temporary.path());
#if !defined(_WIN32)
        test_symlink_save_updates_target_and_preserves_link(temporary.path());
        test_symlink_retarget_during_staging_is_conflict(temporary.path());
#endif
        test_create_if_absent_race(temporary.path());
        test_committed_directory_durability_warning(temporary.path());
        test_replace_durability_warning_retains_prior_content(temporary.path());
        std::cout << "Native POSIX file contract cases passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        clear_native_file_test_hooks();
        std::cerr << "platformator_editor_nativefile_test: " << error.what() << '\n';
        return 1;
    }
}
