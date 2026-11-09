#pragma once

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <map>
#include <optional>
#include <system_error>
#include <utility>

#include <json.hpp>

namespace platformator_editor_test
{
using Json = nlohmann::json;

inline void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

inline Json component(int id, Json type, Json fields = Json::object())
{
    require(fields.is_object(), "component fixture fields must be a JSON object");
    fields["id"] = id;
    fields["type"] = std::move(type);
    return fields;
}

inline Json object(int id, std::string name, Json components = Json::array(), Json children = Json::array())
{
    return Json{{"id", id},
                {"rotation", 0.0},
                {"active", true},
                {"position", {{"x", 0.0}, {"y", 0.0}}},
                {"scale", {{"x", 1.0}, {"y", 1.0}}},
                {"name", std::move(name)},
                {"tag", ""},
                {"children", std::move(children)},
                {"components", std::move(components)}};
}

inline Json scene(std::initializer_list<Json> roots)
{
    return Json(roots);
}

class TemporaryDirectory
{
  public:
    TemporaryDirectory()
    {
        static std::atomic<unsigned long long> sequence{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        std::random_device random;
        path_ = std::filesystem::temp_directory_path() /
                ("platformator-editor-test-" + std::to_string(stamp) + "-" +
                 std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + "-" +
                 std::to_string(random()));
        std::filesystem::create_directories(path_);
    }

    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path &path() const noexcept
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

class ScopedDirectoryPermissions
{
  public:
    ScopedDirectoryPermissions(const std::filesystem::path &directory,
                               std::filesystem::perms permissions)
        : directory_(directory), original_(std::filesystem::status(directory).permissions())
    {
        std::error_code error;
        std::filesystem::permissions(directory_, permissions,
                                     std::filesystem::perm_options::replace, error);
        if (error)
            throw std::runtime_error("could not set fixture directory permissions: " + error.message());
    }

    ScopedDirectoryPermissions(const ScopedDirectoryPermissions &) = delete;
    ScopedDirectoryPermissions &operator=(const ScopedDirectoryPermissions &) = delete;

    ~ScopedDirectoryPermissions()
    {
        std::error_code error;
        std::filesystem::permissions(directory_, original_,
                                     std::filesystem::perm_options::replace, error);
    }

  private:
    std::filesystem::path directory_;
    std::filesystem::perms original_;
};

// Isolates filesystem-backed editor state for one test process. Construct it
// before initializing SDL or asking SDL_GetPrefPath for the preference root.
class TestEnvironment
{
  public:
    TestEnvironment()
    {
        const auto root = temporary_.path();
        for (const auto *directory : {"home", "preferences", "cache", "data", "tmp", "project"})
            std::filesystem::create_directories(root / directory);

#if defined(_WIN32)
        set("USERPROFILE", root / "home");
        set("APPDATA", root / "preferences");
        set("LOCALAPPDATA", root / "data");
        set("TEMP", root / "tmp");
        set("TMP", root / "tmp");
#else
        set("HOME", root / "home");
        set("XDG_CONFIG_HOME", root / "preferences");
        set("XDG_CACHE_HOME", root / "cache");
        set("XDG_DATA_HOME", root / "data");
        set("TMPDIR", root / "tmp");
#endif
    }

    TestEnvironment(const TestEnvironment &) = delete;
    TestEnvironment &operator=(const TestEnvironment &) = delete;

    ~TestEnvironment()
    {
        for (const auto &[name, value] : previous_)
        {
            if (value)
                set_process_environment(name, *value);
            else
                clear_process_environment(name);
        }
    }

    const std::filesystem::path &root() const noexcept { return temporary_.path(); }
    std::filesystem::path preferences_root() const { return root() / "preferences"; }
    std::filesystem::path filesystem_root() const { return root() / "project"; }

  private:
    static bool set_process_environment(const std::string &name, const std::string &value)
    {
#if defined(_WIN32)
        return _putenv_s(name.c_str(), value.c_str()) == 0;
#else
        return ::setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
    }

    static bool clear_process_environment(const std::string &name)
    {
#if defined(_WIN32)
        return _putenv_s(name.c_str(), "") == 0;
#else
        return ::unsetenv(name.c_str()) == 0;
#endif
    }

    void set(const std::string &name, const std::filesystem::path &value)
    {
        if (!previous_.contains(name))
        {
            const char *current = std::getenv(name.c_str());
            previous_.emplace(name, current == nullptr
                                        ? std::nullopt
                                        : std::optional<std::string>(current));
        }
        if (!set_process_environment(name, value.string()))
            throw std::runtime_error("could not isolate environment variable " + name);
    }

    TemporaryDirectory temporary_;
    std::map<std::string, std::optional<std::string>> previous_;
};

inline void write_json(const std::filesystem::path &path, const Json &value)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    stream << value.dump(2);
}

inline Json read_json(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    return Json::parse(stream);
}
} // namespace platformator_editor_test
