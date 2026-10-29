#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <platformator/camera.h>
#include <platformator/runtime.h>
#include <platformator/sprite.h>
#include <platformator/texturewrapper.h>

#include "runtime_work_probe.h"

namespace render_culling_test
{
// Cross-runtime identity. Sprite and GameObject addresses are deliberately not
// used as comparison keys.
struct SpriteRecord
{
    platformator_test::SpriteIdentity identity;
    GameObject *object = nullptr;
    Sprite *sprite = nullptr;
};

enum class FixtureTexture : size_t
{
    BlackSquare = 0,
    RedCircle = 1,
    Missing = 2,
    Null = 3
};

enum class FixtureTrajectoryProfile
{
    Visible,
    Offscreen,
    Crossing,
    CameraPan,
    Churn,
    Sparse,
    Clustered,
    Rotated
};

struct FixturePose
{
    float x = 0.0f;
    float y = 0.0f;
    float rotationRadians = 0.0f;
    uint32_t generation = 0;
};

class DeterministicFixtureTrajectory
{
  public:
    DeterministicFixtureTrajectory(FixtureTrajectoryProfile profile, size_t population, uint32_t seed)
        : profile_(profile), population_(population), seed_(seed)
    {
    }

    FixturePose pose(size_t slot, size_t step) const
    {
        const double tau = 6.283185307179586476925286766559;
        const double phaseOffset = unit(slot, 0xA341316Cu) * tau;
        const double stepPhase = tau * static_cast<double>(step % 240) / 240.0;
        FixturePose result;
        result.generation = generationForSlot(slot, step);
        switch (profile_)
        {
        case FixtureTrajectoryProfile::Visible:
            result.x = static_cast<float>(20.0 + 280.0 * unit(slot, 0xD1B54A35u));
            result.y = static_cast<float>(20.0 + 200.0 * unit(slot, 0x94D049BBu));
            break;
        case FixtureTrajectoryProfile::Offscreen:
            result.x = slot % 2 == 0 ? -420.0f : 740.0f;
            result.y = slot % 3 == 0 ? -340.0f : 560.0f;
            break;
        case FixtureTrajectoryProfile::Crossing:
        {
            const double xAmplitude = 190.0 + 50.0 * unit(slot, 0xC8013EA4u);
            const double yAmplitude = 140.0 + 45.0 * unit(slot, 0xAD90777Du);
            result.x = static_cast<float>(160.0 + xAmplitude * std::sin(stepPhase + phaseOffset));
            result.y = static_cast<float>(120.0 + yAmplitude * std::sin(stepPhase + phaseOffset * 1.37));
            break;
        }
        case FixtureTrajectoryProfile::CameraPan:
            result.x = static_cast<float>(unit(slot, 0x7E95761Eu) * 2400.0);
            result.y = static_cast<float>(unit(slot, 0x4CF5AD43u) * 1600.0);
            break;
        case FixtureTrajectoryProfile::Churn:
            result.x = static_cast<float>((result.generation & 1u) == 0 ? 40.0 + unit(slot, 0xD1B54A35u) * 230.0
                                                                        : 410.0 + unit(slot, 0xD1B54A35u) * 190.0);
            result.y = static_cast<float>(20.0 + unit(slot, 0x94D049BBu) * 200.0);
            break;
        case FixtureTrajectoryProfile::Sparse:
            result.x = slot % 10 == 0 ? static_cast<float>(20.0 + unit(slot, 0x369DEA0Fu) * 280.0)
                                      : static_cast<float>(380.0 + unit(slot, 0xDB4F0B91u) * 1800.0);
            result.y = static_cast<float>(20.0 + unit(slot, 0xBBE05633u) * 200.0);
            break;
        case FixtureTrajectoryProfile::Clustered:
            if (slot % 10 == 0)
            {
                const bool secondCluster = (slot / 10) % 2 != 0;
                result.x = static_cast<float>((secondCluster ? 220.0 : 55.0) + unit(slot, 0xA0F2EC75u) * 36.0);
                result.y = static_cast<float>((secondCluster ? 155.0 : 45.0) + unit(slot, 0x89E18285u) * 36.0);
            }
            else
            {
                result.x = static_cast<float>(410.0 + unit(slot, 0x7C3A2D13u) * 1600.0);
                result.y = static_cast<float>(unit(slot, 0xC6EF3720u) * 1100.0);
            }
            break;
        case FixtureTrajectoryProfile::Rotated:
            result.x = slot % 10 == 0 ? static_cast<float>(35.0 + unit(slot, 0xB5297A4Du) * 250.0)
                                      : static_cast<float>(390.0 + unit(slot, 0x68E31DA4u) * 1200.0);
            result.y = static_cast<float>(20.0 + unit(slot, 0x1B56C4E9u) * 200.0);
            result.rotationRadians = static_cast<float>((unit(slot, 0xAEF17502u) * 2.0 - 1.0) * 3.141592653589793);
            break;
        }
        return result;
    }

    std::pair<float, float> cameraPosition(size_t step) const
    {
        if (profile_ != FixtureTrajectoryProfile::CameraPan)
            return {0.0f, 0.0f};
        const double phase = 6.283185307179586476925286766559 * static_cast<double>(step % 240) / 240.0;
        return {static_cast<float>(840.0 + 720.0 * std::sin(phase)),
                static_cast<float>(560.0 + 440.0 * std::cos(phase))};
    }

    size_t replacementsPerStep() const
    {
        return std::max<size_t>(1, population_ / 100);
    }

    uint32_t generationForSlot(size_t slot, size_t step) const
    {
        if (profile_ != FixtureTrajectoryProfile::Churn || population_ == 0)
            return 0;
        const size_t replaced = step * replacementsPerStep();
        const size_t completeCycles = replaced / population_;
        const size_t remainder = replaced % population_;
        return static_cast<uint32_t>(completeCycles + (slot < remainder ? 1 : 0));
    }

    template <typename Callback> void forEachReplacedSlot(size_t step, Callback &&callback) const
    {
        if (profile_ != FixtureTrajectoryProfile::Churn || population_ == 0 || step == 0)
            return;
        const size_t count = replacementsPerStep();
        const size_t first = ((step - 1) * count) % population_;
        for (size_t offset = 0; offset < count; ++offset)
        {
            const size_t slot = (first + offset) % population_;
            callback(slot, generationForSlot(slot, step));
        }
    }

    size_t population() const
    {
        return population_;
    }

  private:
    FixtureTrajectoryProfile profile_;
    size_t population_;
    uint32_t seed_;

    double unit(size_t slot, uint32_t salt) const
    {
        uint32_t value = seed_ ^ static_cast<uint32_t>(slot * 0x9E3779B9u) ^ salt;
        value ^= value >> 16;
        value *= 0x7FEB352Du;
        value ^= value >> 15;
        value *= 0x846CA68Bu;
        value ^= value >> 16;
        return static_cast<double>(value) / static_cast<double>(std::numeric_limits<uint32_t>::max());
    }
};

class RenderCullingFixture
{
  public:
    RenderCullingFixture(int outputWidth = 320, int outputHeight = 240, float cameraWidth = 320.0f,
                         float cameraHeight = 240.0f, bool enableCulling = true, bool headless = false,
                         const std::filesystem::path &assetRoot = {})
        : runtime_(makeOptions(outputWidth, outputHeight, enableCulling, headless, assetRoot)), camera_(nullptr),
          records_(), identities_(), headless_(headless)
    {
        createCamera(cameraWidth, cameraHeight, 0.0f, 0.0f);
        records_.reserve(64);
        identities_.reserve(64);
    }

    RenderCullingFixture(const RenderCullingFixture &) = delete;
    RenderCullingFixture &operator=(const RenderCullingFixture &) = delete;

    platformator::Runtime &runtime()
    {
        return runtime_;
    }

    Camera *camera() const
    {
        return camera_;
    }

    SpriteRecord &record(size_t slot)
    {
        if (slot >= records_.size() || records_[slot].identity.slot != slot || records_[slot].object == nullptr)
            throw std::out_of_range("Render culling fixture has no live sprite in that slot");
        return records_[slot];
    }

    TextureWrapper *texture(FixtureTexture texture)
    {
        return textureFor(texture);
    }

    void replaceCamera(float width, float height, float x = 0.0f, float y = 0.0f)
    {
        if (camera_ != nullptr && camera_->getGameObject() != nullptr)
            camera_->getGameObject()->removeComponent(CAMERA);
        createCamera(width, height, x, y);
    }

    void removeCamera()
    {
        if (camera_ != nullptr && camera_->getGameObject() != nullptr)
            camera_->getGameObject()->removeComponent(CAMERA);
        camera_ = nullptr;
    }

    void clearSceneAndCreateCamera(float width, float height)
    {
        identities_.clear();
        records_.clear();
        runtime_.clearScene();
        camera_ = nullptr;
        createCamera(width, height, 0.0f, 0.0f);
    }

    void loadSceneAndCreateCamera(const std::string &scenePath, float width, float height)
    {
        identities_.clear();
        records_.clear();
        runtime_.clearScene();
        runtime_.loadScene(scenePath);
        camera_ = nullptr;
        createCamera(width, height, 0.0f, 0.0f);
    }

    SpriteRecord &addSprite(size_t slot, uint32_t generation, float x, float y, float width, float height,
                            FixtureTexture texture = FixtureTexture::BlackSquare,
                            SDL_FlipMode flip = SDL_FLIP_NONE, bool active = true, float rotationRadians = 0.0f,
                            const Eigen::Vector2f &scale = Eigen::Vector2f::Ones())
    {
        GameObject *object = runtime_.createGameObject();
        object->setName("Render Culling Sprite " + std::to_string(slot));
        object->setPosition({x, y});
        object->setActive(active);
        object->setRotation(rotationRadians);
        object->setScale(scale);
        object->addComponent<Sprite>(headless_ ? nullptr : textureFor(texture), flip, width, height);
        Sprite *sprite = object->getComponent<Sprite>();
        if (sprite == nullptr)
        {
            throw std::runtime_error("Render culling fixture could not create a sprite");
        }

        records_.push_back({{slot, generation}, object, sprite});
        identities_.emplace(sprite, platformator_test::SpriteIdentity{slot, generation});
        return records_.back();
    }

    SpriteRecord &replaceSprite(size_t slot, uint32_t generation, float x, float y, float width, float height,
                                FixtureTexture texture = FixtureTexture::BlackSquare,
                                SDL_FlipMode flip = SDL_FLIP_NONE, float rotationRadians = 0.0f,
                                const Eigen::Vector2f &scale = Eigen::Vector2f::Ones())
    {
        if (slot >= records_.size() || records_[slot].identity.slot != slot)
            throw std::out_of_range("Render culling fixture replacement requires its existing slot");

        SpriteRecord &record = records_[slot];
        identities_.erase(record.sprite);
        runtime_.destroyGameObject(record.object);

        GameObject *object = runtime_.createGameObject();
        object->setName("Render Culling Sprite " + std::to_string(slot));
        object->setPosition({x, y});
        object->setRotation(rotationRadians);
        object->setScale(scale);
        object->addComponent<Sprite>(headless_ ? nullptr : textureFor(texture), flip, width, height);
        Sprite *sprite = object->getComponent<Sprite>();
        record = {{slot, generation}, object, sprite};
        identities_.emplace(sprite, record.identity);
        return record;
    }

    void reserveSprites(size_t count)
    {
        records_.reserve(count);
        identities_.reserve(count);
    }

    void forgetSprite(Sprite *sprite)
    {
        identities_.erase(sprite);
    }

    void removeSprite(size_t slot)
    {
        SpriteRecord &record = this->record(slot);
        identities_.erase(record.sprite);
        runtime_.destroyGameObject(record.object);
        record = {};
    }

    SpriteRecord &replaceSpriteComponent(size_t slot, uint32_t generation, FixtureTexture texture,
                                         float width, float height, SDL_FlipMode flip = SDL_FLIP_NONE)
    {
        SpriteRecord &record = this->record(slot);
        identities_.erase(record.sprite);
        record.object->addComponent<Sprite>(headless_ ? nullptr : textureFor(texture), flip, width, height);
        record.sprite = record.object->getComponent<Sprite>();
        record.identity.generation = generation;
        identities_.emplace(record.sprite, record.identity);
        return record;
    }

    const std::vector<SpriteRecord> &records() const
    {
        return records_;
    }

    bool resolveIdentity(const Sprite *sprite, platformator_test::SpriteIdentity &identity) const
    {
        const auto found = identities_.find(sprite);
        if (found == identities_.end())
        {
            return false;
        }
        identity = found->second;
        return true;
    }

    static bool resolveIdentityCallback(const Sprite *sprite, platformator_test::SpriteIdentity &identity,
                                        void *context)
    {
        if (context == nullptr)
        {
            return false;
        }
        return static_cast<const RenderCullingFixture *>(context)->resolveIdentity(sprite, identity);
    }

  private:
    platformator::Runtime runtime_;
    Camera *camera_;
    std::vector<SpriteRecord> records_;
    std::unordered_map<const Sprite *, platformator_test::SpriteIdentity> identities_;
    bool headless_;

    void createCamera(float width, float height, float x, float y)
    {
        GameObject *cameraObject = runtime_.createGameObject()->setName("Render Culling Camera");
        cameraObject->addComponent<Camera>(width, height);
        camera_ = cameraObject->getComponent<Camera>();
        if (camera_ == nullptr)
            throw std::runtime_error("Render culling fixture could not create its camera");
        camera_->setCamera(SDL_FRect{x, y, width, height});
    }

    template <typename Settings> static void setCullingIfSupported(Settings &settings, bool enabled)
    {
        if constexpr (requires(Settings &value) { value.enableRenderCulling = enabled; })
        {
            settings.enableRenderCulling = enabled;
        }
    }

    static RuntimeOptions makeOptions(int outputWidth, int outputHeight, bool enableCulling, bool headless,
                                      const std::filesystem::path &assetRoot)
    {
        if (outputWidth <= 0 || outputHeight <= 0)
        {
            throw std::invalid_argument("Render culling fixture output dimensions must be positive");
        }
        RuntimeOptions options;
        options.windowSettings.width = outputWidth;
        options.windowSettings.height = outputHeight;
        options.windowSettings.headless = headless;
        setCullingIfSupported(options.windowSettings, enableCulling);
        options.assetRoot = assetRoot;
        return options;
    }

    TextureWrapper *textureFor(FixtureTexture texture)
    {
        static constexpr std::array<const char *, 3> paths = {
            "assets/textures/black_square.png", "assets/textures/red_circle.png", "assets/textures/missing.png"};
        const size_t index = static_cast<size_t>(texture);
        if (texture == FixtureTexture::Null)
            return nullptr;
        if (index >= paths.size())
        {
            throw std::invalid_argument("Unknown render culling fixture texture");
        }
        // Runtime::loadTexture returns a borrowed wrapper whose lifetime is
        // governed by Sprite references. Do not retain it across replacement,
        // removal, or scene-clear operations in the fixture.
        return runtime_.loadTexture(paths[index]);
    }
};

} // namespace render_culling_test
