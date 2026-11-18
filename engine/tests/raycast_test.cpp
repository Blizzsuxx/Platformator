#include "runtime_work_probe.h"
#include "test_support.h"

#include <oneapi/tbb/parallel_for.h>

#include <platformator/boxcollider.h>
#include <platformator/circlecollider.h>
#include <platformator/collisiontypes.h>
#include <platformator/gameobject.h>
#include <platformator/runtime.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace
{
bool gCountAllocations = false;
std::size_t gAllocationCount = 0;
} // namespace

void *operator new(std::size_t size)
{
    if (gCountAllocations)
        ++gAllocationCount;
    if (void *ptr = std::malloc(size == 0 ? 1 : size))
        return ptr;
    throw std::bad_alloc();
}

void operator delete(void *ptr) noexcept
{
    std::free(ptr);
}

void operator delete(void *ptr, std::size_t) noexcept
{
    std::free(ptr);
}

void *operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete[](void *ptr) noexcept
{
    std::free(ptr);
}

void operator delete[](void *ptr, std::size_t) noexcept
{
    std::free(ptr);
}

using namespace test_support;

namespace
{
bool nearFloat(float actual, float expected, float eps = 1e-4f)
{
    return std::abs(actual - expected) <= eps;
}

bool nearVec(const Eigen::Vector2f &actual, const Eigen::Vector2f &expected, float eps = 1e-4f)
{
    return nearFloat(actual.x(), expected.x(), eps) && nearFloat(actual.y(), expected.y(), eps);
}

void requireThrowsInvalidArg(const auto &fn, const std::string &message)
{
    bool threw = false;
    try
    {
        fn();
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    require(threw, message);
}

void testAabbRotatedScaledOffsetBoxesAndCircles()
{
    platformator::Runtime runtime(physicsOnly());

    // 1. Axis-aligned box centered at (50, 10) with width=20, height=10 -> x in [40, 60], y in [5, 15].
    GameObject *aabbObj = runtime.createGameObject()->setName("AABB");
    aabbObj->setPosition({50.0f, 10.0f});
    aabbObj->addComponent<BoxCollider>(20.0f, 10.0f);
    Collider *aabbCollider = aabbObj->getComponent<Collider>();

    // Non-unit direction {4.0f, 0.0f} must normalize while keeping maxDistance in world units.
    platformator::RaycastHit hit;
    require(runtime.raycast({10.0f, 10.0f}, {4.0f, 0.0f}, 35.0f, hit),
            "Ray should reach x=40 within maxDistance 35 from x=10");
    require(hit.collider == aabbCollider && hit.gameObject == aabbObj,
            "RaycastHit must identify hit collider and GameObject");
    require(nearVec(hit.point, {40.0f, 10.0f}) && nearVec(hit.normal, {-1.0f, 0.0f}) &&
                nearFloat(hit.distance, 30.0f) && nearFloat(hit.fraction, 30.0f / 35.0f),
            "AABB hit distance, fraction, point, and outward normal must match exact geometry");

    // Miss when maxDistance ends just short of the surface.
    hit.distance = 999.0f;
    require(!runtime.raycast({10.0f, 10.0f}, {1.0f, 0.0f}, 29.9f, hit),
            "Ray stopping short at 39.9 must miss box starting at x=40");
    require(hit.distance == 999.0f, "Failed outHit raycast must leave caller outHit untouched");

    // 2. Rotated + scaled + offset BoxCollider:
    // Local offset (2, 0), base (10, 4), scale (2, 1.5) -> scaled offset (4, 0), world extents (20, 6) -> halfW=10, halfH=3.
    // Rotated by 90 deg (pi/2): center moves to (200 + 0, 100 + 4) = (200, 104), local +X aligns with world +Y,
    // local +Y aligns with world -X. World AABB becomes x in [197, 203], y in [94, 114].
    GameObject *rotObj = runtime.createGameObject()->setName("RotScaledBox");
    rotObj->setPosition({200.0f, 100.0f});
    rotObj->setRotation(std::numbers::pi_v<float> * 0.5f);
    rotObj->setScale({2.0f, 1.5f});
    rotObj->addComponent<BoxCollider>(10.0f, 4.0f);
    rotObj->getComponent<Collider>()->setOffset({2.0f, 0.0f});
    runtime.simulateFrame(dt);

    auto rotHit = runtime.raycast({190.0f, 104.0f}, {1.0f, 0.0f}, 50.0f);
    require(rotHit.has_value(), "Rotated, scaled, offset box should be hit along +X");
    require(nearVec(rotHit->point, {197.0f, 104.0f}) && nearVec(rotHit->normal, {-1.0f, 0.0f}) &&
                nearFloat(rotHit->distance, 7.0f),
            "Rotated+scaled+offset box hit point/normal/distance must reflect exact OBB");

    // 3. 45-degree rotated square box centered at (0, 200), size 20x20 -> vertex at (0, 200 - 10*sqrt(2)).
    GameObject *diagObj = runtime.createGameObject()->setName("DiagBox");
    diagObj->setPosition({0.0f, 200.0f});
    diagObj->setRotation(std::numbers::pi_v<float> * 0.25f);
    diagObj->addComponent<BoxCollider>(20.0f, 20.0f);
    // Cast along (-1, -1) normalized from (20, 220) onto upper-right rotated face at distance = 20*sqrt(2) - 10
    const Eigen::Vector2f diagDir = Eigen::Vector2f{-1.0f, -1.0f}.normalized();
    auto diagHit = runtime.raycast({20.0f, 220.0f}, diagDir, 100.0f);
    require(diagHit.has_value(), "45-degree box must be hit along diagonal normal");
    const float expectedDist = 20.0f * std::sqrt(2.0f) - 10.0f;
    const Eigen::Vector2f expectedNormal = Eigen::Vector2f{1.0f, 1.0f}.normalized();
    require(nearFloat(diagHit->distance, expectedDist) && nearVec(diagHit->normal, expectedNormal),
            "45-degree OBB hit distance and rotated normal must match within 1e-4");

    // 4. Scaled & offset CircleCollider:
    // Collider::getWorldPosition() applies position + rotated offset (without scaling offset):
    // position (300, 50) + offset (3, -1) = (303, 49); scale (2, 2), radius 5 -> world radius 10.
    // Ray from (303, 20) downward (+Y) hits top of circle at (303, 39), distance = 19.
    GameObject *circleObj = runtime.createGameObject()->setName("ScaledCircle");
    circleObj->setPosition({300.0f, 50.0f});
    circleObj->setScale({2.0f, 2.0f});
    circleObj->addComponent<CircleCollider>(5.0f);
    circleObj->getComponent<Collider>()->setOffset({3.0f, -1.0f});
    runtime.simulateFrame(dt);

    auto circleHit = runtime.raycast({303.0f, 20.0f}, {0.0f, 1.0f}, 50.0f);
    require(circleHit.has_value(), "Scaled/offset circle must be hit");
    require(nearVec(circleHit->point, {303.0f, 39.0f}) && nearVec(circleHit->normal, {0.0f, -1.0f}) &&
                nearFloat(circleHit->distance, 19.0f),
            "Scaled/offset circle hit point, normal, and distance must match exact sphere math");

    // Tangential miss just outside circle radius
    require(!runtime.raycast({313.05f, 20.0f}, {0.0f, 1.0f}, 60.0f).has_value(),
            "Ray outside circle radius must miss");
    // Ray pointing away from circle
    require(!runtime.raycast({303.0f, 20.0f}, {0.0f, -1.0f}, 100.0f).has_value(),
            "Ray pointing away from circle must miss");
}

void testImmediateTransformsMasksTriggersAndLifecycle()
{
    platformator::Runtime runtime(physicsOnly());

    GameObject *parent = runtime.createGameObject()->setName("Parent");
    parent->setPosition({0.0f, 0.0f});
    GameObject *child = runtime.createGameObject()->setName("Child");
    parent->addChild(child);
    child->setPosition({10.0f, 0.0f});
    child->addComponent<CircleCollider>(5.0f);
    Collider *childCol = child->getComponent<Collider>();
    childCol->setCollisionGroup(0b0010ULL);

    // Move parent and step simulation so physics world Grid+AABB reflects world position (110, 0).
    parent->setPosition({100.0f, 0.0f});
    runtime.simulateFrame(dt);
    require(!runtime.raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 50.0f).has_value(),
            "Pre-move local position must not leave phantom hit after syncing parent move");
    auto movedHit = runtime.raycast({95.0f, 0.0f}, {1.0f, 0.0f}, 30.0f);
    require(movedHit.has_value() && nearVec(movedHit->point, {105.0f, 0.0f}),
            "Raycast must observe synchronized parent->child world position");

    // Collision group bitmask filtering
    require(!runtime.raycast({95.0f, 0.0f}, {1.0f, 0.0f}, 30.0f, 0b0100ULL).has_value(),
            "Non-matching collisionMask must exclude collider");
    require(runtime.raycast({95.0f, 0.0f}, {1.0f, 0.0f}, 30.0f, 0b0010ULL).has_value(),
            "Matching collisionMask bit must include collider");
    require(!runtime.raycast({95.0f, 0.0f}, {1.0f, 0.0f}, 30.0f, 0ULL).has_value(),
            "Zero collisionMask must hit nothing");

    // Trigger filtering
    childCol->setIsTrigger(true);
    require(!runtime.raycast({95.0f, 0.0f}, {1.0f, 0.0f}, 30.0f, ~uint64_t{0}, false).has_value(),
            "Trigger collider must be ignored when includeTriggers == false");
    require(runtime.raycast({95.0f, 0.0f}, {1.0f, 0.0f}, 30.0f, ~uint64_t{0}, true).has_value(),
            "Trigger collider must be included when includeTriggers == true");
    childCol->setIsTrigger(false);

    // Inactive GameObject filtering
    child->setActive(false);
    require(!runtime.raycast({95.0f, 0.0f}, {1.0f, 0.0f}, 30.0f).has_value(),
            "Inactive GameObject collider must be excluded");
    child->setActive(true);
    require(runtime.raycast({95.0f, 0.0f}, {1.0f, 0.0f}, 30.0f).has_value(),
            "Reactivated GameObject collider must resume hitting");

    // Destroyed GameObject filtering
    runtime.destroyGameObject(child);
    require(!runtime.raycast({95.0f, 0.0f}, {1.0f, 0.0f}, 30.0f).has_value(),
            "Destroyed / marked-for-deletion GameObject must be excluded immediately");
}

void testInsideOriginTieBreakAndRaycastAll()
{
    platformator::Runtime runtime(physicsOnly());

    // Box centered at (0, 0) size 20x20 (x,y in [-10, 10])
    GameObject *insideBox = runtime.createGameObject()->setName("InsideBox");
    insideBox->addComponent<BoxCollider>(20.0f, 20.0f);

    // Origin strictly inside box & maxDistance == 0.0f -> distance=0, fraction=0, point=origin, normal=-dir
    const Eigen::Vector2f dir{3.0f, 4.0f};
    const Eigen::Vector2f expectedNormal = -dir.normalized();
    auto insideHit = runtime.raycast({2.0f, -3.0f}, dir, 0.0f);
    require(insideHit.has_value(), "Inside origin must hit even when maxDistance == 0");
    require(nearFloat(insideHit->distance, 0.0f) && nearFloat(insideHit->fraction, 0.0f) &&
                nearVec(insideHit->point, {2.0f, -3.0f}) && nearVec(insideHit->normal, expectedNormal),
            "Inside-origin hit must set distance=0, fraction=0, point=origin, normal=-normalizedDir");

    // Boundary origin on box edge (10, 0)
    auto edgeHit = runtime.raycast({10.0f, 0.0f}, {1.0f, 0.0f}, 5.0f);
    require(edgeHit.has_value() && nearFloat(edgeHit->distance, 0.0f) &&
                nearVec(edgeHit->point, {10.0f, 0.0f}) && nearVec(edgeHit->normal, {-1.0f, 0.0f}),
            "Boundary-origin hit must set distance=0, point=origin, normal=-normalizedDir");

    // Equal-distance tie-breaking: reorder colliderComponents via swap-and-pop so the higher-ID collider
    // is visited before the lower-ID collider, proving tie-break uses collider->getId() rather than list order.
    GameObject *slotDummy = runtime.createGameObject()->setName("SlotDummy");
    slotDummy->setPosition({500.0f, 500.0f});
    slotDummy->addComponent<BoxCollider>(10.0f, 10.0f);

    GameObject *tieLow = runtime.createGameObject()->setName("TieLow");
    tieLow->setPosition({100.0f, 0.0f});
    tieLow->addComponent<BoxCollider>(20.0f, 20.0f);
    Collider *colLow = tieLow->getComponent<Collider>();

    GameObject *tieHigh = runtime.createGameObject()->setName("TieHigh");
    tieHigh->setPosition({100.0f, 0.0f});
    tieHigh->addComponent<BoxCollider>(20.0f, 20.0f);
    Collider *colHigh = tieHigh->getComponent<Collider>();
    require(colLow->getId() < colHigh->getId(), "Sequential colliders must receive ascending IDs");

    runtime.destroyGameObject(slotDummy);
    runtime.simulateFrame(dt);

    // Further circle at (150, 0) radius 5 -> front face at x=145
    GameObject *farCircle = runtime.createGameObject()->setName("FarCircle");
    farCircle->setPosition({150.0f, 0.0f});
    farCircle->addComponent<CircleCollider>(5.0f);
    Collider *colFar = farCircle->getComponent<Collider>();

    auto closestTie = runtime.raycast({50.0f, 0.0f}, {1.0f, 0.0f}, 120.0f);
    require(closestTie.has_value() && closestTie->collider == colLow && nearFloat(closestTie->distance, 40.0f),
            "Equal-distance tie in raycast must pick lower collider->getId() deterministically");

    const auto allHits = runtime.raycastAll({50.0f, 0.0f}, {1.0f, 0.0f}, 120.0f);
    require(allHits.size() == 3, "raycastAll must return all 3 intersecting colliders along the ray");
    require(allHits[0].collider == colLow && allHits[1].collider == colHigh && allHits[2].collider == colFar,
            "raycastAll must sort by (distance, collider->getId()) ascending");
    require(nearFloat(allHits[0].distance, 40.0f) && nearFloat(allHits[1].distance, 40.0f) &&
                nearFloat(allHits[2].distance, 95.0f) && nearFloat(allHits[2].fraction, 95.0f / 120.0f),
            "raycastAll hit distances and fractions must be exact");
}

void testInvalidArgumentsAndZeroAllocations()
{
    platformator::Runtime runtime(physicsOnly());

    GameObject *boxObj = runtime.createGameObject()->setName("AllocBox");
    boxObj->setPosition({40.0f, 0.0f});
    boxObj->addComponent<BoxCollider>(10.0f, 10.0f);

    GameObject *circleObj = runtime.createGameObject()->setName("AllocCircle");
    circleObj->setPosition({80.0f, 20.0f});
    circleObj->addComponent<CircleCollider>(6.0f);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    requireThrowsInvalidArg([&] { (void)runtime.raycast({nan, 0.0f}, {1.0f, 0.0f}, 10.0f); },
                            "NaN origin must throw invalid_argument");
    requireThrowsInvalidArg([&] { (void)runtime.raycast({0.0f, 0.0f}, {inf, 0.0f}, 10.0f); },
                            "Inf direction must throw invalid_argument");
    requireThrowsInvalidArg([&] { (void)runtime.raycast({0.0f, 0.0f}, {0.0f, 0.0f}, 10.0f); },
                            "Zero direction must throw invalid_argument");
    requireThrowsInvalidArg([&] { (void)runtime.raycast({0.0f, 0.0f}, {1e-7f, 0.0f}, 10.0f); },
                            "Sub-epsilon direction must throw invalid_argument");
    requireThrowsInvalidArg([&] { (void)runtime.raycast({0.0f, 0.0f}, {1.0f, 0.0f}, -0.1f); },
                            "Negative maxDistance must throw invalid_argument");
    requireThrowsInvalidArg([&] { (void)runtime.raycastAll({0.0f, 0.0f}, {1.0f, 0.0f}, inf); },
                            "Infinite maxDistance in raycastAll must throw invalid_argument");

    platformator::RaycastHit outHit;
    gAllocationCount = 0;
    gCountAllocations = true;
    const bool hitTrue = runtime.raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 100.0f, outHit);
    const bool hitMiss = runtime.raycast({0.0f, 500.0f}, {1.0f, 0.0f}, 100.0f, outHit);
    const auto optHit = runtime.raycast({80.0f, 0.0f}, {0.0f, 1.0f}, 100.0f);
    const auto optMiss = runtime.raycast({0.0f, 0.0f}, {-1.0f, 0.0f}, 100.0f);
    gCountAllocations = false;

    require(hitTrue && !hitMiss && optHit.has_value() && !optMiss.has_value(),
            "Zero-alloc raycast verification hits/misses must match expected state");
    require(gAllocationCount == 0,
            "Runtime::raycast must perform zero heap allocations across hits and misses");
}

void testLargeDirectionNormalization()
{
    platformator::Runtime runtime(physicsOnly());

    GameObject *boxObj = runtime.createGameObject()->setName("NormBox");
    boxObj->setPosition({100.0f, 0.0f});
    boxObj->addComponent<BoxCollider>(20.0f, 20.0f);

    GameObject *circleObj = runtime.createGameObject()->setName("NormCircle");
    circleObj->setPosition({160.0f, 0.0f});
    circleObj->addComponent<CircleCollider>(10.0f);

    const auto unitHit = runtime.raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 200.0f);
    const auto hugeHit = runtime.raycast({0.0f, 0.0f}, {1e30f, 0.0f}, 200.0f);
    require(unitHit.has_value() && hugeHit.has_value(),
            "Huge finite direction (1e30, 0) must not overflow normalization");
    require(hugeHit->collider == unitHit->collider && nearFloat(hugeHit->distance, unitHit->distance) &&
                nearVec(hugeHit->point, unitHit->point) && nearVec(hugeHit->normal, unitHit->normal),
            "Huge direction and unit direction must produce identical closest-hit results");

    const auto unitAll = runtime.raycastAll({0.0f, 0.0f}, {1.0f, 0.0f}, 200.0f);
    const auto hugeAll = runtime.raycastAll({0.0f, 0.0f}, {1e30f, 0.0f}, 200.0f);
    require(unitAll.size() == 2 && hugeAll.size() == 2 &&
                hugeAll[0].collider == unitAll[0].collider && hugeAll[1].collider == unitAll[1].collider &&
                nearFloat(hugeAll[1].distance, unitAll[1].distance),
            "Huge direction and unit direction must produce identical raycastAll hits");

    const auto unitInside = runtime.raycast({100.0f, 0.0f}, {1.0f, 0.0f}, 20.0f);
    const auto hugeInside = runtime.raycast({100.0f, 0.0f}, {1e30f, 0.0f}, 20.0f);
    require(unitInside.has_value() && hugeInside.has_value() &&
                nearFloat(hugeInside->distance, 0.0f) && nearVec(hugeInside->normal, unitInside->normal),
            "Inside-origin normal must match between unit and huge finite direction");
}

void testDistantCirclePrecisionAndNearParallelSlab()
{
    platformator::Runtime runtime(physicsOnly());

    // Radius-1 circle 8192 units away along +X
    GameObject *farCircle = runtime.createGameObject()->setName("FarCircle8192");
    farCircle->setPosition({8192.0f, 0.0f});
    farCircle->addComponent<CircleCollider>(1.0f);

    const auto hit = runtime.raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 9000.0f);
    require(hit.has_value(), "Radius-1 circle at distance 8192 must hit");
    require(nearFloat(hit->distance, 8191.0f, 1e-3f) && nearVec(hit->point, {8191.0f, 0.0f}, 1e-3f) &&
                nearVec(hit->normal, {-1.0f, 0.0f}, 1e-4f),
            "Distant circle entry distance, point, and unit normal must remain accurate");

    const auto miss = runtime.raycast({0.0f, 1.05f}, {1.0f, 0.0f}, 9000.0f);
    require(!miss.has_value(), "Ray at y=1.05 past radius-1 circle at y=0, x=8192 must miss cleanly");

    // Near-parallel OBB slab entry check (Box 20x20 centered at (50, 0) -> y in [-10, 10])
    GameObject *slabBox = runtime.createGameObject()->setName("NearParallelBox");
    slabBox->setPosition({50.0f, 0.0f});
    slabBox->addComponent<BoxCollider>(20.0f, 20.0f);

    const auto tinyInsideHit = runtime.raycast({0.0f, 9.5f}, {1.0f, 1e-9f}, 100.0f);
    require(tinyInsideHit.has_value() && tinyInsideHit->collider == slabBox->getComponent<Collider>() &&
                nearFloat(tinyInsideHit->distance, 40.0f, 1e-3f),
            "Box raycast with tiny non-zero Y (1e-9f) starting inside Y slab must hit X entry");

    // Origin just above y=+10 slab with downward 1e-7f slope enters y <= 10 at t = 10 <= 40, hitting left face!
    const auto tinyEnterHit = runtime.raycast({0.0f, 10.0f + 1e-6f}, {1.0f, -1e-7f}, 100.0f);
    require(tinyEnterHit.has_value() && tinyEnterHit->collider == slabBox->getComponent<Collider>() &&
                nearFloat(tinyEnterHit->distance, 40.0f, 1e-3f),
            "Non-zero near-parallel ray component (-1e-7f) entering slab within segment must hit");

    // Origin above y=+10 slab right above box X span [40, 60] entering Y face via tiny slope -1e-6f at t=5
    const auto topSlabEnter = runtime.raycast({40.5f, 10.0f + 5e-6f}, {1.0f, -1e-6f}, 100.0f);
    require(!topSlabEnter.has_value() || topSlabEnter->collider == slabBox->getComponent<Collider>(),
            "Valid entry guard");
    require(topSlabEnter.has_value() && nearVec(topSlabEnter->normal, {0.0f, 1.0f}, 1e-3f),
            "Tiny negative Y component (-1e-6f) entering top box face within segment must hit top face");
}

void testBoundedSpatialTraversalAndSameFrameMovement()
{
    platformator::Runtime runtime(physicsOnly());

    std::vector<GameObject *> objects;
    objects.reserve(400);
    for (int gy = 0; gy < 16; ++gy)
    {
        for (int gx = 0; gx < 25; ++gx)
        {
            GameObject *obj = runtime.createGameObject();
            obj->setPosition({static_cast<float>(gx) * 250.0f + 100.0f,
                              static_cast<float>(gy) * 250.0f + 100.0f});
            obj->addComponent<BoxCollider>(16.0f, 16.0f);
            objects.push_back(obj);
        }
    }
    runtime.simulateFrame(dt);

#if PLATFORMATOR_ENABLE_WORK_PROBES
    platformator_test::work = {};
#endif
    platformator::RaycastHit shortHit;
    gAllocationCount = 0;
    gCountAllocations = true;
    const bool ok = runtime.raycast({50.0f, 100.0f}, {1.0f, 0.0f}, 150.0f, shortHit);
    gCountAllocations = false;

    require(ok && nearFloat(shortHit.distance, 42.0f),
            "Bounded spatial raycast must hit first cell box cleanly");
    require(gAllocationCount == 0,
            "Bounded spatial raycast must allocate zero heap memory");
#if PLATFORMATOR_ENABLE_WORK_PROBES
    require(platformator_test::work.raycastCandidateVisits > 0 &&
                platformator_test::work.raycastCandidateVisits < 25,
            "Bounded spatial traversal must skip distant colliders (visits=" +
                std::to_string(platformator_test::work.raycastCandidateVisits) + " out of 400)");
#endif

    // Movement of a distant synced collider followed by physics sync:
    GameObject *distantObj = objects.back();
    const Eigen::Vector2f oldPos = distantObj->getLocalPosition();
    distantObj->setPosition({70.0f, 100.0f});
    runtime.simulateFrame(dt);

    require(!runtime.raycast({oldPos.x() - 30.0f, oldPos.y()}, {1.0f, 0.0f}, 60.0f).has_value(),
            "Moved collider must not produce a stale phantom hit at its previous cell");
    const auto movedHit = runtime.raycast({50.0f, 100.0f}, {1.0f, 0.0f}, 150.0f);
    require(movedHit.has_value() && movedHit->gameObject == distantObj && nearFloat(movedHit->distance, 12.0f),
            "Bounded traversal must find collider at its new synced position");
}

void testConcurrentRaycastsAndZeroPositionWrites()
{
    platformator::Runtime runtime(physicsOnly());

    for (int i = 0; i < 120; ++i)
    {
        GameObject *obj = runtime.createGameObject();
        obj->setPosition({static_cast<float>(i % 20) * 120.0f, static_cast<float>(i / 20) * 120.0f});
        if (i % 2 == 0)
            obj->addComponent<BoxCollider>(18.0f, 18.0f);
        else
            obj->addComponent<CircleCollider>(9.0f);
    }
    runtime.simulateFrame(dt);

    struct QuerySpec
    {
        Eigen::Vector2f origin;
        Eigen::Vector2f dir;
        float maxDist;
    };
    constexpr std::size_t kQueries = 256;
    std::vector<QuerySpec> queries(kQueries);
    for (std::size_t q = 0; q < kQueries; ++q)
    {
        const float angle = static_cast<float>(q) * 0.137f;
        queries[q] = {
            Eigen::Vector2f{static_cast<float>((q * 53) % 2000), static_cast<float>((q * 29) % 700)},
            Eigen::Vector2f{std::cos(angle), std::sin(angle)},
            500.0f};
    }

    std::vector<std::optional<platformator::RaycastHit>> serialSingle(kQueries);
    std::vector<std::vector<platformator::RaycastHit>> serialAll(kQueries);
    for (std::size_t q = 0; q < kQueries; ++q)
    {
        serialSingle[q] = runtime.raycast(queries[q].origin, queries[q].dir, queries[q].maxDist);
        serialAll[q] = runtime.raycastAll(queries[q].origin, queries[q].dir, queries[q].maxDist);
    }

#if PLATFORMATOR_ENABLE_WORK_PROBES
    platformator_test::work = {};
#endif
    std::vector<std::optional<platformator::RaycastHit>> parallelSingle(kQueries);
    std::vector<std::vector<platformator::RaycastHit>> parallelAll(kQueries);
    tbb::parallel_for(std::size_t(0), kQueries, [&](std::size_t q) {
        parallelSingle[q] = runtime.raycast(queries[q].origin, queries[q].dir, queries[q].maxDist);
        parallelAll[q] = runtime.raycastAll(queries[q].origin, queries[q].dir, queries[q].maxDist);
    });

    for (std::size_t q = 0; q < kQueries; ++q)
    {
        require(serialSingle[q].has_value() == parallelSingle[q].has_value(),
                "Concurrent single raycast hit presence must match serial oracle");
        if (serialSingle[q].has_value())
        {
            require(serialSingle[q]->collider == parallelSingle[q]->collider &&
                        nearFloat(serialSingle[q]->distance, parallelSingle[q]->distance, 1e-6f) &&
                        nearVec(serialSingle[q]->point, parallelSingle[q]->point, 1e-6f) &&
                        nearVec(serialSingle[q]->normal, parallelSingle[q]->normal, 1e-6f),
                    "Concurrent single raycast hit values must match serial oracle");
        }
        require(serialAll[q].size() == parallelAll[q].size(),
                "Concurrent raycastAll hit counts must match serial oracle");
        for (std::size_t h = 0; h < serialAll[q].size(); ++h)
        {
            require(serialAll[q][h].collider == parallelAll[q][h].collider &&
                        nearFloat(serialAll[q][h].distance, parallelAll[q][h].distance, 1e-6f),
                    "Concurrent raycastAll ordering and distances must match serial oracle");
        }
    }
#if PLATFORMATOR_ENABLE_WORK_PROBES
    require(platformator_test::work.positionReadVisits == 0,
            "Concurrent raycasts must never touch non-atomic GameObject::positionReadVisits");
#endif
}

void testGridAabbChunksMultiCellAndLongEmptyRay()
{
    platformator::Runtime runtime(physicsOnly());

    // Place 60 boxes in the same GridCell (0, 0) along Y to force SegmentedIntervalList chunk splitting (MAX_CHUNK_SIZE=32)
    // and checkpoints, plus one multi-cell wall spanning multiple GridCells.
    for (int i = 0; i < 60; ++i)
    {
        GameObject *box = runtime.createGameObject();
        box->setPosition({20.0f + static_cast<float>(i % 10) * 14.0f, 10.0f + static_cast<float>(i) * 3.0f});
        box->addComponent<BoxCollider>(8.0f, 2.5f);
    }

    GameObject *wideWall = runtime.createGameObject();
    wideWall->setPosition({400.0f, 95.0f});
    wideWall->addComponent<BoxCollider>(500.0f, 40.0f);
    auto *wideCollider = wideWall->getComponent<BoxCollider>();

    runtime.simulateFrame(dt);

    // 1. Long miss completely outside active Grid AABB must prune before traversing cells (0 candidate visits).
#if PLATFORMATOR_ENABLE_WORK_PROBES
    platformator_test::work = {};
#endif
    require(!runtime.raycast({-5000.0f, -2000.0f}, {1.0f, 0.0f}, 1e6f).has_value(),
            "Long ray missing active Grid AABB must miss");
#if PLATFORMATOR_ENABLE_WORK_PROBES
    require(platformator_test::work.raycastCandidateVisits == 0,
            "Long ray missing active Grid AABB must visit 0 colliders (visits=" +
                std::to_string(platformator_test::work.raycastCandidateVisits) + ")");
#endif

    // 2. Long ray from far outside Grid entering at X=0 along Y=95 must hit chunked boxes and deduplicate multi-cell wall.
    const auto allAlong = runtime.raycastAll({-5000.0f, 95.0f}, {1.0f, 0.0f}, 1e6f);
    std::size_t wideCount = 0;
    for (const auto &h : allAlong)
    {
        if (h.collider == wideCollider)
            ++wideCount;
    }
    require(wideCount == 1, "Multi-cell collider crossed by one ray must appear once in raycastAll");
    require(!allAlong.empty(), "Long ray entering Grid AABB must still hit aligned cell colliders");

    gAllocationCount = 0;
    gCountAllocations = true;
    const auto singleWide = runtime.raycast({170.0f, 95.0f}, {-1.0f, 0.0f}, 300.0f);
    gCountAllocations = false;
    require(gAllocationCount == 0, "Grid+AABB raycast through multi-cell and chunked cell must allocate zero heap memory");
    require(singleWide.has_value(), "Ray through multi-chunk cell must hit an aligned box cleanly");
}
} // namespace

int main()
{
    headlessEnvironment();
    try
    {
        testAabbRotatedScaledOffsetBoxesAndCircles();
        testImmediateTransformsMasksTriggersAndLifecycle();
        testInsideOriginTieBreakAndRaycastAll();
        testInvalidArgumentsAndZeroAllocations();
        testLargeDirectionNormalization();
        testDistantCirclePrecisionAndNearParallelSlab();
        testBoundedSpatialTraversalAndSameFrameMovement();
        testConcurrentRaycastsAndZeroPositionWrites();
        testGridAabbChunksMultiCellAndLongEmptyRay();
        std::cout << "[PASS] raycast tests\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        gCountAllocations = false;
        std::cerr << "[FAIL] raycast tests: " << error.what() << '\n';
        return 1;
    }
}
