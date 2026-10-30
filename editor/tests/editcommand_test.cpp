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
    require(document.root[0]["unfamiliar"]["nested"] == "preserve",
            "targeted edits must preserve unrelated unfamiliar document fields");
}
