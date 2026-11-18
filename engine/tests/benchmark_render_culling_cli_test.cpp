#define main platformator_benchmark_runner_entrypoint
#include "../src/benchmark_main.cpp"
#undef main

#include "test_support.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
class ScopedEnvSelector
{
  public:
    explicit ScopedEnvSelector(const char *value)
    {
        if (const char *previous = std::getenv("PLATFORMATOR_RENDER_CULLING_MODE"))
        {
            hadPrevious_ = true;
            previous_ = previous;
        }
        if (value == nullptr)
        {
            if (unsetenv("PLATFORMATOR_RENDER_CULLING_MODE") != 0)
                throw std::runtime_error("Could not unset PLATFORMATOR_RENDER_CULLING_MODE");
        }
        else if (setenv("PLATFORMATOR_RENDER_CULLING_MODE", value, 1) != 0)
        {
            throw std::runtime_error("Could not set PLATFORMATOR_RENDER_CULLING_MODE");
        }
    }

    ~ScopedEnvSelector()
    {
        if (hadPrevious_)
            setenv("PLATFORMATOR_RENDER_CULLING_MODE", previous_.c_str(), 1);
        else
            unsetenv("PLATFORMATOR_RENDER_CULLING_MODE");
    }

  private:
    bool hadPrevious_ = false;
    std::string previous_;
};
} // namespace

int main()
{
    try
    {
        {
            ScopedEnvSelector unset(nullptr);
            std::array<char *, 1> defaultArguments{const_cast<char *>("platformator_benchmark_runner")};
            const auto defaults = parseBenchmarkRunnerOptions(static_cast<int>(defaultArguments.size()),
                                                               defaultArguments.data());
            test_support::require(defaults.runtimeOptions.windowSettings.enableRenderCulling,
                                  "Benchmark runner culling must default to enabled");
            const auto defaultMetadata = renderCullingMetadata(defaults);
            test_support::require(defaultMetadata.at("requested_selector") == "cached-linear" &&
                                      defaultMetadata.at("resolved_startup_strategy") == "headless-no-rendering" &&
                                      defaultMetadata.at("requested_mode") == "cached-linear" &&
                                      defaultMetadata.at("headless") == true &&
                                      defaultMetadata.at("disabled") == false &&
                                      defaultMetadata.at("effective_policy") == "headless-no-rendering" &&
                                      defaultMetadata.at("effective_enabled") == false &&
                                      defaultMetadata.at("observed_frame_fallbacks") == 0,
                                  "Headless benchmark metadata must report resolved policy separately");

            std::array<char *, 2> renderArguments{const_cast<char *>("platformator_benchmark_runner"),
                                                  const_cast<char *>("--render")};
            const auto rendered = parseBenchmarkRunnerOptions(static_cast<int>(renderArguments.size()),
                                                               renderArguments.data());
            const auto renderedMetadata = renderCullingMetadata(rendered);
            test_support::require(renderedMetadata.at("requested_selector") == "cached-linear" &&
                                      renderedMetadata.at("resolved_startup_strategy") == "cached-linear" &&
                                      renderedMetadata.at("headless") == false &&
                                      renderedMetadata.at("disabled") == false &&
                                      renderedMetadata.at("effective_policy") == "cached-linear" &&
                                      renderedMetadata.at("effective_enabled") == true,
                                  "Rendered default benchmark metadata must resolve cached-linear");

            std::array<char *, 4> disabledArguments{const_cast<char *>("platformator_benchmark_runner"),
                                                    const_cast<char *>("--no-render-culling"),
                                                    const_cast<char *>("--no-render-culling"),
                                                    const_cast<char *>("--render")};
            const auto disabled = parseBenchmarkRunnerOptions(static_cast<int>(disabledArguments.size()),
                                                               disabledArguments.data());
            test_support::require(!disabled.runtimeOptions.windowSettings.enableRenderCulling,
                                  "Repeated benchmark opt-out must remain idempotently disabled");
            const auto disabledMetadata = renderCullingMetadata(disabled);
            test_support::require(disabledMetadata.at("requested_selector") == "cached-linear" &&
                                      disabledMetadata.at("resolved_startup_strategy") == "submit-all" &&
                                      disabledMetadata.at("requested_mode") == "disabled" &&
                                      disabledMetadata.at("requested_enabled") == false &&
                                      disabledMetadata.at("disabled") == true &&
                                      disabledMetadata.at("headless") == false &&
                                      disabledMetadata.at("effective_policy") == "submit-all" &&
                                      disabledMetadata.at("effective_enabled") == false,
                                  "Rendered disabled mode metadata must preserve selector while reporting opt-out");
        }

        {
            ScopedEnvSelector cached("cached-linear");
            std::array<char *, 2> renderArguments{const_cast<char *>("platformator_benchmark_runner"),
                                                  const_cast<char *>("--render")};
            const auto rendered = parseBenchmarkRunnerOptions(static_cast<int>(renderArguments.size()),
                                                               renderArguments.data());
            const auto cachedMetadata = renderCullingMetadata(rendered);
            test_support::require(cachedMetadata.at("requested_selector") == "cached-linear" &&
                                      cachedMetadata.at("resolved_startup_strategy") == "cached-linear" &&
                                      cachedMetadata.at("effective_policy") == "cached-linear" &&
                                      cachedMetadata.at("effective_enabled") == true,
                                  "Explicit cached-linear selector must appear in benchmark policy metadata");
        }

        for (const char *badSelector : {"indexed", "current-linear", "bogus-strategy"})
        {
            ScopedEnvSelector invalid(badSelector);
            std::array<char *, 2> disabledArgs{const_cast<char *>("platformator_benchmark_runner"),
                                               const_cast<char *>("--no-render-culling")};
            const auto disabled = parseBenchmarkRunnerOptions(static_cast<int>(disabledArgs.size()),
                                                               disabledArgs.data());
            bool rejected = false;
            std::string message;
            try
            {
                (void)renderCullingMetadata(disabled);
            }
            catch (const std::invalid_argument &error)
            {
                rejected = true;
                message = error.what();
            }
            test_support::require(rejected && message.find("cached-linear") != std::string::npos,
                                  "Retired or unknown selector must fail policy resolution with actionable guidance even when disabled");
        }
        std::cout << "[PASS] benchmark render culling CLI and policy metadata\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "[FAIL] benchmark render culling CLI: " << error.what() << '\n';
        return 1;
    }
}
