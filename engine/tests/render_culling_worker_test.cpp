#include "render_culling_fixture.h"

#include <SDL3/SDL_surface.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <oneapi/tbb/global_control.h>
#include <platformator/boxcollider.h>
#include <platformator/rigidbody.h>
#include <platformator/scriptcomponent.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "runtime_work_probe.h"
#include "test_support.h"

namespace
{
using render_culling_test::FixtureTexture;
using render_culling_test::RenderCullingFixture;
using render_culling_test::SpriteRecord;
using platformator_test::SpriteIdentity;

class ScopedStrategy
{
  public:
    explicit ScopedStrategy(const char *mode)
    {
        if (const char *current = std::getenv("PLATFORMATOR_RENDER_CULLING_MODE"))
        {
            hadPrevious_ = true;
            previous_ = current;
        }
        if (setenv("PLATFORMATOR_RENDER_CULLING_MODE", mode, 1) != 0)
            throw std::runtime_error("Could not set render-culling worker-test mode");
    }
    ~ScopedStrategy()
    {
        if (hadPrevious_)
            setenv("PLATFORMATOR_RENDER_CULLING_MODE", previous_.c_str(), 1);
        else
            unsetenv("PLATFORMATOR_RENDER_CULLING_MODE");
    }

  private:
    bool hadPrevious_ = false;
    std::string previous_;
};

struct LateEdit final : test_support::Behavior
{
    bool armed = false;
    bool applied = false;
    void lateUpdate(double) override
    {
        if (!armed || applied)
            return;
        applied = true;
        auto *object = getGameObject();
        object->setPosition(object->getPosition() + Eigen::Vector2f{3.0f, -2.0f});
        object->setRotation(object->getRotation() + 0.31f);
        object->getComponent<Sprite>()->setWidth(23.0f);
        object->getComponent<Sprite>()->setHeight(18.0f);
        object->getComponent<Sprite>()->setFlip(SDL_FLIP_HORIZONTAL);
    }
};

struct RenderCapture
{
    uint64_t imageHash = 14695981039346656037ull;
    int width = 0;
    int height = 0;
    static void beforePresent(SDL_Renderer *renderer, int width, int height, void *context)
    {
        auto *capture = static_cast<RenderCapture *>(context);
        capture->width = width;
        capture->height = height;
        SDL_Surface *raw = SDL_RenderReadPixels(renderer, nullptr);
        if (raw == nullptr)
            throw std::runtime_error(std::string("Worker readback failed: ") + SDL_GetError());
        SDL_Surface *pixels = raw;
        if (raw->format != SDL_PIXELFORMAT_RGBA32)
        {
            pixels = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(raw);
            if (pixels == nullptr)
                throw std::runtime_error(std::string("Worker readback conversion failed: ") + SDL_GetError());
        }
        if (pixels->w != width || pixels->h != height)
        {
            SDL_DestroySurface(pixels);
            throw std::runtime_error("Worker readback dimensions changed unexpectedly");
        }
        if (SDL_MUSTLOCK(pixels) && !SDL_LockSurface(pixels))
        {
            SDL_DestroySurface(pixels);
            throw std::runtime_error(std::string("Worker readback lock failed: ") + SDL_GetError());
        }
        const auto *bytes = static_cast<const uint8_t *>(pixels->pixels);
        for (int row = 0; row < height; ++row)
            for (int column = 0; column < width * 4; ++column)
            {
                capture->imageHash ^= bytes[static_cast<size_t>(row) * pixels->pitch + column];
                capture->imageHash *= 1099511628211ull;
            }
        if (SDL_MUSTLOCK(pixels))
            SDL_UnlockSurface(pixels);
        SDL_DestroySurface(pixels);
    }
};

struct BodyState
{
    Eigen::Vector2f position;
    float rotation = 0.0f;
    std::array<Eigen::Vector2f, 4> colliderVertices;
};

struct Result
{
    RenderCapture image;
    std::vector<SpriteIdentity> submissions;
    std::vector<BodyState> bodies;
    platformator_test::WorkCounters work;
    size_t callbackOffMainThread = 0;
};

struct IdentityContext
{
    RenderCullingFixture *fixture = nullptr;
    std::thread::id owner;
    bool resolvedOffOwner = false;
};

bool resolveIdentity(const Sprite *sprite, SpriteIdentity &identity, void *context)
{
    auto *observation = static_cast<IdentityContext *>(context);
    observation->resolvedOffOwner |= std::this_thread::get_id() != observation->owner;
    return observation->fixture->resolveIdentity(sprite, identity);
}

Result runWorkerFrame(size_t workers, const char *mode)
{
    ScopedStrategy strategy(mode);
    test_support::headlessEnvironment();
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    RenderCullingFixture fixture(320, 240, 320.0f, 240.0f, true);
    constexpr size_t population = 48;
    fixture.reserveSprites(population);
    std::vector<Rigidbody *> bodies;
    bodies.reserve(population);
    for (size_t index = 0; index < population; ++index)
    {
        const float x = index == 3 ? 100000000.0f : 35.0f + static_cast<float>(index) * 48.0f;
        auto &record = fixture.addSprite(index, 0, x, 70.0f + static_cast<float>(index % 4) * 42.0f,
                                         16.0f, 14.0f,
                                         index % 2 == 0 ? FixtureTexture::BlackSquare : FixtureTexture::RedCircle);
        record.object->addComponent<BoxCollider>(18.0f, 16.0f);
        bodies.push_back(record.object->addComponent<Rigidbody>(DYNAMIC, false)->getComponent<Rigidbody>());
    }
    fixture.record(4).object->addComponent<ScriptComponent>();
    auto *lateEdit = fixture.record(4).object->getComponent<ScriptComponent>()->addBehavior(new LateEdit);
    auto *late = dynamic_cast<LateEdit *>(lateEdit);
    test_support::require(late != nullptr, "Could not attach the late-update render mutation");

    // Prepare the cache before physics starts publishing worker results.
    test_support::require(fixture.runtime().simulateAndRenderFrame(test_support::dt),
                          "Worker fixture warmup frame did not render");
    late->armed = true;
    for (size_t index = 0; index < population; ++index)
    {
        if (index == 1)
        {
            bodies[index]->setAngularVelocity(2.25f); // Pure rotation.
        }
        else if (index == 3)
        {
            bodies[index]->setVelocity({1.0f, 0.0f}); // Translation rounds away at this world coordinate.
            bodies[index]->setAngularVelocity(0.8f);
        }
        else
        {
            bodies[index]->setVelocity({8.0f + static_cast<float>(index % 5), 0.0f});
            if (index == 2)
                bodies[index]->setAngularVelocity(-1.7f); // Combined translation and rotation.
        }
    }

    Result result;
    result.submissions.reserve(population);
    platformator_test::RenderObservation observation;
    IdentityContext identityContext{&fixture, std::this_thread::get_id(), false};
    observation.identityContext = &identityContext;
    observation.resolveIdentity = &resolveIdentity;
    observation.submissions = &result.submissions;
    observation.readbackContext = &result.image;
    observation.beforePresent = &RenderCapture::beforePresent;
    {
        platformator_test::ScopedRenderObservation scope(observation);
        platformator_test::work = {};
        test_support::require(fixture.runtime().simulateAndRenderFrame(test_support::dt),
                              "Worker physics frame did not render");
        result.work = platformator_test::work;
    }
    test_support::require(observation.unresolvedSprites == 0 && observation.submissionOverflows == 0,
                          "Worker render trace lost a sprite identity or exceeded reserved capacity");
    result.callbackOffMainThread = identityContext.resolvedOffOwner ? 1 : 0;
    test_support::require(result.callbackOffMainThread == 0,
                          "Render observer callbacks must run on the owning thread after worker publication");
    test_support::require(result.image.width == 320 && result.image.height == 240,
                          "Worker render did not capture the complete output");
    for (const SpriteRecord &record : fixture.records())
    {
        if (record.object == nullptr)
            continue;
        BodyState state;
        state.position = record.object->getPosition();
        state.rotation = record.object->getRotation();
        const auto *collider = record.object->getComponent<BoxCollider>();
        state.colliderVertices = collider->getVertices();
        result.bodies.push_back(state);
    }
    test_support::require(late->applied, "LateUpdate did not apply the same-step geometry edit");
    if (std::strcmp(mode, "indexed") == 0)
    {
        test_support::require(result.work.renderCullingDirtyEnqueues == population &&
                                  result.work.renderCullingBoundsRefreshes == population,
                              "Worker movement and late edits must publish once per affected Sprite after worker join");
        test_support::require(result.work.renderCullingCandidateVisits == population &&
                                  result.work.renderCullingQueries == 0,
                              "Worker-updated indexed bounds must update only dirty visible memberships without a camera query");
    }
    return result;
}

void compare(const Result &reference, const Result &actual, const std::string &name)
{
    test_support::require(reference.image.width == actual.image.width && reference.image.height == actual.image.height &&
                              reference.image.imageHash == actual.image.imageHash,
                          name + ": worker counts or cache strategy changed same-frame rendered pixels");
    test_support::require(reference.submissions == actual.submissions,
                          name + ": worker counts or cache strategy changed ordered submissions");
    test_support::require(reference.bodies.size() == actual.bodies.size(), name + ": body populations differ");
    for (size_t index = 0; index < reference.bodies.size(); ++index)
    {
        const auto &a = reference.bodies[index];
        const auto &b = actual.bodies[index];
        test_support::require(a.position == b.position && a.rotation == b.rotation,
                              name + ": worker count changed rigid-body position or rotation");
        for (size_t vertex = 0; vertex < a.colliderVertices.size(); ++vertex)
            test_support::require(a.colliderVertices[vertex] == b.colliderVertices[vertex],
                                  name + ": worker count changed synchronized collider geometry");
    }
}
} // namespace

int main()
{
#if !PLATFORMATOR_ENABLE_WORK_PROBES
    std::cout << "[SKIP] render_culling_worker_test requires PLATFORMATOR_ENABLE_WORK_PROBES=ON\n";
    return 77;
#else
    try
    {
        const size_t physicalWorkers = std::max<size_t>(2, std::thread::hardware_concurrency());
        const Result single = runWorkerFrame(1, "indexed");
        const Result parallel = runWorkerFrame(physicalWorkers, "indexed");
        const Result currentLinear = runWorkerFrame(physicalWorkers, "current-linear");
        compare(single, parallel, "single versus multiworker indexed");
        compare(currentLinear, parallel, "current-linear versus indexed multiworker");
        std::cout << "[PASS] render_culling_worker_test workers=1/" << physicalWorkers
                  << " moved_records=" << parallel.work.renderCullingBoundsRefreshes
                  << " worker_enqueues=" << parallel.work.renderCullingDirtyEnqueues
                  << " same_frame_pixels=verified collider_geometry=verified late_update=verified\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "[FAIL] render_culling_worker_test: " << exception.what() << '\n';
        return 1;
    }
#endif
}
