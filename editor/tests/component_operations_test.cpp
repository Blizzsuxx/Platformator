#include "componentoperations.h"
#include "test_support.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace
{
void test_builtin_component_copy_paste_reset_and_undo()
{
    auto sourceObject = object(1, "Source component");
    sourceObject["components"] = Json::array({
        Json{{"id", 10}, {"type", 3}, {"colliderType", 2}, {"width", 31.0}, {"height", 45.0},
             {"offset", Json{{"x", 5.0}, {"y", 6.0}, {"sourceNote", "must not copy"}}},
             {"trigger", true}, {"collisionGroup", 7}, {"collisionMask", 3},
             {"sourceExtension", Json{{"keep", true}}}}});
    auto targetObject = object(2, "Target component");
    targetObject["components"] = Json::array({
        Json{{"id", 11}, {"type", 3}, {"colliderType", 1}, {"width", 4.0}, {"height", 8.0},
             {"offset", Json{{"x", 0.0}, {"y", 0.0}, {"targetNote", "keep target"}}},
             {"trigger", false}, {"collisionGroup", 1}, {"collisionMask", 1},
             {"targetExtension", Json{{"keep", "target"}}}}});
    auto differentTypeObject = object(3, "Different type");
    differentTypeObject["components"] = Json::array({
        Json{{"id", 12}, {"type", 1}, {"filePath", "audio.ogg"}, {"gain", 0.25},
             {"loopCount", 4}, {"autoPlay", true}}});
    auto parsed = parse_document(scene({std::move(sourceObject), std::move(targetObject),
                                        std::move(differentTypeObject)}));
    require(static_cast<bool>(parsed), "component operation fixture must parse: " + parsed.error);
    auto &document = *parsed.document;
    ++document.structureRevision;
    rebuild_document_indexes(document);

    BehaviorCatalog catalog;
    EditCommandService commands;
    const Json sourceComponent = document.root[0]["components"][0];
    std::string copyError;
    const auto copied = copy_component_settings(sourceComponent, catalog, copyError);
    require(copied && copyError.empty() && copied->at("componentType") == 3 &&
                !copied->contains("id") && !copied->contains("type") &&
                !copied->at("fields").contains("sourceExtension") &&
                !copied->at("fields").at("offset").contains("sourceNote"),
            "copy must capture only declared settings and exclude component identity and unknown data");

    const Json beforePaste = document.root;
    const Selection current{2, 11, std::nullopt, FocusedPanel::Inspector};
    const auto pasted = paste_component_settings(document, commands, 2, 11, *copied, catalog,
                                                  SelectionState{current, current});
    require(pasted.status == CommandStatus::Changed && commands.history_status().undoCommands == 1 &&
                document.root[1]["components"][0]["id"] == 11 &&
                document.root[1]["components"][0]["type"] == 3 &&
                document.root[1]["components"][0]["width"] == 31.0 &&
                document.root[1]["components"][0]["offset"] ==
                    Json{{"x", 5.0}, {"y", 6.0}, {"targetNote", "keep target"}} &&
                document.root[1]["components"][0]["targetExtension"] == Json{{"keep", "target"}},
            "paste must update declared values in one command while preserving identity and unknown target fields");
    SelectionModel selection;
    selection.set(document, current);
    require(commands.undo(document, selection).status == CommandStatus::Changed &&
                document.root == beforePaste && commands.history_status().redoCommands == 1,
            "Undo must restore the exact target component after a settings paste");
    require(commands.redo(document, selection).status == CommandStatus::Changed,
            "Redo must reapply the component settings as one transaction");

    const Json beforeReset = document.root;
    const auto reset = reset_component_settings(document, commands, 2, 11, catalog,
                                                 SelectionState{current, current});
    require(reset.status == CommandStatus::Changed && commands.history_status().undoCommands == 2 &&
                document.root[1]["components"][0]["colliderType"] == 1 &&
                document.root[1]["components"][0]["width"] == 16.0 &&
                document.root[1]["components"][0]["offset"] ==
                    Json{{"x", 0.0}, {"y", 0.0}, {"targetNote", "keep target"}} &&
                document.root[1]["components"][0]["targetExtension"] == Json{{"keep", "target"}},
            "reset must apply supported constructor defaults without replacing unknown target fields");
    require(commands.undo(document, selection).status == CommandStatus::Changed &&
                document.root == beforeReset,
            "Undo must restore the exact component values before Reset Settings");

    require(commands.undo(document, selection).status == CommandStatus::Changed &&
                document.root == beforePaste,
            "the rollback fixture must restore the pre-paste component settings");
    const Json beforeFailedPaste = document.root;
    const auto historyBeforeFailedPaste = commands.history_status();
    commands.inject_history_retention_failure_once_for_testing();
    const auto failedPaste = paste_component_settings(document, commands, 2, 11, *copied, catalog,
                                                       SelectionState{current, current});
    const auto historyAfterFailedPaste = commands.history_status();
    require(failedPaste.status == CommandStatus::Rejected && document.root == beforeFailedPaste &&
                historyAfterFailedPaste.currentToken == historyBeforeFailedPaste.currentToken &&
                historyAfterFailedPaste.undoCommands == historyBeforeFailedPaste.undoCommands,
            "failed history retention must roll back every component field in a paste");

    const Json beforeMismatch = document.root;
    const auto historyBeforeMismatch = commands.history_status().undoCommands;
    const auto mismatch = paste_component_settings(document, commands, 3, 12, *copied, catalog,
                                                    SelectionState{current, current});
    require(mismatch.status == CommandStatus::Rejected && document.root == beforeMismatch &&
                commands.history_status().undoCommands == historyBeforeMismatch,
            "paste must reject a different component type without changing the document or history");
}

void test_group_component_operations_share_one_undo_entry()
{
    const auto withCollider = [](int objectId, int componentId, double width, std::string extraKey) {
        auto value = object(objectId, "Collider " + std::to_string(objectId));
        value["components"] = Json::array({Json{{"id", componentId}, {"type", 3},
            {"colliderType", 1}, {"width", width}, {"height", 16.0},
            {"offset", Json{{"x", 0.0}, {"y", 0.0}}}, {"trigger", false},
            {"collisionGroup", 1}, {"collisionMask", 1},
            {std::move(extraKey), Json{{"preserve", true}}}}});
        return value;
    };
    auto source = withCollider(1, 10, 50.0, "sourceExtra");
    source["components"][0]["trigger"] = true;
    auto firstTarget = withCollider(2, 20, 4.0, "firstExtra");
    auto secondTarget = withCollider(3, 30, 8.0, "secondExtra");
    auto parsed = parse_document(scene({std::move(source), std::move(firstTarget),
                                        std::move(secondTarget)}));
    require(static_cast<bool>(parsed), "group component fixture must parse: " + parsed.error);
    auto &document = *parsed.document;
    BehaviorCatalog catalog;
    EditCommandService commands;
    std::string error;
    const auto copied = copy_component_settings(document.root[0]["components"][0], catalog, error);
    require(copied && error.empty(), "group source settings must be copied");

    const Json before = document.root;
    const Selection group{{2}, std::nullopt, std::nullopt, FocusedPanel::Inspector,
                          WindowHostGeneration{1}, {2, 3}, 2};
    const std::vector<ComponentSettingsTarget> targets{{2, 20}, {3, 30}};
    const auto pasted = paste_component_settings_group(document, commands, targets, *copied, catalog,
                                                        SelectionState{group, group});
    require(pasted.status == CommandStatus::Changed && commands.history_status().undoCommands == 1 &&
                document.root[1]["components"][0]["width"] == 50.0 &&
                document.root[2]["components"][0]["width"] == 50.0 &&
                document.root[1]["components"][0]["id"] == 20 &&
                document.root[2]["components"][0]["id"] == 30 &&
                document.root[1]["components"][0].contains("firstExtra") &&
                document.root[2]["components"][0].contains("secondExtra"),
            "group paste must preserve per-target identity/extensions and commit all fields in one Undo entry");
    SelectionModel selection;
    selection.set(document, group);
    require(commands.undo(document, selection).status == CommandStatus::Changed && document.root == before,
            "one Undo must restore every component in a group paste");

    const auto reset = reset_component_settings_group(document, commands, targets, catalog,
                                                      SelectionState{group, group});
    require(reset.status == CommandStatus::Changed && commands.history_status().undoCommands == 1 &&
                document.root[1]["components"][0]["width"] == 16.0 &&
                document.root[2]["components"][0]["width"] == 16.0,
            "group reset must apply defaults to all compatible components in one Undo entry");
    require(commands.undo(document, selection).status == CommandStatus::Changed && document.root == before,
            "one Undo must restore every component in a group reset");
}

void test_sprite_source_rectangle_is_copied_and_reset_as_optional_data()
{
    auto source = object(1, "Sprite source");
    source["components"] = Json::array({Json{{"id", 40}, {"type", 6},
        {"textureFilePath", "source.png"}, {"flip", 0}, {"width", 8.0}, {"height", 9.0},
        {"sourceRectEnabled", true},
        {"sourceRect", Json{{"x", 3.0}, {"y", 4.0}, {"w", 8.0}, {"h", 9.0},
                             {"sourceNote", "not copied"}}}}});
    auto target = object(2, "Sprite target");
    target["components"] = Json::array({Json{{"id", 41}, {"type", 6},
        {"textureFilePath", "target.png"}, {"flip", 1}, {"width", 20.0}, {"height", 21.0},
        {"sourceRectEnabled", false},
        {"sourceRect", Json{{"x", 1.0}, {"y", 2.0}, {"w", 3.0}, {"h", 4.0},
                             {"targetNote", "preserve"}}},
        {"targetExtension", Json{{"keep", true}}}}});
    auto parsed = parse_document(scene({std::move(source), std::move(target)}));
    require(static_cast<bool>(parsed), "sprite rectangle fixture must parse: " + parsed.error);
    auto &document = *parsed.document;
    BehaviorCatalog catalog;
    std::string error;
    const auto copied = copy_component_settings(document.root[0]["components"][0], catalog, error);
    require(copied && error.empty() && copied->at("fields").at("sourceRect").size() == 4,
            "copy must retain supported source rectangle values and omit unknown nested keys");
    EditCommandService commands;
    const Selection current{2, 41, std::nullopt, FocusedPanel::Inspector};
    const Json beforePaste = document.root;
    const auto pasted = paste_component_settings(document, commands, 2, 41, *copied, catalog,
                                                  SelectionState{current, current});
    require(pasted.status == CommandStatus::Changed &&
                document.root[1]["components"][0]["sourceRect"] ==
                    Json{{"x", 3.0}, {"y", 4.0}, {"w", 8.0}, {"h", 9.0}, {"targetNote", "preserve"}} &&
                document.root[1]["components"][0]["textureFilePath"] == "source.png" &&
                document.root[1]["components"][0]["targetExtension"] == Json{{"keep", true}},
            "sprite paste must update supported fields without losing nested or component-level unknown data");
    SelectionModel selection;
    selection.set(document, current);
    require(commands.undo(document, selection).status == CommandStatus::Changed &&
                document.root == beforePaste,
            "Undo must restore the exact source rectangle and destination texture values");
    require(commands.redo(document, selection).status == CommandStatus::Changed,
            "Redo must restore the copied sprite settings");
    const Json beforeReset = document.root;
    const auto reset = reset_component_settings(document, commands, 2, 41, catalog,
                                                 SelectionState{current, current});
    require(reset.status == CommandStatus::Changed &&
                !document.root[1]["components"][0].contains("sourceRect") &&
                !document.root[1]["components"][0]["sourceRectEnabled"].get<bool>() &&
                document.root[1]["components"][0]["targetExtension"] == Json{{"keep", true}},
            "reset must remove the optional source rectangle and preserve unrelated destination fields");
    require(commands.undo(document, selection).status == CommandStatus::Changed &&
                document.root == beforeReset,
            "Undo must restore optional source rectangle presence and values");
}

void test_script_copy_and_reset_remove_unknown_default_overrides()
{
    auto sourceObject = object(1, "Script source");
    sourceObject["components"] = Json::array({
        Json{{"id", 20}, {"type", 5},
             {"behaviors", Json::array({Json{{"type", "Move"}, {"speed", 5.5},
                                             {"label", "source"}, {"opaque", Json{{"x", 1}}}}})}}});
    auto targetObject = object(2, "Script target");
    targetObject["components"] = Json::array({
        Json{{"id", 21}, {"type", 5},
             {"behaviors", Json::array({Json{{"type", "Move"}, {"speed", 1.5},
                                             {"label", "target"}, {"opaque", Json{{"keep", true}}}}})}}});
    auto parsed = parse_document(scene({std::move(sourceObject), std::move(targetObject)}));
    require(static_cast<bool>(parsed), "script component fixture must parse: " + parsed.error);
    auto &document = *parsed.document;
    ++document.structureRevision;
    rebuild_document_indexes(document);

    BehaviorCatalog catalog;
    catalog.configure(CatalogProtocol::CommonRunnerV1,
                      CatalogIdentity{"project", "debug", "game", "runner"});
    const Json schema{{"schema_version", 1},
        {"behaviors", Json::array({Json{{"name", "Move"}, {"field_schema_available", true},
            {"fields", Json::array({Json{{"name", "speed"}, {"kind", "number"}},
                                    Json{{"name", "label"}, {"kind", "string"}}})}}})}};
    require(catalog.begin_query() && catalog.append_stdout(schema.dump()) &&
                catalog.finish_query(0, std::chrono::milliseconds(1)),
            "script component field schema must become current");

    std::string error;
    const auto copied = copy_component_settings(document.root[0]["components"][0], catalog, error);
    require(copied && error.empty() && copied->at("behaviors")[0].at("fields").size() == 2 &&
                !copied->at("behaviors")[0].at("fields").contains("opaque"),
            "script copy must include only fields declared by the current compiled schema");

    EditCommandService commands;
    const Selection current{2, 21, std::nullopt, FocusedPanel::Inspector};
    const Json beforePaste = document.root;
    const auto pasted = paste_component_settings(document, commands, 2, 21, *copied, catalog,
                                                  SelectionState{current, current});
    require(pasted.status == CommandStatus::Changed &&
                document.root[1]["components"][0]["behaviors"][0]["speed"] == 5.5 &&
                document.root[1]["components"][0]["behaviors"][0]["label"] == "source" &&
                document.root[1]["components"][0]["behaviors"][0]["opaque"] == Json{{"keep", true}} &&
                commands.history_status().undoCommands == 1,
            "script paste must use declared behavior fields in one command and preserve unknown target data");
    SelectionModel selection;
    selection.set(document, current);
    require(commands.undo(document, selection).status == CommandStatus::Changed &&
                document.root == beforePaste,
            "Undo must restore exact script overrides after paste");
    require(commands.redo(document, selection).status == CommandStatus::Changed,
            "Redo must replay declared script fields");

    const Json beforeReset = document.root;
    const auto reset = reset_component_settings(document, commands, 2, 21, catalog,
                                                 SelectionState{current, current});
    const Json &behavior = document.root[1]["components"][0]["behaviors"][0];
    require(reset.status == CommandStatus::Changed && !behavior.contains("speed") &&
                !behavior.contains("label") && behavior["opaque"] == Json{{"keep", true}} &&
                commands.history_status().undoCommands == 2,
            "reset must remove supported script overrides without inventing constructor defaults");
    require(commands.undo(document, selection).status == CommandStatus::Changed &&
                document.root == beforeReset,
            "Undo must restore the exact script overrides removed by Reset Settings");

    const Json beforeFailedReset = document.root;
    const auto historyBeforeFailedReset = commands.history_status();
    commands.inject_history_retention_failure_once_for_testing();
    const auto failedReset = reset_component_settings(document, commands, 2, 21, catalog,
                                                       SelectionState{current, current});
    const auto historyAfterFailedReset = commands.history_status();
    require(failedReset.status == CommandStatus::Rejected && document.root == beforeFailedReset &&
                historyAfterFailedReset.currentToken == historyBeforeFailedReset.currentToken &&
                historyAfterFailedReset.undoCommands == historyBeforeFailedReset.undoCommands,
            "failed history retention must roll back all Script override removals");
}
} // namespace

int main()
{
    try
    {
        test_builtin_component_copy_paste_reset_and_undo();
        test_group_component_operations_share_one_undo_entry();
        test_sprite_source_rectangle_is_copied_and_reset_as_optional_data();
        test_script_copy_and_reset_remove_unknown_default_overrides();
        std::cout << "Component operation cases passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "platformator_editor_component_operations_test: " << error.what() << '\n';
        return 1;
    }
}
