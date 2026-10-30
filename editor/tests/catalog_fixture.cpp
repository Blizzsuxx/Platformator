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
