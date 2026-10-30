#include <platformator/runtime.h>
#include <platformator/boxcollider.h>
#include <platformator/camera.h>
#include <platformator/objectreference.h>
#include <platformator/sprite.h>
#include <iostream>

int main(int argc, char **argv)
{
    try
    {
        RuntimeOptions options;
        options.windowSettings.headless = true;
        options.windowSettings.enableRenderCulling = true;
        if (argc > 1)
            options.assetRoot = argv[1];
        platformator::Runtime runtime(options);
        if (!std::filesystem::is_regular_file(runtime.getAssetRoot() / "textures/missing.png"))
            return 3;
        auto *object = runtime.createGameObject()->addComponent<BoxCollider>(10, 10);
        ObjectReference<GameObject> reference(object);
        runtime.createMainCameraIfNoMainCameraExists();
        runtime.createGameObject()->addComponent<Sprite>(static_cast<TextureWrapper *>(nullptr), SDL_FLIP_NONE,
                                                         24.0f, 16.0f);
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
