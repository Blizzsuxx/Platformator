#include "catalog_fixture.h"
#include "test_support.h"
#include "../src/behaviorcatalog.h"
#include "../src/process.h"

#include <SDL3/SDL.h>
#include <platformator/runtime.h>
#include <platformator/runner.h>
#include <platformator/scriptregistration.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <thread>

using namespace platformator_editor_test;

namespace
{
const nlohmann::json *find_behavior(const nlohmann::json &catalog, std::string_view name)
{
    if (!catalog.is_object() || !catalog.contains("behaviors") || !catalog["behaviors"].is_array())
        return nullptr;
    for (const auto &behavior : catalog["behaviors"])
    {
        if (behavior.is_object() && behavior.value("name", "") == std::string(name))
            return &behavior;
    }
    return nullptr;
}

bool has_field(const nlohmann::json &behavior, std::string_view name, std::string_view kind)
{
    const auto fields = behavior.find("fields");
    if (fields == behavior.end() || !fields->is_array())
        return false;
    return std::any_of(fields->begin(), fields->end(), [&](const auto &field) {
        return field.is_object() && field.value("name", "") == std::string(name) &&
               field.value("kind", "") == std::string(kind);
    });
}

platformator::ScriptDescriptor conflicting_descriptor_a()
{
    return {"ConflictFixture", true,
            {platformator::ScriptFieldDescriptor{"value", platformator::ScriptFieldKind::SignedInteger}}};
}

platformator::ScriptDescriptor conflicting_descriptor_b()
{
    return {"ConflictFixture", true,
            {platformator::ScriptFieldDescriptor{"value", platformator::ScriptFieldKind::String}}};
}

const nlohmann::json *find_field(const nlohmann::json &behavior, std::string_view name)
{
    const auto fields = behavior.find("fields");
    if (fields == behavior.end() || !fields->is_array())
        return nullptr;
    for (const auto &field : *fields)
    {
        if (field.is_object() && field.value("name", "") == std::string(name))
            return &field;
    }
    return nullptr;
}

void run_shared_runner_catalog_cases()
{
    std::ostringstream stdoutCapture;
    std::ostringstream stderrCapture;
    auto *oldOut = std::cout.rdbuf(stdoutCapture.rdbuf());
    auto *oldError = std::cerr.rdbuf(stderrCapture.rdbuf());
    char executable[] = "catalog-fixture";
    char command[] = "--editor-catalog";
    char *validArguments[] = {executable, command};
    const int validStatus = platformator::run(2, validArguments, "");
    std::cout.rdbuf(oldOut);
    std::cerr.rdbuf(oldError);
    require(validStatus == 0, "the shared runner must accept its early catalog command");
    const auto runnerCatalog = nlohmann::json::parse(stdoutCapture.str());
    require(runnerCatalog.value("schema_version", 0) == 1 &&
                find_behavior(runnerCatalog, "TypedCatalogFixture") != nullptr,
            "shared-runner catalog mode must export the linked registration set");
    require(stderrCapture.str().empty(), "a successful catalog export must not mix diagnostics into stderr");

    stdoutCapture.str({});
    stdoutCapture.clear();
    stderrCapture.str({});
    stderrCapture.clear();
    oldOut = std::cout.rdbuf(stdoutCapture.rdbuf());
    oldError = std::cerr.rdbuf(stderrCapture.rdbuf());
    char incompatible[] = "--scene";
    char scene[] = "unexpected.scene";
    char *invalidArguments[] = {executable, command, incompatible, scene};
    const int invalidStatus = platformator::run(4, invalidArguments, "");
    std::cout.rdbuf(oldOut);
    std::cerr.rdbuf(oldError);
    require(invalidStatus != 0 && stdoutCapture.str().empty() && !stderrCapture.str().empty(),
            "catalog mode must reject run arguments without emitting a partial document");
}

void run_catalog_identity_and_protocol_cases(const nlohmann::json &engineCatalog)
{
    using namespace std::chrono_literals;
    using platformator_editor::BehaviorCatalog;
    using platformator_editor::CatalogIdentity;
    using platformator_editor::CatalogProtocol;
    using platformator_editor::CatalogState;

    const CatalogIdentity identity{"/tmp/custom-project", "debug", "custom-main", "/tmp/custom-game",
                                   "binary-1", "inputs-1"};
    BehaviorCatalog undeclared;
    undeclared.configure(CatalogProtocol::Unavailable, identity);
    require(!undeclared.can_query() && undeclared.can_build_only() && !undeclared.can_automatic_run() &&
                !undeclared.begin_query(),
            "undeclared custom entries must remain buildable without querying or automatic Run");

    TemporaryDirectory temporary;
    const auto customSource = temporary.path() / "custom_main.cpp";
    {
        std::ofstream source(customSource, std::ios::binary);
        source << "int custom_main() { return 0; }\n";
    }
    std::ifstream originalStream(customSource, std::ios::binary);
    const std::string original((std::istreambuf_iterator<char>(originalStream)), {});
    require(original == "int custom_main() { return 0; }\n",
            "the custom-entry fixture must retain its original source content");
    std::ifstream afterStream(customSource, std::ios::binary);
    const std::string after((std::istreambuf_iterator<char>(afterStream)), {});
    require(after == original, "catalog policy checks must not rewrite custom game source");

    BehaviorCatalog catalog;
    catalog.configure(CatalogProtocol::CommonRunnerV1, identity);
    require(catalog.can_query() && catalog.can_build_only() && !catalog.can_automatic_run(),
            "declaring the protocol permits a query but does not certify catalog availability");
    require(catalog.begin_query() && catalog.append_stdout(engineCatalog.dump()) &&
                catalog.append_stderr("fixture diagnostic") && catalog.finish_query(0, 250ms),
            "a successful schema query must complete independently from stderr diagnostics");
    require(catalog.state() == CatalogState::Current && catalog.can_automatic_run() && catalog.snapshot() &&
                catalog.snapshot()->document == engineCatalog,
            "a valid identity-bound query must publish an immutable current schema snapshot");

    const CatalogIdentity changedIdentity{"/tmp/custom-project", "debug", "custom-main", "/tmp/custom-game",
                                          "binary-2", "inputs-2"};
    catalog.configure(CatalogProtocol::CommonRunnerV1, changedIdentity);
    require(catalog.state() == CatalogState::Stale && !catalog.can_automatic_run() && catalog.snapshot(),
            "binary/source identity changes must retain data only as stale and block automatic Run");
    require(catalog.begin_query() && catalog.append_stdout(engineCatalog.dump()),
            "a compatible target may refresh its stale catalog");
    catalog.configure(CatalogProtocol::CommonRunnerV1, identity);
    require(!catalog.finish_query(0, 50ms) && catalog.state() == CatalogState::Stale,
            "a completion for an identity superseded during the query must not become current");

    BehaviorCatalog falseDeclaration;
    falseDeclaration.configure(CatalogProtocol::CommonRunnerV1, identity);
    require(falseDeclaration.begin_query() && falseDeclaration.append_stdout("gameplay started") &&
                !falseDeclaration.finish_query(1, 100ms) && !falseDeclaration.can_automatic_run() &&
                falseDeclaration.can_build_only(),
            "a falsely declared custom entry must fail catalog verification without enabling Run");

    BehaviorCatalog malformed;
    malformed.configure(CatalogProtocol::CommonRunnerV1, identity);
    require(malformed.begin_query() && malformed.append_stdout(engineCatalog.dump() + "extra") &&
                !malformed.finish_query(0, 100ms) && malformed.state() == CatalogState::Unavailable,
            "extra non-JSON stdout must reject the full schema payload");

    BehaviorCatalog timeout;
    timeout.configure(CatalogProtocol::CommonRunnerV1, identity);
    require(timeout.begin_query() && timeout.append_stdout(engineCatalog.dump()) &&
                !timeout.finish_query(0, 10001ms, false) && !timeout.can_automatic_run(),
            "catalog queries exceeding ten seconds must be rejected");

    BehaviorCatalog oversized;
    oversized.configure(CatalogProtocol::CommonRunnerV1, identity);
    require(oversized.begin_query() && !oversized.append_stdout(std::string(4 * 1024 * 1024 + 1, 'x')) &&
                !oversized.finish_query(0, 100ms) && oversized.state() == CatalogState::Unavailable,
            "catalog queries exceeding the four MiB stdout/stderr budget must be rejected");
}

void run_process_query_cases()
{
    using namespace std::chrono_literals;
    using platformator_editor::BehaviorCatalog;
    using platformator_editor::BehaviorCatalogQuery;
    using platformator_editor::CatalogIdentity;
    using platformator_editor::CatalogProtocol;
    using platformator_editor::CatalogState;
    using platformator_editor::ProcessRequest;

    BehaviorCatalog catalog;
    catalog.configure(CatalogProtocol::CommonRunnerV1,
                      CatalogIdentity{"/tmp/process-project", "debug", "fixture", "/tmp/catalog-fixture",
                                      "binary", "inputs"});
    BehaviorCatalogQuery query;
    ProcessRequest request;
    request.argv = {PLATFORMATOR_EDITOR_CATALOG_PROCESS_FIXTURE, "valid"};
    request.workingDirectory = std::filesystem::current_path();
    require(query.start(catalog, std::move(request), 1),
            "a declared compatible target can launch a separate-stream catalog query");
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (query.running() && std::chrono::steady_clock::now() < deadline)
    {
        query.poll();
        std::this_thread::sleep_for(1ms);
    }
    query.poll();
    require(!query.running() && catalog.state() == CatalogState::Current &&
                catalog.can_automatic_run() && catalog.last_error().empty(),
            "the process-backed query must accept one JSON stdout document while keeping stderr separate");

    ProcessRequest failing;
    failing.argv = {PLATFORMATOR_EDITOR_CATALOG_PROCESS_FIXTURE, "fail"};
    failing.workingDirectory = std::filesystem::current_path();
    require(query.start(catalog, std::move(failing), 2), "a current catalog may be explicitly refreshed");
    const auto failureDeadline = std::chrono::steady_clock::now() + 2s;
    while (query.running() && std::chrono::steady_clock::now() < failureDeadline)
    {
        query.poll();
        std::this_thread::sleep_for(1ms);
    }
    query.poll();
    require(!query.running() && catalog.state() == CatalogState::Stale &&
                !catalog.can_automatic_run() && catalog.last_error().find("status 7") != std::string::npos,
            "a failed refresh must retain only a stale snapshot and disable automatic Run");
}
} // namespace

int main()
{
    try
    {
        require(platformator::Runtime::tryGetCurrent() == nullptr && SDL_WasInit(0) == 0,
                "catalog export tests must begin without Runtime or SDL initialization");
        const auto first = platformator::exportRegisteredBehaviorCatalog();
        const auto second = platformator::exportRegisteredBehaviorCatalog();
        require(first == second, "catalog export must produce deterministic JSON across repeated calls");
        require(first.is_object() && first.value("schema_version", 0) == 1 && first["behaviors"].is_array(),
                "the catalog must use the versioned envelope");
        require(TypedCatalogFixture::constructor_count() == 0,
                "metadata export must not construct a behavior instance");

        const auto *typed = find_behavior(first, "TypedCatalogFixture");
        const nlohmann::json *bodyType = nullptr;
        if (typed && typed->contains("fields"))
        {
            for (const auto &field : typed->at("fields"))
            {
                if (field.value("name", "") == "bodyType")
                    bodyType = &field;
            }
        }
        require(typed != nullptr && typed->value("field_schema_available", false) &&
                    has_field(*typed, "enabled", "boolean") && has_field(*typed, "count", "signed_integer") &&
                    has_field(*typed, "samples", "array") && has_field(*typed, "origin", "vector2") &&
                    has_field(*typed, "fixedSamples", "array") && has_field(*typed, "optionalScale", "optional") &&
                    has_field(*typed, "nestedValues", "array") &&
                    has_field(*typed, "label", "string") && has_field(*typed, "bounds", "rectangle") &&
                    has_field(*typed, "bodyType", "enum") &&
                    has_field(*typed, "texture", "asset_reference") &&
                    has_field(*typed, "target", "object_reference") &&
                    has_field(*typed, "cameraTarget", "object_reference") &&
                    has_field(*typed, "opaque", "opaque_json") && bodyType != nullptr &&
                    bodyType->contains("enum_values") && bodyType->at("enum_values").size() == 3,
                "macro descriptors must report private fields and supported recursive/reference types");
        const auto *count = find_field(*typed, "count");
        const auto *fixedSamples = find_field(*typed, "fixedSamples");
        const auto *optionalScale = find_field(*typed, "optionalScale");
        const auto *nestedValues = find_field(*typed, "nestedValues");
        const auto *target = find_field(*typed, "cameraTarget");
        require(count != nullptr && count->value("minimum", "") == "-9223372036854775808" &&
                    count->value("maximum", "") == "9223372036854775807" && fixedSamples != nullptr &&
                    fixedSamples->contains("element") &&
                    fixedSamples->at("element").value("kind", "") == "unsigned_integer" &&
                    fixedSamples->at("element").value("minimum", "") == "0" &&
                    optionalScale != nullptr && optionalScale->at("element").value("kind", "") == "number" &&
                    nestedValues != nullptr && nestedValues->at("element").value("kind", "") == "optional" &&
                    nestedValues->at("element").at("element").value("kind", "") == "number" &&
                    target != nullptr && target->value("target", "") == "Camera" &&
                    target->value("nullable", false),
                "descriptors must carry representable ranges, recursive element types, optionality and target types");
        const auto *empty = find_behavior(first, "EmptyCatalogFixture");
        require(empty != nullptr && empty->value("field_schema_available", false) &&
                    empty->at("fields").empty(),
                "zero-field macro behaviors must expose an available empty schema");
        const auto *factoryOnly = find_behavior(first, "FactoryOnlyCatalogFixture");
        require(factoryOnly != nullptr && !factoryOnly->value("field_schema_available", true),
                "factory-only behaviors must remain registered with an unavailable field schema");
        require(platformator::Runtime::tryGetCurrent() == nullptr && SDL_WasInit(0) == 0,
                "metadata export must leave Runtime and SDL uninitialized");
        run_shared_runner_catalog_cases();
        require(platformator::Runtime::tryGetCurrent() == nullptr && SDL_WasInit(0) == 0,
                "shared-runner catalog mode must exit before Runtime or SDL initialization");

        platformator::registerScriptFactory("ConflictFixture", []() -> Behavior * { return nullptr; });
        platformator::registerScriptDescriptorProvider("ConflictFixture", &conflicting_descriptor_a);
        platformator::registerScriptDescriptorProvider("ConflictFixture", &conflicting_descriptor_b);
        bool rejectedConflict = false;
        try
        {
            (void)platformator::exportRegisteredBehaviorCatalog();
        }
        catch (const std::exception &)
        {
            rejectedConflict = true;
        }
        require(rejectedConflict, "conflicting duplicate behavior schemas must be rejected");
        run_catalog_identity_and_protocol_cases(first);
        run_process_query_cases();
    }
    catch (const std::exception &error)
    {
        std::cerr << "platformator_editor_catalog_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
