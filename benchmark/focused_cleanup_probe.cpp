#include "debugdraw.h"
#include "test_support.h"
#include <platformator/assetreference.h>
#include <platformator/texturewrapper.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::cerr << "usage: focused_cleanup_probe asset-hit|asset-alias|asset-churn|debug count asset-root\n";
        return 2;
    }
    const std::string mode = argv[1];
    const auto count = std::stoull(argv[2]);
    if (!count) return 2;
    test_support::headlessEnvironment();
    RuntimeOptions options;
    options.assetRoot = argv[3];
    platformator::Runtime runtime(options);
    std::chrono::steady_clock::time_point start;
    std::int64_t ns = 0;
    size_t warmup = 0;
    if (mode == "asset-hit" || mode == "asset-alias" || mode == "asset-churn")
    {
        if (mode == "asset-hit" || mode == "asset-alias")
        {
            AssetReference<TextureWrapper> retained(runtime.loadTexture("assets/textures/missing.png"));
            const char *request = mode == "asset-alias" ? "assets\\textures\\missing.png" : "assets/textures/missing.png";
            warmup = 100;
            for (size_t i = 0; i < warmup; ++i)
                if (runtime.loadTexture(request) != retained.get()) return 3;
            start = std::chrono::steady_clock::now();
            for (size_t i = 0; i < count; ++i)
                if (runtime.loadTexture(request) != retained.get()) return 3;
            ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
        }
        else
        {
            warmup = 2;
            for (size_t i = 0; i < warmup; ++i)
            {
                AssetReference<TextureWrapper> asset(runtime.loadTexture("assets/textures/missing.png"));
                if (!asset) return 3;
            }
            start = std::chrono::steady_clock::now();
            for (size_t i = 0; i < count; ++i)
            {
                AssetReference<TextureWrapper> asset(runtime.loadTexture("assets/textures/missing.png"));
                if (!asset) return 3;
            }
            ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
        }
    }
    else if (mode == "debug")
    {
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
        std::vector<DebugObject> objects;
        start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < count; ++i)
            objects.emplace_back(std::vector<Eigen::Vector2f>(16), true, std::string(80, 'x'));
        if (objects.size() != count) return 3;
        ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
        std::cout << "nothrow_move=" << std::is_nothrow_move_constructible_v<DebugObject> << '\n';
#else
        std::cerr << "debug tools disabled\n";
        return 4;
#endif
    }
    else return 2;
    std::cout << "mode=" << mode << " count=" << count << " warmup=" << warmup << " elapsed_ns=" << ns << '\n';
}
