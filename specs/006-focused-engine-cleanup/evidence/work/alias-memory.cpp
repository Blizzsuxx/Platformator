#include "test_support.h"
#include <platformator/assetreference.h>
#include <platformator/texturewrapper.h>
#include <malloc.h>
#include <iostream>
#include <string>
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    test_support::headlessEnvironment();
    RuntimeOptions options; options.assetRoot = argv[1];
    platformator::Runtime runtime(options);
    AssetReference<TextureWrapper> held(runtime.loadTexture("assets/textures/missing.png"));
    const auto initial = mallinfo2().uordblks;
    for (size_t i = 1; i <= 300; ++i)
    {
        const auto path = std::string("assets/") + std::string(i, '/') + "textures/missing.png";
        if (runtime.loadTexture(path) != held.get()) return 3;
    }
    const auto withAliases = mallinfo2().uordblks;
    held.clear();
    const auto afterRelease = mallinfo2().uordblks;
    std::cout << "aliases=300 initial=" << initial << " live=" << withAliases
              << " released=" << afterRelease << '\n';
}
