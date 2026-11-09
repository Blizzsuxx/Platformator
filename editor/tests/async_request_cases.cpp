#include "async_requests.h"
#include "filedialogs.h"
#include "test_support.h"

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace platformator_editor;
using namespace platformator_editor_test;

void run_async_request_cases()
{
    const auto sceneFilters = make_file_dialog_filters(AsyncRequestKind::OpenScene);
    const auto saveFilters = make_file_dialog_filters(AsyncRequestKind::SaveScene);
    const auto animationSaveFilters = make_file_dialog_filters(AsyncRequestKind::SaveAnimation);
    const auto textureFilters = make_file_dialog_filters(AsyncRequestKind::OpenAsset, "Texture");
    const auto audioFilters = make_file_dialog_filters(AsyncRequestKind::OpenAsset, "Audio");
    const auto animationFilters = make_file_dialog_filters(AsyncRequestKind::OpenAsset, "AnimationClip");
    const auto executableFilters = make_file_dialog_filters(AsyncRequestKind::OpenExecutable);
    require(sceneFilters.size() == 1 && sceneFilters.front().pattern == "scene" &&
                saveFilters.size() == 1 && saveFilters.front().pattern == "scene",
            "SDL scene filters must use extension patterns instead of unsupported glob syntax");
    require(animationSaveFilters.size() == 1 &&
                animationSaveFilters.front().pattern == "animset",
            "animation Save As must expose the native animation-set extension filter");
    for (const auto &filters : {textureFilters, audioFilters, animationFilters})
        require(filters.size() == 1 && valid_file_dialog_filter_pattern(filters.front().pattern),
                "all asset dialog filters must use SDL's supported semicolon-separated extension syntax");
    require(executableFilters.size() == 1 &&
                valid_file_dialog_filter_pattern(executableFilters.front().pattern),
            "executable browsing must use a valid SDL filter on every platform");
    require(valid_file_dialog_filter_pattern("*") &&
                !valid_file_dialog_filter_pattern("*.scene") &&
                !valid_file_dialog_filter_pattern("scene;;json") &&
                !valid_file_dialog_filter_pattern("scene/path"),
            "SDL filter validation must accept extension lists and reject globs, empty entries and paths");

    {
        AsyncRequestQueue queue(31);
        auto open = queue.begin(AsyncRequestKind::OpenScene, 7);
        auto asset = queue.begin(AsyncRequestKind::OpenAsset, 7);
        auto save = queue.begin(AsyncRequestKind::SaveScene, 7);
        auto saveAnimation = queue.begin(AsyncRequestKind::SaveAnimation, 7);
        auto folder = queue.begin(AsyncRequestKind::SelectFolder, 7);
        auto executable = queue.begin(AsyncRequestKind::OpenExecutable, 7);
        open.complete_selected("scenes/selected.scene");
        asset.complete_selected("assets/sprite.png");
        save.complete_cancelled();
        saveAnimation.complete_selected("assets/walk.animset");
        folder.complete_error("provider unavailable");
        executable.complete_selected("bin/Launch Ω.exe");

        const auto results = queue.take_ready(7);
        require(results.size() == 6, "current async requests must publish one result each");
        const auto selected = std::find_if(results.begin(), results.end(), [](const auto &result) {
            return result.identity.kind == AsyncRequestKind::OpenScene;
        });
        const auto cancelled = std::find_if(results.begin(), results.end(), [](const auto &result) {
            return result.identity.kind == AsyncRequestKind::SaveScene;
        });
        const auto failed = std::find_if(results.begin(), results.end(), [](const auto &result) {
            return result.identity.kind == AsyncRequestKind::SelectFolder;
        });
        const auto selectedAsset = std::find_if(results.begin(), results.end(), [](const auto &result) {
            return result.identity.kind == AsyncRequestKind::OpenAsset;
        });
        const auto selectedExecutable = std::find_if(results.begin(), results.end(), [](const auto &result) {
            return result.identity.kind == AsyncRequestKind::OpenExecutable;
        });
        const auto selectedAnimationSave = std::find_if(results.begin(), results.end(), [](const auto &result) {
            return result.identity.kind == AsyncRequestKind::SaveAnimation;
        });
        require(selected != results.end() && selected->outcome == AsyncRequestOutcome::Selected &&
                    selected->selectedPath == "scenes/selected.scene",
                "selected path results must copy the returned path");
        require(cancelled != results.end() && cancelled->outcome == AsyncRequestOutcome::Cancelled &&
                    cancelled->selectedPath.empty() && cancelled->error.empty(),
                "dialog cancellation must remain distinct from provider failure");
        require(failed != results.end() && failed->outcome == AsyncRequestOutcome::Failed &&
                    failed->selectedPath.empty() && failed->error == "provider unavailable",
                "provider errors must remain distinct from cancellation and selection");
        require(selectedAsset != results.end() && selectedAsset->outcome == AsyncRequestOutcome::Selected &&
                    selectedAsset->selectedPath == "assets/sprite.png",
                "asset dialog results must preserve their independent request identity");
        require(selectedExecutable != results.end() &&
                    selectedExecutable->outcome == AsyncRequestOutcome::Selected &&
                    selectedExecutable->selectedPath == std::filesystem::path(std::u8string(u8"bin/Launch Ω.exe")),
                "executable dialog results must preserve their UTF-8 path and request identity");
        require(selectedAnimationSave != results.end() &&
                    selectedAnimationSave->outcome == AsyncRequestOutcome::Selected &&
                    selectedAnimationSave->selectedPath == "assets/walk.animset",
                "animation Save As results must preserve the selected destination");
        require(selected->identity.sessionGeneration == 31 && selected->identity.documentGeneration == 7,
                "results must retain their originating session and document generations");
    }

    {
        AsyncRequestQueue queue(41);
        auto superseded = queue.begin(AsyncRequestKind::OpenScene, 9);
        auto latest = queue.begin(AsyncRequestKind::OpenScene, 9);
        superseded.complete_selected("old.scene");
        latest.complete_cancelled();
        const auto results = queue.take_ready(9);
        require(results.size() == 1 && results.front().identity.requestId == latest.identity().requestId &&
                    results.front().outcome == AsyncRequestOutcome::Cancelled,
                "completion from a superseded request must not replace the latest request result");

        auto staleDocument = queue.begin(AsyncRequestKind::SaveScene, 9);
        staleDocument.complete_selected("stale.scene");
        require(queue.take_ready(10).empty(),
                "completion from an old document generation must be ignored");
    }

    {
        AsyncRequestQueue queue(45);
        const DocumentIdentity firstTab{ProjectGeneration{45}, TabId{1}, DocumentGeneration{1}};
        const DocumentIdentity secondTab{ProjectGeneration{45}, TabId{2}, DocumentGeneration{1}};
        auto firstRequest = queue.begin(AsyncRequestKind::OpenScene, firstTab);
        auto secondRequest = queue.begin(AsyncRequestKind::OpenScene, secondTab);
        firstRequest.complete_selected("first.scene");
        secondRequest.complete_selected("second.scene");

        const auto secondResults = queue.take_ready(secondTab);
        require(secondResults.size() == 1 && secondResults.front().selectedPath == "second.scene" &&
                    secondResults.front().identity.documentIdentity == secondTab,
                "a tab switch must keep a dialog result bound to its original tab");
        const auto firstResults = queue.take_ready(firstTab);
        require(firstResults.size() == 1 && firstResults.front().selectedPath == "first.scene" &&
                    firstResults.front().identity.documentIdentity == firstTab,
                "an inactive tab's still-live dialog result must remain available to that tab");

        const DocumentIdentity replacedFirst{ProjectGeneration{45}, TabId{1}, DocumentGeneration{2}};
        auto stale = queue.begin(AsyncRequestKind::SaveScene, firstTab);
        auto current = queue.begin(AsyncRequestKind::SaveScene, replacedFirst);
        stale.complete_selected("old.scene");
        current.complete_selected("current.scene");
        const auto currentResults = queue.take_ready(replacedFirst);
        require(currentResults.size() == 1 && currentResults.front().selectedPath == "current.scene",
                "closing or replacing a tab document must reject its stale dialog result");
    }

    {
        AsyncRequestQueue queue(46);
        const DocumentIdentity identity{ProjectGeneration{46}, TabId{3}, DocumentGeneration{1}};
        auto chooser = queue.begin(AsyncRequestKind::SelectFolder, identity);
        chooser.complete_cancelled();
        const auto results = queue.take_ready(identity);
        require(results.size() == 1 &&
                    results.front().identity.documentIdentity == identity &&
                    results.front().outcome == AsyncRequestOutcome::Cancelled &&
                    results.front().selectedPath.empty(),
                "cancelling the project chooser must produce no selected root for the current document");
    }

    {
        AsyncRequestToken late;
        std::weak_ptr<int> documentLifetime;
        {
            auto queue = std::make_unique<AsyncRequestQueue>(51);
            late = queue->begin(AsyncRequestKind::OpenScene, 12);
            auto documentMarker = std::make_shared<int>(1);
            documentLifetime = documentMarker;
            queue.reset();
            documentMarker.reset();
        }
        require(documentLifetime.expired(),
                "pending callback state must not keep a document alive");
        late.complete_selected("late.scene");
        require(late.identity().sessionGeneration == 51,
                "late callback context must retain only its stable request identity");
    }

    {
        AsyncRequestQueue queue(61);
        auto pending = queue.begin(AsyncRequestKind::SelectFolder, 4);
        queue.shutdown();
        pending.complete_error("late provider failure");
        require(queue.take_ready(4).empty(),
                "shutdown must discard late callback results without publishing into dead UI state");
    }

    {
        AsyncRequestQueue queue(71);
        auto callback = queue.begin(AsyncRequestKind::OpenScene, 3);
        std::thread nativeCallback([callback] {
            callback.complete_selected("threaded.scene");
            callback.complete_error("second completion must be ignored");
        });
        nativeCallback.join();
        const auto results = queue.take_ready(3);
        require(results.size() == 1 && results.front().outcome == AsyncRequestOutcome::Selected &&
                    results.front().selectedPath == "threaded.scene",
                "off-thread native completion must publish once for main-thread consumption");
    }

    {
        FileDialogService dialogs(81);
        const auto identity = dialogs.submit_manual_path(AsyncRequestKind::OpenScene, 5,
                                                          "manual/scene with spaces Ω.scene");
        const auto results = dialogs.take_ready(5);
        require(results.size() == 1 && results.front().identity.requestId == identity.requestId &&
                    results.front().outcome == AsyncRequestOutcome::Selected &&
                    results.front().selectedPath == "manual/scene with spaces Ω.scene",
                "provider failures can be recovered through a manually entered Unicode path with spaces");
        dialogs.shutdown();
        const auto late = dialogs.submit_manual_path(AsyncRequestKind::SelectFolder, 5, "ignored-project");
        require(late.requestId == 0 && dialogs.take_ready(5).empty(),
                "a stale folder callback must not publish after the project dialog session is invalidated");
    }
}
