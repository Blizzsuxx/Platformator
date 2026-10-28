#include <platformator/runtime.h>
#include <platformator/boxcollider.h>
#include <platformator/collider.h>
#include <platformator/objectreference.h>
#include <cmath>
#include <iostream>

int main(int argc, char **argv)
{
    try
    {
        RuntimeOptions options;
        options.windowSettings.headless = true;
        options.spatialSettings.broadPhase = BroadPhaseMode::DynamicBvh;
        options.spatialSettings.renderCulling = RenderCullingMode::Segmented;
        if (argc > 1)
            options.assetRoot = argv[1];
        platformator::Runtime runtime(options);
        if (!std::filesystem::is_regular_file(runtime.getAssetRoot() / "textures/missing.png"))
            return 3;
        auto *object = runtime.createGameObject()->setPosition({12, -4})->addComponent<BoxCollider>(10, 10);
        auto *collider = object->getComponent<BoxCollider>();
        ObjectReference<GameObject> reference(object);
        runtime.simulateFrame(1.0 / 120.0);
        const auto *xMin = collider->getXProjections()->getMin();
        const auto *yMax = collider->getYProjections()->getMax();
        if (runtime.getSpatialSettings().renderCulling != RenderCullingMode::Segmented ||
            runtime.getRenderer() != nullptr || !collider->getIsRegisteredInBroadPhase() || collider->getIsRegisteredInGrid() ||
            !collider->getIsRegisteredInPhysicsManager() || collider->hasValidGridCellRange() ||
            !std::isfinite(xMin->getProjectedPosition()) || !std::isfinite(yMax->getProjectedPosition()) ||
            collider->getGridCellRange() != collider->getGridCellRangeCache())
            return 4;
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
