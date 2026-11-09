#include "assetdocument.h"
#include "platform/nativefile.h"

#include "test_support.h"

#include <cstdlib>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace
{
thread_local bool allocationProbeEnabled = false;
thread_local std::size_t allocationProbeBytes = 0;
thread_local std::size_t failAllocationAtOrAbove = std::numeric_limits<std::size_t>::max();
}

void *operator new(std::size_t size)
{
    if (allocationProbeEnabled)
    {
        allocationProbeBytes += size;
        if (size >= failAllocationAtOrAbove)
        {
            failAllocationAtOrAbove = std::numeric_limits<std::size_t>::max();
            throw std::bad_alloc();
        }
    }
    if (void *memory = std::malloc(size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc();
}

void *operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }

namespace
{
Json animation_asset()
{
    return Json{{"name", "Walk"},
                {"frames", Json::array({Json{{"duration", 0.1}, {"texture", "walk.png"},
                                         {"unknownFrameField", Json{{"preserve", true}}}},
                                        Json{{"duration", 0.2}, {"texture", "run.png"}}})},
                {"framesPerSecond", 12.0}, {"loop", true},
                {"unknownTopLevel", Json{{"keep", 7}}}};
}

void test_path_history_preserves_unknown_fields()
{
    AssetDocument document(animation_asset(), 4);
    require(document.dirty() && !document.can_undo(),
            "new asset documents begin as unsaved without a fabricated history entry");
    auto edited = document.set("/frames/0/duration", 0.15);
    require(edited.changed && document.can_undo() && document.dirty() &&
                document.root()["frames"][0]["unknownFrameField"]["preserve"] == true &&
                document.root()["unknownTopLevel"]["keep"] == 7,
            "a path edit must preserve unrelated and unknown JSON members");
    require(document.undo().changed && document.root() == animation_asset() && document.dirty(),
            "one path Undo must restore the exact JSON asset including unknown members");
    require(document.redo().changed && document.root()["frames"][0]["duration"] == 0.15,
            "path-addressed Redo must reapply the exact asset edit");

    require(document.erase("/frames/0/unknownFrameField").changed &&
                document.undo().changed && document.root() ==
                    Json{{"name", "Walk"},
                         {"frames", Json::array({Json{{"duration", 0.15}, {"texture", "walk.png"},
                                                  {"unknownFrameField", Json{{"preserve", true}}}},
                                                 Json{{"duration", 0.2}, {"texture", "run.png"}}})},
                         {"framesPerSecond", 12.0}, {"loop", true},
                         {"unknownTopLevel", Json{{"keep", 7}}}},
            "removing a previously unknown field must undo without snapshotting away other data");

    const Json appendedFrame{{"duration", 0.0}, {"texture", "idle.png"}, {"editorNote", "new"}};
    require(document.set("/frames/2", appendedFrame).changed &&
                document.root()["frames"].size() == 3 && document.undo().changed &&
                document.root()["frames"].size() == 2 && document.redo().changed &&
                document.root()["frames"][2] == appendedFrame,
            "path transactions must support exact append, Undo, and Redo for array entries");

    require(document.erase("/frames/0").changed && document.root()["frames"].size() == 2 &&
                document.undo().changed && document.root()["frames"].size() == 3 &&
                document.root()["frames"][0]["texture"] == "walk.png",
            "Undo of an array removal must insert the original item at its exact index");
    require(!document.set("/frames/9/duration", 0.3).changed &&
                !document.set("/missing/child", 1).changed &&
                !document.erase("/missing").changed &&
                !document.set("", Json::object()).changed,
            "invalid parents, out-of-range indexes, missing removals, and root replacement are rejected");
}

void test_targeted_history_cost_and_retention_failure()
{
    Json largeRoot{{"metadata", Json{{"value", 1}}},
                   {"unrelated", std::string(2 * 1024 * 1024, 'x')}};
    AssetDocument large(std::move(largeRoot));
    allocationProbeBytes = 0;
    allocationProbeEnabled = true;
    const auto largeEdit = large.set("/metadata/value", 2);
    allocationProbeEnabled = false;
    require(largeEdit.changed && allocationProbeBytes < 64 * 1024 &&
                large.root()["unrelated"].get_ref<const std::string &>().size() == 2 * 1024 * 1024,
            "a scalar edit must not allocate in proportion to unrelated asset payload");
    allocationProbeBytes = 0;
    allocationProbeEnabled = true;
    const auto largeUndo = large.undo();
    allocationProbeEnabled = false;
    require(largeUndo.changed && allocationProbeBytes < 64 * 1024,
            "Undo must apply the retained field value without copying unrelated asset payload");
    allocationProbeBytes = 0;
    allocationProbeEnabled = true;
    const auto largeRedo = large.redo();
    allocationProbeEnabled = false;
    require(largeRedo.changed && allocationProbeBytes < 64 * 1024,
            "Redo must apply the retained field value without copying unrelated asset payload");

    AssetDocument longHistory(Json{{"value", 0}});
    for (int value = 1; value <= 300; ++value)
        require(longHistory.set("/value", value).changed,
                "a long sequence of scalar edits must be retained");
    allocationProbeBytes = 0;
    allocationProbeEnabled = true;
    const auto appended = longHistory.set("/value", 301);
    allocationProbeEnabled = false;
    require(appended.changed && allocationProbeBytes < 4096,
            "appending to reserved history must not copy retained edits");
    require(longHistory.undo().changed && longHistory.root()["value"] == 300 &&
                longHistory.set("/value", 999).changed && !longHistory.can_redo() &&
                longHistory.undo().changed && longHistory.root()["value"] == 300 &&
                longHistory.redo().changed && longHistory.root()["value"] == 999,
            "path history must prune redo entries exactly when a new branch is committed");

    AssetDocument nullValue(Json{{"owner", "preserved"}});
    require(nullValue.set("/optional", nullptr).changed &&
                nullValue.root().contains("optional") && nullValue.root()["optional"].is_null() &&
                nullValue.undo().changed && !nullValue.root().contains("optional") &&
                nullValue.redo().changed && nullValue.root()["optional"].is_null(),
            "an explicit null value must remain distinct from a missing property through history");

    AssetDocument retention(Json{{"value", 0}});
    std::size_t successfulEdits = 0;
    bool failed = false;
    failAllocationAtOrAbove = 4096;
    allocationProbeEnabled = true;
    for (std::size_t value = 1; value <= 1024; ++value)
    {
        const auto result = retention.set("/value", value);
        if (!result.changed)
        {
            failed = true;
            break;
        }
        successfulEdits = value;
    }
    allocationProbeEnabled = false;
    const auto tokenBeforeUndo = retention.history_token();
    require(failed && successfulEdits > 0 && retention.root()["value"] == successfulEdits &&
                tokenBeforeUndo == successfulEdits + 1 && retention.can_undo() && !retention.can_redo(),
            "history allocation failure must leave the last committed value, cursor and token intact");
    require(retention.undo().changed && retention.root()["value"] == successfulEdits - 1 &&
                retention.redo().changed && retention.root()["value"] == successfulEdits,
            "history must remain usable after a failed retention allocation");
}

void test_lossless_load_save_and_conflicts()
{
    TemporaryDirectory temporary;
    const auto path = temporary.path() / "walk.animset";
    write_json(path, animation_asset());
    auto loaded = load_asset_document(path, 17);
    require(loaded && loaded.document->generation() == 17 && !loaded.document->dirty() &&
                loaded.document->root() == animation_asset() && loaded.document->source_identity(),
            "asset load must retain source identity and the complete object document");

    require(loaded.document->set("/framesPerSecond", 24.0).changed,
            "loaded asset must accept a path-addressed authoring edit");
    const auto saved = loaded.document->save(path);
    require(saved.succeeded() && !loaded.document->dirty(),
            "saving the same source must update the saved history token after atomic commit");
    auto reread = load_asset_document(path);
    require(reread && reread.document->root()["framesPerSecond"] == 24.0 &&
                reread.document->root()["unknownTopLevel"] == animation_asset()["unknownTopLevel"],
            "save/reopen must retain edited and unrecognized asset fields");

    require(reread.document->set("/loop", false).changed,
            "second loaded draft must be independent of the saved instance");
    write_json(path, Json{{"external", "new version"}});
    const auto conflict = reread.document->save(path);
    require(conflict.status == AssetSaveStatus::Conflict && reread.document->dirty() &&
                Json::parse(std::ifstream(path)) == Json{{"external", "new version"}},
            "a late external replacement must conflict and preserve both disk bytes and dirty work");

    const auto other = temporary.path() / "existing.animset";
    write_json(other, Json{{"owner", "existing"}});
    const auto noOverwrite = reread.document->save(other);
    require(noOverwrite.status == AssetSaveStatus::Conflict,
            "Save As must not overwrite an existing asset without an explicit decision");
    std::string destinationIdentityError;
    const auto observedDestination = native_file_identity(other, destinationIdentityError);
    require(observedDestination && destinationIdentityError.empty(),
            "an explicit Save As overwrite must capture the selected destination identity");
    write_json(other, Json{{"owner", "changed after observation"}});
    const auto staleOverwrite = reread.document->save(
        other, AssetOverwriteObservation{observedDestination});
    require(staleOverwrite.status == AssetSaveStatus::Conflict && reread.document->dirty() &&
                read_json(other)["owner"] == "changed after observation",
            "an overwrite decision must not silently rebase after the observed destination changes");
    const auto currentDestination = native_file_identity(other, destinationIdentityError);
    const auto overwrite = reread.document->save(
        other, AssetOverwriteObservation{currentDestination});
    require(overwrite.succeeded() && !reread.document->dirty() &&
                load_asset_document(other).document->root() == reread.document->root(),
            "explicit Save As overwrite must use an observed identity and commit the authored object");

    const auto sceneArray = temporary.path() / "not-an-asset.json";
    write_json(sceneArray, Json::array({Json{{"id", 1}}}));
    require(!load_asset_document(sceneArray),
            "JSON scene arrays must be rejected instead of entering the asset-document path");
}
} // namespace

int main()
{
    try
    {
        test_path_history_preserves_unknown_fields();
        test_targeted_history_cost_and_retention_failure();
        test_lossless_load_save_and_conflicts();
        std::cout << "asset document tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception &error)
    {
        std::cerr << "asset document tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
