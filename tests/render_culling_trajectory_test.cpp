#include "render_culling_fixture.h"

#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <vector>

#include <SDL3/SDL_render.h>

#include "test_support.h"

namespace
{
using render_culling_test::DeterministicFixtureTrajectory;
using render_culling_test::FixturePose;
using render_culling_test::FixtureTrajectoryProfile;
using render_culling_test::fixtureProfileUsesStepCallback;

void checkChangedFractions()
{
    constexpr size_t population = 1000;
    for (const size_t percent : {0, 1, 10, 100})
    {
        DeterministicFixtureTrajectory first(FixtureTrajectoryProfile::ChangedSubset, population, 7, percent);
        DeterministicFixtureTrajectory replay(FixtureTrajectoryProfile::ChangedSubset, population, 7, percent);
        std::set<size_t> changed;
        for (size_t slot = 0; slot < population; ++slot)
        {
            const FixturePose before = first.pose(slot, 31);
            const FixturePose after = first.pose(slot, 32);
            const FixturePose repeated = replay.pose(slot, 32);
            test_support::require(after.x == repeated.x && after.y == repeated.y &&
                                      after.generation == repeated.generation,
                                  "Trajectory identity/pose must be deterministic for a fixed seed");
            if (before.x != after.x || before.y != after.y || before.rotationRadians != after.rotationRadians)
                changed.insert(slot);
        }
        const size_t expected = population * percent / 100;
        test_support::require(changed.size() == expected && first.changedSpriteCount() == expected,
                              "Actual continuously moving Sprite subset must match its requested fraction");
        for (const size_t slot : changed)
            test_support::require(first.isChanged(slot), "A changed Sprite must belong to the declared subset");
    }

    DeterministicFixtureTrajectory allVisible(FixtureTrajectoryProfile::VisibleMotion, population, 13);
    size_t moving = 0;
    for (size_t slot = 0; slot < population; ++slot)
    {
        const auto before = allVisible.pose(slot, 12);
        const auto after = allVisible.pose(slot, 13);
        moving += before.x != after.x || before.y != after.y;
    }
    test_support::require(moving == population, "All-visible motion must continuously move every Sprite");
}

void checkSpatialAndTransitionProfiles()
{
    constexpr size_t population = 50000;
    DeterministicFixtureTrajectory spreadSmall(FixtureTrajectoryProfile::Spread, 1000, 2);
    DeterministicFixtureTrajectory spreadLarge(FixtureTrajectoryProfile::Spread, population, 2);
    DeterministicFixtureTrajectory clustered(FixtureTrajectoryProfile::Clustered, population, 2);
    const auto spreadSmallExtent = spreadSmall.pose(999, 0);
    const auto spreadLargeExtent = spreadLarge.pose(population - 1, 0);
    const auto clusteredExtent = clustered.pose(population - 1, 0);
    test_support::require(spreadLargeExtent.x > spreadSmallExtent.x && spreadLargeExtent.y > spreadSmallExtent.y,
                          "Spread layout density must stay bounded as population grows to 50k");
    test_support::require(spreadLargeExtent.y > clusteredExtent.y * 3.0f,
                          "Spread and fixed-area clustered layouts must exercise different spatial densities");

    DeterministicFixtureTrajectory crossing(FixtureTrajectoryProfile::Crossing, 1000, 5);
    DeterministicFixtureTrajectory pan(FixtureTrajectoryProfile::CameraPan, 1000, 5);
    DeterministicFixtureTrajectory panSpread(FixtureTrajectoryProfile::CameraPan, population, 5, 0, true);
    DeterministicFixtureTrajectory panSpreadSmall(FixtureTrajectoryProfile::CameraPan, 1000, 5, 0, true);
    const FixturePose crossingStart = crossing.pose(0, 0);
    const FixturePose crossingNext = crossing.pose(0, 1);
    const auto panStart = pan.cameraPosition(0);
    const auto panNext = pan.cameraPosition(1);
    test_support::require(crossingStart.x != crossingNext.x && crossingStart.y != crossingNext.y,
                          "Crossing sprites must move continuously between adjacent steps");
    test_support::require(panStart != panNext, "Camera pan must move continuously between adjacent steps");
    test_support::require(panSpread.pose(population - 1, 0).x > panSpreadSmall.pose(999, 0).x &&
                              panSpread.pose(population - 1, 0).y > panSpreadSmall.pose(999, 0).y,
                          "Camera-pan constant-density layout must expand with population instead of reusing fixed-area poses");

    DeterministicFixtureTrajectory giant(FixtureTrajectoryProfile::Giant, 1000, 9, 10);
    DeterministicFixtureTrajectory rotated(FixtureTrajectoryProfile::Rotated, 1000, 9);
    test_support::require(giant.spriteDimensions() == std::pair{2048.0f, 1024.0f},
                          "Giant profile must resolve to the oversized sprite dimensions");
    test_support::require(giant.changedSpriteCount() == 100 && giant.isChanged(0) && !giant.isChanged(100),
                          "Giant sprite motion must change only the requested subset");
    test_support::require(giant.pose(0, 31).x != giant.pose(0, 32).x && giant.pose(100, 31).x == giant.pose(100, 32).x,
                          "Giant stress must continuously move only its declared subset");
    test_support::require(std::abs(rotated.pose(0, 0).rotationRadians) > 0.01f,
                          "Rotated profile must contain actual rotated sprites");

    DeterministicFixtureTrajectory offset(FixtureTrajectoryProfile::LargeOffset, 1000, 11, 1);
    const auto offsetCamera = offset.cameraPosition(0);
    const auto offsetSprite = offset.pose(0, 0);
    test_support::require(offsetCamera.first > 9000000.0f && offsetSprite.x > 9000000.0f,
                          "Large-offset profile must preserve a large shared world-coordinate origin");
    test_support::require(offset.changedSpriteCount() == 10 && offset.pose(0, 31).x != offset.pose(0, 32).x &&
                              offset.pose(10, 31).x == offset.pose(10, 32).x,
                          "Large-offset 1% movement must remain observable despite float coordinate precision");

    DeterministicFixtureTrajectory resize(FixtureTrajectoryProfile::OutputResize, 1000, 3);
    test_support::require(resize.outputDimensions(239) == std::pair{320, 240} &&
                              resize.outputDimensions(240) == std::pair{400, 300},
                          "Output resize trajectory must use a stable explicit step schedule");

    DeterministicFixtureTrajectory churn(FixtureTrajectoryProfile::Churn, 1000, 17);
    std::set<size_t> replaced;
    churn.forEachReplacedSlot(2, [&](size_t slot, uint32_t generation) {
        test_support::require(replaced.insert(slot).second, "Stable churn must never replace one slot twice per step");
        test_support::require(generation == churn.generationForSlot(slot, 2),
                              "Churn identity generation must match the live slot generation");
    });
    test_support::require(replaced.size() == churn.replacementsPerStep(),
                          "Stable churn must replace the declared unique Sprite count per step");
}

void checkRuntimeDynamicProfileDispatch()
{
    constexpr size_t population = 20;
    constexpr size_t changedPercent = 10;
    const std::array dynamicProfiles = {FixtureTrajectoryProfile::Crossing,
                                        FixtureTrajectoryProfile::CameraPan,
                                        FixtureTrajectoryProfile::Churn,
                                        FixtureTrajectoryProfile::Rotated,
                                        FixtureTrajectoryProfile::VisibleMotion,
                                        FixtureTrajectoryProfile::ChangedSubset,
                                        FixtureTrajectoryProfile::Giant,
                                        FixtureTrajectoryProfile::LargeOffset,
                                        FixtureTrajectoryProfile::OutputResize,
                                        FixtureTrajectoryProfile::Clustered};
    for (const auto profile : dynamicProfiles)
        test_support::require(render_culling_test::fixtureProfileUsesStepCallback(profile),
                              "Every declared dynamic profile must use the shared runtime callback dispatch");
    for (const auto profile : {FixtureTrajectoryProfile::Visible, FixtureTrajectoryProfile::Offscreen,
                               FixtureTrajectoryProfile::Sparse, FixtureTrajectoryProfile::Spread,
                               FixtureTrajectoryProfile::SceneDefault, FixtureTrajectoryProfile::SceneStacking})
        test_support::require(!render_culling_test::fixtureProfileUsesStepCallback(profile),
                              "Static or scene-driven profiles must not claim a synthetic fixture update callback");

    test_support::headlessEnvironment();
    for (const auto profile : dynamicProfiles)
    {
        const size_t step = profile == FixtureTrajectoryProfile::OutputResize ? 240 : 1;
        DeterministicFixtureTrajectory trajectory(profile, population, 29, changedPercent);
        render_culling_test::RenderCullingFixture fixture(320, 240, 320.0f, 240.0f, true);
        fixture.reserveSprites(population);
        if (profile == FixtureTrajectoryProfile::CameraPan || profile == FixtureTrajectoryProfile::LargeOffset)
        {
            const auto [x, y] = trajectory.cameraPosition(0);
            fixture.camera()->setCamera({x, y, 320.0f, 240.0f});
        }
        const auto [width, height] = trajectory.spriteDimensions();
        std::vector<FixturePose> before;
        before.reserve(population);
        for (size_t slot = 0; slot < population; ++slot)
        {
            const FixturePose pose = trajectory.pose(slot, 0);
            before.push_back(pose);
            fixture.addSprite(slot, pose.generation, pose.x, pose.y, width, height,
                              render_culling_test::FixtureTexture::BlackSquare, SDL_FLIP_NONE, true,
                              pose.rotationRadians);
        }

        const auto update = [&](double) {
            if (profile == FixtureTrajectoryProfile::CameraPan || profile == FixtureTrajectoryProfile::LargeOffset)
            {
                const auto [x, y] = trajectory.cameraPosition(step);
                fixture.camera()->setCamera({x, y, 320.0f, 240.0f});
            }
            if (profile == FixtureTrajectoryProfile::OutputResize && step == 240)
                fixture.resizeOutput(400, 300);
            if (profile == FixtureTrajectoryProfile::Churn)
            {
                trajectory.forEachReplacedSlot(step, [&](size_t slot, uint32_t generation) {
                    const FixturePose pose = trajectory.pose(slot, step);
                    fixture.replaceSprite(slot, generation, pose.x, pose.y, width, height,
                                          render_culling_test::FixtureTexture::BlackSquare, SDL_FLIP_NONE,
                                          pose.rotationRadians);
                });
            }
            else if (profile == FixtureTrajectoryProfile::Crossing ||
                     profile == FixtureTrajectoryProfile::VisibleMotion ||
                     profile == FixtureTrajectoryProfile::ChangedSubset || profile == FixtureTrajectoryProfile::Giant ||
                     profile == FixtureTrajectoryProfile::LargeOffset || profile == FixtureTrajectoryProfile::Clustered ||
                     profile == FixtureTrajectoryProfile::Rotated)
            {
                for (size_t slot = 0; slot < population; ++slot)
                {
                    if ((profile == FixtureTrajectoryProfile::ChangedSubset || profile == FixtureTrajectoryProfile::Giant ||
                         profile == FixtureTrajectoryProfile::LargeOffset || profile == FixtureTrajectoryProfile::Clustered ||
                         profile == FixtureTrajectoryProfile::Rotated) && !trajectory.isChanged(slot))
                        continue;
                    const FixturePose pose = trajectory.pose(slot, step);
                    fixture.record(slot).object->setPosition({pose.x, pose.y});
                    fixture.record(slot).object->setRotation(pose.rotationRadians);
                }
            }
        };
        test_support::require(fixtureProfileUsesStepCallback(profile),
                              "The runtime probe must dispatch the profile update callback");
        test_support::require(fixture.runtime().simulateAndRenderFrameWithCustomCallback(test_support::dt, update),
                              "Dynamic fixture runtime stopped before its callback could run");

        if (profile == FixtureTrajectoryProfile::Churn)
        {
            size_t replacements = 0;
            trajectory.forEachReplacedSlot(step, [&](size_t slot, uint32_t generation) {
                ++replacements;
                test_support::require(fixture.record(slot).identity.generation == generation,
                                      "Runtime churn callback must publish the new fixture generation");
            });
            test_support::require(replacements == trajectory.replacementsPerStep(),
                                  "Runtime churn callback must perform its declared replacements");
        }
        else if (profile == FixtureTrajectoryProfile::CameraPan || profile == FixtureTrajectoryProfile::LargeOffset)
        {
            const auto expected = trajectory.cameraPosition(step);
            test_support::require(fixture.camera()->getGameObject()->getPosition() ==
                                      Eigen::Vector2f(expected.first, expected.second),
                                  "Runtime camera trajectory callback must publish the requested pan");
        }
        else if (profile == FixtureTrajectoryProfile::OutputResize)
        {
            int outputWidth = 0;
            int outputHeight = 0;
            test_support::require(SDL_GetRenderOutputSize(fixture.runtime().getRenderer(), &outputWidth, &outputHeight) &&
                                      outputWidth == 400 && outputHeight == 300,
                                  "Runtime output-resize callback must perform the declared resize");
        }
        else
        {
            size_t changed = 0;
            for (size_t slot = 0; slot < population; ++slot)
            {
                const auto position = fixture.record(slot).object->getPosition();
                const float rotation = fixture.record(slot).object->getRotation();
                changed += position.x() != before[slot].x || position.y() != before[slot].y ||
                           rotation != before[slot].rotationRadians;
            }
            const size_t expected = profile == FixtureTrajectoryProfile::Crossing ||
                                            profile == FixtureTrajectoryProfile::VisibleMotion
                                        ? population
                                        : trajectory.changedSpriteCount();
            test_support::require(changed == expected && changed > 0,
                                  "Runtime trajectory callback must change the declared unique sprite count");
        }
    }
}
}

int main()
{
    try
    {
        checkChangedFractions();
        checkSpatialAndTransitionProfiles();
        checkRuntimeDynamicProfileDispatch();
        std::cout << "[PASS] deterministic render-culling trajectories and changed subsets\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "[FAIL] render-culling trajectory: " << error.what() << '\n';
        return 1;
    }
}
