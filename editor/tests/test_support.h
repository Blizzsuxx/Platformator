#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
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
