#pragma once

#include "documentio.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace platformator_editor
{
// These operations return portable file identity and commit outcomes. Native
// handles, timestamps, and replacement APIs stay inside the selected adapter.
// Identity verification is optimistic: the final recheck narrows but cannot
// eliminate the check-to-commit race against an uncooperative external writer.
std::optional<FileIdentity> native_file_identity(const std::filesystem::path &path,
                                                  std::string &error);
AtomicWriteResult native_write_file_atomically(const std::filesystem::path &path,
                                                std::string_view contents,
                                                bool allowReplaceExisting,
                                                std::optional<FileIdentity> expectedDestination,
                                                bool verifyDestinationIdentity);
} // namespace platformator_editor
