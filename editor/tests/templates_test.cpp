#include "templates.h"
#include "documentio.h"
#include "panels/referencefields.h"
#include "preferences.h"
#include "test_support.h"

#include <algorithm>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <platformator/runtime.h>
#include <platformator/runtimeoptions.h>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace
{
Json valid_template_root()
{
    Json child = object(12, "Child target", Json::array({
        component(13, 2, Json{{"width", 64.0}, {"height", 64.0}})}));
    Json behavior{{"type", "LinkBehavior"}, {"target", 12}, {"componentTarget", 13},
                  {"externalTarget", 98}, {"opaqueInteger", 774}};
    return object(10, "Template root", Json::array({
        component(11, 5, Json{{"behaviors", Json::array({std::move(behavior)})}})}),
        Json::array({std::move(child)}));
}

Json valid_bindings()
{
    return Json::array({
        Json{{"owner_id", 10}, {"component_id", 11}, {"path", "/behaviors/0/target"},
             {"target_kind", "GameObject"}, {"scope", "internal"}},
        Json{{"owner_id", 10}, {"component_id", 11}, {"path", "/behaviors/0/componentTarget"},
             {"target_kind", "Component"}, {"scope", "internal"}},
        Json{{"owner_id", 10}, {"component_id", 11}, {"path", "/behaviors/0/externalTarget"},
             {"target_kind", "GameObject"}, {"scope", "external"},
             {"source_scene_identity", "/projects/source.scene"},
             {"original_target_kind", "GameObject"}, {"original_target_id", 98}}
    });
}

Json *find_object(Json &root, int id)
{
    if (root.is_array())
    {
        for (auto &record : root)
            if (auto *found = find_object(record, id)) return found;
        return nullptr;
    }
    if (!root.is_object()) return nullptr;
    if (root.value("id", -1) == id) return &root;
    for (auto &child : root["children"])
        if (auto *found = find_object(child, id)) return found;
    return nullptr;
}

Json reference_catalog(bool available = true)
{
    Json fields = Json::array();
    if (available)
    {
        fields.push_back(Json{{"name", "target"}, {"kind", "object_reference"},
                              {"target", "GameObject"}});
        fields.push_back(Json{{"name", "componentTarget"}, {"kind", "object_reference"},
                              {"target", "Component"}});
        fields.push_back(Json{{"name", "externalTarget"}, {"kind", "object_reference"},
                              {"target", "GameObject"}});
        fields.push_back(Json{{"name", "opaqueInteger"}, {"kind", "signed_integer"}});
    }
    return Json{{"schema_version", 1},
                {"behaviors", Json::array({Json{{"name", "LinkBehavior"},
                                               {"field_schema_available", available},
                                               {"fields", std::move(fields)}}})}};
}

void test_typed_binding_collection_and_opaque_schema_gate()
{
    const auto parsed = parse_document(scene({valid_template_root(), object(98, "Source external")}));
    require(static_cast<bool>(parsed), "reference collection fixture must parse as a valid source scene");
    const auto collected = collect_template_bindings(*parsed.document, parsed.document->root[0],
                                                      "/projects/source.scene", reference_catalog());
    require(collected && collected.assured && collected.unresolvedBehaviors.empty() &&
                collected.bindings.size() == 3,
            "compiled metadata must collect only the three declared reference sites");
    require(collected.bindings[0]["scope"] == "internal" &&
                collected.bindings[1]["scope"] == "internal" &&
                collected.bindings[2]["scope"] == "external" &&
                collected.bindings[2]["source_scene_identity"] == "/projects/source.scene",
            "reference collection must classify typed internal and external targets with source identity");

    const auto unavailable = collect_template_bindings(*parsed.document, parsed.document->root[0],
                                                        "/projects/source.scene", reference_catalog(false));
    require(unavailable && !unavailable.assured &&
                unavailable.unresolvedBehaviors.size() == 1 &&
                unavailable.unresolvedBehaviors[0].find("LinkBehavior") != std::string::npos,
            "missing behavior metadata must request an author declaration and block assured remapping");
    const std::vector<Json> declarations{
        make_template_reference_binding_declaration(10, 11, 0, "target", "GameObject"),
        make_template_reference_binding_declaration(10, 11, 0, "componentTarget", "Component"),
        make_template_reference_binding_declaration(10, 11, 0, "externalTarget", "GameObject")
    };
    const auto escapedDeclaration = make_template_reference_binding_declaration(
        10, 11, 0, "nested/reference~value", "GameObject");
    require(escapedDeclaration["path"] == "/behaviors/0/nested~1reference~0value" &&
                make_template_reference_binding_declaration(10, 11, 0, "target", "integer").empty(),
            "author declaration paths must escape JSON-pointer segments and use an explicit target kind");
    const auto declared = collect_template_bindings(*parsed.document, parsed.document->root[0],
        "/projects/source.scene", reference_catalog(false), declarations,
        {{10, 11, 0, "LinkBehavior"}});
    require(declared && declared.assured && declared.bindings.size() == 3,
            "explicit author declarations must provide typed bindings when compiled metadata is unavailable");
    const auto misScoped = collect_template_bindings(*parsed.document, parsed.document->root[0],
        "/projects/source.scene", reference_catalog(false), declarations,
        {{10, 404, 0, "LinkBehavior"}});
    require(misScoped && !misScoped.assured && !misScoped.unresolvedBehaviors.empty(),
            "an author completeness declaration must match the exact object/component/behavior instance");
}

void test_referencefields_author_declaration_resolves_escaped_site()
{
    const std::string opaqueField = "target/opaque~";
    const std::string nestedField = "nested/data~";
    Json behavior{{"type", "OpaqueLink"}, {opaqueField, 98},
        {nestedField, Json{{"links", Json::array({Json{{"target/id~kind", 98}}})}}},
        {"refs", Json::array({Json{{"target", 98}}})}};
    const Json root = object(10, "Opaque root", Json::array({component(11, 5,
        Json{{"behaviors", Json::array({behavior})}})}));
    const auto parsed = parse_document(scene({root, object(98, "External target")}));
    require(static_cast<bool>(parsed), "opaque reference declaration fixture must parse");
    const Json catalog{{"schema_version", 1},
                       {"behaviors", Json::array({Json{{"name", "OpaqueLink"},
                           {"field_schema_available", false}, {"fields", Json::array()}}})}};
    const Json declaration = make_template_reference_binding_declaration(
        10, 11, 0, opaqueField, "GameObject");
    const Json nestedDeclaration = make_template_reference_path_declaration(
        10, 11, 0, "/nested~1data~0/links/0/target~1id~0kind", "GameObject");
    const Json arrayDeclaration = make_template_reference_path_declaration(
        10, 11, 0, "/refs/0/target", "GameObject");
    const auto collected = collect_template_bindings(*parsed.document, parsed.document->root[0],
        "/projects/source.scene", catalog, {declaration, nestedDeclaration, arrayDeclaration},
        {{10, 11, 0, "OpaqueLink"}});
    std::set<std::string> paths;
    for (const auto &binding : collected.bindings)
        paths.insert(binding.value("path", std::string{}));
    require(collected && collected.assured && collected.bindings.size() == 3 &&
                paths.contains("/behaviors/0/target~1opaque~0") &&
                paths.contains("/behaviors/0/nested~1data~0/links/0/target~1id~0kind") &&
                paths.contains("/behaviors/0/refs/0/target") &&
                std::all_of(collected.bindings.begin(), collected.bindings.end(), [](const Json &binding) {
                    return binding["scope"] == "external" && binding["original_target_id"] == 98;
                }),
            "author declarations must resolve escaped nested object/array sites and preserve external identity");
}

void test_scene_identity_distinguishes_replaced_files()
{
    TemporaryDirectory temporary;
    const auto scenePath = temporary.path() / "identity.scene";
    auto first = make_default_document();
    first.sourcePath = scenePath;
    first.sourceIdentity = FileIdentity{7, 101, 10, 0, 20, 30};
    auto sameFile = make_default_document();
    sameFile.sourcePath = scenePath;
    sameFile.sourceIdentity = first.sourceIdentity;
    auto replaced = make_default_document();
    replaced.sourcePath = scenePath;
    replaced.sourceIdentity = FileIdentity{7, 202, 10, 0, 20, 30};
    require(template_scene_identity(first) == template_scene_identity(sameFile) &&
                template_scene_identity(first) != template_scene_identity(replaced),
            "scene identity must include native file identity so a replaced file at the same path cannot inherit externals");
}

void test_template_insert_allocates_maps_and_retains_exact_redo_payload()
{
    auto parsed = parse_document(scene({
        object(10, "Existing", Json::array({component(11, 2)})),
        object(98, "Destination external")}));
    require(static_cast<bool>(parsed), "template insertion destination must parse");
    DocumentSession &document = *parsed.document;
    const Json before = document.root;
    const auto loaded = load_template_envelope(make_template_envelope(
        "Assembly", valid_template_root(), valid_bindings()));
    require(static_cast<bool>(loaded), "template insertion fixture must validate");
    const auto source = parse_document(scene({valid_template_root(), object(98, "Source external")}));
    require(static_cast<bool>(source), "template binding source scene must parse");
    const auto coverage = collect_template_bindings(*source.document, source.document->root[0],
        "/projects/source.scene", reference_catalog());
    const auto incompleteCoverage = collect_template_bindings(*source.document, source.document->root[0],
        "/projects/source.scene", reference_catalog(false));
    const auto storedCoverage = template_asset_binding_coverage(*loaded.asset);
    require(coverage && coverage.assured && coverage.bindings == loaded.asset->reference_bindings(),
            "template insertion must use reference coverage collected from this exact source subtree");
    require(storedCoverage && storedCoverage.assured &&
                storedCoverage.validatedSubtree == loaded.asset->root() &&
                storedCoverage.bindings == loaded.asset->reference_bindings(),
            "a loaded template must revalidate its persisted explicit binding contract for browsing/instantiation");
    EditCommandService commands;
    SelectionModel selection;
    const auto needsAssignment = insert_template(commands, document, *loaded.asset,
        "/projects/destination.scene", storedCoverage);
    require(!needsAssignment && needsAssignment.error.find("explicit destination assignment") != std::string::npos &&
                document.root == before && !commands.history_status().canUndo,
            "cross-scene external IDs must require explicit assignment before any scene mutation");

    const auto incomplete = insert_template(commands, document, *loaded.asset,
        "/projects/destination.scene", incompleteCoverage);
    require(!incomplete && document.root == before,
            "unassured reference coverage must block template instantiation without mutation");

    commands.inject_history_retention_failure_once_for_testing();
    const std::vector<TemplateExternalAssignment> assignments{
        {10, 11, "/behaviors/0/externalTarget", 98}
    };
    const auto failedRetention = insert_template(commands, document, *loaded.asset,
        "/projects/destination.scene", storedCoverage, std::nullopt,
        std::numeric_limits<std::size_t>::max(), assignments);
    require(!failedRetention && document.root == before && !commands.history_status().canUndo,
            "failed history retention must roll back a fully prepared template insertion");

    const auto inserted = insert_template(commands, document, *loaded.asset,
        "/projects/destination.scene", storedCoverage, std::nullopt,
        std::numeric_limits<std::size_t>::max(), assignments);
    require(static_cast<bool>(inserted), "template insertion should commit: " + inserted.error);
    require(inserted.sourceToDestinationIds.size() == 4 && inserted.insertedObjectIds.size() == 2 &&
                commands.history_status().undoCommands == 1,
            "template insertion must allocate a complete object/component map in one history entry");
    std::set<int> allocated;
    for (const auto &[sourceId, destinationId] : inserted.sourceToDestinationIds)
    {
        (void)sourceId;
        require(destinationId != 10 && destinationId != 11 && destinationId != 98 &&
                    allocated.insert(destinationId).second,
                "destination identities must be unique and avoid every occupied object/component ID");
    }
    const int newRootId = inserted.sourceToDestinationIds.at(10);
    const int newChildId = inserted.sourceToDestinationIds.at(12);
    const int newScriptId = inserted.sourceToDestinationIds.at(11);
    const int newComponentTargetId = inserted.sourceToDestinationIds.at(13);
    Json *newRoot = find_object(document.root, newRootId);
    require(newRoot != nullptr && newRoot->at("children")[0].at("id") == newChildId &&
                newRoot->at("components")[0].at("id") == newScriptId &&
                newRoot->at("components")[0].at("behaviors")[0].at("target") == newChildId &&
                newRoot->at("components")[0].at("behaviors")[0].at("componentTarget") == newComponentTargetId &&
                newRoot->at("components")[0].at("behaviors")[0].at("externalTarget") == 98 &&
                newRoot->at("components")[0].at("behaviors")[0].at("opaqueInteger") == 774,
            "instantiation must remap only declared internals and preserve assigned external and opaque values");
    const Json retainedSubtree = *newRoot;
    const auto gameSceneRoundTrip = parse_document(Json::parse(document.root.dump()),
                                                    document.documentGeneration + 1);
    require(gameSceneRoundTrip && gameSceneRoundTrip.document->root == document.root,
            "instantiated templates must save and reopen through the ordinary game scene array format");
    require(commands.undo(document, selection).status == CommandStatus::Changed && document.root == before,
            "one Undo must remove the complete inserted subtree");
    require(commands.redo(document, selection).status == CommandStatus::Changed &&
                find_object(document.root, newRootId) != nullptr &&
                *find_object(document.root, newRootId) == retainedSubtree,
            "Redo must restore the exact allocated identities and external assignments");
}

void test_duplicate_reference_policies_and_multi_root_identity_map()
{
    Json behavior{{"type", "LinkBehavior"}, {"selfTarget", 10}, {"childTarget", 12},
                  {"componentTarget", 13}, {"crossRoot", 20},
                  {"externalTarget", 30}, {"opaqueInteger", 777}};
    const Json sourceRoot = object(10, "A", Json::array({component(11, 5,
        Json{{"behaviors", Json::array({behavior})}})}),
        Json::array({object(12, "A child", Json::array({component(13, 2)}))}));
    const auto parsed = parse_document(scene({sourceRoot, object(20, "B"), object(30, "External")}));
    require(static_cast<bool>(parsed), "multi-root duplicate fixture must parse");
    const auto sourceLocation = parsed.document->indexes.objectsById.find(10);
    require(sourceLocation != parsed.document->indexes.objectsById.end(),
            "multi-root source object must be indexed");
    const Json *sourceObject = nullptr;
    {
        const auto &indices = sourceLocation->second.childIndices;
        if (!indices.empty()) sourceObject = &parsed.document->root[indices.front()];
        for (std::size_t index = 1; sourceObject != nullptr && index < indices.size(); ++index)
            sourceObject = &sourceObject->at("children")[indices[index]];
    }
    require(sourceObject != nullptr, "multi-root duplicate source subtree must resolve");
    Json catalog = reference_catalog(false);
    catalog["behaviors"][0]["field_schema_available"] = true;
    catalog["behaviors"][0]["fields"] = Json::array({
        Json{{"name", "selfTarget"}, {"kind", "object_reference"}, {"target", "GameObject"}},
        Json{{"name", "childTarget"}, {"kind", "object_reference"}, {"target", "GameObject"}},
        Json{{"name", "componentTarget"}, {"kind", "object_reference"}, {"target", "Component"}},
        Json{{"name", "crossRoot"}, {"kind", "object_reference"}, {"target", "GameObject"}},
        Json{{"name", "externalTarget"}, {"kind", "object_reference"}, {"target", "GameObject"}}
    });
    const auto coverage = collect_template_bindings(*parsed.document, *sourceObject,
        "/projects/duplicate.scene", catalog);
    require(coverage && coverage.assured && coverage.bindings.size() == 5,
            "duplicate fixture must have typed self, descendant, component, cross-root, and external sites");

    auto preserveParsed = parse_document(scene({sourceRoot, object(20, "B"), object(30, "External")}));
    DocumentSession &preserveDocument = *preserveParsed.document;
    EditCommandService preserveCommands;
    const auto preserved = preserveCommands.duplicate_objects(preserveDocument, {10, 20});
    require(preserved.status == CommandStatus::Changed && preserved.affectedObjectIds.size() == 2,
            "legacy duplication must retain the preserve-existing-targets default");
    Json *preservedA = find_object(preserveDocument.root, preserved.affectedObjectIds[0]);
    require(preservedA != nullptr &&
                preservedA->at("components")[0].at("behaviors")[0].at("crossRoot") == 20 &&
                preservedA->at("components")[0].at("behaviors")[0].at("externalTarget") == 30,
            "default duplication must preserve references to selected peers and outside targets");

    auto remapParsed = parse_document(scene({sourceRoot, object(20, "B"), object(30, "External")}));
    DocumentSession &remapDocument = *remapParsed.document;
    const Json original = remapDocument.root;
    EditCommandService remapCommands;
    SelectionModel selection;
    const auto remapped = remapCommands.duplicate_objects(remapDocument, {10, 20}, {},
        DuplicateReferencePolicy::RemapInternalTargets, coverage.bindings, coverage.assured);
    require(remapped.status == CommandStatus::Changed && remapped.affectedObjectIds.size() == 2,
            "remap policy must duplicate both selected roots as one complete mapping");
    Json *remappedA = find_object(remapDocument.root, remapped.affectedObjectIds[0]);
    require(remappedA != nullptr &&
                remappedA->at("components")[0].at("behaviors")[0].at("selfTarget") ==
                    remapped.affectedObjectIds[0] &&
                remappedA->at("components")[0].at("behaviors")[0].at("childTarget") ==
                    remappedA->at("children")[0].at("id") &&
                remappedA->at("components")[0].at("behaviors")[0].at("componentTarget") ==
                    remappedA->at("children")[0].at("components")[0].at("id") &&
                remappedA->at("components")[0].at("behaviors")[0].at("crossRoot") ==
                    remapped.affectedObjectIds[1] &&
                remappedA->at("components")[0].at("behaviors")[0].at("externalTarget") == 30 &&
                remappedA->at("components")[0].at("behaviors")[0].at("opaqueInteger") == 777,
            "one multi-root map must remap typed internal sites while preserving external and opaque values");
    const Json remappedCopy = *remappedA;
    require(remapCommands.undo(remapDocument, selection).status == CommandStatus::Changed &&
                remapDocument.root == original,
            "multi-root duplication must undo in one step");
    require(remapCommands.redo(remapDocument, selection).status == CommandStatus::Changed &&
                *find_object(remapDocument.root, remapped.affectedObjectIds[0]) == remappedCopy,
            "multi-root duplicate redo must retain exact allocated identities and references");
}

void test_legacy_settings_migrate_to_preserve_duplicate_targets()
{
    TemporaryDirectory temporary;
    const auto paths = make_project_preference_paths(temporary.path() / "prefs", temporary.path());
    std::filesystem::create_directories(paths.settingsPath.parent_path());
    {
        std::ofstream settings(paths.settingsPath, std::ios::binary);
        settings << Json{{"version", 1}, {"projectKey", paths.projectKey}}.dump(2);
    }
    const auto migrated = load_workspace_preferences(paths.settingsPath, paths.layoutPath,
                                                      temporary.path());
    require(migrated.migrated && !migrated.recovered &&
                migrated.preferences.duplicationReferencePolicy == "preserve-existing-targets",
            "legacy project preferences without a duplicate policy must migrate to PreserveExistingTargets");
}

void test_template_v1_is_lossless_and_checks_typed_bindings()
{
    Json envelope = make_template_envelope("Enemy assembly", valid_template_root(), valid_bindings());
    envelope["authorExtension"] = Json{{"keep", true}};
    envelope["root"]["components"][0]["behaviors"][0]["unknown"] = Json{{"nested", "keep"}};
    const auto loaded = load_template_envelope(envelope);
    require(loaded && loaded.asset->root() == envelope["root"] &&
                loaded.asset->reference_bindings().size() == 3 &&
                loaded.asset->serialize() == envelope &&
                loaded.asset->root()["components"][0]["behaviors"][0]["opaqueInteger"] == 774,
            "version-1 templates must preserve unknown envelope/root data and opaque integers losslessly");

    const auto textParsed = parse_template_envelope(envelope.dump());
    require(textParsed && textParsed.asset->serialize() == envelope,
            "JSON text parsing must retain the complete validated template envelope");
    const auto malformed = parse_template_envelope("{");
    require(!malformed && malformed.error.find("could not be parsed") != std::string::npos,
            "malformed template JSON must return a useful diagnostic");
}

void test_template_file_save_is_atomic_and_refuses_overwrite_conflicts()
{
    TemporaryDirectory temporary;
    const auto path = temporary.path() / "templates" / "assembly.pttemplate";
    std::filesystem::create_directories(path.parent_path());
    const Json envelope = make_template_envelope("Assembly", valid_template_root(), valid_bindings());
    const std::string original = envelope.dump(2) + "\n";
    const auto created = write_file_atomically(path, original, false, std::nullopt, true);
    require(created.succeeded(), "template save must atomically create a new file");
    const auto conflict = write_file_atomically(path, "replacement", false, std::nullopt, true);
    require(conflict.status == AtomicWriteStatus::Conflict,
            "template save must not overwrite a destination that appeared after selection");
    std::ifstream input(path, std::ios::binary);
    const std::string retained((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto reopened = parse_template_envelope(retained);
    require(reopened && reopened.asset->serialize() == envelope,
            "conflict refusal must preserve the original complete template contents");
}

void test_instantiated_template_is_consumed_by_game_scene_loader()
{
    TemporaryDirectory temporary;
    auto parsed = parse_document(scene({object(10, "Existing")}));
    require(static_cast<bool>(parsed), "game-consumer destination scene must parse");
    const Json templateRoot = object(20, "Template Root", Json::array(),
                                     Json::array({object(21, "Template Child")}));
    const auto loaded = load_template_envelope(make_template_envelope("Runtime Fixture", templateRoot));
    require(static_cast<bool>(loaded), "game-consumer template must validate");
    const auto coverage = template_asset_binding_coverage(*loaded.asset);
    EditCommandService commands;
    const auto inserted = insert_template(commands, *parsed.document, *loaded.asset, {}, coverage);
    require(static_cast<bool>(inserted), "game-consumer template should instantiate: " + inserted.error);
    const auto scenePath = temporary.path() / "instantiated.scene";
    {
        std::ofstream output(scenePath, std::ios::binary);
        output << parsed.document->root.dump(2) << '\n';
    }

    RuntimeOptions runtimeOptions;
    runtimeOptions.assetRoot = temporary.path().string();
    runtimeOptions.windowSettings.headless = true;
    platformator::Runtime runtime(runtimeOptions);
    runtime.loadScene(scenePath.string());
    require(runtime.getGameObject("Template Root") != nullptr &&
                runtime.getGameObject("Template Child") != nullptr &&
                runtime.getObjectById(inserted.sourceToDestinationIds.at(20)) != nullptr &&
                runtime.getObjectById(inserted.sourceToDestinationIds.at(21)) != nullptr,
            "the ordinary game Runtime must load the instantiated scene with allocated template identities");
}

void test_template_failure_first_validation_cases()
{
    Json envelope = make_template_envelope("Failure fixture", valid_template_root(), valid_bindings());
    const auto reject = [&](Json candidate, std::string_view message) {
        const auto result = load_template_envelope(std::move(candidate));
        require(!result && !result.error.empty(), message);
    };

    auto candidate = envelope;
    candidate["version"] = 2;
    reject(std::move(candidate), "unsupported template versions must fail with an explanation");
    candidate = envelope;
    candidate["root"]["children"] = Json::object();
    reject(std::move(candidate), "malformed object child collections must be rejected");
    candidate = envelope;
    candidate["root"]["children"][0]["id"] = 10;
    reject(std::move(candidate), "duplicate object identities across the subtree must be rejected");
    candidate = envelope;
    candidate["root"]["children"][0]["components"][0]["id"] = 11;
    reject(std::move(candidate), "duplicate component identities across the subtree must be rejected");
    candidate = envelope;
    candidate["reference_bindings"][0]["path"] = "/behaviors/0/type";
    reject(std::move(candidate), "bindings cannot overwrite behavior kind discriminators");
    candidate = envelope;
    candidate["reference_bindings"][0]["path"] = "/behaviors/0/target~2value";
    reject(std::move(candidate), "invalid JSON pointer escapes must be rejected");
    candidate = envelope;
    candidate["reference_bindings"][0]["owner_id"] = 404;
    reject(std::move(candidate), "binding owner identities must resolve within the template");
    candidate = envelope;
    candidate["reference_bindings"][0]["component_id"] = 13;
    reject(std::move(candidate), "binding components must belong to the declared owner object");
    candidate = envelope;
    candidate["reference_bindings"][0]["target_kind"] = "Component";
    reject(std::move(candidate), "internal targets must resolve to the declared target kind");
    candidate = envelope;
    candidate["reference_bindings"][2].erase("source_scene_identity");
    reject(std::move(candidate), "external bindings require source-scene and original-target identity");
    candidate = envelope;
    candidate["reference_bindings"][2]["original_target_id"] = 12;
    reject(std::move(candidate), "external metadata must agree with the authored target ID");
    candidate = envelope;
    candidate["reference_bindings"].push_back(candidate["reference_bindings"][0]);
    reject(std::move(candidate), "duplicate declared reference sites must be rejected");
}
} // namespace

int main()
{
    try
    {
        test_template_v1_is_lossless_and_checks_typed_bindings();
        test_template_file_save_is_atomic_and_refuses_overwrite_conflicts();
        test_template_failure_first_validation_cases();
        test_typed_binding_collection_and_opaque_schema_gate();
        test_referencefields_author_declaration_resolves_escaped_site();
        test_scene_identity_distinguishes_replaced_files();
        test_template_insert_allocates_maps_and_retains_exact_redo_payload();
        test_duplicate_reference_policies_and_multi_root_identity_map();
        test_legacy_settings_migrate_to_preserve_duplicate_targets();
        test_instantiated_template_is_consumed_by_game_scene_loader();
        std::cout << "Template, binding, insertion, and duplicate cases passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "platformator_editor_template_test: " << error.what() << '\n';
        return 1;
    }
}
