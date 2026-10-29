#define main platformator_benchmark_runner_entrypoint
#include "../src/benchmark_main.cpp"
#undef main

#include "test_support.h"

#include <array>
#include <iostream>

int main()
{
    try
    {
        std::array<char *, 1> defaultArguments{const_cast<char *>("platformator_benchmark_runner")};
        const auto defaults = parseBenchmarkRunnerOptions(static_cast<int>(defaultArguments.size()),
                                                           defaultArguments.data());
        test_support::require(defaults.runtimeOptions.windowSettings.enableRenderCulling,
                              "Benchmark runner culling must default to enabled");
        const auto defaultMetadata = renderCullingMetadata(defaults);
        test_support::require(defaultMetadata.at("requested_mode") == "linear" &&
                                  defaultMetadata.at("effective_policy") == "headless-no-rendering" &&
                                  defaultMetadata.at("effective_enabled") == false,
                              "Headless benchmark metadata must preserve requested and effective policy");

        std::array<char *, 4> disabledArguments{const_cast<char *>("platformator_benchmark_runner"),
                                                const_cast<char *>("--no-render-culling"),
                                                const_cast<char *>("--no-render-culling"),
                                                const_cast<char *>("--render")};
        const auto disabled = parseBenchmarkRunnerOptions(static_cast<int>(disabledArguments.size()),
                                                           disabledArguments.data());
        test_support::require(!disabled.runtimeOptions.windowSettings.enableRenderCulling,
                              "Repeated benchmark opt-out must remain idempotently disabled");
        const auto disabledMetadata = renderCullingMetadata(disabled);
        test_support::require(disabledMetadata.at("requested_mode") == "disabled" &&
                                  disabledMetadata.at("requested_enabled") == false &&
                                  disabledMetadata.at("effective_policy") == "submit-all" &&
                                  disabledMetadata.at("effective_enabled") == false,
                              "Rendered disabled mode metadata is incomplete");
        std::cout << "[PASS] benchmark render culling CLI and policy metadata\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "[FAIL] benchmark render culling CLI: " << error.what() << '\n';
        return 1;
    }
}
