#include "catalog_fixture.h"

#include <platformator/scriptregistration.h>

TypedCatalogFixture::TypedCatalogFixture()
{
    ++constructorCount;
}

int TypedCatalogFixture::constructor_count() noexcept
{
    return constructorCount;
}

void TypedCatalogFixture::set_inspection_values()
{
    enabled = true;
    count = -9;
    samples = {3, 5, 8};
    origin = Eigen::Vector2f{1.5f, -2.0f};
    fixedSamples = {13, 21, 34};
    optionalScale = 0.75f;
    nestedValues = {0.25f, std::nullopt, 0.5f};
    label = "ab\xcf\x80" "cd";
    bounds = SDL_FRect{1.0f, 2.0f, 30.0f, 40.0f};
    bodyType = STATIC;
    texture.setFilePath("textures/hero.png");
    target.setReferencedId(42);
    cameraTarget.setReferencedId(43);
    opaque.value = 99;
}

std::string FactoryOnlyCatalogFixture::getTypeName() const
{
    return "FactoryOnlyCatalogFixture";
}

void FactoryOnlyCatalogFixture::serialize(nlohmann::json &json) const
{
    json = nlohmann::json::object();
    json["type"] = getTypeName();
}

void FactoryOnlyCatalogFixture::deserialize(const nlohmann::json &)
{
}

void FactoryOnlyCatalogFixture::resolveReferences(platformator::ReferenceResolver &)
{
}

namespace
{
const bool factoryOnlyRegistered = [] {
    platformator::registerScriptFactory("FactoryOnlyCatalogFixture",
                                        []() -> Behavior * { return new FactoryOnlyCatalogFixture(); });
    return true;
}();
}
