#pragma once

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <SDL3/SDL.h>
#include <platformator/camera.h>
#include <platformator/assetreference.h>
#include <platformator/behavior.h>
#include <platformator/objectreference.h>
#include <platformator/scriptregistration.h>

struct CatalogOpaqueValue
{
    int value = 0;
};

inline void to_json(nlohmann::json &json, const CatalogOpaqueValue &value)
{
    json = value.value;
}

inline void from_json(const nlohmann::json &json, CatalogOpaqueValue &value)
{
    value.value = json.get<int>();
}

class TypedCatalogFixture final : public Behavior
{
  public:
    TypedCatalogFixture();
    static int constructor_count() noexcept;

  private:
    bool enabled = false;
    std::int64_t count = 0;
    std::vector<int> samples;
    Eigen::Vector2f origin = Eigen::Vector2f::Zero();
    std::array<unsigned, 3> fixedSamples{};
    std::optional<float> optionalScale;
    std::vector<std::optional<float>> nestedValues;
    std::string label;
    SDL_FRect bounds{};
    BodyType bodyType = DYNAMIC;
    AssetReference<TextureWrapper> texture;
    ObjectReference<GameObject> target;
    ObjectReference<Camera> cameraTarget;
    CatalogOpaqueValue opaque;
    inline static int constructorCount = 0;

    SERIALIZABLE_SCRIPT(TypedCatalogFixture, enabled, count, samples, origin, fixedSamples, optionalScale,
                        nestedValues, label, bounds, bodyType, texture, target, cameraTarget, opaque);
};

class EmptyCatalogFixture final : public Behavior
{
  public:
    SERIALIZABLE_SCRIPT(EmptyCatalogFixture);
};

class FactoryOnlyCatalogFixture final : public Behavior
{
  public:
    std::string getTypeName() const override;
    void serialize(nlohmann::json &json) const override;
    void deserialize(const nlohmann::json &json) override;
    void resolveReferences(platformator::ReferenceResolver &resolver) override;
};
