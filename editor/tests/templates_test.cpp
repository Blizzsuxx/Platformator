#include "templates.h"
#include "documentio.h"
#include "documentmanager.h"
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

void test_reference_array_override_ancestor_and_revert()
{
    TemporaryDirectory temporary;
    const auto assetRoot = temporary.path() / "assets";
    const auto templatePath = assetRoot / "templates" / "ArrayLinks.pttemplate";
    std::filesystem::create_directories(templatePath.parent_path());

    Json sourceRoot = object(1, "Array root", Json::array({component(2, 5,
        Json{{"behaviors", Json::array({Json{{"type", "ArrayLink"},
            {"refs", Json::array({Json{{"target", 3}}})}}})}})}),
        Json::array({object(3, "Original target"), object(4, "Retargeted source target")}));
    const Json bindings = Json::array({Json{{"owner_id", 1}, {"component_id", 2},
        {"path", "/behaviors/0/refs/0/target"}, {"target_kind", "GameObject"},
        {"scope", "internal"}}});
    Json envelope = make_template_envelope("ArrayLinks", sourceRoot, bindings);
    write_json(templatePath, envelope);
    const auto loaded = load_template_envelope(envelope);
    require(static_cast<bool>(loaded), "array reference template fixture must validate");

    Json instanceRoot = sourceRoot;
    instanceRoot["id"] = 10;
    instanceRoot["template"] = "templates/ArrayLinks.pttemplate";
    instanceRoot["templateNodeId"] = 1;
    instanceRoot["templateSourceKeys"] = Json::array({
        "active", "name", "position", "rotation", "scale", "tag"});
    instanceRoot["templateSourcePaths"] = Json::array({
        "component[id=2].behaviors[0].refs[0].target"});
    instanceRoot["components"][0]["id"] = 11;
    instanceRoot["components"][0]["templateComponentId"] = 2;
    instanceRoot["components"][0]["templateSourceFields"] = Json::array({"behaviors", "type"});
    instanceRoot["components"][0]["behaviors"][0]["refs"][0]["target"] = 12;
    instanceRoot["children"][0]["id"] = 12;
    instanceRoot["children"][0]["templateNodeId"] = 3;
    instanceRoot["children"][1]["id"] = 13;
    instanceRoot["children"][1]["templateNodeId"] = 4;
    instanceRoot["children"][0]["templateSourceKeys"] = Json::array({
        "active", "name", "position", "rotation", "scale", "tag"});
    instanceRoot["children"][1]["templateSourceKeys"] = instanceRoot["children"][0]["templateSourceKeys"];

    auto parsed = parse_document(scene({instanceRoot}));
    require(static_cast<bool>(parsed), "array reference instance fixture must parse");
    parsed.document->projectRoot = temporary.path();
    parsed.document->assetRoot = assetRoot;
    rebuild_document_indexes(*parsed.document);
    EditCommandService commands;
    SelectionModel selection;
    mark_template_override(*parsed.document, 10, "component[id=2].behaviors[0].refs");
    require(commands.set_property(*parsed.document,
                address_for_document(*parsed.document, 10, 11, 0,
                                     {std::string("refs")}),
                Json::array({Json{{"target", 10}}})).status == CommandStatus::Changed,
            "an array of reference records can be overridden as a container");
    require(find_document_object_by_id(*parsed.document, 10)->at("components")[0]
                .at("behaviors")[0].at("refs")[0].at("target") == 10,
            "the array container override must be present before source propagation");

    sourceRoot["components"][0]["behaviors"][0]["refs"][0]["target"] = 4;
    envelope = make_template_envelope("ArrayLinks", sourceRoot, bindings);
    const auto retargeted = load_template_envelope(envelope);
    require(static_cast<bool>(retargeted), "retargeted array reference template must validate");
    write_json(templatePath, envelope);
    const auto propagated = propagate_template_to_scene(*parsed.document, commands,
                "templates/ArrayLinks.pttemplate", *retargeted.asset);
    require(propagated == 1 &&
                find_document_object_by_id(*parsed.document, 10)->at("components")[0]
                    .at("behaviors")[0].at("refs")[0].at("target") == 10,
            "reference synchronization must honor an overridden ancestor array: count=" +
                std::to_string(propagated) + " root=" +
                find_document_object_by_id(*parsed.document, 10)->dump());

    std::string revertError;
    require(revert_template_property(*parsed.document, commands, 10, 11, 0,
                {std::string("refs"), std::size_t{0}, std::string("target")},
                assetRoot, revertError) &&
                find_document_object_by_id(*parsed.document, 10)->at("components")[0]
                    .at("behaviors")[0].at("refs")[0].at("target") == 13 &&
                commands.undo(*parsed.document, selection).status == CommandStatus::Changed &&
                find_document_object_by_id(*parsed.document, 10)->at("components")[0]
                    .at("behaviors")[0].at("refs")[0].at("target") == 10 &&
                commands.redo(*parsed.document, selection).status == CommandStatus::Changed &&
                find_document_object_by_id(*parsed.document, 10)->at("components")[0]
                    .at("behaviors")[0].at("refs")[0].at("target") == 13 &&
                !is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].refs"),
            "Revert Property on an array element must clear the parent array override and remain undoable");
}

void test_grouped_revert_clears_child_template_override_metadata()
{
    TemporaryDirectory temporary;
    const auto assetRoot = temporary.path() / "assets";
    const auto templatePath = assetRoot / "templates" / "ChildOverride.pttemplate";
    std::filesystem::create_directories(templatePath.parent_path());

    Json sourceRoot = object(1, "Root", Json::array(),
                             Json::array({object(2, "Source child")}));
    const Json envelope = make_template_envelope("ChildOverride", sourceRoot);
    write_json(templatePath, envelope);

    Json instanceRoot = sourceRoot;
    instanceRoot["id"] = 10;
    instanceRoot["template"] = "templates/ChildOverride.pttemplate";
    instanceRoot["templateNodeId"] = 1;
    instanceRoot["templateSourceKeys"] = Json::array({
        "active", "name", "position", "rotation", "scale", "tag"});
    instanceRoot["children"][0]["id"] = 20;
    instanceRoot["children"][0]["templateNodeId"] = 2;
    instanceRoot["children"][0]["templateSourceKeys"] = instanceRoot["templateSourceKeys"];
    instanceRoot["children"][0]["name"] = "Instance child";
    instanceRoot["children"][0]["overrides"] = Json::array({"name"});

    auto parsed = parse_document(scene({instanceRoot}));
    require(static_cast<bool>(parsed), "child template override fixture must parse: " + parsed.error);
    parsed.document->projectRoot = temporary.path();
    parsed.document->assetRoot = assetRoot;
    rebuild_document_indexes(*parsed.document);
    EditCommandService commands;
    SelectionModel selection;
    std::string error;
    const std::vector<TemplatePropertyTarget> targets{{
        20, std::nullopt, std::nullopt, {std::string("name")}}};

    require(revert_template_properties(*parsed.document, commands, targets, assetRoot, error) &&
                find_document_object_by_id(*parsed.document, 20)->value("name", std::string{}) ==
                    "Source child" &&
                !find_document_object_by_id(*parsed.document, 20)->contains("overrides"),
            "grouped Revert must clear overrides stored on the affected child templateNodeId: " +
                error + " child=" + find_document_object_by_id(*parsed.document, 20)->dump());
    require(commands.undo(*parsed.document, selection).status == CommandStatus::Changed &&
                find_document_object_by_id(*parsed.document, 20)->value("name", std::string{}) ==
                    "Instance child" &&
                find_document_object_by_id(*parsed.document, 20)->value("overrides", Json{}) ==
                    Json::array({"name"}) &&
                commands.redo(*parsed.document, selection).status == CommandStatus::Changed &&
                find_document_object_by_id(*parsed.document, 20)->value("name", std::string{}) ==
                    "Source child" &&
                !find_document_object_by_id(*parsed.document, 20)->contains("overrides"),
            "child override cleanup must be atomic with the value edit and remain undoable");
}

void test_array_override_paths_follow_compacted_elements()
{
    TemporaryDirectory temporary;
    const auto assetRoot = temporary.path() / "assets";
    const auto templatePath = assetRoot / "templates" / "ArrayCompaction.pttemplate";
    std::filesystem::create_directories(templatePath.parent_path());
    Json sourceRoot = object(1, "Array source");
    sourceRoot["values"] = Json::array({1, 2, 3});
    Json envelope = make_template_envelope("ArrayCompaction", sourceRoot, Json::array());
    write_json(templatePath, envelope);
    auto loaded = load_template_envelope(envelope);
    require(static_cast<bool>(loaded), "array compaction template must validate");

    Json instanceRoot = sourceRoot;
    instanceRoot["id"] = 10;
    instanceRoot["template"] = "templates/ArrayCompaction.pttemplate";
    instanceRoot["templateNodeId"] = 1;
    instanceRoot["templateSourceKeys"] = Json::array({
        "active", "name", "position", "rotation", "scale", "tag", "values"});
    instanceRoot["templateSourcePaths"] = Json::array({
        "values[0]", "values[1]", "values[2]"});
    auto parsed = parse_document(scene({instanceRoot}));
    require(static_cast<bool>(parsed), "array compaction instance must parse");
    parsed.document->projectRoot = temporary.path();
    parsed.document->assetRoot = assetRoot;
    rebuild_document_indexes(*parsed.document);
    EditCommandService commands;
    SelectionModel selection;
    mark_template_override(*parsed.document, 10, "values[2]");
    require(commands.set_property(*parsed.document,
                address_for_document(*parsed.document, 10, std::nullopt, std::nullopt,
                                     {std::string("values"), std::size_t{2}}), 99).status ==
                CommandStatus::Changed,
            "the last source array element can carry an instance override");

    sourceRoot["values"] = Json::array({8});
    envelope = make_template_envelope("ArrayCompaction", sourceRoot, Json::array());
    write_json(templatePath, envelope);
    loaded = load_template_envelope(envelope);
    require(static_cast<bool>(loaded) &&
                propagate_template_to_scene(*parsed.document, commands,
                    "templates/ArrayCompaction.pttemplate", *loaded.asset) == 1,
            "shrinking the source array must propagate into its linked instance");
    const Json *instance = find_document_object_by_id(*parsed.document, 10);
    require(instance != nullptr && instance->at("values") == Json::array({8, 99}) &&
                is_property_overridden(*instance, "values[1]") &&
                !is_property_overridden(*instance, "values[2]"),
            "compacting an overridden tail element must move its value and override path together");

    sourceRoot["values"] = Json::array({8, 7});
    envelope = make_template_envelope("ArrayCompaction", sourceRoot, Json::array());
    write_json(templatePath, envelope);
    loaded = load_template_envelope(envelope);
    require(static_cast<bool>(loaded) &&
                propagate_template_to_scene(*parsed.document, commands,
                    "templates/ArrayCompaction.pttemplate", *loaded.asset) == 1,
            "growing the source array after compaction must propagate repeatedly");
    instance = find_document_object_by_id(*parsed.document, 10);
    require(instance != nullptr && instance->at("values") == Json::array({8, 99}) &&
                is_property_overridden(*instance, "values[1]"),
            "source growth must not overwrite or misidentify the compacted override");

    std::string revertError;
    require(revert_template_property(*parsed.document, commands, 10, std::nullopt, std::nullopt,
                {std::string("values"), std::size_t{1}}, assetRoot, revertError) &&
                find_document_object_by_id(*parsed.document, 10)->at("values") == Json::array({8, 7}) &&
                !is_property_overridden(*find_document_object_by_id(*parsed.document, 10), "values[1]") &&
                commands.undo(*parsed.document, selection).status == CommandStatus::Changed &&
                find_document_object_by_id(*parsed.document, 10)->at("values") == Json::array({8, 99}) &&
                is_property_overridden(*find_document_object_by_id(*parsed.document, 10), "values[1]") &&
                commands.redo(*parsed.document, selection).status == CommandStatus::Changed &&
                find_document_object_by_id(*parsed.document, 10)->at("values") == Json::array({8, 7}) &&
                !is_property_overridden(*find_document_object_by_id(*parsed.document, 10), "values[1]"),
            "Inspector override state, Revert, Undo, and Redo must follow the compacted array address: " +
                revertError);
}

void test_reverting_a_leaf_splits_ancestor_override()
{
    TemporaryDirectory temporary;
    const auto assetRoot = temporary.path() / "assets";
    const auto templatePath = assetRoot / "templates" / "RecordRevert.pttemplate";
    std::filesystem::create_directories(templatePath.parent_path());
    Json behavior{{"type", "RecordBehavior"},
                  {"config", Json{{"left", 1}, {"right", 2},
                                   {"numericSibling", 2}, {"componentReference", 2},
                                   {"mappedComponentReference", 2},
                                   {"nestedReferences", Json{{"links", Json::array({
                                       Json{{"target", 2}}})}}}}}};
    Json sourceRoot = object(1, "Record source", Json::array({component(2, 5,
        Json{{"behaviors", Json::array({behavior})}})}));
    const auto componentBinding = [](std::string path) {
        return Json{{"owner_id", 1}, {"component_id", 2}, {"path", std::move(path)},
                    {"target_kind", "Component"}, {"scope", "internal"}};
    };
    const Json bindings = Json::array({
        componentBinding("/behaviors/0/config/componentReference"),
        componentBinding("/behaviors/0/config/mappedComponentReference"),
        componentBinding("/behaviors/0/config/nestedReferences/links/0/target")});
    Json envelope = make_template_envelope("RecordRevert", sourceRoot, bindings);
    write_json(templatePath, envelope);

    Json instanceRoot = sourceRoot;
    instanceRoot["id"] = 10;
    instanceRoot["template"] = "templates/RecordRevert.pttemplate";
    instanceRoot["templateNodeId"] = 1;
    instanceRoot["templateSourceKeys"] = Json::array({
        "active", "name", "position", "rotation", "scale", "tag"});
    instanceRoot["templateSourcePaths"] = Json::array({
        "component[id=2].behaviors[0].config.left",
        "component[id=2].behaviors[0].config.right",
        "component[id=2].behaviors[0].config.numericSibling",
        "component[id=2].behaviors[0].config.componentReference",
        "component[id=2].behaviors[0].config.mappedComponentReference",
        "component[id=2].behaviors[0].config.nestedReferences.links[0].target"});
    instanceRoot["components"][0]["id"] = 11;
    instanceRoot["components"][0]["templateComponentId"] = 2;
    instanceRoot["components"][0]["templateSourceFields"] = Json::array({"behaviors", "type"});
    instanceRoot["components"][0]["behaviors"][0]["config"] =
        Json{{"left", 10}, {"right", 20}, {"numericSibling", 11},
             {"componentReference", 2}, {"mappedComponentReference", 11},
             {"nestedReferences", Json{{"links", Json::array({Json{{"target", 2}}})}}}};
    instanceRoot["overrides"] = Json::array({"component[id=2].behaviors[0]"});
    Json externalTarget = object(20, "Separate component target", Json::array({component(2, 2)}));
    auto parsed = parse_document(scene({instanceRoot, externalTarget}));
    require(static_cast<bool>(parsed), "record revert instance must parse: " + parsed.error);
    parsed.document->projectRoot = temporary.path();
    parsed.document->assetRoot = assetRoot;
    rebuild_document_indexes(*parsed.document);
    EditCommandService commands;
    SelectionModel selection;

    std::string revertError;
    const bool didRevert = revert_template_property(*parsed.document, commands, 10, 11, 0,
                {std::string("config"), std::string("left")}, assetRoot, revertError);
    require(didRevert &&
                find_document_object_by_id(*parsed.document, 10)->at("components")[0]
                    .at("behaviors")[0].at("config") ==
                    Json{{"left", 1}, {"right", 20}, {"numericSibling", 11},
                         {"componentReference", 2}, {"mappedComponentReference", 11},
                         {"nestedReferences", Json{{"links", Json::array({Json{{"target", 2}}})}}}} &&
                is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].config.right") &&
                is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].config.numericSibling") &&
                is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].config.componentReference") &&
                !is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].config.mappedComponentReference") &&
                is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].config.nestedReferences") &&
                !is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0]"),
            "leaf Revert must retain a sibling when splitting a local behavior-record override: " +
                revertError);
    require(commands.undo(*parsed.document, selection).status == CommandStatus::Changed &&
                find_document_object_by_id(*parsed.document, 10)->at("components")[0]
                    .at("behaviors")[0].at("config") ==
                    Json{{"left", 10}, {"right", 20}, {"numericSibling", 11},
                         {"componentReference", 2}, {"mappedComponentReference", 11},
                         {"nestedReferences", Json{{"links", Json::array({Json{{"target", 2}}})}}}} &&
                is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0]") &&
                commands.redo(*parsed.document, selection).status == CommandStatus::Changed &&
                find_document_object_by_id(*parsed.document, 10)->at("components")[0]
                    .at("behaviors")[0].at("config") ==
                    Json{{"left", 1}, {"right", 20}, {"numericSibling", 11},
                         {"componentReference", 2}, {"mappedComponentReference", 11},
                         {"nestedReferences", Json{{"links", Json::array({Json{{"target", 2}}})}}}} &&
                is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].config.right") &&
                is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].config.numericSibling") &&
                !is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].config.mappedComponentReference") &&
                is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].config.componentReference") &&
                is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].config.nestedReferences"),
            "leaf Revert must record the value and split override change atomically for Undo and Redo");

    sourceRoot["components"][0]["behaviors"][0]["config"] =
        Json{{"left", 3}, {"right", 4}, {"numericSibling", 3},
             {"componentReference", 2}, {"mappedComponentReference", 2},
             {"nestedReferences", Json{{"links", Json::array({Json{{"target", 2}}})}}}};
    envelope = make_template_envelope("RecordRevert", sourceRoot, bindings);
    write_json(templatePath, envelope);
    const auto loaded = load_template_envelope(envelope);
    require(static_cast<bool>(loaded) &&
                propagate_template_to_scene(*parsed.document, commands,
                    "templates/RecordRevert.pttemplate", *loaded.asset) == 1 &&
                find_document_object_by_id(*parsed.document, 10)->at("components")[0]
                    .at("behaviors")[0].at("config") ==
                    Json{{"left", 3}, {"right", 20}, {"numericSibling", 11},
                         {"componentReference", 2}, {"mappedComponentReference", 11},
                         {"nestedReferences", Json{{"links", Json::array({Json{{"target", 2}}})}}}} &&
                is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].config.right") &&
                is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].config.numericSibling") &&
                !is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].config.mappedComponentReference") &&
                is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].config.componentReference") &&
                is_property_overridden(*find_document_object_by_id(*parsed.document, 10),
                    "component[id=2].behaviors[0].config.nestedReferences"),
            "later source updates must reach the reverted leaf while preserving the independently edited sibling");
}

void test_reserved_behavior_metadata_names_remain_authored_data()
{
    TemporaryDirectory temporary;
    const auto assetRoot = temporary.path() / "assets";
    const auto templatePath = assetRoot / "templates" / "ReservedFields.pttemplate";
    std::filesystem::create_directories(templatePath.parent_path());

    Json behavior{{"type", "ReservedFieldsBehavior"}, {"id", 1},
                  {"children", Json::array({2, 3})}, {"components", 4},
                  {"overrides", Json::array({5, 6})},
                  {"config", Json{{"id", 7}, {"children", Json::array({8, 9})},
                                   {"components", 10}, {"overrides", Json::array({11, 12})},
                                   {"nested", Json{{"id", 13}, {"children", Json::array({14})},
                                                    {"components", 15},
                                                    {"overrides", Json::array({16})}}},
                                   {"neighbor", "source"}, {"removed", "old"}}},
                  {"removedTop", "old"}};
    Json sourceRoot = object(1, "Reserved source", Json::array({component(2, 5,
        Json{{"behaviors", Json::array({behavior})}})}));
    Json envelope = make_template_envelope("ReservedFields", sourceRoot);
    write_json(templatePath, envelope);
    auto loaded = load_template_envelope(envelope);
    require(static_cast<bool>(loaded), "reserved-field template must validate");

    auto parsed = parse_document(scene({object(90, "Host")}));
    require(static_cast<bool>(parsed), "reserved-field destination scene must parse");
    parsed.document->projectRoot = temporary.path();
    parsed.document->assetRoot = assetRoot;
    rebuild_document_indexes(*parsed.document);
    EditCommandService commands;
    SelectionModel selection;
    require(instantiate_asset_drop(*parsed.document, commands, selection, templatePath.string(),
                                   EditorAssetKind::Template),
            "first reserved-field template instance must be inserted");
    const int firstInstanceId = *selection.current().objectId;
    require(instantiate_asset_drop(*parsed.document, commands, selection, templatePath.string(),
                                   EditorAssetKind::Template),
            "second reserved-field template instance must be inserted");
    const int secondInstanceId = *selection.current().objectId;
    const Json *firstInstance = find_document_object_by_id(*parsed.document, firstInstanceId);
    require(firstInstance != nullptr, "first reserved-field instance must remain in the scene");
    const int componentInstanceId = firstInstance->at("components")[0].at("id").get<int>();
    const auto has_source_path = [](const Json &paths, std::string_view path) {
        return paths.is_array() && std::any_of(paths.begin(), paths.end(), [&](const Json &entry) {
            return entry.is_string() && entry.get<std::string>() == path;
        });
    };
    const Json sourcePaths = firstInstance->value("templateSourcePaths", Json::array());
    require(has_source_path(sourcePaths, "component[id=2].behaviors[0].id") &&
                has_source_path(sourcePaths, "component[id=2].behaviors[0].children[0]") &&
                has_source_path(sourcePaths, "component[id=2].behaviors[0].components") &&
                has_source_path(sourcePaths, "component[id=2].behaviors[0].overrides[0]") &&
                has_source_path(sourcePaths, "component[id=2].behaviors[0].config.id") &&
                has_source_path(sourcePaths, "component[id=2].behaviors[0].config.nested.overrides[0]"),
            "source-path collection must include reserved names inside behavior and nested data payloads");

    const auto read_behavior_field = [&](int objectId,
                                         const std::vector<FieldPathSegment> &fieldPath) -> Json {
        const Json *root = find_document_object_by_id(*parsed.document, objectId);
        require(root != nullptr, "template instance must remain available while reading a field");
        const Json *value = &root->at("components")[0].at("behaviors")[0];
        for (const auto &part : fieldPath)
        {
            if (const auto *key = std::get_if<std::string>(&part))
                value = &value->at(*key);
            else
                value = &value->at(std::get<std::size_t>(part));
        }
        return *value;
    };
    const auto check_edit_history = [&](std::vector<FieldPathSegment> fieldPath,
                                        Json original, Json edited, std::string overridePath) {
        const auto address = address_for_document(*parsed.document, firstInstanceId,
            componentInstanceId, 0, fieldPath);
        const auto changed = commands.set_property(*parsed.document, address, edited);
        const Json *instance = find_document_object_by_id(*parsed.document, firstInstanceId);
        require(changed.status == CommandStatus::Changed &&
                    read_behavior_field(firstInstanceId, fieldPath) == edited &&
                    is_property_overridden(*instance, overridePath),
                "editing a behavior field named like metadata must atomically record its override: " + overridePath);
        require(commands.undo(*parsed.document, selection).status == CommandStatus::Changed &&
                    read_behavior_field(firstInstanceId, fieldPath) == original &&
                    !is_property_overridden(*find_document_object_by_id(*parsed.document, firstInstanceId),
                                            overridePath),
                "Undo must restore both the authored metadata-like field and its override marker: " + overridePath);
        require(commands.redo(*parsed.document, selection).status == CommandStatus::Changed &&
                    read_behavior_field(firstInstanceId, fieldPath) == edited &&
                    is_property_overridden(*find_document_object_by_id(*parsed.document, firstInstanceId),
                                           overridePath),
                "Redo must restore both the authored metadata-like field and its override marker: " + overridePath);
        require(commands.undo(*parsed.document, selection).status == CommandStatus::Changed &&
                    read_behavior_field(firstInstanceId, fieldPath) == original &&
                    !is_property_overridden(*find_document_object_by_id(*parsed.document, firstInstanceId),
                                            overridePath),
                "the history fixture must return to its source value after the Undo/Redo check");
    };
    check_edit_history({std::string("id")}, 1, 31,
        "component[id=2].behaviors[0].id");
    check_edit_history({std::string("children")}, Json::array({2, 3}), Json::array({20, 30}),
        "component[id=2].behaviors[0].children");
    check_edit_history({std::string("components")}, 4, 40,
        "component[id=2].behaviors[0].components");
    check_edit_history({std::string("overrides")}, Json::array({5, 6}), Json::array({50, 60}),
        "component[id=2].behaviors[0].overrides");
    check_edit_history({std::string("config"), std::string("id")}, 7, 70,
        "component[id=2].behaviors[0].config.id");
    check_edit_history({std::string("config"), std::string("children")},
        Json::array({8, 9}), Json::array({80, 90}),
        "component[id=2].behaviors[0].config.children");
    check_edit_history({std::string("config"), std::string("components")}, 10, 100,
        "component[id=2].behaviors[0].config.components");
    check_edit_history({std::string("config"), std::string("overrides")},
        Json::array({11, 12}), Json::array({110, 120}),
        "component[id=2].behaviors[0].config.overrides");

    const auto set_local_field = [&](std::vector<FieldPathSegment> fieldPath,
                                     Json value, std::string overridePath) {
        const auto changed = commands.set_property(*parsed.document,
            address_for_document(*parsed.document, firstInstanceId, componentInstanceId, 0, fieldPath),
            std::move(value));
        require(changed.status == CommandStatus::Changed &&
                    is_property_overridden(*find_document_object_by_id(*parsed.document, firstInstanceId),
                                           overridePath),
                "local metadata-like behavior edits must have their own stable override paths: " + overridePath);
    };
    set_local_field({std::string("id")}, 31, "component[id=2].behaviors[0].id");
    set_local_field({std::string("children")}, Json::array({20, 30}),
                    "component[id=2].behaviors[0].children");
    set_local_field({std::string("components")}, 40,
                    "component[id=2].behaviors[0].components");
    set_local_field({std::string("overrides")}, Json::array({50, 60}),
                    "component[id=2].behaviors[0].overrides");
    set_local_field({std::string("config"), std::string("id")}, 70,
                    "component[id=2].behaviors[0].config.id");
    set_local_field({std::string("config"), std::string("children")}, Json::array({80, 90}),
                    "component[id=2].behaviors[0].config.children");
    set_local_field({std::string("config"), std::string("components")}, 100,
                    "component[id=2].behaviors[0].config.components");
    set_local_field({std::string("config"), std::string("overrides")}, Json::array({110, 120}),
                    "component[id=2].behaviors[0].config.overrides");
    set_local_field({std::string("config"), std::string("nested"), std::string("id")}, 130,
                    "component[id=2].behaviors[0].config.nested.id");
    set_local_field({std::string("config"), std::string("nested"), std::string("children")},
                    Json::array({140}), "component[id=2].behaviors[0].config.nested.children");
    set_local_field({std::string("config"), std::string("nested"), std::string("components")}, 150,
                    "component[id=2].behaviors[0].config.nested.components");
    set_local_field({std::string("config"), std::string("nested"), std::string("overrides")},
                    Json::array({160}), "component[id=2].behaviors[0].config.nested.overrides");
    set_local_field({std::string("config"), std::string("neighbor")}, "instance",
                    "component[id=2].behaviors[0].config.neighbor");
    mark_template_override(*parsed.document, firstInstanceId,
                           "component[id=2].behaviors[0]");

    std::string revertError;
    require(revert_template_property(*parsed.document, commands, firstInstanceId,
                componentInstanceId, 0, {std::string("config"), std::string("neighbor")},
                assetRoot, revertError) &&
                read_behavior_field(firstInstanceId, {std::string("config"), std::string("neighbor")}) == "source",
            "reverting a neighboring behavior field must split its covering record override: " + revertError);
    const auto assert_local_reserved_values = [&] {
        const Json *instance = find_document_object_by_id(*parsed.document, firstInstanceId);
        return read_behavior_field(firstInstanceId, {std::string("id")}) == 31 &&
               read_behavior_field(firstInstanceId, {std::string("children")}) == Json::array({20, 30}) &&
               read_behavior_field(firstInstanceId, {std::string("components")}) == 40 &&
               read_behavior_field(firstInstanceId, {std::string("overrides")}) == Json::array({50, 60}) &&
               read_behavior_field(firstInstanceId, {std::string("config"), std::string("id")}) == 70 &&
               read_behavior_field(firstInstanceId,
                   {std::string("config"), std::string("children")}) == Json::array({80, 90}) &&
               read_behavior_field(firstInstanceId,
                   {std::string("config"), std::string("components")}) == 100 &&
               read_behavior_field(firstInstanceId,
                   {std::string("config"), std::string("overrides")}) == Json::array({110, 120}) &&
               read_behavior_field(firstInstanceId,
                   {std::string("config"), std::string("nested"), std::string("id")}) == 130 &&
               read_behavior_field(firstInstanceId,
                   {std::string("config"), std::string("nested"), std::string("children")}) == Json::array({140}) &&
               read_behavior_field(firstInstanceId,
                   {std::string("config"), std::string("nested"), std::string("components")}) == 150 &&
               read_behavior_field(firstInstanceId,
                   {std::string("config"), std::string("nested"), std::string("overrides")}) == Json::array({160}) &&
               is_property_overridden(*instance, "component[id=2].behaviors[0].id") &&
               is_property_overridden(*instance, "component[id=2].behaviors[0].children") &&
               is_property_overridden(*instance, "component[id=2].behaviors[0].components") &&
               is_property_overridden(*instance, "component[id=2].behaviors[0].overrides") &&
               is_property_overridden(*instance, "component[id=2].behaviors[0].config.id") &&
               is_property_overridden(*instance, "component[id=2].behaviors[0].config.children") &&
               is_property_overridden(*instance, "component[id=2].behaviors[0].config.components") &&
               is_property_overridden(*instance, "component[id=2].behaviors[0].config.overrides") &&
               is_property_overridden(*instance, "component[id=2].behaviors[0].config.nested");
    };
    require(assert_local_reserved_values(),
            "neighbor Revert must retain every changed metadata-like behavior field and its override");
    require(commands.undo(*parsed.document, selection).status == CommandStatus::Changed &&
                read_behavior_field(firstInstanceId, {std::string("config"), std::string("neighbor")}) == "instance" &&
                is_property_overridden(*find_document_object_by_id(*parsed.document, firstInstanceId),
                    "component[id=2].behaviors[0]"),
            "Undo must restore the local neighbor value and covering override as one operation");
    require(commands.redo(*parsed.document, selection).status == CommandStatus::Changed &&
                read_behavior_field(firstInstanceId, {std::string("config"), std::string("neighbor")}) == "source" &&
                assert_local_reserved_values(),
            "Redo must restore the reverted neighbor and all split metadata-like overrides atomically");

    Json &nextBehavior = sourceRoot["components"][0]["behaviors"][0];
    nextBehavior["id"] = 2;
    nextBehavior.erase("children");
    nextBehavior["components"] = 6;
    nextBehavior["overrides"] = Json::array({5, 7});
    nextBehavior["config"]["id"] = 17;
    nextBehavior.erase("removedTop");
    nextBehavior["config"].erase("children");
    nextBehavior["config"]["components"] = 20;
    nextBehavior["config"].erase("overrides");
    nextBehavior["config"].erase("removed");
    nextBehavior["config"]["nested"]["id"] = 21;
    nextBehavior["config"]["nested"].erase("children");
    nextBehavior["config"]["nested"]["components"] = 22;
    nextBehavior["config"]["nested"].erase("overrides");
    nextBehavior["config"]["neighbor"] = "updated source";
    envelope = make_template_envelope("ReservedFields", sourceRoot);
    write_json(templatePath, envelope);
    loaded = load_template_envelope(envelope);
    require(static_cast<bool>(loaded) &&
                propagate_template_to_scene(*parsed.document, commands,
                    "templates/ReservedFields.pttemplate", *loaded.asset) == 2,
            "reserved-field source edits must propagate to both linked instances");

    firstInstance = find_document_object_by_id(*parsed.document, firstInstanceId);
    const Json *secondInstance = find_document_object_by_id(*parsed.document, secondInstanceId);
    require(firstInstance != nullptr && secondInstance != nullptr &&
                read_behavior_field(firstInstanceId, {std::string("id")}) == 31 &&
                read_behavior_field(firstInstanceId, {std::string("children")}) == Json::array({20, 30}) &&
                read_behavior_field(firstInstanceId, {std::string("components")}) == 40 &&
                read_behavior_field(firstInstanceId, {std::string("overrides")}) == Json::array({50, 60}) &&
                read_behavior_field(firstInstanceId, {std::string("config"), std::string("id")}) == 70 &&
                read_behavior_field(firstInstanceId,
                    {std::string("config"), std::string("children")}) == Json::array({80, 90}) &&
                read_behavior_field(firstInstanceId,
                    {std::string("config"), std::string("components")}) == 100 &&
                read_behavior_field(firstInstanceId,
                    {std::string("config"), std::string("overrides")}) == Json::array({110, 120}) &&
                read_behavior_field(firstInstanceId,
                    {std::string("config"), std::string("nested"), std::string("id")}) == 130 &&
                read_behavior_field(firstInstanceId,
                    {std::string("config"), std::string("nested"), std::string("children")}) == Json::array({140}) &&
                read_behavior_field(firstInstanceId,
                    {std::string("config"), std::string("nested"), std::string("components")}) == 150 &&
                read_behavior_field(firstInstanceId,
                    {std::string("config"), std::string("nested"), std::string("overrides")}) == Json::array({160}) &&
                read_behavior_field(firstInstanceId,
                    {std::string("config"), std::string("neighbor")}) == "updated source" &&
                read_behavior_field(secondInstanceId, {std::string("id")}) == 2 &&
                !find_document_object_by_id(*parsed.document, secondInstanceId)->at("components")[0]
                    .at("behaviors")[0].contains("children") &&
                read_behavior_field(secondInstanceId, {std::string("components")}) == 6 &&
                read_behavior_field(secondInstanceId, {std::string("overrides")}) == Json::array({5, 7}) &&
                read_behavior_field(secondInstanceId, {std::string("config"), std::string("id")}) == 17 &&
                !find_document_object_by_id(*parsed.document, secondInstanceId)->at("components")[0]
                    .at("behaviors")[0].at("config").contains("children") &&
                read_behavior_field(secondInstanceId,
                    {std::string("config"), std::string("components")}) == 20 &&
                !find_document_object_by_id(*parsed.document, secondInstanceId)->at("components")[0]
                    .at("behaviors")[0].at("config").contains("overrides") &&
                !find_document_object_by_id(*parsed.document, secondInstanceId)->at("components")[0]
                    .at("behaviors")[0].at("config").contains("removed") &&
                read_behavior_field(secondInstanceId,
                    {std::string("config"), std::string("nested"), std::string("id")}) == 21 &&
                !find_document_object_by_id(*parsed.document, secondInstanceId)->at("components")[0]
                    .at("behaviors")[0].at("config").at("nested").contains("children") &&
                read_behavior_field(secondInstanceId,
                    {std::string("config"), std::string("nested"), std::string("components")}) == 22 &&
                !find_document_object_by_id(*parsed.document, secondInstanceId)->at("components")[0]
                    .at("behaviors")[0].at("config").at("nested").contains("overrides") &&
                read_behavior_field(secondInstanceId,
                    {std::string("config"), std::string("neighbor")}) == "updated source" &&
                !find_document_object_by_id(*parsed.document, secondInstanceId)->at("components")[0]
                    .at("behaviors")[0].contains("removedTop"),
            "source propagation must update and remove reserved-named data while preserving explicit overrides");
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

void test_linked_templates_flattening_and_live_sync()
{
    const auto tempDir = std::filesystem::temp_directory_path() / "platformator_linked_tmpl_test";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    const auto assetRoot = tempDir / "assets";
    const auto tmplDir = assetRoot / "templates";
    std::filesystem::create_directories(tmplDir);

    // 1. Single-level flattening on make_template_envelope
    Json nested = platformator_editor_test::object(10, "Outer", {}, {
        platformator_editor_test::object(11, "Inner")
    });
    nested["template"] = "templates/Old.pttemplate";
    nested["templateNodeId"] = 5;
    nested["overrides"] = Json::array({"name"});
    nested["children"][0]["templateNodeId"] = 6;
    const Json env = make_template_envelope("Outer", nested);
    require(!env["root"].contains("template") && !env["root"].contains("templateNodeId") &&
            !env["root"].contains("overrides") && !env["root"]["children"][0].contains("templateNodeId"),
            "make_template_envelope must flatten nested template metadata");

    // 2. Write template asset and open a scene + template in DocumentManager
    const auto tmplPath = tmplDir / "Crate.pttemplate";
    const Json templateSchema{{"schema_version", 1}, {"behaviors", Json::array({Json{
        {"name", "LinkBehavior"}, {"field_schema_available", true},
        {"fields", Json::array({
            Json{{"name", "target"}, {"kind", "object_reference"}, {"target", "GameObject"}},
            Json{{"name", "target2"}, {"kind", "object_reference"}, {"target", "GameObject"}}})}}})}};
    const Json initialTemplateBindings = Json::array({Json{
        {"owner_id", 1}, {"component_id", 4}, {"path", "/behaviors/0/target"},
        {"target_kind", "GameObject"}, {"scope", "internal"}}});
    {
        Json crateRoot = platformator_editor_test::object(1, "Crate",
            Json::array({component(4, 5, Json{{"behaviors", Json::array({
                Json{{"type", "LinkBehavior"}, {"target", 2}}})}})}),
            Json::array({object(2, "Crate child")}));
        crateRoot["tag"] = "Solid";
        crateRoot["config"] = Json{{"obsolete", true}, {"nested", Json{{"x", 1}, {"y", 2}}}};
        crateRoot["values"] = Json::array({1, 2, 3});
        std::ofstream out(tmplPath);
        out << make_template_envelope("Crate", crateRoot, initialTemplateBindings).dump(2);
    }

    DocumentManager manager(tempDir, assetRoot);
    manager.set_template_binding_collector([&](const DocumentSession &source, const Json &subtree,
                                                const Json &) {
        return collect_template_bindings(source, subtree, template_scene_identity(source), templateSchema);
    });
    auto *sceneTab = manager.active_tab();
    require(sceneTab != nullptr && sceneTab->kind() == DocumentTabKind::Scene,
            "DocumentManager must start with a Scene tab");
    auto &sceneDoc = sceneTab->workspace().document_for_edit();
    auto &sceneCmds = sceneTab->history();
    SelectionModel sceneSel;
    require(instantiate_asset_drop(sceneDoc, sceneCmds, sceneSel, tmplPath.string(),
                                   EditorAssetKind::Template, 5.0f, 3.0f),
            "instantiate_asset_drop must instantiate linked template");

    const int instId = *sceneSel.current().objectId;
    SelectionModel secondSceneSel;
    require(instantiate_asset_drop(sceneDoc, sceneCmds, secondSceneSel, tmplPath.string(),
                                   EditorAssetKind::Template, 8.0f, 3.0f),
            "a second linked template instance must instantiate from the saved reference contract");
    const int secondInstId = *secondSceneSel.current().objectId;
    require(instId != secondInstId,
            "two linked instances must receive distinct root identities");
    require(find_template_instance_root(sceneDoc, instId) == instId,
            "Instantiated object must be a linked template root");
    const Json *instObj = find_document_object_by_id(sceneDoc, instId);
    require(instObj != nullptr && instObj->value("template", std::string{}) == "templates/Crate.pttemplate",
            "Instantiated object must store normalized template path");

    // Mark override on "name" while keeping "tag" non-overridden.
    sceneCmds.set_property(sceneDoc, address_for_document(sceneDoc, instId, std::nullopt, std::nullopt, {std::string("name")}), "CustomCrate");
    mark_template_override(sceneDoc, instId, "name");
    require(is_property_overridden(*find_document_object_by_id(sceneDoc, instId), "name"),
            "Marked override must be recorded");

    // 3. Open template in Template Editing Mode, edit tag, save -> live sync to Scene!
    std::string tmplErr;
    const auto tmplTabId = manager.open_template(tmplPath, &tmplErr);
    require(tmplTabId.has_value(), "open_template must succeed: " + tmplErr);
    auto *tmplTab = manager.find(*tmplTabId);
    require(tmplTab != nullptr && tmplTab->kind() == DocumentTabKind::Template,
            "Template tab must have DocumentTabKind::Template");
    auto &tmplDoc = tmplTab->workspace().document_for_edit();
    const int tmplRootId = tmplDoc.indexes.orderedObjectIds.front();
    const auto setTagRes = tmplTab->history().set_property(
        tmplDoc, address_for_document(tmplDoc, tmplRootId, std::nullopt, std::nullopt, {std::string("tag")}),
        "Destructible");
    require(setTagRes.status == CommandStatus::Changed,
            "Template tab set_property(tag) must succeed: " + setTagRes.message);
    auto addedChild = tmplTab->history().create_object(tmplDoc, tmplRootId, "New source child");
    require(addedChild && !addedChild.command->payload["id"].is_null(),
            "template editing must retain an added source child in the document history");
    const int addedChildId = addedChild.command->payload["id"].get<int>();
    auto addedChildComponent = tmplTab->history().add_component(tmplDoc, addedChildId,
        component(0, 5, Json{{"behaviors", Json::array({
            Json{{"type", "LinkBehavior"}, {"target", addedChildId}}})},
            {"config", Json{{"state", "base"}}}}));
    require(addedChildComponent && addedChildComponent.command.has_value(),
            "a new source child can receive a reference-bearing component");
    const int addedChildComponentId = addedChildComponent.command->payload.at("id").get<int>();
    const auto setSecondTarget = tmplTab->history().set_property(
        tmplDoc, address_for_document(tmplDoc, tmplRootId, 4, 0, {std::string("target2")}), addedChildId);
    const auto retargetSourceReference = tmplTab->history().set_property(
        tmplDoc, address_for_document(tmplDoc, tmplRootId, 4, 0, {std::string("target")}), addedChildId);
    require(setSecondTarget.status == CommandStatus::Changed &&
                retargetSourceReference.status == CommandStatus::Changed,
            "a newly authored declared reference field must be editable before Save");
    const auto savedConfig = tmplTab->history().set_property(tmplDoc,
        address_for_document(tmplDoc, tmplRootId, std::nullopt, std::nullopt, {std::string("config")}),
        Json{{"nested", Json{{"x", 5}, {"newDefault", true}}}});
    const auto savedValues = tmplTab->history().set_property(tmplDoc,
        address_for_document(tmplDoc, tmplRootId, std::nullopt, std::nullopt, {std::string("values")}),
        Json::array({8}));
    require(savedConfig.status == CommandStatus::Changed && savedValues.status == CommandStatus::Changed,
            "source edits must update nested defaults and shorten source arrays");

    const Json *firstInstance = find_document_object_by_id(sceneDoc, instId);
    require(firstInstance != nullptr, "first linked instance remains in the destination scene");
    const int firstChildId = firstInstance->at("children")[0].at("id").get<int>();
    const auto firstTargetAddress = address_for_document(
        sceneDoc, instId, firstInstance->at("components")[0].at("id").get<int>(), 0,
        {std::string("target")});
    require(sceneCmds.set_property(sceneDoc, firstTargetAddress, instId).status == CommandStatus::Changed,
            "the first instance can keep a local internal-reference target");
    mark_template_override(sceneDoc, instId, "component[id=4].behaviors[0].target");
    mark_template_override(sceneDoc, instId, "config.nested.x");
    mark_template_override(sceneDoc, instId, "values[2]");
    sceneCmds.set_property(sceneDoc, address_for_document(sceneDoc, instId, std::nullopt, std::nullopt,
        {std::string("config"), std::string("nested"), std::string("x")}), 77);
    sceneCmds.set_property(sceneDoc, address_for_document(sceneDoc, instId, std::nullopt, std::nullopt,
        {std::string("config"), std::string("localOnly")}), "instance-local");
    sceneCmds.set_property(sceneDoc, address_for_document(sceneDoc, instId, std::nullopt, std::nullopt,
        {std::string("values"), std::size_t{2}}), 99);
    const Json *secondBeforeSync = find_document_object_by_id(sceneDoc, secondInstId);
    const int secondScriptId = secondBeforeSync->at("components")[0].value("id", -1);
    mark_template_override(sceneDoc, secondInstId, "component[id=4].behaviors[0]");
    require(sceneCmds.set_property(sceneDoc,
                address_for_document(sceneDoc, secondInstId, secondScriptId, 0,
                                     {std::string("target")}), secondInstId).status ==
                CommandStatus::Changed,
            "a whole behavior record can keep a reference value override");
    const std::size_t sceneUndoCountBeforeSave = sceneCmds.history_status().undoCommands;
    const bool templateSaved = manager.save_template_tab(*tmplTabId, &tmplErr);
    require(templateSaved, "save_template_tab must succeed: " + tmplErr);
    const auto sceneUndoCountAfterSave = sceneCmds.history_status().undoCommands;
    require(sceneUndoCountAfterSave == sceneUndoCountBeforeSave + 1,
            "propagating one template save across multiple linked instances must create one Undo step (before=" +
                std::to_string(sceneUndoCountBeforeSave) + ", after=" +
                std::to_string(sceneUndoCountAfterSave) + ")");

    std::ifstream savedTemplateInput(tmplPath, std::ios::binary);
    const auto savedTemplate = parse_template_envelope(
        std::string{std::istreambuf_iterator<char>(savedTemplateInput),
                    std::istreambuf_iterator<char>()});
    require(savedTemplate && savedTemplate.asset->reference_bindings().size() == 3 &&
                savedTemplate.asset->reference_bindings()[1].value("owner_id", -1) == tmplRootId &&
                savedTemplate.asset->reference_bindings()[1].value("component_id", -1) == 4 &&
                savedTemplate.asset->reference_bindings()[1].value("path", std::string{}) ==
                    "/behaviors/0/target2" &&
                savedTemplate.asset->reference_bindings()[2].value("owner_id", -1) == addedChildId &&
                savedTemplate.asset->reference_bindings()[2].value("component_id", -1) == addedChildComponentId &&
                savedTemplate.asset->reference_bindings()[2].value("path", std::string{}) ==
                    "/behaviors/0/target",
            "Template Save must recollect reference-bearing new children and persist source identities");

    const Json *syncedObj = find_document_object_by_id(sceneDoc, instId);
    require(syncedObj != nullptr && syncedObj->value("tag", std::string{}) == "Destructible",
            "Live template save must update non-overridden tag in open scene, got: " +
                (syncedObj ? syncedObj->dump() : std::string("null")) +
                "; saved template tag=" + (savedTemplate ? savedTemplate.asset->root().value("tag", std::string("missing")) : "unavailable"));
    require(syncedObj->value("name", std::string{}) == "CustomCrate",
            "Live template save must preserve overridden name in open scene");
    const auto secondSynced = find_document_object_by_id(sceneDoc, secondInstId);
    require(secondSynced != nullptr && secondSynced->at("children").size() == 2 &&
                secondSynced->at("children")[1].value("templateNodeId", -1) == addedChildId &&
                secondSynced->at("components")[0].at("behaviors")[0].at("target") == secondInstId &&
                !secondSynced->at("components")[0].at("behaviors")[0].contains("target2") &&
                secondSynced->at("children")[1]["components"][0].value(
                    "templateComponentId", -1) == addedChildComponentId &&
                secondSynced->at("children")[1]["components"][0]["behaviors"][0]["target"] ==
                    secondSynced->at("children")[1]["id"] &&
                std::find(secondSynced->at("children")[1]["templateSourcePaths"].begin(),
                          secondSynced->at("children")[1]["templateSourcePaths"].end(),
                          "component[id=" + std::to_string(addedChildComponentId) +
                              "].behaviors[0].target") !=
                    secondSynced->at("children")[1]["templateSourcePaths"].end() &&
                secondSynced->at("children")[1].at("id") !=
                    find_document_object_by_id(sceneDoc, instId)->at("children")[1].at("id"),
            "source reference propagation must remap new child and component identities in two instances: " +
                (secondSynced != nullptr ? secondSynced->dump() : std::string("null")));
    const auto *overriddenFirst = find_document_object_by_id(sceneDoc, instId);
    require(overriddenFirst != nullptr && overriddenFirst->at("components")[0].at("behaviors")[0].at("target") ==
                instId && overriddenFirst->at("config").at("nested").at("x") == 77 &&
                overriddenFirst->at("components")[0].at("behaviors")[0].at("target2") ==
                    overriddenFirst->at("children")[1].at("id") &&
                overriddenFirst->at("config").at("nested").at("newDefault") == true &&
                overriddenFirst->at("config").at("localOnly") == "instance-local" &&
                !overriddenFirst->at("config").contains("obsolete") &&
                overriddenFirst->at("values") == Json::array({8, 99}),
            "propagation must preserve explicit reference, nested-field and array-tail overrides while deleting source-owned data: " +
                (overriddenFirst ? overriddenFirst->dump() : std::string("null")));
    require(secondSynced->at("components")[0].at("behaviors")[0].at("target") ==
                secondInstId,
            "reference sync must preserve a whole behavior-record override in the second instance");
    require(sceneCmds.undo(sceneDoc, sceneSel).status == CommandStatus::Changed &&
                find_document_object_by_id(sceneDoc, instId)->value("tag", std::string{}) == "Solid" &&
                find_document_object_by_id(sceneDoc, secondInstId)->value("tag", std::string{}) == "Solid" &&
                sceneCmds.redo(sceneDoc, sceneSel).status == CommandStatus::Changed &&
                find_document_object_by_id(sceneDoc, instId)->value("tag", std::string{}) == "Destructible" &&
                find_document_object_by_id(sceneDoc, secondInstId)->value("tag", std::string{}) == "Destructible",
            "Undo and Redo must reverse and restore propagation for every linked instance together");

    std::string revertReferenceError;
    const auto *overriddenFirstAfterRedo = find_document_object_by_id(sceneDoc, instId);
    const int instanceScriptId = overriddenFirstAfterRedo->at("components")[0].at("id").get<int>();
    require(revert_template_property(sceneDoc, sceneCmds, instId, instanceScriptId, 0,
                {std::string("target")}, assetRoot, revertReferenceError),
            "Revert Property must clear an internal-reference override: " + revertReferenceError);
    const int propagatedChildId = find_document_object_by_id(sceneDoc, instId)->at("children")[1].at("id");
    require(find_document_object_by_id(sceneDoc, instId)->at("components")[0].at("behaviors")[0].at("target") ==
                propagatedChildId &&
                sceneCmds.undo(sceneDoc, sceneSel).status == CommandStatus::Changed &&
                find_document_object_by_id(sceneDoc, instId)->at("components")[0].at("behaviors")[0].at("target") ==
                    instId &&
                sceneCmds.redo(sceneDoc, sceneSel).status == CommandStatus::Changed &&
                find_document_object_by_id(sceneDoc, instId)->at("components")[0].at("behaviors")[0].at("target") ==
                    propagatedChildId,
            "deliberate Revert must adopt the source target and remain undoable and redoable");

    std::string revertParentOverrideError;
    const int secondPropagatedChildId = find_document_object_by_id(sceneDoc, secondInstId)
                                            ->at("children")[1].value("id", -1);
    require(revert_template_property(sceneDoc, sceneCmds, secondInstId, secondScriptId, 0,
                {std::string("target")}, assetRoot, revertParentOverrideError) &&
                find_document_object_by_id(sceneDoc, secondInstId)->at("components")[0]
                    .at("behaviors")[0].at("target") == secondPropagatedChildId &&
                sceneCmds.undo(sceneDoc, sceneSel).status == CommandStatus::Changed &&
                find_document_object_by_id(sceneDoc, secondInstId)->at("components")[0]
                    .at("behaviors")[0].at("target") == secondInstId &&
                sceneCmds.redo(sceneDoc, sceneSel).status == CommandStatus::Changed &&
                find_document_object_by_id(sceneDoc, secondInstId)->at("components")[0]
                    .at("behaviors")[0].at("target") == secondPropagatedChildId &&
                !is_property_overridden(*find_document_object_by_id(sceneDoc, secondInstId),
                    "component[id=4].behaviors[0]"),
            "Revert Property on a reference must clear an ancestor behavior override and remain undoable");

    const int firstAddedChildId = find_document_object_by_id(sceneDoc, instId)->at("children")[1]
                                      .value("id", -1);
    const int firstAddedChildComponentId = find_document_object_by_id(sceneDoc, firstAddedChildId)
                                               ->at("components")[0].value("id", -1);
    mark_template_override(sceneDoc, firstAddedChildId,
        "component[id=" + std::to_string(addedChildComponentId) + "].config.state");
    require(sceneCmds.set_property(sceneDoc,
                address_for_document(sceneDoc, firstAddedChildId, firstAddedChildComponentId,
                                     std::nullopt, {std::string("config"), std::string("state")}),
                "instance-local").status == CommandStatus::Changed,
            "one propagated child component can keep an independent nested field override");
    require(tmplTab->history().set_property(tmplDoc,
                address_for_document(tmplDoc, addedChildId, addedChildComponentId, std::nullopt,
                                     {std::string("config"), std::string("state")}), "updated").status ==
                CommandStatus::Changed &&
            tmplTab->history().erase_property(tmplDoc,
                address_for_document(tmplDoc, addedChildId, addedChildComponentId, 0,
                                     {std::string("target")})).status == CommandStatus::Changed &&
            manager.save_template_tab(*tmplTabId, &tmplErr),
            "new child component defaults and reference deletion must save through propagation: " + tmplErr);
    const int secondAddedChildId = find_document_object_by_id(sceneDoc, secondInstId)
                                       ->at("children")[1].value("id", -1);
    const Json *updatedFirstChild = find_document_object_by_id(sceneDoc, firstAddedChildId);
    const Json *updatedSecondChild = find_document_object_by_id(sceneDoc, secondAddedChildId);
    require(updatedFirstChild != nullptr && updatedSecondChild != nullptr &&
                updatedFirstChild->at("components")[0].at("config").at("state") == "instance-local" &&
                updatedSecondChild->at("components")[0].at("config").at("state") == "updated" &&
                !updatedFirstChild->at("components")[0].at("behaviors")[0].contains("target") &&
                !updatedSecondChild->at("components")[0].at("behaviors")[0].contains("target") &&
                std::find(updatedSecondChild->at("templateSourcePaths").begin(),
                          updatedSecondChild->at("templateSourcePaths").end(),
                          "component[id=" + std::to_string(addedChildComponentId) +
                              "].behaviors[0].target") == updatedSecondChild->at("templateSourcePaths").end(),
            "new child source paths must use source component IDs through nested defaults and field deletion");
    require(tmplTab->history().set_property(tmplDoc,
                address_for_document(tmplDoc, addedChildId, addedChildComponentId, std::nullopt,
                                     {std::string("config"), std::string("state")}), "updated-again").status ==
                CommandStatus::Changed && manager.save_template_tab(*tmplTabId, &tmplErr) &&
                find_document_object_by_id(sceneDoc, firstAddedChildId)->at("components")[0]
                    .at("config").at("state") == "instance-local" &&
                find_document_object_by_id(sceneDoc, secondAddedChildId)->at("components")[0]
                    .at("config").at("state") == "updated-again",
            "repeated propagation must retain independent child overrides and update source defaults");

    require(tmplTab->history().erase_property(tmplDoc,
                address_for_document(tmplDoc, tmplRootId, std::nullopt, std::nullopt,
                                     {std::string("config")})).status == CommandStatus::Changed &&
                manager.save_template_tab(*tmplTabId, &tmplErr),
            "removing a source parent with an overridden descendant must propagate successfully: " + tmplErr);
    require(find_document_object_by_id(sceneDoc, instId)->at("config").at("nested").at("x") == 77 &&
                is_property_overridden(*find_document_object_by_id(sceneDoc, instId), "config.nested.x") &&
                !find_document_object_by_id(sceneDoc, secondInstId)->contains("config") &&
                manager.save_template_tab(*tmplTabId, &tmplErr) &&
                find_document_object_by_id(sceneDoc, instId)->at("config").at("nested").at("x") == 77 &&
                !find_document_object_by_id(sceneDoc, secondInstId)->contains("config"),
            "source parent deletion and repeated propagation must preserve only descendant-overridden data");

    const auto oldComponentId = 4;
    auto addSameType = tmplTab->history().add_component(tmplDoc, tmplRootId,
        component(0, 5, Json{{"behaviors", Json::array()}}));
    require(static_cast<bool>(addSameType) && addSameType.command.has_value(),
            "a second source Script component can be added to the template");
    const int newSourceComponentId = addSameType.command->payload.at("id").get<int>();
    auto addLocalSameType = sceneCmds.add_component(sceneDoc, instId,
        component(0, 5, Json{{"behaviors", Json::array()}}));
    require(static_cast<bool>(addLocalSameType) && addLocalSameType.command.has_value(),
            "an instance-local Script component can be added independently");
    const int localComponentId = addLocalSameType.command->payload.at("id").get<int>();
    require(manager.save_template_tab(*tmplTabId, &tmplErr),
            "saving a same-type source component must succeed: " + tmplErr);
    const Json *sameTypeSynced = find_document_object_by_id(sceneDoc, instId);
    require(sameTypeSynced != nullptr && sameTypeSynced->at("components").size() == 3 &&
                std::any_of(sameTypeSynced->at("components").begin(), sameTypeSynced->at("components").end(),
                    [&](const Json &value) { return value.value("id", -1) == localComponentId &&
                                                    !value.contains("templateComponentId"); }) &&
                std::any_of(sameTypeSynced->at("components").begin(), sameTypeSynced->at("components").end(),
                    [&](const Json &value) { return value.value("templateComponentId", -1) == newSourceComponentId; }),
            "adding a same-type source component must allocate a separate linked component and preserve local additions");

    Json reordered = tmplDoc.root[0];
    std::reverse(reordered["components"].begin(), reordered["components"].end());
    std::string replaceError;
    require(replace_template_document_root(tmplDoc, tmplTab->history(), reordered, replaceError),
            "source component order can be recorded as one undoable template edit: " + replaceError);
    require(manager.save_template_tab(*tmplTabId, &tmplErr), "saving reordered components must succeed: " + tmplErr);
    const Json *reorderedInstance = find_document_object_by_id(sceneDoc, instId);
    require(reorderedInstance != nullptr && reorderedInstance->at("components").size() == 3 &&
                reorderedInstance->at("components")[0].value("templateComponentId", -1) == newSourceComponentId &&
                reorderedInstance->at("components")[1].value("templateComponentId", -1) == oldComponentId &&
                reorderedInstance->at("components")[2].value("id", -1) == localComponentId,
            "source component reordering must follow stable identities without consuming local components");

    require(static_cast<bool>(tmplTab->history().remove_component(tmplDoc, tmplRootId, newSourceComponentId)),
            "a source-linked Script component can be removed from the template");
    require(manager.save_template_tab(*tmplTabId, &tmplErr), "saving source component removal must succeed: " + tmplErr);
    const Json *afterRemoval = find_document_object_by_id(sceneDoc, instId);
    require(afterRemoval != nullptr && afterRemoval->at("components").size() == 2 &&
                afterRemoval->at("components")[0].value("templateComponentId", -1) == oldComponentId &&
                afterRemoval->at("components")[1].value("id", -1) == localComponentId &&
                sceneCmds.undo(sceneDoc, sceneSel).status == CommandStatus::Changed &&
                sceneCmds.redo(sceneDoc, sceneSel).status == CommandStatus::Changed,
            "source component removal must preserve local same-type components through propagation Undo and Redo");

    require(tmplTab->history().set_property(tmplDoc,
                address_for_document(tmplDoc, tmplRootId, oldComponentId, 0,
                                     {std::string("target")}), 2).status == CommandStatus::Changed,
            "the remaining source reference must be retargeted to a surviving child before deletion");
    require(tmplTab->history().erase_property(tmplDoc,
                address_for_document(tmplDoc, tmplRootId, oldComponentId, 0,
                                     {std::string("target2")})).status == CommandStatus::Changed,
            "an explicitly removed source reference field must be deleted from the template hierarchy");
    require(static_cast<bool>(tmplTab->history().delete_object(tmplDoc, addedChildId)),
            "a source child can be removed after its reference field is removed");
    require(manager.save_template_tab(*tmplTabId, &tmplErr),
            "saving removed reference fields and children must succeed: " + tmplErr);
    std::ifstream finalTemplateInput(tmplPath, std::ios::binary);
    const auto finalTemplate = parse_template_envelope(
        std::string{std::istreambuf_iterator<char>(finalTemplateInput),
                    std::istreambuf_iterator<char>()});
    require(finalTemplate && finalTemplate.asset->reference_bindings().size() == 1 &&
                finalTemplate.asset->root()["children"].size() == 1 &&
                finalTemplate.asset->reference_bindings()[0].value("path", std::string{}) ==
                    "/behaviors/0/target",
            "resaving must remove stale reference sites from the persisted binding contract");

    auto localApplyChild = sceneCmds.create_object(sceneDoc, instId, "Instance-only child");
    require(localApplyChild && localApplyChild.command.has_value(),
            "a linked instance can receive a local child before Apply Instance");
    const int localApplyChildId = localApplyChild.command->payload.value("id", -1);
    auto localApplyComponent = sceneCmds.add_component(sceneDoc, localApplyChildId,
        component(0, 5, Json{{"behaviors", Json::array({
            Json{{"type", "LinkBehavior"}, {"target", localApplyChildId}}})}}));
    require(localApplyComponent && localApplyComponent.command.has_value(),
            "an instance-only child can receive a new reference-bearing component");

    std::string applyError;
    require(apply_template_instance(sceneDoc, sceneCmds, instId, assetRoot, applyError, &templateSchema),
            "applying a linked instance must promote new reference-bearing nodes: " + applyError);
    std::ifstream appliedTemplateInput(tmplPath, std::ios::binary);
    const auto appliedTemplate = parse_template_envelope(
        std::string{std::istreambuf_iterator<char>(appliedTemplateInput),
                    std::istreambuf_iterator<char>()});
    const Json *appliedRoot = appliedTemplate ? &appliedTemplate.asset->root() : nullptr;
    const Json *appliedLocalChild = nullptr;
    if (appliedRoot != nullptr && appliedRoot->contains("children") && (*appliedRoot)["children"].is_array())
        for (const auto &child : (*appliedRoot)["children"])
            if (child.value("name", std::string{}) == "Instance-only child")
                appliedLocalChild = &child;
    const int promotedSourceNodeId = appliedLocalChild != nullptr
        ? appliedLocalChild->value("id", -1) : -1;
    const int promotedSourceComponentId = appliedLocalChild != nullptr &&
            appliedLocalChild->contains("components") && (*appliedLocalChild)["components"].is_array()
        ? (*appliedLocalChild)["components"][0].value("id", -1) : -1;
    require(appliedTemplate && appliedTemplate.asset->reference_bindings().size() == 2 &&
                appliedTemplate.asset->reference_bindings()[0].value("owner_id", -1) == tmplRootId &&
                appliedTemplate.asset->reference_bindings()[0].value("component_id", -1) == oldComponentId &&
                appliedTemplate.asset->reference_bindings()[0].value("path", std::string{}) ==
                    "/behaviors/0/target" && appliedLocalChild != nullptr &&
                promotedSourceNodeId >= 0 && promotedSourceComponentId >= 0 &&
                appliedTemplate.asset->reference_bindings()[1].value("owner_id", -1) == promotedSourceNodeId &&
                appliedTemplate.asset->reference_bindings()[1].value("component_id", -1) == promotedSourceComponentId &&
                appliedTemplate.asset->reference_bindings()[1].value("path", std::string{}) ==
                    "/behaviors/0/target" &&
                appliedLocalChild->at("components")[0].at("behaviors")[0].at("target") ==
                    promotedSourceNodeId &&
                find_document_object_by_id(sceneDoc, localApplyChildId)->value(
                    "templateNodeId", -1) == promotedSourceNodeId &&
                find_document_object_by_id(sceneDoc, localApplyChildId)->at("components")[0].value(
                    "templateComponentId", -1) == promotedSourceComponentId,
            "Apply Instance must persist complete source IDs, recollected bindings, and promoted metadata");
    require(sceneCmds.undo(sceneDoc, sceneSel).status == CommandStatus::Changed &&
                !find_document_object_by_id(sceneDoc, localApplyChildId)->contains("templateNodeId") &&
                sceneCmds.redo(sceneDoc, sceneSel).status == CommandStatus::Changed &&
                find_document_object_by_id(sceneDoc, localApplyChildId)->value(
                    "templateNodeId", -1) == promotedSourceNodeId,
            "Apply Instance identity promotion must be one undoable and redoable scene operation");
    require(propagate_template_to_scene(sceneDoc, sceneCmds, "templates/Crate.pttemplate",
                                        *appliedTemplate.asset) > 0,
            "the applied source must propagate to both still-linked instances");
    const Json *secondAfterApply = find_document_object_by_id(sceneDoc, secondInstId);
    const Json *secondPromotedChild = nullptr;
    if (secondAfterApply != nullptr && secondAfterApply->contains("children") &&
        secondAfterApply->at("children").is_array())
        for (const auto &child : secondAfterApply->at("children"))
            if (child.value("templateNodeId", -1) == promotedSourceNodeId)
                secondPromotedChild = &child;
    require(secondAfterApply != nullptr && secondAfterApply->at("children").size() == 2 &&
                secondPromotedChild != nullptr &&
                secondPromotedChild->at("components")[0].value(
                    "templateComponentId", -1) == promotedSourceComponentId &&
                secondPromotedChild->at("components")[0].at("behaviors")[0].at("target") ==
                    secondPromotedChild->at("id") &&
                propagate_template_to_scene(sceneDoc, sceneCmds, "templates/Crate.pttemplate",
                                            *appliedTemplate.asset) > 0 &&
                find_document_object_by_id(sceneDoc, secondInstId)->at("children").size() == 2,
            "Apply must promote and propagate new identities without duplicates on repeated sync");

    // 4. Unpack removes template metadata
    require(unpack_template_instance(sceneDoc, sceneCmds, instId, tmplErr), "unpack must succeed");
    require(!find_template_instance_root(sceneDoc, instId).has_value(),
            "Unpacked object must no longer be a linked template");

    std::filesystem::remove_all(tempDir, ec);
}

void test_template_revert_is_atomic_and_scene_save_as_is_refused()
{
    TemporaryDirectory temporary;
    const auto assetRoot = temporary.path() / "assets";
    const auto templateDirectory = assetRoot / "templates";
    std::filesystem::create_directories(templateDirectory);
    const auto templatePath = templateDirectory / "History.pttemplate";
    const Json originalEnvelope = make_template_envelope(
        "History", valid_template_root(), valid_bindings());
    std::ofstream(templatePath, std::ios::binary) << originalEnvelope.dump(2) << '\n';

    DocumentManager manager(temporary.path(), assetRoot);
    std::string error;
    const auto tabId = manager.open_template(templatePath, &error);
    require(tabId.has_value(), "template history fixture must open: " + error);
    auto *tab = manager.find(*tabId);
    require(tab != nullptr, "opened template tab must remain available");
    require(manager.save_template_tab(*tabId, &error),
            "saving the initial template must establish editor reference metadata: " + error);
    const Json savedRoot = tab->workspace().document().root[0];
    require(savedRoot.contains(std::string(EditorTemplateBindingsKey)) &&
                savedRoot[std::string(EditorTemplateBindingsKey)] == valid_bindings(),
            "the editor root must retain the exact validated reference binding metadata");

    auto &document = tab->workspace().document_for_edit();
    auto &history = tab->history();
    const int rootId = document.root[0].value("id", -1);
    const auto editedName = history.set_property(document,
        address_for_document(document, rootId, std::nullopt, std::nullopt, {std::string("name")}),
        "Unsaved template name");
    const auto editedBindings = history.set_property(document,
        address_for_document(document, rootId, std::nullopt, std::nullopt,
                             {std::string(EditorTemplateBindingsKey)}), Json::array());
    require(editedName.status == CommandStatus::Changed && editedBindings.status == CommandStatus::Changed &&
                tab->workspace().dirty(),
            "template Revert fixture must dirty both the template root and reference metadata");
    const Json beforeRevert = document.root[0];

    const auto destination = temporary.path() / "must-stay.pttemplate";
    constexpr std::string_view protectedContents = "preserve-existing-template";
    std::ofstream(destination, std::ios::binary) << protectedContents;
    const auto rejectedSaveAs = manager.save_scene_tab_as(tab->identity(), destination, true);
    std::ifstream protectedInput(destination, std::ios::binary);
    const std::string afterRejectedSaveAs{std::istreambuf_iterator<char>(protectedInput),
                                          std::istreambuf_iterator<char>()};
    require(rejectedSaveAs.status == WorkspaceActionStatus::Failed &&
                rejectedSaveAs.message.find("Template") != std::string::npos &&
                afterRejectedSaveAs == protectedContents &&
                parse_template_envelope([&] {
                    std::ifstream input(templatePath, std::ios::binary);
                    return std::string{std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>()};
                }()),
            "Template Save As through the Scene writer must be refused without corrupting either template file");

    const bool reverted = manager.revert_template_tab(*tabId, &error);
    const Json revertedRoot = document.root[0];
    require(reverted && revertedRoot == savedRoot && !tab->workspace().dirty(),
            "Revert must atomically restore both the template root and its reference binding metadata");
    require(history.undo(document, tab->selection()).status == CommandStatus::Changed &&
                document.root[0] == beforeRevert && tab->workspace().dirty(),
            "one Undo after Revert must restore the exact pre-Revert root and metadata draft");
    require(history.redo(document, tab->selection()).status == CommandStatus::Changed &&
                document.root[0] == revertedRoot && !tab->workspace().dirty() &&
                manager.save_template_tab(*tabId, &error),
            "Redo after Revert must restore the exact saved root, leave usable history, and allow Save");
}

void test_template_file_identity_and_external_conflict_resolution()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assetRoot = project / "assets";
    const auto templateDirectory = assetRoot / "templates";
    std::filesystem::create_directories(templateDirectory);
    const auto templatePath = templateDirectory / "Identity.pttemplate";
    const auto writeTemplate = [&](std::string name) {
        Json root = valid_template_root();
        root["name"] = std::move(name);
        std::ofstream output(templatePath, std::ios::binary | std::ios::trunc);
        output << make_template_envelope("Identity", std::move(root), valid_bindings()).dump(2) << '\n';
    };
    writeTemplate("Original template");

    DocumentManager manager(project, assetRoot);
    std::string error;
    const auto tabId = manager.open_template(templatePath, &error);
    require(tabId.has_value(), "template identity fixture must open: " + error);
    auto *tab = manager.find(*tabId);
    require(tab != nullptr && tab->workspace().source_file_status().state == SourceFileState::Current,
            "opening an unchanged template must establish a current saved-file identity");
    auto &document = tab->workspace().document_for_edit();
    require(tab->history().set_property(document,
                address_for_document(document, 10, std::nullopt, std::nullopt,
                                     {std::string("name")}),
                "In-memory template edit").status == CommandStatus::Changed,
            "template conflict fixture must have a dirty draft");

    writeTemplate("External version with a larger identity");
    const auto externalStatus = tab->workspace().source_file_status();
    require(externalStatus.state == SourceFileState::Changed && externalStatus.diskIdentity,
            "an external template replacement must be detected with its observed file identity");
    require(!manager.save_template_tab(*tabId, &error) && tab->workspace().dirty() &&
                error.find("changed on disk") != std::string::npos &&
                parse_template_envelope([&] {
                    std::ifstream input(templatePath, std::ios::binary);
                    return std::string{std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>()};
                }()).asset->root().value("name", std::string{}) ==
                    "External version with a larger identity",
            "ordinary Save must refuse to overwrite an external template version and retain the dirty draft");
    require(manager.save_template_tab(*tabId, &error, true, externalStatus.diskIdentity) &&
                !tab->workspace().dirty() &&
                tab->workspace().source_file_status().state == SourceFileState::Current,
            "explicit Overwrite must validate the observed external identity and refresh the template identity");

    writeTemplate("Reloaded external template");
    require(tab->workspace().source_file_status().state == SourceFileState::Changed &&
                manager.reload_template_tab(*tabId, &error),
            "template Reload must parse the external envelope through the template-aware path: " + error);
    require(!tab->workspace().dirty() && !tab->history().history_status().canUndo &&
                tab->workspace().document().root[0].value("name", std::string{}) ==
                    "Reloaded external template" &&
                tab->workspace().document().root[0][std::string(EditorTemplateBindingsKey)] == valid_bindings() &&
                tab->workspace().source_file_status().state == SourceFileState::Current,
            "external Reload must restore root and bindings, clear the draft, reset history, and refresh identity");
    require(static_cast<bool>(manager.close_tab(*tabId, UnsavedDecision::Discard)),
            "the externally reloaded template tab must close cleanly");
    const auto reopened = manager.open_template(templatePath, &error);
    require(reopened && manager.find(*reopened)->workspace().document().root[0].value(
                "name", std::string{}) == "Reloaded external template" &&
                manager.find(*reopened)->template_reference_bindings() == valid_bindings(),
            "saving and reopening after conflict resolution must preserve the template root and bindings");
}

void test_template_file_and_directory_moves_retarget_open_and_closed_scenes()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assetRoot = project / "assets";
    const auto templateDirectory = assetRoot / "templates";
    const auto sceneDirectory = assetRoot / "scenes";
    std::filesystem::create_directories(templateDirectory);
    std::filesystem::create_directories(sceneDirectory);
    std::ofstream(project / "CMakeLists.txt") << "project(template-move-test)\n";
    const auto oldTemplate = templateDirectory / "Crate.pttemplate";
    const auto newTemplate = templateDirectory / "Renamed.pttemplate";
    const auto finalTemplate = templateDirectory / "Final.pttemplate";
    const auto openScenePath = sceneDirectory / "open.scene";
    const auto closedScenePath = sceneDirectory / "closed.scene";
    const auto retryScenePath = sceneDirectory / "retry.scene";
    Json templateRoot = object(1, "Crate");
    std::ofstream(oldTemplate, std::ios::binary)
        << make_template_envelope("Crate", templateRoot).dump(2) << '\n';
    const auto linked_scene = [&](int id, std::string templateRef = "templates/Crate.pttemplate") {
        Json instance = object(id, "Locally named crate");
        instance["template"] = std::move(templateRef);
        instance["templateNodeId"] = 1;
        instance["overrides"] = Json::array({"name"});
        instance["templateSourceKeys"] = Json::array(
            {"active", "name", "position", "rotation", "scale", "tag"});
        instance["templateSourcePaths"] = Json::array(
            {"active", "name", "position", "position.x", "position.y", "rotation",
             "scale", "scale.x", "scale.y", "tag"});
        return Json::array({instance});
    };
    write_json(openScenePath, linked_scene(10));
    write_json(closedScenePath, linked_scene(20));
    {
        std::ofstream invalidScene(retryScenePath, std::ios::binary);
        invalidScene << "[";
    }

    DocumentManager manager(project, assetRoot);
    const auto openSceneId = manager.open_scene(openScenePath);
    const auto templateTabId = manager.open_template(oldTemplate);
    require(openSceneId && templateTabId,
            "move coverage must open one scene, one isolated template and leave another scene closed");
    auto *openSceneTab = manager.find(*openSceneId);
    auto *templateTab = manager.find(*templateTabId);
    require(openSceneTab && templateTab, "move fixture tabs must remain addressable");
    require(!openSceneTab->workspace().dirty(),
            "reconciling an already-current linked template during Scene open must not mark the Scene dirty");

    std::error_code moveError;
    std::filesystem::rename(oldTemplate, newTemplate, moveError);
    require(!moveError, "the template file move must complete before retargeting");
    const auto firstRetarget = manager.retarget_template_asset_links(oldTemplate, newTemplate, false);
    require(firstRetarget.updatedCount > 0 && !firstRetarget.failures.empty() &&
                firstRetarget.recoveryStored &&
                std::filesystem::is_regular_file(project / ".platformator" /
                                                 "template-retarget-pending.json"),
            "file moves must retarget readable links, surface a failed closed scene, and persist a retry mapping");
    require(templateTab->template_path() == std::filesystem::canonical(newTemplate) &&
                templateTab->workspace().document().sourcePath == std::filesystem::canonical(newTemplate) &&
                find_document_object_by_id(openSceneTab->workspace().document(), 10)->value(
                    "template", std::string{}) == "templates/Renamed.pttemplate" &&
                read_json(closedScenePath)[0].value("template", std::string{}) ==
                    "templates/Renamed.pttemplate",
            "file moves must retarget the isolated editing tab, open links, and later-loaded closed-scene data");
    moveError.clear();
    std::filesystem::rename(newTemplate, finalTemplate, moveError);
    require(!moveError, "the second consecutive template file move must complete");
    const auto secondFileRetarget = manager.retarget_template_asset_links(newTemplate, finalTemplate, false);
    const Json composedFileRecovery = read_json(project / ".platformator" /
                                                "template-retarget-pending.json");
    const auto hasComposedFileMapping = std::any_of(
        composedFileRecovery.at("mappings").begin(), composedFileRecovery.at("mappings").end(),
        [&](const Json &mapping) {
            return mapping.at("old") == std::filesystem::weakly_canonical(oldTemplate).string() &&
                   mapping.at("new") == std::filesystem::canonical(finalTemplate).string() &&
                   !mapping.at("directory").get<bool>();
        });
    require(!secondFileRetarget.failures.empty() && secondFileRetarget.recoveryStored &&
                hasComposedFileMapping,
            "a pending file mapping must compose A -> B -> C into a durable A -> C destination");
    write_json(retryScenePath, linked_scene(30));
    DocumentManager restartedAfterFileMoves(project, assetRoot);
    const auto retried = restartedAfterFileMoves.retry_pending_template_asset_links();
    require(retried.succeeded() && retried.updatedCount > 0 &&
                read_json(retryScenePath)[0].value("template", std::string{}) ==
                    "templates/Final.pttemplate" &&
                !std::filesystem::exists(project / ".platformator" /
                                         "template-retarget-pending.json"),
            "a restarted manager must retry a repaired closed scene through the composed file mapping");
    const auto reopenedClosed = manager.open_scene(closedScenePath);
    require(reopenedClosed.has_value() &&
                find_document_object_by_id(manager.find(*reopenedClosed)->workspace().document(), 20)->value(
                    "name", std::string{}) == "Locally named crate" &&
                find_document_object_by_id(manager.find(*reopenedClosed)->workspace().document(), 20)->value(
                    "template", std::string{}) == "templates/Final.pttemplate",
            "a moved linked asset must reopen with its instance override intact");
    require(static_cast<bool>(manager.close_tab(*reopenedClosed, UnsavedDecision::Discard)),
            "the closed-scene directory-move fixture can return to a closed state");

    const auto movedDirectory = assetRoot / "relocated-templates";
    const auto movedDirectoryAgain = assetRoot / "final-templates";
    {
        std::ofstream invalidScene(retryScenePath, std::ios::binary | std::ios::trunc);
        invalidScene << "[";
    }
    moveError.clear();
    std::filesystem::rename(templateDirectory, movedDirectory, moveError);
    require(!moveError, "the template directory move must complete before retargeting");
    const auto firstDirectoryRetarget = manager.retarget_template_asset_links(
        templateDirectory, movedDirectory, true);
    moveError.clear();
    std::filesystem::rename(movedDirectory, movedDirectoryAgain, moveError);
    require(!moveError, "the second consecutive template directory move must complete");
    const auto secondDirectoryRetarget = manager.retarget_template_asset_links(
        movedDirectory, movedDirectoryAgain, true);
    const Json composedDirectoryRecovery = read_json(project / ".platformator" /
                                                      "template-retarget-pending.json");
    const auto hasComposedDirectoryMapping = std::any_of(
        composedDirectoryRecovery.at("mappings").begin(), composedDirectoryRecovery.at("mappings").end(),
        [&](const Json &mapping) {
            return mapping.at("old") == std::filesystem::weakly_canonical(templateDirectory).string() &&
                   mapping.at("new") == std::filesystem::canonical(movedDirectoryAgain).string() &&
                   mapping.at("directory").get<bool>();
        });
    require(!firstDirectoryRetarget.failures.empty() && !secondDirectoryRetarget.failures.empty() &&
                secondDirectoryRetarget.recoveryStored && hasComposedDirectoryMapping,
            "a pending directory mapping must compose A -> B -> C and retain its original source");
    write_json(retryScenePath, linked_scene(31, "templates/Final.pttemplate"));
    DocumentManager restartedAfterDirectoryMoves(project, assetRoot);
    const auto directoryRetry = restartedAfterDirectoryMoves.retry_pending_template_asset_links();
    require(directoryRetry.succeeded() && directoryRetry.updatedCount > 0 &&
                read_json(retryScenePath)[0].value("template", std::string{}) ==
                    "final-templates/Final.pttemplate" &&
                !std::filesystem::exists(project / ".platformator" /
                                         "template-retarget-pending.json"),
            "a restarted manager must resolve the original directory reference to its final destination");
    const auto movedAgain = movedDirectoryAgain / finalTemplate.filename();
    require(templateTab->template_path() == std::filesystem::canonical(movedAgain) &&
                templateTab->workspace().document().sourcePath == std::filesystem::canonical(movedAgain) &&
                find_document_object_by_id(openSceneTab->workspace().document(), 10)->value(
                    "template", std::string{}) == "final-templates/Final.pttemplate" &&
                read_json(closedScenePath)[0].value("template", std::string{}) ==
                    "final-templates/Final.pttemplate",
            "consecutive directory moves must update open and closed scene references without losing overrides");
    std::string saveError;
    require(manager.save_template_tab(*templateTabId, &saveError) &&
                std::filesystem::is_regular_file(movedAgain) && !std::filesystem::exists(oldTemplate),
            "saving after a file and directory move must write only to the current template path: " + saveError);
    const auto reopenedAfterDirectoryMove = manager.open_scene(closedScenePath);
    require(reopenedAfterDirectoryMove.has_value() &&
                find_document_object_by_id(manager.find(*reopenedAfterDirectoryMove)->workspace().document(), 20)->value(
                    "name", std::string{}) == "Locally named crate" &&
                find_document_object_by_id(manager.find(*reopenedAfterDirectoryMove)->workspace().document(), 20)->value(
                    "template", std::string{}) == "final-templates/Final.pttemplate" &&
                !std::filesystem::exists(oldTemplate),
            "reopening after directory moves must preserve overrides and must not recreate the old asset");
}

void test_template_save_failure_preserves_previous_file_and_dirty_draft()
{
    TemporaryDirectory temporary;
    const auto assetRoot = temporary.path() / "assets";
    const auto templateDirectory = assetRoot / "templates";
    std::filesystem::create_directories(templateDirectory);
    const auto templatePath = templateDirectory / "Autosave.pttemplate";
    std::ofstream(templatePath, std::ios::binary)
        << make_template_envelope("Autosave", object(1, "Before")).dump(2) << '\n';
    DocumentManager manager(temporary.path(), assetRoot);
    std::string openError;
    const auto tabId = manager.open_template(templatePath, &openError);
    require(tabId.has_value(), "autosave template fixture must open: " + openError);
    auto *tab = manager.find(*tabId);
    require(tab != nullptr, "autosave fixture tab must remain open");
    auto &document = tab->workspace().document_for_edit();
    const int rootId = document.root[0].value("id", -1);
    require(tab->history().set_property(document,
                address_for_document(document, rootId, std::nullopt, std::nullopt,
                                     {std::string("name")}), "After").status == CommandStatus::Changed,
            "autosave failure fixture must contain one unsaved template revision");

    std::ifstream originalInput(templatePath, std::ios::binary);
    const std::string originalContents{std::istreambuf_iterator<char>(originalInput),
                                       std::istreambuf_iterator<char>()};
    const auto blockedDirectory = temporary.path() / "templates.backup";
    std::error_code moveError;
    std::filesystem::rename(templateDirectory, blockedDirectory, moveError);
    require(!moveError, "autosave failure fixture must isolate the prior template file");
    std::ofstream(templateDirectory, std::ios::binary) << "parent path is blocked";

    std::string saveError;
    const bool firstSave = manager.save_template_tab(*tabId, &saveError);
    require(!firstSave && !saveError.empty() && tab->workspace().dirty(),
            "a failed template save must leave the draft dirty for an explicit retry");
    std::ifstream preservedInput(blockedDirectory / "Autosave.pttemplate", std::ios::binary);
    const std::string preservedContents{std::istreambuf_iterator<char>(preservedInput),
                                        std::istreambuf_iterator<char>()};
    require(std::filesystem::is_regular_file(templateDirectory) &&
                preservedContents == originalContents,
            "a failed template save must leave the previous template file intact");

    std::filesystem::remove(templateDirectory, moveError);
    moveError.clear();
    std::filesystem::rename(blockedDirectory, templateDirectory, moveError);
    require(!moveError, "the original template file must be restored before retry");
    require(manager.save_template_tab(*tabId, &saveError) && !tab->workspace().dirty(),
            "an explicit retry must write the preserved dirty draft");
    std::ifstream savedInput(templatePath, std::ios::binary);
    const auto saved = parse_template_envelope(
        std::string{std::istreambuf_iterator<char>(savedInput), std::istreambuf_iterator<char>()});
    require(saved && saved.asset->root().value("name", std::string{}) == "After",
            "the successful retry must write the preserved dirty draft");
}

void test_unsaved_inventory_covers_inactive_scene_template_and_animation_tabs()
{
    TemporaryDirectory temporary;
    const auto assetRoot = temporary.path() / "assets";
    const auto templateDirectory = assetRoot / "templates";
    std::filesystem::create_directories(templateDirectory);
    const auto firstScene = assetRoot / "first.scene";
    const auto activeScene = assetRoot / "active.scene";
    write_json(firstScene, Json::array({object(50, "First scene")}));
    write_json(activeScene, Json::array({object(60, "Active scene")}));

    const auto templatePath = templateDirectory / "Dirty.pttemplate";
    std::ofstream(templatePath, std::ios::binary) <<
        make_template_envelope("Dirty", valid_template_root(), valid_bindings()).dump(2) << '\n';

    const auto animationPath = assetRoot / "dirty.animset";
    write_json(animationPath, Json{
        {"frames", Json::array({Json{{"duration", 0.25}, {"textureWrapperFilePath", ""},
                                   {"hasSourceRect", false},
                                   {"sourceRect", Json{{"x", 0.0}, {"y", 0.0},
                                                        {"w", 0.0}, {"h", 0.0}}}}})},
        {"framesPerSecond", 4.0}, {"loop", true}, {"width", 16.0}, {"height", 16.0},
        {"filePath", animationPath.string()}});

    DocumentManager manager(temporary.path(), assetRoot);
    const auto firstSceneId = manager.open_scene(firstScene);
    const auto activeSceneId = manager.open_scene(activeScene);
    require(firstSceneId && activeSceneId && manager.activate_tab(*activeSceneId),
            "scene inventory fixture must keep one clean active tab and one inactive tab");
    auto *firstSceneTab = manager.find(*firstSceneId);
    require(firstSceneTab != nullptr && firstSceneTab->history().set_property(
                firstSceneTab->workspace().document_for_edit(),
                address_for_document(firstSceneTab->workspace().document(), 50, std::nullopt,
                                     std::nullopt, {std::string("name")}), "Edited first scene").status ==
                    CommandStatus::Changed,
            "an inactive Scene tab can carry an unsaved edit");

    std::string templateError;
    const auto templateId = manager.open_template(templatePath, &templateError);
    require(templateId.has_value(), "template inventory fixture must open: " + templateError);
    auto *templateTab = manager.find(*templateId);
    require(templateTab != nullptr && templateTab->history().set_property(
                templateTab->workspace().document_for_edit(),
                address_for_document(templateTab->workspace().document(), 10, std::nullopt,
                                     std::nullopt, {std::string("name")}), "Edited template").status ==
                    CommandStatus::Changed && manager.activate_tab(*activeSceneId),
            "an inactive Template tab can carry an unsaved edit while the Scene stays active");

    const auto animationId = manager.open_animation(animationPath);
    require(animationId.has_value(), "animation inventory fixture must open");
    auto *animationTab = manager.find_animation_tab(*animationId);
    require(animationTab != nullptr && animationTab->document().set("/loop", false).changed &&
                manager.has_unsaved_changes(),
            "the unsaved inventory must include inactive Scene, Template, and Animation tabs");

    const auto saved = manager.save_all();
    require(saved.action.status == WorkspaceActionStatus::Completed && saved.savedTabs.size() == 3 &&
                !manager.has_unsaved_changes() &&
                read_json(firstScene)[0].value("name", std::string{}) == "Edited first scene" &&
                parse_template_envelope([&] {
                    std::ifstream input(templatePath, std::ios::binary);
                    return std::string{std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>()};
                }()) && read_json(animationPath).value("loop", true) == false,
            "Save All must persist every dirty document kind and clear the shutdown dirty inventory");
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
        test_reference_array_override_ancestor_and_revert();
        test_grouped_revert_clears_child_template_override_metadata();
        test_array_override_paths_follow_compacted_elements();
        test_reverting_a_leaf_splits_ancestor_override();
        test_reserved_behavior_metadata_names_remain_authored_data();
        test_scene_identity_distinguishes_replaced_files();
        test_template_insert_allocates_maps_and_retains_exact_redo_payload();
        test_duplicate_reference_policies_and_multi_root_identity_map();
        test_legacy_settings_migrate_to_preserve_duplicate_targets();
        test_instantiated_template_is_consumed_by_game_scene_loader();
        test_linked_templates_flattening_and_live_sync();
        test_template_revert_is_atomic_and_scene_save_as_is_refused();
        test_template_file_identity_and_external_conflict_resolution();
        test_template_file_and_directory_moves_retarget_open_and_closed_scenes();
        test_template_save_failure_preserves_previous_file_and_dirty_draft();
        test_unsaved_inventory_covers_inactive_scene_template_and_animation_tabs();
        std::cout << "Template, binding, insertion, and duplicate cases passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "platformator_editor_template_test: " << error.what() << '\n';
        return 1;
    }
}
