#pragma once

#include <filesystem>
#include <iostream>
#include <system_error>

#if defined(_WIN32)
#include <winerror.h>
#endif

namespace test_filesystem
{
inline bool supports_symlinks([[maybe_unused]] const std::filesystem::path &directory)
{
#if defined(_WIN32)
    const auto link = directory / "platformator-symlink-probe";
    std::error_code error;
    std::filesystem::create_directory_symlink(directory, link, error);
    if (!error)
    {
        std::filesystem::remove(link);
        return true;
    }
    if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted ||
        error == std::errc::function_not_supported || error == std::errc::not_supported ||
        error == std::error_code(ERROR_PRIVILEGE_NOT_HELD, std::system_category()))
    {
        std::clog << "Skipping symlink cases: " << error.message() << '\n';
        return false;
    }
    throw std::filesystem::filesystem_error("cannot create test symlink", link, error);
#else
    return true;
#endif
}
} // namespace test_filesystem
