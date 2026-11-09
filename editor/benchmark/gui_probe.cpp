#include "editorapp.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
class ProbeEnvironment
{
  public:
    ProbeEnvironment()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("platformator-gui-probe-" + std::to_string(stamp));
        for (const auto *name : {"home", "config", "cache", "data", "tmp"})
            std::filesystem::create_directories(root_ / name);
#if defined(_WIN32)
        set("USERPROFILE", root_ / "home");
        set("APPDATA", root_ / "config");
        set("LOCALAPPDATA", root_ / "data");
        set("TEMP", root_ / "tmp");
        set("TMP", root_ / "tmp");
#else
        set("HOME", root_ / "home");
        set("XDG_CONFIG_HOME", root_ / "config");
        set("XDG_CACHE_HOME", root_ / "cache");
        set("XDG_DATA_HOME", root_ / "data");
        set("TMPDIR", root_ / "tmp");
#endif
    }

    ProbeEnvironment(const ProbeEnvironment &) = delete;
    ProbeEnvironment &operator=(const ProbeEnvironment &) = delete;

    ~ProbeEnvironment()
    {
        for (const auto &[name, value] : previous_)
        {
            if (value)
                set_environment(name, *value);
            else
                clear_environment(name);
        }
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

  private:
    static bool set_environment(const std::string &name, const std::string &value)
    {
#if defined(_WIN32)
        return _putenv_s(name.c_str(), value.c_str()) == 0;
#else
        return ::setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
    }

    static bool clear_environment(const std::string &name)
    {
#if defined(_WIN32)
        return _putenv_s(name.c_str(), "") == 0;
#else
        return ::unsetenv(name.c_str()) == 0;
#endif
    }

    void set(const std::string &name, const std::filesystem::path &value)
    {
        const char *current = std::getenv(name.c_str());
        previous_.emplace(name, current == nullptr
                                    ? std::nullopt
                                    : std::optional<std::string>(current));
        if (!set_environment(name, value.string()))
            throw std::runtime_error("could not isolate probe environment variable " + name);
    }

    std::filesystem::path root_;
    std::map<std::string, std::optional<std::string>> previous_;
};

std::size_t parse_count(const char *text, const char *option)
{
    if (text == nullptr || *text == '\0')
        throw std::invalid_argument(std::string(option) + " requires a non-negative integer");
    std::size_t consumed = 0;
    const std::string value(text);
    const auto result = std::stoull(value, &consumed);
    if (consumed != value.size())
        throw std::invalid_argument(std::string(option) + " requires a non-negative integer");
    return static_cast<std::size_t>(result);
}
} // namespace

int main(int argc, char *argv[])
{
    try
    {
        std::size_t frames = 35;
        std::size_t warmup = 5;
        std::string workload = "idle";
        std::vector<char *> editorArguments;
        if (argc > 0)
            editorArguments.push_back(argv[0]);
        for (int i = 1; i < argc; ++i)
        {
            const std::string option(argv[i]);
            if (option == "--frames" || option == "--warmup-frames")
            {
                if (i + 1 >= argc)
                    throw std::invalid_argument(option + " requires a value");
                const auto value = parse_count(argv[++i], option.c_str());
                if (option == "--frames")
                    frames = value;
                else
                    warmup = value;
            }
            else if (option == "--workload")
            {
                if (i + 1 >= argc)
                    throw std::invalid_argument(option + " requires a value");
                workload = argv[++i];
                if (workload != "idle" && workload != "hover")
                    throw std::invalid_argument("--workload must be idle or hover");
            }
            else
                editorArguments.push_back(argv[i]);
        }
        if (frames == 0 || warmup >= frames)
            throw std::invalid_argument("--frames must be positive and greater than --warmup-frames");

        static char noRestore[] = "--no-restore";
        bool hasNoRestore = false;
        for (const auto *argument : editorArguments)
            if (std::string(argument) == "--no-restore")
                hasNoRestore = true;
        if (!hasNoRestore)
            editorArguments.push_back(noRestore);

        ProbeEnvironment isolatedPreferences;
        std::cout << "GUI_PROBE,version=2,workload=" << workload << ",configuration="
#if defined(PLATFORMATOR_EDITOR_PROBE_CONFIGURATION)
                  << PLATFORMATOR_EDITOR_PROBE_CONFIGURATION
#else
                  << "unknown"
#endif
                  << ",compiler="
#if defined(PLATFORMATOR_EDITOR_PROBE_COMPILER)
                  << PLATFORMATOR_EDITOR_PROBE_COMPILER
#else
                  << "unknown"
#endif
                  << ",frames=" << frames << ",warmup_frames=" << warmup << '\n';
        return platformator_editor::run_editor_probe(
            static_cast<int>(editorArguments.size()), editorArguments.data(),
            platformator_editor::EditorAppProbeOptions{frames, warmup, workload == "hover"});
    }
    catch (const std::exception &error)
    {
        std::cerr << "GUI probe: " << error.what() << '\n';
        return 2;
    }
}
