#include "editcommands.h"
#include "editorwork.h"
#include "test_support.h"

#include <iostream>
#include <stdexcept>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace
{
PropertyAddress address(std::uint64_t generation, int objectId,
                        std::initializer_list<FieldPathSegment> path)
{
    return PropertyAddress{generation, objectId, std::nullopt, std::nullopt,
                            std::vector<FieldPathSegment>(path)};
}

PropertyAddress component_address(std::uint64_t generation, int objectId, int componentId,
                                  std::initializer_list<FieldPathSegment> path)
{
    return PropertyAddress{generation, objectId, componentId, std::nullopt,
                            std::vector<FieldPathSegment>(path)};
}

PropertyAddress behavior_address(std::uint64_t generation, int objectId, int componentId,
                                 std::size_t behaviorOrdinal,
                                 std::initializer_list<FieldPathSegment> path)
{
    return PropertyAddress{generation, objectId, componentId, behaviorOrdinal,
                            std::vector<FieldPathSegment>(path)};
}

DocumentSession fixture()
{
    auto parsed = parse_document(scene({object(1, "Before")}));
    require(static_cast<bool>(parsed), "batch command fixture must parse");
    parsed.document->root[0]["position"]["x"] = 13.0;
    parsed.document->root[0]["removeMe"] = true;
    return std::move(*parsed.document);
}

DocumentSession canonical_path_fixture()
{
    auto document = fixture();
    document.root[0]["components"] = Json::array({
        component(2, 90, Json{{"width", 10}, {"custom", Json{{"id", "component-note"}}}}),
        component(3, 91, Json{{"behaviors", Json::array({Json{{"type", "Move"}, {"speed", 2.0}}})}})});
    document.root[0]["children"] = Json::array({object(4, "Child")});
    document.root[0]["custom"] = Json{{"id", "opaque-object-note"},
                                        {"type", "opaque-user-type"},
                                        {"child", Json{{"value", 1}}}};
    ++document.structureRevision;
    rebuild_document_indexes(document);
    return document;
}

void test_canonical_property_boundaries_and_alias_overlap()
{
    auto document = canonical_path_fixture();
    const auto generation = document.documentGeneration;
    EditCommandService commands;
    const Json before = document.root;
    const auto revision = document.valueRevision;
    const auto reject = [&](const PropertyAddress &target, Json value, const char *reason) {
        const auto result = commands.set_property(document, target, std::move(value));
        require(result.status == CommandStatus::Rejected && document.root == before &&
                    document.valueRevision == revision,
                reason);
    };

    reject(address(generation, 1, {std::string("children"), std::size_t{0}, std::string("id")}), 40,
           "object identity must remain protected through a nested object address");
    reject(address(generation, 1, {std::string("components"), std::size_t{0}, std::string("id")}), 40,
           "component identity must remain protected through an object-owned component path");
    reject(address(generation, 1, {std::string("components"), std::size_t{0}, std::string("type")}), 91,
           "component type must remain protected through an object-owned component path");
    reject(address(generation, 1, {std::string("components"), std::size_t{1},
                                  std::string("behaviors"), std::size_t{0}, std::string("type")}),
           "Run", "behavior type must remain protected through nested owner paths");
    reject(behavior_address(generation, 1, 3, 0, {std::string("type")}), "Run",
           "behavior type must remain protected through a behavior address");
    reject(address(generation, 1, {std::string("components")}), Json::array(),
           "component collections must only change through structural commands");
    reject(address(generation, 1, {std::string("components"), std::size_t{0}}), component(8, 92),
           "whole component records must only change through structural commands");
    reject(address(generation, 1, {std::string("children")}), Json::array(),
           "child collections must only change through structural commands");

    require(commands.set_property(document, address(generation, 1,
                                           {std::string("custom"), std::string("id")}),
                                  "new opaque id").status == CommandStatus::Changed &&
                commands.set_property(document, address(generation, 1,
                                           {std::string("custom"), std::string("type")}),
                                  "new opaque type").status == CommandStatus::Changed &&
                document.root[0]["custom"]["id"] == "new opaque id" &&
                document.root[0]["custom"]["type"] == "new opaque type",
            "opaque nested user fields named id/type must remain editable");

    auto conflictDocument = canonical_path_fixture();
    const Json conflictBefore = conflictDocument.root;
    const auto conflictGeneration = conflictDocument.documentGeneration;
    EditCommandService conflictCommands;
    const std::vector<BatchPropertyEdit> aliases{
        {address(conflictGeneration, 1, {std::string("name")}), PropertyValue::of("Must roll back")},
        {address(conflictGeneration, 1, {std::string("components"), std::size_t{0}, std::string("width")}),
         PropertyValue::of(20)},
        {component_address(conflictGeneration, 1, 2, {std::string("width")}), PropertyValue::of(30)}};
    const auto aliasResult = conflictCommands.apply_batch(conflictDocument, aliases);
    require(aliasResult.status == CommandStatus::Rejected && conflictDocument.root == conflictBefore &&
                conflictCommands.history_status().undoCommands == 0 &&
                conflictCommands.last_work_stats().propertyBatchOverlapChecks <= aliases.size() * 2,
            "same-field object/component aliases must be rejected before any patch or history is committed");

    const std::vector<BatchPropertyEdit> ancestorConflict{
        {address(conflictGeneration, 1, {std::string("custom")}), PropertyValue::of(Json::object())},
        {address(conflictGeneration, 1, {std::string("custom"), std::string("child"), std::string("value")}),
         PropertyValue::of(2)}};
    require(conflictCommands.apply_batch(conflictDocument, ancestorConflict).status == CommandStatus::Rejected &&
                conflictDocument.root == conflictBefore,
            "a nested property and its ancestor must be rejected as an overlapping batch");

    const auto committed = conflictCommands.apply_batch(conflictDocument,
        {{component_address(conflictGeneration, 1, 2, {std::string("width")}), PropertyValue::of(25)}});
    require(committed.status == CommandStatus::Changed && conflictDocument.root[0]["components"][0]["width"] == 25,
            "a single canonical component-address patch must commit");
    SelectionModel selection;
    require(conflictCommands.undo(conflictDocument, selection).status == CommandStatus::Changed &&
                conflictDocument.root == conflictBefore,
            "Undo must resolve the canonical component address and restore the full prior document");
    require(conflictCommands.redo(conflictDocument, selection).status == CommandStatus::Changed &&
                conflictDocument.root[0]["components"][0]["width"] == 25,
            "Redo must replay the canonical component address without orphaning its history");
}

void test_exact_property_values_and_one_history_entry()
{
    auto document = fixture();
    EditCommandService commands;
    const auto generation = document.documentGeneration;
    const std::vector<BatchPropertyEdit> edits{
        {address(generation, 1, {std::string("name")}), PropertyValue::of("")},
        {address(generation, 1, {std::string("customNull")}), PropertyValue::of(nullptr)},
        {address(generation, 1, {std::string("position"), std::string("x")}), PropertyValue::of(0.0)},
        {address(generation, 1, {std::string("optionalText")}), PropertyValue::of("")},
        {address(generation, 1, {std::string("removeMe")}), PropertyValue::missing()}};

    const auto applied = commands.apply_batch(document, edits);
    require(applied.status == CommandStatus::Changed && commands.history_status().undoCommands == 1,
            "one completed property batch must publish exactly one history entry");
    const Json &edited = document.root[0];
    require(edited["name"] == "" && edited["customNull"].is_null() &&
                edited["position"]["x"] == 0.0 && edited["optionalText"] == "" &&
                !edited.contains("removeMe"),
            "a batch must preserve missing, null, zero, and empty values distinctly");

    SelectionModel selection;
    const auto undone = commands.undo(document, selection);
    require(undone.status == CommandStatus::Changed && document.root[0]["name"] == "Before" &&
                !document.root[0].contains("customNull") && document.root[0]["position"]["x"] == 13.0 &&
                !document.root[0].contains("optionalText"),
            "Undo must restore exact prior field presence and values for the whole batch");
}

void test_prevalidation_and_exact_rollback()
{
    auto document = fixture();
    const Json before = document.root;
    EditCommandService commands;
    const std::vector<BatchPropertyEdit> edits{
        {address(document.documentGeneration, 1, {std::string("name")}), PropertyValue::of("After")},
        {address(document.documentGeneration, 999, {std::string("name")}), PropertyValue::of("stale")}};
    const auto applied = commands.apply_batch(document, edits);
    require(applied.status == CommandStatus::StaleAddress && document.root == before &&
                commands.history_status().undoCommands == 0,
            "failure after a valid operation must leave the exact prior document and history intact");
}

void test_saved_token_branch_and_selection_restore()
{
    auto document = fixture();
    EditCommandService commands;
    SelectionModel selection;
    selection.set(document, Selection{.objectId = 1, .focusedPanel = FocusedPanel::Hierarchy});
    commands.set_selection_provider([&] { return selection.current(); });
    commands.mark_saved();

    const Selection after{.objectId = 1, .focusedPanel = FocusedPanel::Inspector};
    auto edit = commands.apply_batch(document,
        {{address(document.documentGeneration, 1, {std::string("name")}), PropertyValue::of("A")}},
        SelectionState{selection.current(), after});
    require(edit.status == CommandStatus::Changed && commands.dirty(),
            "a changed batch after the saved token must make the document dirty");
    auto undone = commands.undo(document, selection);
    require(undone.status == CommandStatus::Changed && !commands.dirty() &&
                selection.current().focusedPanel == FocusedPanel::Hierarchy,
            "Undo to the saved token must restore clean state and the before-selection");

    edit = commands.apply_batch(document,
        {{address(document.documentGeneration, 1, {std::string("name")}), PropertyValue::of("B")}},
        SelectionState{selection.current(), after});
    require(edit.status == CommandStatus::Changed && !commands.history_status().canRedo,
            "a new batch after undo must discard the abandoned redo branch");
    auto redone = commands.undo(document, selection);
    require(redone.status == CommandStatus::Changed && !commands.dirty() &&
                selection.current().focusedPanel == FocusedPanel::Hierarchy,
            "the replacement branch must retain saved-token and selection semantics");
}

void test_mixed_structural_batch_is_atomic_and_reversible()
{
    auto document = fixture();
    const Json before = document.root;
    EditCommandService commands;
    int notifications = 0;
    commands.subscribe([&](const DocumentChange &change, const std::vector<int> &affected) {
        ++notifications;
        require(change.categories == std::vector<DocumentChangeCategory>{
                                           DocumentChangeCategory::Label,
                                           DocumentChangeCategory::Structure},
                "mixed batch changes must publish sorted label and structure categories");
        require(affected == std::vector<int>{2},
                "batch notification object IDs must be sorted and deduplicated");
    });

    StructuralCommand insert;
    insert.documentGeneration = document.documentGeneration;
    insert.kind = StructuralCommandKind::InsertObject;
    insert.insertionIndex = 1;
    insert.payload = object(2, "Added");
    const PropertyCommand changeName{
        address(document.documentGeneration, 2, {std::string("name")}),
        PropertyValue::of("Added"), PropertyValue::of("Changed"), {}};
    const std::vector<BatchOperation> validOperations{insert, changeName};
    const auto applied = commands.apply_batch(document, validOperations);
    require(applied.status == CommandStatus::Changed && commands.history_status().undoCommands == 1 &&
                notifications == 1 && document.root[1]["name"] == "Changed",
            "mixed property and structural operations must commit and notify once");
    require(applied.change.affectedObjectIds == std::vector<int>{2},
            "overlapping affected owners must appear once in the typed delta");

    SelectionModel selection;
    const auto undone = commands.undo(document, selection);
    require(undone.status == CommandStatus::Changed && document.root == before && notifications == 2,
            "Undo must reverse a mixed batch in reverse operation order as one complete change");
    const auto redone = commands.redo(document, selection);
    require(redone.status == CommandStatus::Changed && document.root[1]["name"] == "Changed" &&
                notifications == 3,
            "Redo must replay the complete mixed batch with one notification");

    auto failingDocument = fixture();
    const Json failingBefore = failingDocument.root;
    EditCommandService failingCommands;
    auto stale = changeName;
    stale.address.objectId = 999;
    const std::vector<BatchOperation> failingOperations{insert, stale};
    const auto failed = failingCommands.apply_batch(failingDocument, failingOperations);
    require(failed.status == CommandStatus::StaleAddress && failingDocument.root == failingBefore &&
                failingCommands.history_status().undoCommands == 0,
            "a late stale operation must roll back prior structural work and leave history untouched");
}

void test_history_retention_failure_rejects_batch_without_mutation()
{
    auto document = fixture();
    EditCommandService commands;
    commands.mark_saved();
    const Json before = document.root;
    const auto valueRevision = document.valueRevision;
    const auto structureRevision = document.structureRevision;
    const auto historyBefore = commands.history_status();
    commands.inject_history_retention_failure_once_for_testing();

    const auto result = commands.apply_batch(document,
        {{address(document.documentGeneration, 1, {std::string("name")}), PropertyValue::of("After")},
         {address(document.documentGeneration, 1, {std::string("missing")}), PropertyValue::of(nullptr)}});
    const auto historyAfter = commands.history_status();
    require(result.status == CommandStatus::Rejected && document.root == before &&
                !result.message.empty() &&
                document.valueRevision == valueRevision && document.structureRevision == structureRevision &&
                historyAfter.currentToken == historyBefore.currentToken &&
                historyAfter.savedToken == historyBefore.savedToken &&
                historyAfter.undoCommands == historyBefore.undoCommands &&
                !commands.last_error().empty(),
            "failed batch history retention must preserve authored state, revisions, saved token and history");
}

void test_targeted_batch_work_bounds_for_fields_and_deep_rectangle_gesture()
{
    auto document = fixture();
    EditCommandService commands;
    const auto generation = document.documentGeneration;
    const auto single = commands.apply_batch(document,
        {{address(generation, 1, {std::string("position"), std::string("x")}),
          PropertyValue::of(4.0)}});
    auto work = commands.last_work_stats();
    require(single.status == CommandStatus::Changed && work.propertyBatchTargetsResolved == 1 &&
                work.propertyBatchPathSegmentsResolved == 3 &&
                work.propertyBatchPatchesPrepared == 1 && work.propertyBatchPatchesCommitted == 1 &&
                work.propertyBatchIndexRebuildAttempts == 0 && work.propertyBatchDocumentCopies == 0 &&
                work.retainedHistoryEntriesCopied == 0,
            "one-field edits must resolve and patch only their indexed target without copying the document or history");

    const auto few = commands.apply_batch(document,
        {{address(generation, 1, {std::string("name")}), PropertyValue::of("Few fields")},
         {address(generation, 1, {std::string("position"), std::string("y")}), PropertyValue::of(5.0)},
         {address(generation, 1, {std::string("scale"), std::string("x")}), PropertyValue::of(2.0)}});
    work = commands.last_work_stats();
    require(few.status == CommandStatus::Changed && work.propertyBatchTargetsResolved == 3 &&
                work.propertyBatchPatchesPrepared == 3 && work.propertyBatchPatchesCommitted == 3 &&
                work.propertyBatchIndexRebuildAttempts == 0 && work.propertyBatchDocumentCopies == 0 &&
                work.retainedHistoryEntriesCopied == 0,
            "few-field work must grow with the touched targets, not scene size or retained history depth");

    Json deep = object(128, "Rectangle leaf");
    for (int id = 127; id >= 1; --id)
        deep = object(id, "Rectangle parent", Json::array(), Json::array({std::move(deep)}));
    auto parsed = parse_document(scene({std::move(deep)}));
    require(static_cast<bool>(parsed), "depth-128 rectangle fixture must parse");
    auto &deepDocument = *parsed.document;
    const Json deepBefore = deepDocument.root;
    EditCommandService deepCommands;
    const auto deepGeneration = deepDocument.documentGeneration;
    auto rectangle = deepCommands.begin_batch_gesture(deepDocument, {
        address(deepGeneration, 128, {std::string("position"), std::string("x")}),
        address(deepGeneration, 128, {std::string("position"), std::string("y")}),
        address(deepGeneration, 128, {std::string("scale"), std::string("x")}),
        address(deepGeneration, 128, {std::string("scale"), std::string("y")})});
    require(rectangle.has_value(), "depth-128 rectangle gesture must resolve its four leaf fields");
    require(rectangle->update(deepCommands, deepDocument,
                              {Json(10.0), Json(11.0), Json(12.0), Json(13.0)}).status ==
                CommandStatus::Changed,
            "depth-128 rectangle gesture must apply as a targeted field batch");
    work = deepCommands.last_work_stats();
    require(work.propertyBatchTargetsResolved == 4 && work.propertyBatchPathSegmentsResolved == 520 &&
                work.propertyBatchPatchesPrepared == 4 && work.propertyBatchPatchesCommitted == 4 &&
                work.propertyBatchIndexRebuildAttempts == 0 && work.propertyBatchDocumentCopies == 0,
            "depth-128 rectangle work must be bounded by four targets and their indexed path depth");
    require(rectangle->commit() && deepCommands.history_status().undoCommands == 1,
            "the depth-128 rectangle gesture must retain one undo entry");
    SelectionModel selection;
    require(deepCommands.undo(deepDocument, selection).status == CommandStatus::Changed &&
                deepDocument.root == deepBefore &&
                deepCommands.last_work_stats().propertyBatchTargetsResolved == 4 &&
                deepCommands.last_work_stats().propertyBatchDocumentCopies == 0,
            "Undo of a depth-128 rectangle must replay four local patches without cloning the scene");

    auto historyDocument = fixture();
    EditCommandService historyCommands;
    for (int index = 0; index < 64; ++index)
        require(historyCommands.set_property(historyDocument,
                    address(historyDocument.documentGeneration, 1, {std::string("tag")}),
                    std::to_string(index)).status == CommandStatus::Changed,
                "history-depth fixture edits must be retained");
    require(historyCommands.apply_batch(historyDocument,
                {{address(historyDocument.documentGeneration, 1, {std::string("active")}),
                  PropertyValue::of(false)}}).status == CommandStatus::Changed &&
                historyCommands.last_work_stats().retainedHistoryEntriesCopied == 0 &&
                historyCommands.history_status().undoCommands == 65,
            "appending one transaction after retained history must not copy existing history entries");
}

void test_batch_overlap_work_probe_has_an_independent_scaling_oracle()
{
    constexpr std::size_t objectCount = 512;
    Json roots = Json::array();
    std::vector<BatchPropertyEdit> edits;
    edits.reserve(objectCount);
    for (std::size_t index = 0; index < objectCount; ++index)
    {
        const int objectId = static_cast<int>(index + 1);
        roots.push_back(object(objectId, "Object " + std::to_string(objectId)));
    }
    auto parsed = parse_document(std::move(roots));
    require(static_cast<bool>(parsed), "large overlap-work fixture must parse");
    auto &document = *parsed.document;
    const auto generation = document.documentGeneration;
    for (std::size_t index = 0; index < objectCount; ++index)
    {
        const int objectId = static_cast<int>(index + 1);
        edits.push_back({address(generation, objectId, {std::string("name")}),
                         PropertyValue::of("Updated " + std::to_string(objectId))});
    }

    EditCommandService commands;
    work::reset();
    const auto result = commands.apply_batch(document, edits);
    const auto &stats = commands.last_work_stats();
    require(result.status == CommandStatus::Changed &&
                stats.propertyBatchTargetsResolved == objectCount &&
                stats.propertyBatchPatchesPrepared == objectCount &&
                stats.propertyBatchPatchesCommitted == objectCount &&
                stats.propertyBatchOverlapChecks >= objectCount &&
                stats.propertyBatchOverlapChecks <= objectCount * 2 &&
                stats.propertyBatchDocumentCopies == 0 &&
                document.root.front()["name"] == "Updated 1" &&
                document.root.back()["name"] == "Updated 512" &&
                commands.history_status().undoCommands == 1,
            "512 disjoint property edits must resolve, validate and commit one local patch each without a document copy");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    const auto counts = work::snapshot();
    const auto comparisons = counts[static_cast<std::size_t>(work::Counter::BatchOverlapPathComparisons)];
    require(counts[static_cast<std::size_t>(work::Counter::BatchPropertyTargetsResolved)] == objectCount &&
                counts[static_cast<std::size_t>(work::Counter::BatchPropertyPatchesCommitted)] == objectCount &&
                comparisons > objectCount && comparisons <= objectCount * 64,
            "the work probe must observe ordered path comparisons, reject an uninstrumented path, and bound overlap work below quadratic all-pairs validation");
#endif
}

void test_staged_history_prunes_redo_branch_and_keeps_saved_token()
{
    auto document = fixture();
    EditCommandService commands;
    SelectionModel selection;
    const auto tag = address(document.documentGeneration, 1, {std::string("tag")});
    require(commands.set_property(document, tag, "First").status == CommandStatus::Changed,
            "first branch edit must be retained");
    commands.mark_saved();
    require(commands.set_property(document, tag, "Second").status == CommandStatus::Changed &&
                commands.set_property(document, tag, "Third").status == CommandStatus::Changed,
            "two redo candidates must be retained");
    require(commands.undo(document, selection).status == CommandStatus::Changed &&
                commands.undo(document, selection).status == CommandStatus::Changed &&
                commands.history_status().redoCommands == 2,
            "the test must start its branch with two available Redo entries");

    require(commands.apply_batch(document,
                {{address(document.documentGeneration, 1, {std::string("active")}),
                  PropertyValue::of(false)}}).status == CommandStatus::Changed &&
                commands.history_status().redoCommands == 0 &&
                commands.history_status().savedTokenReachable,
            "staged append must discard the complete redo branch and retain the saved prefix");
    require(commands.undo(document, selection).status == CommandStatus::Changed &&
                document.root[0]["tag"] == "First" && !commands.dirty() &&
                commands.history_status().canRedo,
            "Undo of the new branch must return to the exact reachable saved token");
}
} // namespace

int main()
{
    try
    {
        test_exact_property_values_and_one_history_entry();
        test_prevalidation_and_exact_rollback();
        test_saved_token_branch_and_selection_restore();
        test_mixed_structural_batch_is_atomic_and_reversible();
        test_history_retention_failure_rejects_batch_without_mutation();
        test_canonical_property_boundaries_and_alias_overlap();
        test_targeted_batch_work_bounds_for_fields_and_deep_rectangle_gesture();
        test_batch_overlap_work_probe_has_an_independent_scaling_oracle();
        test_staged_history_prunes_redo_branch_and_keeps_saved_token();
    }
    catch (const std::exception &error)
    {
        std::cerr << "batch command cases failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
