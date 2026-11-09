#include "editcommands.h"
#include "test_support.h"

#include <string>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace
{
PropertyAddress behavior_address(std::uint64_t generation, std::vector<FieldPathSegment> path)
{
    return PropertyAddress{generation, 1, 4, 0, std::move(path)};
}

Json make_document()
{
    Json behavior{{"type", "ExampleBehavior"},
                  {"references", {{"target", 1}}},
                  {"fields", {{"enabled", false}, {"values", Json::array({1, 2})}}}};
    Json behaviorComponent = component(4, "ScriptComponent", Json{{"behaviors", Json::array({behavior})}});
    Json rootObject = object(1, "Root", Json::array({behaviorComponent}));
    rootObject["unfamiliar"] = Json{{"nested", "preserve"}};
    return scene({rootObject});
}
} // namespace

void run_editcommand_cases()
{
    DocumentSession document;
    document.documentGeneration = 19;
    document.root = make_document();
    EditCommandService commands;
    int notifications = 0;
    std::vector<int> lastAffected;
    commands.subscribe([&](const DocumentChange &change, const std::vector<int> &affected) {
        require(change.documentGeneration == document.documentGeneration,
                "command notifications must identify their document generation");
        ++notifications;
        lastAffected = affected;
    });

    const auto enabled = behavior_address(19, {std::string("fields"), std::string("enabled")});
    auto result = commands.set_property(document, enabled, true);
    require(result.status == CommandStatus::Changed && result.propertyCommand.has_value() &&
                result.propertyCommand->before == PropertyValue::of(false) &&
                result.propertyCommand->after == PropertyValue::of(true),
            "property edits must retain only the targeted before/after values");
    require(document.root[0]["components"][0]["behaviors"][0]["fields"]["enabled"] == true &&
                document.valueRevision == 1 && notifications == 1 && lastAffected == std::vector<int>{1},
            "successful changes must update the document revision and notify the affected object ID");

    const PropertyCommand command = *result.propertyCommand;
    result = commands.apply(document, command, CommandDirection::Reverse);
    require(result.status == CommandStatus::Changed &&
                document.root[0]["components"][0]["behaviors"][0]["fields"]["enabled"] == false,
            "reverse application must restore the original nested behavior value");
    result = commands.apply(document, command, CommandDirection::Forward);
    require(result.status == CommandStatus::Changed &&
                document.root[0]["components"][0]["behaviors"][0]["fields"]["enabled"] == true,
            "forward application must replay the same command payload");
    result = commands.apply(document, command, CommandDirection::Reverse);
    require(result.status == CommandStatus::Changed,
            "the command must remain reversible after a matching forward application");
    result = commands.apply(document, command, CommandDirection::Reverse);
    require(result.status == CommandStatus::Rejected && notifications == 4,
            "precondition failures must reject without partial mutation or notification");

    const PropertyAddress missing = behavior_address(19, {std::string("fields"), std::string("draft")});
    result = commands.set_property(document, missing, Json{{"kept", 3}});
    require(result.status == CommandStatus::Changed && result.propertyCommand &&
                !result.propertyCommand->before.present && result.propertyCommand->after.present,
            "new object keys must use an explicit missing-value sentinel");
    result = commands.apply(document, *result.propertyCommand, CommandDirection::Reverse);
    require(result.status == CommandStatus::Changed &&
                !document.root[0]["components"][0]["behaviors"][0]["fields"].contains("draft"),
            "reverse insertion must remove a formerly missing field");

    const PropertyAddress arrayValue = behavior_address(19, {std::string("fields"), std::string("values"),
                                                              std::size_t{1}});
    result = commands.set_property(document, arrayValue, 9);
    require(result.status == CommandStatus::Changed &&
                document.root[0]["components"][0]["behaviors"][0]["fields"]["values"][1] == 9,
            "nested JSON array positions must resolve within the command address");

    const auto beforeRejectedEdit = document.root;
    const auto revisionBeforeRejectedEdit = document.valueRevision;
    const PropertyAddress invalidPath = behavior_address(19, {std::string("fields"), std::string("absent"),
                                                              std::string("nested")});
    result = commands.set_property(document, invalidPath, 1);
    require(result.status == CommandStatus::Rejected && document.root == beforeRejectedEdit &&
                document.valueRevision == revisionBeforeRejectedEdit,
            "invalid intermediate paths must leave the complete document unchanged");

    auto staleAddress = enabled;
    staleAddress.documentGeneration = 18;
    result = commands.set_property(document, staleAddress, false);
    require(result.status == CommandStatus::StaleAddress && document.root == beforeRejectedEdit,
            "commands from a previous document generation must not mutate the current scene");

    auto gesture = commands.begin_gesture(document, enabled);
    require(gesture && gesture->active(), "a gesture must capture its starting property without a DOM pointer");
    result = gesture->update(commands, document, true);
    require(result.status == CommandStatus::Changed, "gesture updates must use the shared command path");
    result = gesture->cancel(commands, document);
    require(result.status == CommandStatus::Changed &&
                document.root[0]["components"][0]["behaviors"][0]["fields"]["enabled"] == false,
            "gesture cancellation must restore the exact initial value");

    gesture = commands.begin_gesture(document, enabled);
    require(gesture.has_value(), "a second gesture should begin at the current value");
    gesture->update(commands, document, true);
    const auto committed = gesture->commit();
    require(committed && committed->before == PropertyValue::of(false) &&
                committed->after == PropertyValue::of(true) && !gesture->active(),
            "a completed gesture must produce one compact command with its initial and final values");

    result = commands.set_property(document,
                                   address_for_document(document, 1, std::nullopt, std::nullopt,
                                                        {std::string("name")}),
                                   "Renamed");
    require(result.status == CommandStatus::Changed &&
                result.change.identity == DocumentIdentity{ProjectGeneration{document.projectGeneration},
                                                            document.tabId,
                                                            DocumentGeneration{document.documentGeneration}} &&
                result.change.affectedObjectIds == std::vector<int>{1} &&
                result.change.categories == std::vector<DocumentChangeCategory>{DocumentChangeCategory::Label} &&
                result.change.changedPaths == std::vector<std::string>{"object[1].name"},
            "typed label deltas must carry complete identity, one deduplicated owner and a local path");
    result = commands.set_property(document,
                                   behavior_address(19, {std::string("references"), std::string("target")}),
                                   2);
    require(result.status == CommandStatus::Changed &&
                result.change.categories == std::vector<DocumentChangeCategory>{
                                                DocumentChangeCategory::BehaviorReference},
            "behavior reference deltas must carry their dedicated category");
    require(document.root[0]["unfamiliar"]["nested"] == "preserve",
            "targeted edits must preserve unrelated unfamiliar document fields");

    Json script = component(13, 5, Json{{"behaviors", Json::array({
                                  Json{{"type", "ReferenceBehavior"},
                                       {"references", {{"target", 1}}}}})}});
    auto parsed = parse_document(scene({object(1, "Typed", Json::array({
        component(10, 6, Json{{"width", 24.0}}),
        component(11, 3, Json{{"width", 16.0}}),
        component(12, 2, Json{{"width", 640.0}}),
        std::move(script)}))}), 31);
    require(static_cast<bool>(parsed), "typed delta fixture must parse");
    auto &typedDocument = *parsed.document;
    EditCommandService typedCommands;
    auto typedEdit = [&](std::optional<int> componentId, std::optional<std::size_t> behaviorOrdinal,
                         std::vector<FieldPathSegment> path, Json value) {
        return typedCommands.set_property(typedDocument,
            address_for_document(typedDocument, 1, componentId, behaviorOrdinal, std::move(path)),
            std::move(value));
    };
    require(typedEdit(std::nullopt, std::nullopt, {std::string("active")}, false).change.categories ==
                std::vector<DocumentChangeCategory>{DocumentChangeCategory::Active},
            "active edits must carry the Active category");
    require(typedEdit(std::nullopt, std::nullopt,
                      {std::string("position"), std::string("x")}, 3.0).change.categories ==
                std::vector<DocumentChangeCategory>{DocumentChangeCategory::Pose},
            "transform edits must carry the Pose category");
    require(typedEdit(10, std::nullopt, {std::string("width")}, 25.0).change.categories ==
                std::vector<DocumentChangeCategory>{DocumentChangeCategory::Sprite},
            "sprite edits must carry the Sprite category");
    require(typedEdit(11, std::nullopt, {std::string("width")}, 17.0).change.categories ==
                std::vector<DocumentChangeCategory>{DocumentChangeCategory::Collider},
            "collider edits must carry the Collider category");
    require(typedEdit(12, std::nullopt, {std::string("width")}, 641.0).change.categories ==
                std::vector<DocumentChangeCategory>{DocumentChangeCategory::Camera},
            "camera edits must carry the Camera category");
}
