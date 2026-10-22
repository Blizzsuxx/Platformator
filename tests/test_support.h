#pragma once

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <fstream>

#include <platformator/behavior.h>
#include <platformator/runtime.h>

namespace test_support
{
inline constexpr double dt = 1.0 / 120.0;
inline RuntimeOptions physicsOnly()
{
    RuntimeOptions options;
    options.windowSettings.headless = true;
    return options;
}
inline void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}
inline void headlessEnvironment()
{
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_RENDER_DRIVER", "software", 1);
}
inline void writeScene(const std::filesystem::path &path, const std::string &name, int id = 0)
{
    nlohmann::json object = {{"id", id},
                             {"rotation", 0},
                             {"active", true},
                             {"position", {{"x", 0}, {"y", 0}}},
                             {"scale", {{"x", 1}, {"y", 1}}},
                             {"name", name},
                             {"tag", ""},
                             {"children", nlohmann::json::array()},
                             {"components", nlohmann::json::array()}};
    std::ofstream file(path);
    file.exceptions(std::ios::failbit | std::ios::badbit);
    file << nlohmann::json::array({object});
}
class Behavior : public ::Behavior
{
  public:
    std::string getTypeName() const override
    {
        return "TestBehavior";
    }
    void serialize(nlohmann::json &j) const override
    {
        j = nlohmann::json::object();
    }
    void deserialize(const nlohmann::json &) override
    {
    }
    void resolveReferences(platformator::ReferenceResolver &) override
    {
    }
};
} // namespace test_support
