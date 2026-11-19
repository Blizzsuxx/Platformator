#include <platformator/runtime.h>
#include <platformator/boxcollider.h>
#include <platformator/camera.h>
#include <platformator/objectreference.h>
#include <platformator/sprite.h>
#include <cstdint>
#include <iostream>
#include <memory>
#include <new>

namespace
{
struct alignas(2 * __STDCPP_DEFAULT_NEW_ALIGNMENT__) AlignedCollider : BoxCollider
{
};

class HeapCollider : public BoxCollider
{
public:
    static void *operator new(std::size_t size) { return ::operator new(size); }

private:
    char extraStorage[64]{};
};
}

int main(int argc, char **argv)
{
    try
    {
        const std::unique_ptr<BoxCollider> pooled(new (std::nothrow) BoxCollider);
        const std::unique_ptr<BoxCollider> aligned(new (std::nothrow) AlignedCollider);
        if (!pooled || !aligned || reinterpret_cast<std::uintptr_t>(aligned.get()) % alignof(AlignedCollider) != 0)
            return 5;
        std::unique_ptr<BoxCollider> heapAllocated(new HeapCollider);
        heapAllocated.reset();

        RuntimeOptions options;
        options.windowSettings.headless = true;
        options.windowSettings.enableRenderCulling = true;
        if (argc > 1)
            options.assetRoot = argv[1];
        platformator::Runtime runtime(options);
        auto *object = runtime.createGameObject()->addComponent<BoxCollider>(10, 10);
        ObjectReference<GameObject> reference(object);
        runtime.createMainCameraIfNoMainCameraExists();
        auto *sprite =
            runtime.createGameObject()
                ->addComponent<Sprite>(static_cast<TextureWrapper *>(nullptr), SDL_FLIP_NONE, 24.0f, 16.0f)
                ->getComponent<Sprite>();
        if (sprite == nullptr || sprite->getZOrder() != 0)
            return 3;
        sprite->setZOrder(-10);
        if (sprite->getZOrder() != -10)
            return 4;
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
