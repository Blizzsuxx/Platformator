#include <platformator/runtime.h>
#include <platformator/boxcollider.h>
#include <platformator/objectreference.h>
#include <iostream>

int main(int argc, char **argv)
{
    try
    {
        RuntimeOptions options;
        options.windowSettings.headless = true;
        if (argc > 1)
            options.assetRoot = argv[1];
        platformator::Runtime runtime(options);
        if (!std::filesystem::is_regular_file(runtime.getAssetRoot() / "textures/missing.png"))
            return 3;
        auto *object = runtime.createGameObject()->addComponent<BoxCollider>(10, 10);
        ObjectReference<GameObject> reference(object);
        runtime.simulateFrame(1.0 / 120.0);
        runtime.clearScene();
        if (reference)
            return 2;
        std::cout << "consumer ok root=" << runtime.getAssetRoot() << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
