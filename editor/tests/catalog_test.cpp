#include "catalog_fixture.h"
#include "test_support.h"
#include "../src/behaviorcatalog.h"
#include "../src/sourcescanner.h"
#include "../src/process.h"
#include <editorprotocol.h>

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

const platformator::ScriptFieldSnapshot *find_inspected_field(
    const platformator::ScriptInspectionResult &inspection, std::string_view name)
{
    for (const auto &field : inspection.fields)
        if (field.name == name)
            return &field;
    return nullptr;
}

void run_generated_inspection_cases()
{
    TypedCatalogFixture instance;
    instance.set_inspection_values();
    CatalogOpaqueValue::serializationCount = 0;

    platformator::ScriptInspectionLimits limits;
    limits.maxStringBytes = 3;
    limits.maxArrayItems = 2;
    const auto inspection = platformator::inspectRegisteredScriptFields(
        "TypedCatalogFixture", instance, limits);
    const auto *enabled = find_inspected_field(inspection, "enabled");
    const auto *count = find_inspected_field(inspection, "count");
    const auto *samples = find_inspected_field(inspection, "samples");
    const auto *origin = find_inspected_field(inspection, "origin");
    const auto *label = find_inspected_field(inspection, "label");
    const auto *bodyType = find_inspected_field(inspection, "bodyType");
    const auto *texture = find_inspected_field(inspection, "texture");
    const auto *target = find_inspected_field(inspection, "target");
    const auto *opaque = find_inspected_field(inspection, "opaque");
    require(inspection.providerAvailable && !inspection.outputTruncated && inspection.fields.size() == 14 &&
                enabled != nullptr && enabled->available && enabled->value == true &&
                count != nullptr && count->available && count->value == -9 &&
                samples != nullptr && samples->available && samples->truncated &&
                samples->value == nlohmann::json::array({3, 5}) &&
                origin != nullptr && origin->available && origin->value["x"] == 1.5 &&
                label != nullptr && label->available && label->truncated && label->value == "ab" &&
                bodyType != nullptr && bodyType->available && bodyType->value["name"] == "STATIC" &&
                texture != nullptr && texture->available && texture->truncated && texture->value["path"] == "tex" &&
                texture->value["runtime_serial"] == 0 &&
                target != nullptr && target->available && target->value["state"] == "unresolved" &&
                target->value["file_id"] == 42 && target->value["handle"].is_null() &&
                opaque != nullptr && !opaque->available && !opaque->unavailableReason.empty() &&
                CatalogOpaqueValue::serializationCount == 0,
            "generated inspection must read only allowlisted values, bound strings/arrays, summarize references and omit opaque serialization");

    limits.maxFields = 2;
    const auto fieldLimited = platformator::inspectRegisteredScriptFields(
        "TypedCatalogFixture", instance, limits);
    require(fieldLimited.fields.size() == 2 && fieldLimited.outputTruncated &&
                fieldLimited.omittedFieldCount == 12,
            "generated inspection must cap fields and report omitted declarations");

    limits.maxFields = 64;
    limits.maxStringBytes = 256;
    limits.maxArrayItems = 16;
    limits.maxOutputBytes = 256;
    const auto byteLimited = platformator::inspectRegisteredScriptFields(
        "TypedCatalogFixture", instance, limits);
    require(byteLimited.outputTruncated && byteLimited.retainedOutputBytes <= 256 &&
                byteLimited.omittedFieldCount > 0,
            "generated inspection must cap aggregate field output bytes before retaining more values");

    FactoryOnlyCatalogFixture factoryOnly;
    const auto unavailable = platformator::inspectRegisteredScriptFields(
        "FactoryOnlyCatalogFixture", factoryOnly, {});
    require(!unavailable.providerAvailable && unavailable.fields.empty(),
            "factory-only custom behaviors must remain explicitly unavailable to live inspection");

    EmptyCatalogFixture empty;
    const auto emptyInspection = platformator::inspectRegisteredScriptFields("EmptyCatalogFixture", empty, {});
    require(emptyInspection.providerAvailable && emptyInspection.fields.empty(),
            "zero-field generated behaviors must publish an available empty provider");
}

void run_shared_runner_catalog_cases()
{
    std::ostringstream stdoutCapture;
    std::ostringstream stderrCapture;
    auto *oldOut = std::cout.rdbuf(stdoutCapture.rdbuf());
    auto *oldError = std::cerr.rdbuf(stderrCapture.rdbuf());
    char executable[] = "catalog-fixture";
    char command[] = "--editor-catalog";
    char *removedArguments[] = {executable, command};
    const int status = platformator::run(2, removedArguments, "");
    std::cout.rdbuf(oldOut);
    std::cerr.rdbuf(oldError);
    require(status != 0, "the shared runner must reject the removed --editor-catalog CLI flag");
}

void run_catalog_identity_and_protocol_cases(const nlohmann::json &engineCatalog)
{
    using namespace std::chrono_literals;
    using platformator_editor::BehaviorCatalog;
    using platformator_editor::CatalogIdentity;
    using platformator_editor::CatalogState;

    const CatalogIdentity identity{"/tmp/custom-project", "debug", "custom-main", "inputs-1"};
    BehaviorCatalog catalog;
    catalog.configure(identity);
    require(catalog.set_source_catalog(engineCatalog),
            "a valid source catalog must populate an immutable current schema snapshot");
    require(catalog.state() == CatalogState::Current && catalog.snapshot() &&
                catalog.snapshot()->document == engineCatalog,
            "a valid source catalog must publish an immutable current schema snapshot");

    const CatalogIdentity changedIdentity{"/tmp/custom-project", "debug", "custom-main", "inputs-2"};
    catalog.configure(changedIdentity);
    require(catalog.state() == CatalogState::Stale && catalog.snapshot(),
            "configuring a different identity while Current must stale the cached snapshot");
    catalog.configure(changedIdentity);
    catalog.configure(identity);
    require(catalog.state() == CatalogState::Stale && catalog.snapshot(),
            "switching back to a previously cached catalog identity must not restore stale data as current");

    BehaviorCatalog malformed;
    malformed.configure(identity);
    require(!malformed.set_source_catalog(engineCatalog.dump() + "extra") &&
                malformed.state() == CatalogState::Unavailable,
            "extra non-JSON input must reject the schema payload");
}

void run_mario_source_reference_cases()
{
    using platformator_editor::BehaviorCatalog;
    using platformator_editor::SourceBehaviorScanner;

    const auto sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    std::vector<std::pair<std::filesystem::path, std::string>> sources;
    for (const char *name : {"mario_camera_rig.h", "mario_game.h"})
    {
        const auto path = sourceRoot / "examples/mario/assets/behaviors" / name;
        std::ifstream input(path, std::ios::binary);
        require(input.is_open(), "the real Mario behavior header must be available: " + path.string());
        sources.emplace_back(path, std::string{std::istreambuf_iterator<char>(input),
                                              std::istreambuf_iterator<char>()});
    }
    const auto scan = SourceBehaviorScanner::scan_sources(sources);
    BehaviorCatalog catalog;
    require(catalog.load_from_source_scan(scan), "real Mario source schemas must form a valid catalog");
    const auto snapshot = catalog.snapshot();
    const auto *rig = find_behavior(snapshot->document, "MarioCameraRig");
    const auto *game = find_behavior(snapshot->document, "MarioGame");
    require(rig != nullptr && game != nullptr, "both real Mario behaviors must be discovered");
    const auto require_reference = [](const nlohmann::json &behavior, std::string_view name,
                                      std::string_view target) {
        const auto *field = find_field(behavior, name);
        require(field != nullptr && field->value("kind", "") == "object_reference" &&
                    field->value("target", "") == target && field->value("nullable", false) &&
                    !field->contains("scanner_warning"),
                "source-scanned ObjectReference field must expose its editable reference type: " +
                    std::string(name));
    };
    require_reference(*rig, "camera", "Camera");
    require_reference(*rig, "target", "GameObject");
    require_reference(*game, "player", "GameObject");
    require_reference(*game, "bgm", "Audio");
    require(find_field(*rig, "cameraLead") != nullptr &&
                find_field(*rig, "cameraLead")->value("kind", "") == "number",
            "the camera rig's numeric field must remain available alongside its references");
}

void run_process_query_cases()
{
    using platformator_editor::BehaviorCatalog;
    using platformator_editor::CatalogIdentity;
    using platformator_editor::CatalogState;
    using platformator_editor::SourceBehaviorScanner;

    const auto tempDir = std::filesystem::temp_directory_path() / "platformator_scanner_test";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    const auto srcDir = tempDir / "assets" / "behaviors";
    std::filesystem::create_directories(srcDir);
    {
        std::ofstream out(srcDir / "PlayerMover.h");
        out << "enum class MoveMode { Walk = 0, Run = 1 };\n"
               "class PlayerMover : public Behavior {\n"
               "public:\n"
               "    float speed = 5.5f;\n"
               "    MoveMode mode = MoveMode::Run;\n"
               "    SERIALIZABLE_SCRIPT(PlayerMover, speed, mode)\n"
               "};\n";
    }

    SourceBehaviorScanner scanner;
    const auto scanResult = scanner.scan_project(tempDir, tempDir / "assets");
    const auto it = scanResult.behaviors.find("PlayerMover");
    require(scanResult.behaviors.size() == 1 && it != scanResult.behaviors.end() && it->second.typeName == "PlayerMover",
            "SourceBehaviorScanner must extract PlayerMover behavior");
    require(it->second.fields.size() == 2,
            "SourceBehaviorScanner must extract speed and mode fields");

    BehaviorCatalog catalog;
    catalog.configure(CatalogIdentity{tempDir.string(), "debug", "main", "src"});
    const auto loaded = catalog.load_from_source_scan(scanResult);
    require(loaded && catalog.state() == CatalogState::Current,
            "BehaviorCatalog::load_from_source_scan must populate current catalog");

    const auto bracedPath = srcDir / "BracedDefaults.h";
    const auto write_braced_source = [&](std::string_view macro) {
        std::ofstream out(bracedPath, std::ios::binary | std::ios::trunc);
        out << "class BracedDefaults : public Behavior {\n"
               "public:\n"
               "    bool enabled{true};\n"
               "    int count{4};\n"
               "    float ratio{1.25f};\n"
               "    Eigen::Vector2f offset{1.0f, 0.0f};\n"
               "    SDL_FRect bounds{1.0f, 2.0f, 3.0f, 4.0f};\n"
               "    ObjectRef<GameObject> target{17};\n"
               "    std::vector<int> samples{1, 2, 3};\n"
               "    std::optional<float> factor{2.5f};\n"
               "    int id{7};\n"
               "    std::vector<int> children{6, 7};\n"
               "    std::optional<int> components{8};\n"
               "    std::vector<int> overrides{9, 10};\n"
               "    " << macro << "\n"
               "};\n";
    };
    write_braced_source("SERIALIZABLE_SCRIPT(BracedDefaults, enabled, count, ratio, offset, bounds, target, samples, factor, id, children, components, overrides)");
    const auto bracedScan = scanner.scan_project(tempDir, tempDir / "assets");
    const auto braced = bracedScan.behaviors.find("BracedDefaults");
    const auto field_named = [](const auto &behavior, std::string_view name)
        -> const platformator_editor::SourceBehaviorFieldDescriptor * {
        const auto field = std::find_if(behavior.fields.begin(), behavior.fields.end(),
            [&](const auto &candidate) { return candidate.name == name; });
        return field == behavior.fields.end() ? nullptr : &*field;
    };
    require(braced != bracedScan.behaviors.end() && braced->second.fields.size() == 12,
            "a closing brace in a member initializer must not discard its declaration before the semicolon");
    const auto *offset = field_named(braced->second, "offset");
    const auto *bounds = field_named(braced->second, "bounds");
    const auto *target = field_named(braced->second, "target");
    const auto *samples = field_named(braced->second, "samples");
    const auto *factor = field_named(braced->second, "factor");
    const auto *idField = field_named(braced->second, "id");
    const auto *childrenField = field_named(braced->second, "children");
    const auto *componentsField = field_named(braced->second, "components");
    const auto *overridesField = field_named(braced->second, "overrides");
    require(offset && offset->defaultValue && *offset->defaultValue == nlohmann::json{{"x", 1.0}, {"y", 0.0}} &&
                bounds && bounds->defaultValue && *bounds->defaultValue ==
                    nlohmann::json{{"x", 1.0}, {"y", 2.0}, {"w", 3.0}, {"h", 4.0}} &&
                target && target->defaultValue && *target->defaultValue == 17 &&
                samples && samples->defaultValue && *samples->defaultValue == nlohmann::json::array({1, 2, 3}) &&
                factor && factor->defaultValue && *factor->defaultValue == 2.5 &&
                idField && idField->kind == "signed_integer" && idField->defaultValue &&
                    *idField->defaultValue == 7 &&
                childrenField && childrenField->kind == "array" && childrenField->element &&
                    childrenField->element->kind == "signed_integer" && childrenField->defaultValue &&
                    *childrenField->defaultValue == nlohmann::json::array({6, 7}) &&
                componentsField && componentsField->kind == "optional" && componentsField->element &&
                    componentsField->element->kind == "signed_integer" && componentsField->defaultValue &&
                    *componentsField->defaultValue == 8 &&
                overridesField && overridesField->kind == "array" && overridesField->element &&
                    overridesField->element->kind == "signed_integer" && overridesField->defaultValue &&
                    *overridesField->defaultValue == nlohmann::json::array({9, 10}),
            "primitive and container fields named like editor metadata must remain in the scanned schema");
    const bool loadedBracedDefaults = catalog.load_from_source_scan(bracedScan);
    require(loadedBracedDefaults,
            "the initial complete source scan must publish braced defaults: " + catalog.last_error() +
                "; schema=" + bracedScan.to_catalog_json().dump());

    {
        std::ofstream out(srcDir / "PlayerMover.h", std::ios::binary | std::ios::trunc);
        out << "enum class MoveMode { Walk = 0, Run = 1 };\n"
               "class PlayerMover : public Behavior { public: float speed = 9.5f; MoveMode mode = MoveMode::Run; "
               "SERIALIZABLE_SCRIPT(PlayerMover, speed, mode) };\n";
    }
    write_braced_source("SERIALIZABLE_SCRIPT(BracedDefaults, ratio, factor");
    const auto malformedScan = scanner.scan_project(tempDir, tempDir / "assets");
    require(std::any_of(malformedScan.diagnostics.begin(), malformedScan.diagnostics.end(),
                [](const auto &diagnostic) {
                    return diagnostic.typeName == "BracedDefaults" &&
                           diagnostic.message.find("incomplete") != std::string::npos;
                }),
            "an incomplete SERIALIZABLE_SCRIPT call must be diagnosed even when source braces balance");
    require(catalog.load_from_source_scan(malformedScan),
            "the malformed edit scan must remain publishable with last-valid schemas retained");
    const auto retainedBraced = find_behavior(catalog.snapshot()->document, "BracedDefaults");
    const auto updatedPlayer = find_behavior(catalog.snapshot()->document, "PlayerMover");
    require(retainedBraced && retainedBraced->at("fields").size() == 12 &&
                retainedBraced->value("scanner_warning", std::string{}).find("last valid schema") != std::string::npos &&
                updatedPlayer && find_field(*updatedPlayer, "speed") != nullptr &&
                find_field(*updatedPlayer, "speed")->value("default_value", 0.0) == 9.5,
            "the incomplete behavior must retain its last-valid fields while unaffected behaviors update");

    write_braced_source("SERIALIZABLE_SCRIPT(BracedDefaults, ratio, factor)");
    const auto recoveredScan = scanner.scan_project(tempDir, tempDir / "assets");
    require(catalog.load_from_source_scan(recoveredScan),
            "a repaired macro declaration must recover the current schema");
    const auto recoveredBraced = find_behavior(catalog.snapshot()->document, "BracedDefaults");
    require(recoveredBraced && recoveredBraced->at("fields").size() == 2 &&
                find_field(*recoveredBraced, "enabled") == nullptr,
            "valid recovery must apply intentional field deletion after the incomplete edit is fixed");

    std::filesystem::remove_all(tempDir, ec);
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
#if defined(PLATFORMATOR_ENABLE_EDITOR_SESSION) && PLATFORMATOR_ENABLE_EDITOR_SESSION
        const auto sessionCapabilities = platformator::editor_session::read_session_capabilities(first);
        require(sessionCapabilities.supported && sessionCapabilities.playControl &&
                    sessionCapabilities.runtimeInspection && sessionCapabilities.frameStream &&
                    !sessionCapabilities.liveAssetReload && sessionCapabilities.inputForwarding,
                "the opt-in stock-runner catalog must advertise implemented pause/step, bounded inspection, frame support, and input forwarding");
#else
        require(!first.contains("editor_capabilities"),
                "the default-off stock runner must omit editor session capabilities");
#endif
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
        run_generated_inspection_cases();
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
        run_mario_source_reference_cases();
        run_process_query_cases();
    }
    catch (const std::exception &error)
    {
        std::cerr << "platformator_editor_catalog_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
