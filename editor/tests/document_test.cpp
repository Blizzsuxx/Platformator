#include "document.h"
#include "documentio.h"
#include "editcommands.h"
#include "panels/hierarchy.h"
#include "panels/referencefields.h"
#include "selection.h"
#include "test_support.h"

#include <limits>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <platformator/runtime.h>
#include <platformator/runtimeoptions.h>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace
{
Json scene_fixture()
{
    Json behavior{{"type", "CustomBehavior"}, {"target", 0}, {"sound", "assets\\audio\\click.wav"},
                  {"unknownPath", "assets\\user\\literal.txt"}};
    Json camera = component(1, 2, Json{{"camera", {{"w", 320.0}, {"h", 200.0}}}});
    Json script = component(2, 5, Json{{"behaviors", Json::array({behavior})}});
    Json unfamiliar = component(3, 99, Json{{"opaque", {{"integer", 42}, {"array", Json::array({"x", 0})}}}});
    Json root = object(0, "Root", Json::array({camera, script, unfamiliar}),
                       Json::array({object(4, "Child", Json::array({component(
                           5, 6, Json{{"textureFilePath", "assets\\Textures\\Hero.PNG"},
                                     {"userPath", "assets\\MyFolder\\readme.txt"}})}))}));
    root["unknownObjectField"] = Json{{"preserved", true}};
    return Json::array({root});
}

void expect_invalid(Json root, std::string_view label)
{
    const auto result = parse_document(std::move(root), 12);
    require(!result && !result.error.empty(), label);
}
} // namespace

void run_document_cases()
{
    const Json input = scene_fixture();
    auto parsed = parse_document(input, 12, std::filesystem::path("scene with spaces.scene"),
                                 std::filesystem::path("project"), std::filesystem::path("project/assets"));
    require(parsed && parsed.document->root.is_array() && parsed.document->root.size() == 1,
            "scene documents must retain their top-level array representation");
    const auto &document = *parsed.document;
    require(document.documentGeneration == 12 && document.sourcePath == std::filesystem::path("scene with spaces.scene"),
            "document identity and source path must be retained");
    require(document.root[0]["unknownObjectField"] == Json{{"preserved", true}} &&
                document.root[0]["components"][2] == input[0]["components"][2],
            "unfamiliar object fields and component payloads must survive parsing");
    require(document.root[0]["components"][0].contains("camera") &&
                !document.root[0]["components"][0].contains("width") &&
                document.root[0]["components"][0]["camera"]["w"] == 320.0,
            "legacy nested camera dimensions must remain unchanged on a no-edit load");
    require(document.root[0]["children"][0]["components"][0]["textureFilePath"] ==
                "assets/Textures/Hero.PNG" &&
                document.root[0]["children"][0]["components"][0]["userPath"] ==
                    "assets\\MyFolder\\readme.txt" &&
                document.root[0]["components"][1]["behaviors"][0]["unknownPath"] ==
                    "assets\\user\\literal.txt",
            "only known asset path fields may normalize separators, preserving case and unknown strings");
    require(document.root[0]["components"][1]["behaviors"][0]["target"] == 0,
            "serialized object references must remain exact integer IDs, including ID zero");
    require(document.root[0]["components"][1]["behaviors"][0]["sound"] == "assets/audio/click.wav",
            "known behavior asset fields must follow their established path normalization");
    require(document.indexes.current_for(12, document.structureRevision) &&
                document.indexes.orderedObjectIds == std::vector<int>({0, 4}) &&
                document.indexes.orderedSpriteIds == std::vector<int>({4}),
            "indexes must retain hierarchy and draw order in document order");
    require(document.indexes.objectsById.at(4).childIndices == std::vector<std::size_t>({0, 0}) &&
                document.indexes.parentById.at(0) == std::nullopt &&
                document.indexes.parentById.at(4) == std::optional<int>(0) &&
                document.indexes.componentsById.at(5).ownerObjectId == 4,
            "generation-bound indexes must resolve objects, parents and component ownership");

    const auto invalidRoot = parse_document(Json{{"not", "an array"}}, 1);
    require(!invalidRoot, "non-array scene roots must be rejected before document adoption");
    expect_invalid(Json::array({Json{{"id", 0}}}), "malformed object records must be rejected");

    auto bad = scene_fixture();
    bad[0]["id"] = -1;
    expect_invalid(bad, "negative file IDs must be rejected");
    bad = scene_fixture();
    bad[0]["components"][0]["id"] = std::numeric_limits<int>::max();
    bad[0]["children"][0]["id"] = std::numeric_limits<int>::max();
    expect_invalid(bad, "duplicate IDs across objects and components must be rejected");
    bad = scene_fixture();
    bad[0]["id"] = std::numeric_limits<std::uint64_t>::max();
    expect_invalid(bad, "file IDs outside the engine's signed integer range must be rejected");
    bad = scene_fixture();
    bad[0]["components"][2]["type"] = 2;
    bad[0]["components"][2]["id"] = 8;
    expect_invalid(bad, "duplicate known component slots must be rejected");
    bad = scene_fixture();
    bad[0]["children"] = Json{{"malformed", true}};
    expect_invalid(bad, "invalid hierarchy representations must be rejected");
    bad = scene_fixture();
    bad[0]["components"] = "not an array";
    expect_invalid(bad, "malformed component collections must be rejected");
    bad = scene_fixture();
    bad[0]["components"][1]["behaviors"] = Json{{"not", "an array"}};
    expect_invalid(bad, "malformed behavior collection representations must be rejected");

    auto zeroId = scene_fixture();
    const auto zeroAccepted = parse_document(std::move(zeroId), 20);
    require(zeroAccepted && zeroAccepted.document->indexes.objectsById.contains(0),
            "file ID zero must remain compatible with current runtime and legacy scene fixtures");

    auto deep = Json::array({object(0, "Deep Root")});
    Json *parent = &deep[0];
    constexpr int depth = 2048;
    for (int id = 1; id <= depth; ++id)
    {
        parent->at("children").push_back(object(id, "Deep " + std::to_string(id)));
        parent = &parent->at("children").back();
    }
    const auto deepParsed = parse_document(std::move(deep), 33);
    require(deepParsed && deepParsed.document->indexes.orderedObjectIds.size() == depth + 1 &&
                deepParsed.document->indexes.objectsById.at(depth).childIndices.size() ==
                    static_cast<std::size_t>(depth + 1),
            "deep hierarchy indexing must use explicit traversal storage");

    const auto starter = make_default_document("project", "project/assets", 44);
    require(starter.root.is_array() && starter.root.size() == 1 &&
                starter.root[0]["components"].size() == 1 &&
                starter.root[0]["components"][0]["type"] == 2 &&
                starter.indexes.current_for(44, starter.structureRevision),
            "new documents must contain an indexed default camera without using Runtime serialization");
}

void run_document_io_cases()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    auto parsed = parse_document(scene_fixture(), 51, std::nullopt, project, assets);
    require(static_cast<bool>(parsed), "document fixture must parse before serialization");
    DocumentSession document = std::move(*parsed.document);
    const auto destination = project / "scene with spaces \xC3\xBC.scene";
    const auto saved = save_document(document, destination, SaveOptions{.saveAs = true});
    require(saved.status == SaveStatus::Saved && saved.destinationReplaced && document.sourcePath &&
                document.sourceIdentity.has_value(),
            "successful Save As must atomically replace its target and update source identity");
    const auto reopened = load_document_file(destination, 52, project, assets);
    require(reopened && reopened.document->root == document.root &&
                reopened.document->root[0]["components"][0].contains("camera"),
            "saved documents must round-trip semantic JSON and legacy camera aliases");

    const auto currentRoot = document.root;
    const auto currentPath = document.sourcePath;
    const auto currentIdentity = document.sourceIdentity;
    const auto failed = save_document(document, project / "absent" / "failure.scene",
                                      SaveOptions{.saveAs = true});
    require(failed.status == SaveStatus::Failed && !failed.destinationReplaced &&
                document.root == currentRoot && document.sourcePath == currentPath &&
                document.sourceIdentity == currentIdentity,
            "pre-replacement save failure must leave document identity and source marker unchanged");

    const auto blocked = project / "blocked.scene";
    std::filesystem::create_directory(blocked);
    write_json(blocked / "marker", Json{{"preserve", true}});
    const auto blockedSave = save_document(document, blocked,
                                           SaveOptions{.saveAs = true,
                                                       .overwrite = OverwriteDecision::Overwrite});
    require(blockedSave.status == SaveStatus::Failed && !blockedSave.destinationReplaced &&
                read_json(blocked / "marker") == Json{{"preserve", true}} &&
                document.sourcePath == currentPath && document.sourceIdentity == currentIdentity,
            "failed replacement must preserve prior destination content and the current source identity");

    document.root[0]["name"] = "local edit";
    ++document.valueRevision;
    write_json(destination, Json::array({object(0, "external edit")}));
    const auto externallyChanged = save_document(document, destination,
                                                 SaveOptions{.saveAs = false,
                                                             .overwrite = OverwriteDecision::Ask});
    require(externallyChanged.status == SaveStatus::Conflict && !externallyChanged.destinationReplaced &&
                read_json(destination)[0]["name"] == "external edit" && document.root[0]["name"] == "local edit",
            "external modification must require an explicit overwrite decision");
    const auto confirmedOverwrite = save_document(document, destination,
                                                  SaveOptions{.saveAs = false,
                                                              .overwrite = OverwriteDecision::Overwrite});
    require(confirmedOverwrite.succeeded() && confirmedOverwrite.destinationReplaced &&
                read_json(destination)[0]["name"] == "local edit",
            "explicit overwrite must atomically save the current authoritative document");
}

void run_document_authoring_cases()
{
    const Json behavior{{"type", "FieldFixture"},
                        {"value", 0},
                        {"nullable", nullptr},
                        {"references", {{"self", 0}, {"internal", 10}, {"external", 100}, {"component", 2}}}};
    Json camera = component(1, 2, Json{{"width", 640.0}, {"height", 480.0}});
    Json collider = component(2, 3, Json{{"colliderType", 1}, {"width", 10.0}, {"height", 20.0},
                                        {"offset", {{"x", 0.0}, {"y", 0.0}}}, {"trigger", false},
                                        {"collisionGroup", 1}, {"collisionMask", 3}});
    Json body = component(3, 4, Json{{"velocity", {{"x", 0.0}, {"y", 0.0}}},
                                     {"force", {{"x", 0.0}, {"y", 0.0}}}, {"mass", 1.0},
                                     {"angularVelocity", 0.0}, {"torque", 0.0}, {"friction", 0.5},
                                     {"restitution", 0.2}, {"bodyType", 0}, {"gravity", true}});
    Json sprite = component(4, 6, Json{{"textureFilePath", "assets/Textures/Original.PNG"}, {"flip", 0},
                                      {"width", 32.0}, {"height", 24.0}, {"sourceRectEnabled", true},
                                      {"sourceRect", {{"x", 0.0}, {"y", 0.0}, {"w", 16.0}, {"h", 12.0}}}});
    Json animator = component(5, 0, Json{{"currentFrameIndex", 0}, {"playbackSpeed", 1.0},
                                         {"playing", false}, {"animationClipFilePath", "assets/Run.animset"}});
    Json audio = component(6, 1, Json{{"filePath", "assets/Sounds/Click.wav"}, {"gain", 1.0},
                                     {"loopCount", 0}, {"autoPlay", false}});
    Json scripts = component(7, 5, Json{{"behaviors", Json::array({behavior})}});
    Json root = object(0, "Authoring", Json::array({camera, collider, body, sprite, animator, audio, scripts}));
    root["position"] = {{"x", 1.0}, {"y", 2.0}};
    root["unfamiliar"] = Json{{"preserve", true}};
    Json child = object(10, "Internal Target",
                        Json::array({component(8, 3, Json{{"colliderType", 0}, {"radius", 6.0},
                                                           {"offset", {{"x", 1.0}, {"y", -1.0}}},
                                                           {"trigger", false}, {"collisionGroup", 2},
                                                           {"collisionMask", 5}})}));
    auto parsed = parse_document(Json::array({root, child}), 80);
    require(static_cast<bool>(parsed), "all built-in component fixture fields must form a valid document");
    DocumentSession document = std::move(*parsed.document);
    EditCommandService commands;
    auto set = [&](std::optional<int> componentId, std::optional<std::size_t> behaviorOrdinal,
                   std::vector<FieldPathSegment> path, Json value) {
        std::string pathLabel;
        for (const auto &segment : path)
        {
            if (!pathLabel.empty())
                pathLabel += ".";
            if (const auto *key = std::get_if<std::string>(&segment))
                pathLabel += *key;
            else
                pathLabel += std::to_string(std::get<std::size_t>(segment));
        }
        const auto changed = commands.set_property(
            document, PropertyAddress{document.documentGeneration, 0, componentId, behaviorOrdinal, std::move(path)},
            std::move(value));
        if (changed.status != CommandStatus::Changed || !changed.propertyCommand)
            throw std::runtime_error("built-in property mutation rejected: " + changed.message +
                                     " (status=" + std::to_string(static_cast<int>(changed.status)) +
                                     ", component=" + (componentId ? std::to_string(*componentId) : "object") +
                                     ", field=" + pathLabel + ")");
        return *changed.propertyCommand;
    };

    set(std::nullopt, std::nullopt, {std::string("name")}, "Edited");
    set(std::nullopt, std::nullopt, {std::string("tag")}, "Player");
    set(std::nullopt, std::nullopt, {std::string("active")}, false);
    set(std::nullopt, std::nullopt, {std::string("position"), std::string("x")}, 3.0);
    set(std::nullopt, std::nullopt, {std::string("position"), std::string("y")}, 4.0);
    set(std::nullopt, std::nullopt, {std::string("rotation")}, 0.25);
    set(std::nullopt, std::nullopt, {std::string("scale"), std::string("x")}, 2.0);
    set(std::nullopt, std::nullopt, {std::string("scale"), std::string("y")}, 3.0);

    set(1, std::nullopt, {std::string("width")}, 800.0);
    set(1, std::nullopt, {std::string("height")}, 600.0);
    set(2, std::nullopt, {std::string("width")}, 12.0);
    set(2, std::nullopt, {std::string("height")}, 22.0);
    set(2, std::nullopt, {std::string("offset"), std::string("x")}, 1.0);
    set(2, std::nullopt, {std::string("offset"), std::string("y")}, -2.0);
    set(2, std::nullopt, {std::string("trigger")}, true);
    set(2, std::nullopt, {std::string("collisionGroup")}, 4);
    set(2, std::nullopt, {std::string("collisionMask")}, 7);

    const auto setCircle = [&](std::vector<FieldPathSegment> path, Json value) {
        const auto changed = commands.set_property(
            document, PropertyAddress{document.documentGeneration, 10, 8, std::nullopt, std::move(path)},
            std::move(value));
        require(changed.status == CommandStatus::Changed && changed.propertyCommand,
                "circle collider fields must use the same targeted property command path");
    };
    setCircle({std::string("radius")}, 8.0);
    setCircle({std::string("offset"), std::string("x")}, 2.0);
    setCircle({std::string("offset"), std::string("y")}, -3.0);
    setCircle({std::string("trigger")}, true);
    setCircle({std::string("collisionGroup")}, 3);
    setCircle({std::string("collisionMask")}, 9);

    set(3, std::nullopt, {std::string("velocity"), std::string("x")}, 1.0);
    set(3, std::nullopt, {std::string("velocity"), std::string("y")}, 2.0);
    set(3, std::nullopt, {std::string("force"), std::string("x")}, 3.0);
    set(3, std::nullopt, {std::string("force"), std::string("y")}, 4.0);
    set(3, std::nullopt, {std::string("mass")}, 2.0);
    set(3, std::nullopt, {std::string("angularVelocity")}, 0.5);
    set(3, std::nullopt, {std::string("torque")}, 1.5);
    set(3, std::nullopt, {std::string("friction")}, 0.8);
    set(3, std::nullopt, {std::string("restitution")}, 0.4);
    set(3, std::nullopt, {std::string("bodyType")}, 1);
    set(3, std::nullopt, {std::string("gravity")}, false);

    set(4, std::nullopt, {std::string("textureFilePath")}, "assets/Textures/New.PNG");
    set(4, std::nullopt, {std::string("flip")}, 1);
    set(4, std::nullopt, {std::string("width")}, 40.0);
    set(4, std::nullopt, {std::string("height")}, 30.0);
    set(4, std::nullopt, {std::string("sourceRectEnabled")}, false);
    set(4, std::nullopt, {std::string("sourceRect"), std::string("x")}, 2.0);
    set(4, std::nullopt, {std::string("sourceRect"), std::string("y")}, 3.0);
    set(4, std::nullopt, {std::string("sourceRect"), std::string("w")}, 14.0);
    set(4, std::nullopt, {std::string("sourceRect"), std::string("h")}, 10.0);

    set(5, std::nullopt, {std::string("currentFrameIndex")}, 1);
    set(5, std::nullopt, {std::string("playbackSpeed")}, 1.5);
    set(5, std::nullopt, {std::string("playing")}, true);
    set(5, std::nullopt, {std::string("animationClipFilePath")}, "assets/Walk.animset");
    set(6, std::nullopt, {std::string("filePath")}, "assets/Sounds/Jump.wav");
    set(6, std::nullopt, {std::string("gain")}, 0.75);
    set(6, std::nullopt, {std::string("loopCount")}, 2);
    set(6, std::nullopt, {std::string("autoPlay")}, true);

    const auto nullable = set(7, 0, {std::string("nullable")}, true);
    require(nullable.before.present && nullable.before.value.is_null() && nullable.after.present &&
                nullable.after.value == true,
            "an explicitly serialized null must remain distinct from a missing field");
    const PropertyAddress selfReferenceAddress{document.documentGeneration, 0, 7, 0,
                                               {std::string("references"), std::string("self")}};
    const auto zeroReference = commands.set_property(document, selfReferenceAddress, 11);
    require(zeroReference.status == CommandStatus::Changed && zeroReference.propertyCommand &&
                zeroReference.propertyCommand->before.value == 0 && zeroReference.propertyCommand->after.value == 11,
            "file ID zero must remain an editable integer reference value");
    const auto restoreZeroReference = commands.apply(document, *zeroReference.propertyCommand,
                                                     CommandDirection::Reverse);
    require(restoreZeroReference.status == CommandStatus::Changed &&
                document.root[0]["components"][6]["behaviors"][0]["references"]["self"] == 0,
            "reversing a reference edit must restore the exact zero ID");
    const auto missing = set(7, 0, {std::string("draft")}, "");
    require(!missing.before.present && missing.after.present && missing.after.value == "",
            "missing keys must remain distinct from explicit empty strings");
    auto result = commands.apply(document, missing, CommandDirection::Reverse);
    require(result.status == CommandStatus::Changed &&
                !document.root[0]["components"][6]["behaviors"][0].contains("draft"),
            "undoing a materialized field must restore its missing-key state");

    require(document.root[0]["unfamiliar"] == Json{{"preserve", true}} &&
                document.root[0]["components"][6]["behaviors"][0]["references"] ==
                    Json{{"self", 0}, {"internal", 10}, {"external", 100}, {"component", 2}},
            "field edits must preserve unknown data and self/internal/external/component reference payloads");

    Json referenceBehavior{{"type", "ReferenceFixture"},
                           {"references", {{"self", 0}, {"internal", 10}, {"external", 100},
                                            {"component", 2}}}};
    Json structuralRoot = object(0, "Root", Json::array({
        component(1, 5, Json{{"behaviors", Json::array({referenceBehavior})}}),
        component(2, 3, Json{{"colliderType", 1}, {"width", 8.0}, {"height", 8.0},
                             {"offset", {{"x", 0.0}, {"y", 0.0}}}, {"trigger", false},
                             {"collisionGroup", 1}, {"collisionMask", 1}})}),
        Json::array({object(10, "Internal Child")}));
    auto structuralParsed = parse_document(Json::array({structuralRoot}), 81);
    require(static_cast<bool>(structuralParsed), "structural mutation fixture must parse");
    DocumentSession structuralDocument = std::move(*structuralParsed.document);
    EditCommandService structuralCommands;
    auto duplicate = structuralCommands.duplicate_object(structuralDocument, 0);
    require(duplicate.status == CommandStatus::Changed && duplicate.command &&
                structuralDocument.root.size() == 2,
            "subtree duplication must insert one complete copy after its source");
    const Json &copy = structuralDocument.root[1];
    require(copy["id"] != 0 && copy["name"] != "Root" && copy["position"]["x"] == 32.0 &&
                copy["position"]["y"] == 32.0 && copy["children"][0]["id"] != 10 &&
                copy["components"][0]["id"] != 1 && copy["components"][1]["id"] != 2,
            "duplicate subtrees must receive unique object/component IDs, a new name and the local +32 offset");
    require(copy["components"][0]["behaviors"][0]["references"] == referenceBehavior["references"],
            "duplication must preserve self, internal, external and component-target reference numbers");
    auto conflict = structuralCommands.add_component(
        structuralDocument, 0,
        Json{{"type", 3}, {"colliderType", 0}, {"radius", 4.0}, {"offset", {{"x", 0.0}, {"y", 0.0}}},
             {"trigger", false}, {"collisionGroup", 1}, {"collisionMask", 1}});
    require(conflict.status == CommandStatus::Rejected && structuralDocument.root[0]["components"].size() == 2,
            "box and circle colliders must reject an incompatible duplicate component slot");

    auto addedComponent = structuralCommands.add_component(
        structuralDocument, 0,
        Json{{"type", 6}, {"textureFilePath", "assets/hero.png"}, {"flip", 0}, {"width", 16.0},
             {"height", 16.0}, {"sourceRectEnabled", false}});
    require(addedComponent.status == CommandStatus::Changed && addedComponent.command &&
                structuralDocument.root[0]["components"].size() == 3,
            "component creation must allocate its identity and retain only the inserted payload");
    const int insertedComponentId = structuralDocument.root[0]["components"][2]["id"].get<int>();
    auto removedComponent = structuralCommands.remove_component(structuralDocument, 0, insertedComponentId);
    require(removedComponent.status == CommandStatus::Changed && removedComponent.command &&
                structuralDocument.root[0]["components"].size() == 2,
            "component removal must target a stable component ID");
    auto restoredComponent = structuralCommands.apply(structuralDocument, *removedComponent.command,
                                                      CommandDirection::Reverse);
    require(restoredComponent.status == CommandStatus::Changed &&
                structuralDocument.root[0]["components"][2]["id"] == insertedComponentId,
            "reversing component removal must restore the exact component identity and order");

    auto created = structuralCommands.create_object(structuralDocument, 0, "Created Child");
    require(created.status == CommandStatus::Changed && created.command &&
                structuralDocument.root[0]["children"].size() == 2,
            "creating a child must update the hierarchy through a reversible structural command");
    auto deleted = structuralCommands.delete_object(structuralDocument,
                                                     structuralDocument.root[0]["children"][1]["id"].get<int>());
    require(deleted.status == CommandStatus::Changed && deleted.command &&
                structuralDocument.root[0]["children"].size() == 1,
            "deleting an object must retain only its affected subtree in the command");
    auto restored = structuralCommands.apply(structuralDocument, *deleted.command, CommandDirection::Reverse);
    require(restored.status == CommandStatus::Changed && structuralDocument.root[0]["children"].size() == 2,
            "reversing object deletion must restore its original sibling position and payload");

    auto addedBehavior = structuralCommands.add_behavior(
        structuralDocument, 0, 1, Json{{"type", "AdditionalBehavior"}, {"nullable", nullptr}});
    require(addedBehavior.status == CommandStatus::Changed && addedBehavior.command &&
                structuralDocument.root[0]["components"][0]["behaviors"].size() == 2,
            "adding a behavior must preserve explicit null fields and use a structural command");
    auto removedBehavior = structuralCommands.remove_behavior(structuralDocument, 0, 1, 1);
    require(removedBehavior.status == CommandStatus::Changed && removedBehavior.command &&
                structuralDocument.root[0]["components"][0]["behaviors"].size() == 1,
            "removing a behavior must preserve its exact payload for reversal");
    restored = structuralCommands.apply(structuralDocument, *removedBehavior.command, CommandDirection::Reverse);
    require(restored.status == CommandStatus::Changed &&
                structuralDocument.root[0]["components"][0]["behaviors"][1]["nullable"].is_null(),
            "reversing behavior removal must distinguish explicit null from missing data");

    HierarchyPanel hierarchy;
    const auto &initialRows = hierarchy.visible_rows(structuralDocument);
    const std::size_t initialRowCount = initialRows.size();
    const std::vector<int> expectedRowOrder{0, 0, 1, 2, 15, 15, 10, 16, 16, 11, 12, 13, 14};
    require(initialRows.size() == expectedRowOrder.size() &&
                std::equal(initialRows.begin(), initialRows.end(), expectedRowOrder.begin(),
                           [](const HierarchyRow &row, int id) { return row.id == id; }) &&
                initialRows[0].kind == HierarchyRowKind::Object &&
                initialRows[1].kind == HierarchyRowKind::ComponentsNode &&
                initialRows[2].kind == HierarchyRowKind::Component &&
                initialRows[2].parentRowIndex == 1 &&
                initialRows[4].kind == HierarchyRowKind::Object &&
                initialRows[5].kind == HierarchyRowKind::ComponentsNode &&
                initialRows[6].kind == HierarchyRowKind::Component &&
                initialRows[6].parentRowIndex == 5,
            "hierarchy rows must group components under their owner while preserving saved order");
    const auto rootRename = structuralCommands.set_property(
        structuralDocument, PropertyAddress{structuralDocument.documentGeneration, 0, std::nullopt,
                                             std::nullopt, {std::string("name")}},
        "Renamed Root");
    require(rootRename.status == CommandStatus::Changed && hierarchy.visible_rows(structuralDocument)[0].label ==
                                                              "Renamed Root",
            "hierarchy labels must refresh after value edits while row order stays structural-revision cached");
    const auto extraChild = structuralCommands.create_object(structuralDocument, 0, "Ordered Child");
    const auto &rowsAfterStructure = hierarchy.visible_rows(structuralDocument);
    require(extraChild.status == CommandStatus::Changed && rowsAfterStructure.size() > initialRowCount,
            "hierarchy row order must rebuild when the document structure revision changes");

    SelectionModel selection;
    selection.set(document, Selection{0, 2, std::nullopt, FocusedPanel::Inspector});
    document.root[0]["components"].erase(document.root[0]["components"].begin() + 1);
    ++document.structureRevision;
    selection.repair(document);
    require(selection.current().objectId == 0 && !selection.current().componentId,
            "a removed selected component must repair selection to its surviving owner");
    document.root.erase(document.root.begin());
    ++document.structureRevision;
    selection.repair(document);
    require(!selection.current().objectId && !selection.current().componentId,
            "a removed selected object must clear selection");
}

void run_typed_reference_cases()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets / "Textures");
    std::filesystem::create_directories(assets / "Textures" / "Space Folder");
    std::filesystem::create_directories(assets / "Audio");
    std::filesystem::create_directories(assets / "Animations");
    std::ofstream(assets / "Textures" / "Hero.PNG", std::ios::binary) << "texture";
    std::ofstream(assets / "Textures" / "Space Folder" / "雪 Hero.PNG", std::ios::binary) << "texture";
    std::ofstream(assets / "Audio" / "Click.WAV", std::ios::binary) << "audio";
    std::ofstream(assets / "Animations" / "Walk.animset", std::ios::binary) << "animation";
    std::ofstream(temporary.path() / "Outside.png", std::ios::binary) << "outside";
    std::filesystem::create_directory_symlink(temporary.path(), assets / "Escape");

    Json referenceBehavior{{"type", "ReferenceFixture"}, {"fields", {{"target", 99}}}};
    auto root = object(0, "Root", Json::array({component(1, 2, Json{{"width", 640.0}, {"height", 480.0}}),
                                               component(2, 3, Json{{"colliderType", 1}, {"width", 8.0},
                                                                   {"height", 8.0}, {"offset", {{"x", 0.0}, {"y", 0.0}}},
                                                                   {"trigger", false}, {"collisionGroup", 1},
                                                                   {"collisionMask", 1}}),
                                               component(3, 5, Json{{"behaviors", Json::array({referenceBehavior})}}),
                                               component(4, 6, Json{{"textureFilePath", "assets/Textures/Hero.PNG"}})}),
                       Json::array({object(5, "Reference Target")}));
    auto parsed = parse_document(Json::array({root}), 90, std::nullopt, project, assets);
    require(static_cast<bool>(parsed), "typed reference fixture must parse");
    auto document = std::move(*parsed.document);

    const auto objectReference = inspect_scene_reference(document, 0, "GameObject");
    const auto cameraReference = inspect_scene_reference(document, 1, "Camera");
    const auto wrongKind = inspect_scene_reference(document, 2, "Camera");
    const auto dangling = inspect_scene_reference(document, 99, "GameObject");
    require(objectReference.state == ReferenceState::Resolved &&
                cameraReference.state == ReferenceState::Resolved &&
                wrongKind.state == ReferenceState::WrongKind && dangling.state == ReferenceState::Dangling,
            "typed scene references must distinguish resolved, wrong-kind and dangling IDs");
    std::string error;
    require(validate_scene_reference_assignment(document, 1, "Camera", false, error) &&
                !validate_scene_reference_assignment(document, 2, "Camera", false, error) &&
                !validate_scene_reference_assignment(document, 99, "GameObject", false, error) &&
                validate_scene_reference_assignment(document, nullptr, "Camera", true, error) &&
                !validate_scene_reference_assignment(document, nullptr, "Camera", false, error) &&
                !validate_scene_reference_assignment(document, "1", "GameObject", true, error),
            "reference assignment must reject wrong-kind, dangling, non-integer and disallowed-null values");

    const Json texturePath = "assets/Textures/Hero.PNG";
    const Json spacedUnicodePath = "assets\\Textures\\Space Folder\\雪 Hero.PNG";
    const Json audioPath = "assets/Audio/Click.WAV";
    const Json animationPath = "assets/Animations/Walk.animset";
    const auto texture = inspect_asset_reference(document, texturePath, "Texture");
    const auto spacedUnicodeTexture = inspect_asset_reference(document, spacedUnicodePath, "Texture");
    const auto audio = inspect_asset_reference(document, audioPath, "Audio");
    const auto animation = inspect_asset_reference(document, animationPath, "AnimationClip");
    require(texture.state == ReferenceState::Resolved && texture.path == texturePath.get<std::string>() &&
                spacedUnicodeTexture.state == ReferenceState::Resolved &&
                spacedUnicodeTexture.path == "assets/Textures/Space Folder/雪 Hero.PNG" &&
                audio.state == ReferenceState::Resolved && animation.state == ReferenceState::Resolved,
            "asset validation must preserve case, normalize known separators, and support spaces and UTF-8 paths");
    require(validate_asset_reference_assignment(document, texturePath, "Texture", false, error) &&
                !validate_asset_reference_assignment(document, audioPath, "Texture", false, error) &&
                !validate_asset_reference_assignment(document, "assets/../Outside.png", "Texture", false, error) &&
                !validate_asset_reference_assignment(document, "assets/Escape/Outside.png", "Texture", false, error) &&
                inspect_asset_reference(document, "assets/missing.png", "Texture").state == ReferenceState::Dangling &&
                validate_asset_reference_assignment(document, nullptr, "Texture", true, error) &&
                !validate_asset_reference_assignment(document, nullptr, "Texture", false, error) &&
                inspect_asset_reference(document, "42", "Texture").state == ReferenceState::Invalid,
            "asset assignment must reject wrong type, traversal, symlink escape and unsupported null values");

    SelectionModel selection;
    EditCommandService commands;
    commands.reset_history(document.documentGeneration);
    commands.mark_saved();
    const PropertyAddress assetAddress{90, 0, 4, std::nullopt, {std::string("textureFilePath")}};
    require(validate_asset_reference_assignment(document, spacedUnicodePath, "Texture", false, error) &&
                commands.set_property(document, assetAddress, "assets/Textures/Space Folder/雪 Hero.PNG").status ==
                    CommandStatus::Changed &&
                inspect_asset_reference(document, document.root[0]["components"][3]["textureFilePath"],
                                        "Texture").state == ReferenceState::Resolved,
            "a valid Unicode asset assignment must commit through the shared command path");
    require(commands.undo(document, selection) &&
                document.root[0]["components"][3]["textureFilePath"] == "assets/Textures/Hero.PNG" &&
                commands.redo(document, selection) &&
                document.root[0]["components"][3]["textureFilePath"] == "assets/Textures/Space Folder/雪 Hero.PNG",
            "asset assignment must restore exact prior text across undo and redo");

    const PropertyAddress sceneReferenceAddress{90, 0, 3, 0,
        {std::string("fields"), std::string("target")}};
    require(commands.set_property(document, sceneReferenceAddress, 5).status == CommandStatus::Changed &&
                inspect_scene_reference(document, document.root[0]["components"][2]["behaviors"][0]["fields"]["target"],
                                        "GameObject").state == ReferenceState::Resolved,
            "scene-reference assignments must use the common reversible property command");
    require(commands.undo(document, selection) && commands.dirty() &&
                document.root[0]["components"][2]["behaviors"][0]["fields"]["target"] == 99 &&
                commands.redo(document, selection),
            "scene-reference undo/redo must preserve a dangling prior integer without rebinding");
    const auto removedTarget = commands.delete_object(document, 5);
    require(removedTarget.status == CommandStatus::Changed &&
                document.root[0]["components"][2]["behaviors"][0]["fields"]["target"] == 5 &&
                inspect_scene_reference(document, 5, "GameObject").state == ReferenceState::Dangling,
            "deleting a target must leave its serialized reference unchanged and visibly dangling");
    require(commands.undo(document, selection) && inspect_scene_reference(document, 5, "GameObject").state ==
                ReferenceState::Resolved,
            "undoing target deletion must resolve the original serialized reference again");
}

void run_runtime_compatibility_cases()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    auto authored = make_default_document(project, assets, 70);
    authored.root[0]["unfamiliar"] = Json{{"retained", true}};
    const auto editorPath = project / "editor-output.scene";
    const auto runtimePath = project / "runtime-output.scene";
    const auto saved = save_document(authored, editorPath, SaveOptions{.saveAs = true});
    require(saved.succeeded(), "editor output must save before the runtime compatibility check");

    RuntimeOptions options;
    options.assetRoot = assets.string();
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    platformator::Runtime runtime(options);
    runtime.loadScene(editorPath.string());
    require(runtime.getGameObject("Camera") != nullptr && runtime.getMainCamera() != nullptr,
            "the runtime scene loader must accept a valid editor-authored camera scene");
    runtime.saveScene(runtimePath.string());

    const auto editorRead = load_document_file(editorPath, 71, project, assets);
    require(editorRead && editorRead.document->root[0]["unfamiliar"] == Json{{"retained", true}},
            "the editor reopen path must retain unfamiliar data that the runtime does not own");
    const Json runtimeOutput = read_json(runtimePath);
    require(runtimeOutput.is_array() && runtimeOutput.size() == 1 &&
                runtimeOutput[0]["components"][0]["type"] == 2 &&
                runtimeOutput[0]["components"][0]["width"] == authored.root[0]["components"][0]["width"] &&
                runtimeOutput[0]["components"][0]["height"] == authored.root[0]["components"][0]["height"] &&
                !runtimeOutput[0].contains("unfamiliar"),
            "runtime scene I/O must agree on known camera semantics while editor serialization retains unfamiliar fields");
}

int main()
{
    try
    {
        run_document_cases();
        run_document_io_cases();
        run_document_authoring_cases();
        run_typed_reference_cases();
        run_runtime_compatibility_cases();
    }
    catch (const std::exception &error)
    {
        std::cerr << "platformator_editor_document_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
