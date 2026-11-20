#include "benchmarkbuildconfig.h"
#include "runtimeaccess.h"
#include "sdlwindow.h"
#include "transform_measurement.h"
#include <platformator/boxcollider.h>
#include <platformator/camera.h>
#include <platformator/rigidbody.h>
#include <platformator/runtime.h>
#include <platformator/sprite.h>
#include <oneapi/tbb/global_control.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    try
    {
        constexpr int warmup = 5, measured = 30, rootCount = 128, branchCount = 4, leavesPerBranch = 8;
        tbb::global_control workers(tbb::global_control::max_allowed_parallelism, 6);
        RuntimeOptions options;
        options.assetRoot = argv[1];
        options.windowSettings.hidden = true;
        platformator::Runtime runtime(options);
        auto *window = platformator_detail::RuntimeAccess::window();
        runtime.createGameObject()->setPosition({320, 240})->addComponent<Camera>(640.f, 480.f);
        SDL_SetRenderVSync(window->getRenderer(), 0);
        auto *texture = runtime.loadTexture("assets/textures/black_square.png");
        struct Root
        {
            GameObject *object;
            Eigen::Vector2f position;
        };
        struct Leaf
        {
            GameObject *object;
            Eigen::Vector2f offset;
            int root;
        };
        std::vector<Root> roots;
        std::vector<Leaf> leaves;
        for (int index = 0; index < rootCount; ++index)
        {
            const Eigen::Vector2f rootPosition{float(index % 16 * 40 + 4), float(index / 16 * 50 + 8)};
            auto *root = runtime.createGameObject()->setLocalPosition(rootPosition);
            roots.push_back({root, rootPosition});
            for (int group = 0; group < branchCount; ++group)
            {
                const Eigen::Vector2f branchPosition{0, float(group * 8)};
                auto *branch = runtime.createGameObject();
                root->addChild(branch);
                branch->setLocalPosition(branchPosition);
                for (int slot = 0; slot < leavesPerBranch; ++slot)
                {
                    const Eigen::Vector2f local{float(slot * 4), 0};
                    auto *leaf = runtime.createGameObject()->addComponent<Sprite>(texture, 3.f, 3.f);
                    branch->addChild(leaf);
                    leaf->setLocalPosition(local);
                    if (group == 0 && slot == 0)
                    {
                        leaf->addComponent<BoxCollider>(3.f, 3.f)->addComponent<Rigidbody>(STATIC, false);
                        leaf->getComponent<BoxCollider>()->setCollisionMask(0);
                    }
                    leaves.push_back({leaf, branchPosition + local, index});
                }
            }
        }

        std::ofstream csv(argv[2]);
        if (!csv)
            throw std::runtime_error("Cannot open transform sample output");
        csv << "step_ns,sync_ns,position_walk_ns,publication_ns\n";
        for (int frame = 0; frame < warmup + measured; ++frame)
        {
            const Eigen::Vector2f shift{frame % 2 == 0 ? 2.f : -2.f, 1.f};
            auto &sample = transform_measurement::sample;
            sample = {};
            const auto start = transform_measurement::Clock::now();
            runtime.simulateFrameWithCustomCallback(1.0 / 120, [&](double) {
                for (const auto &root : roots)
                    root.object->setLocalPosition(root.position + shift);
            });
            const auto duration = transform_measurement::elapsed(start);
            for (const auto &leaf : leaves)
                if (leaf.object->getPosition() != roots[leaf.root].position + shift + leaf.offset)
                    throw std::runtime_error("Published hierarchy position differs from the fixture");
            // Drain the renderer's pending geometry between steps, outside the simulation timing.
            window->render();
            if (frame >= warmup)
                csv << duration << ',' << sample.sync << ',' << sample.walk << ',' << sample.publish << '\n';
        }
        std::cout << PLATFORMATOR_BENCH_SOURCE_SHA256 << '\n';
        return csv ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
