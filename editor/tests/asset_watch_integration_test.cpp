#include "platform/watch.h"
#include "editorapp.h"
#include "preview.h"
#include "projectchanges.h"
#include "projectsession.h"
#include "resourceindex.h"
#include "editorwork.h"
#include "selection.h"
#include "panels/project.h"

#include "test_support.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <platformator/runtime.h>
#include <platformator/runtimeoptions.h>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace platformator_editor
{
struct ProjectSessionTestProbe
{
    static std::size_t watcher_owner_count(const ProjectSession &session)
    {
        return static_cast<std::size_t>(session.watcher_ != nullptr);
    }
};
} // namespace platformator_editor

namespace
{
bool wait_for_event(std::mutex &mutex, std::condition_variable &condition,
                    const std::vector<ProjectChangeEvent> &events,
                    const std::function<bool(const ProjectChangeEvent &)> &predicate,
                    std::chrono::milliseconds timeout = std::chrono::seconds(3))
{
    std::unique_lock lock(mutex);
    return condition.wait_for(lock, timeout, [&] {
        return std::any_of(events.begin(), events.end(), predicate);
    });
}

std::string recent_watch_events(std::mutex &mutex,
                                const std::vector<ProjectChangeEvent> &events,
                                std::size_t maximum = 12)
{
    std::lock_guard lock(mutex);
    std::ostringstream result;
    const std::size_t first = events.size() > maximum ? events.size() - maximum : 0;
    for (std::size_t index = first; index < events.size(); ++index)
    {
        if (index != first)
            result << "; ";
        const auto &event = events[index];
        result << "kind=" << static_cast<int>(event.kind)
               << " path=" << event.path.generic_string()
               << " old=" << event.oldPath.generic_string()
               << " dir=" << event.directory;
    }
    return result.str();
}

void write_solid_png(const std::filesystem::path &path, std::uint8_t red,
                     std::uint8_t green, std::uint8_t blue)
{
    std::filesystem::create_directories(path.parent_path());
    SDL_Surface *surface = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
    require(surface != nullptr, std::string("create watcher texture fixture: ") + SDL_GetError());
    const bool filled = SDL_FillSurfaceRect(surface, nullptr,
        SDL_MapSurfaceRGBA(surface, red, green, blue, 255));
    const bool saved = filled && IMG_SavePNG(surface, path.string().c_str());
    SDL_DestroySurface(surface);
    require(saved, std::string("write watcher texture fixture: ") + SDL_GetError());
}

std::array<std::uint8_t, 4> read_preview_pixel(SDL_Renderer *renderer, SDL_Texture *target,
                                              int x, int y)
{
    require(SDL_SetRenderTarget(renderer, target),
            std::string("set watcher preview target: ") + SDL_GetError());
    SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
    require(surface != nullptr, std::string("read watcher preview target: ") + SDL_GetError());
    if (surface->format != SDL_PIXELFORMAT_RGBA32)
    {
        SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);
        require(converted != nullptr,
                std::string("convert watcher preview pixels: ") + SDL_GetError());
        surface = converted;
    }
    const auto *pixel = static_cast<const std::uint8_t *>(surface->pixels) +
        static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->pitch) +
        static_cast<std::size_t>(x) * 4U;
    const std::array<std::uint8_t, 4> result{pixel[0], pixel[1], pixel[2], pixel[3]};
    SDL_DestroySurface(surface);
    return result;
}

void wait_for_preview_assets(EditorPreview &preview, const DocumentSession &document)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline)
    {
        preview.poll_asset_reloads(document);
        if (!preview.asset_preparation_pending())
        {
            preview.poll_asset_reloads(document);
            if (!preview.asset_preparation_pending())
                return;
        }
        SDL_Delay(1);
    }
    throw std::runtime_error("scene asset preparation did not settle within three seconds");
}

void test_native_change_reaches_project_listing_and_scene_preview()
{
    TestEnvironment environment;
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    const auto cachedFolder = assets / "cached";
    std::filesystem::create_directories(cachedFolder);
    std::ofstream(project / "CMakeLists.txt") << "project(watcher-preview-test)\n";
    const auto texturePath = cachedFolder / "texture.png";
    const auto unrelatedPath = cachedFolder / "unrelated.png";
    write_solid_png(texturePath, 230, 8, 8);
    write_solid_png(unrelatedPath, 8, 8, 230);

    RuntimeOptions options;
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    options.assetRoot = assets.string();
    platformator::Runtime runtime(options);
    SDL_Renderer *renderer = runtime.getRenderer();
    require(renderer != nullptr, "watcher preview integration needs an SDL renderer");
    ProjectSession session(project, assets);
    require(session.source_notifications_available(),
            "the watcher preview integration requires native source monitoring");
    require(ProjectSessionTestProbe::watcher_owner_count(session) == 1,
            "nested source and asset roots must share one native watcher owner");

    DocumentSession document = make_default_document(project, assets, session.generation().value);
    document.root = Json::array({object(10, "Watched sprite", Json::array({component(11, 6,
        Json{{"textureFilePath", "assets/cached/texture.png"}, {"width", 64.0},
             {"height", 64.0}, {"sourceRectEnabled", false}})}))});
    document.root[0]["position"] = Json{{"x", 160.0}, {"y", 120.0}};
    ++document.structureRevision;
    rebuild_document_indexes(document);
    SelectionModel selection;
    selection.set(document, Selection{10, std::nullopt, std::nullopt, FocusedPanel::Hierarchy});
    const Selection originalSelection = selection.current();
    const Json authoredBefore = document.root;
    const auto valueRevisionBefore = document.valueRevision;
    const auto structureRevisionBefore = document.structureRevision;

    auto &resources = session.resource_index();
    resources.register_consumer(unrelatedPath, ResourceKind::Texture,
        {DocumentIdentity{session.generation(), document.tabId,
                          DocumentGeneration{document.documentGeneration}}, 99, 100, "unrelated-test"});
    const auto unrelatedVersionBefore = resources.resolve(unrelatedPath, ResourceKind::Texture).logicalVersion;
    EditorPreview preview(runtime);
    preview.set_resource_index(&resources);
    preview.reconcile(document, true);
    wait_for_preview_assets(preview, document);
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "the initial watched Scene must render");
    const auto initialPixel = read_preview_pixel(renderer, preview.target(), 80, 60);
    require(initialPixel[0] > initialPixel[1] * 4 && initialPixel[0] > initialPixel[2] * 4,
            "the initial Scene should show the original red resource version");

    auto &changes = session.project_changes();
    ProjectScanService scans(4);
    ProjectPanel projectPanel(session.project_context(), changes, scans, renderer);
    const auto event_mentions = [](const ProjectChangeEvent &event, const std::filesystem::path &path) {
        return event.path == path || event.oldPath == path ||
               std::find(event.affectedPaths.begin(), event.affectedPaths.end(), path) !=
                   event.affectedPaths.end();
    };
    const auto dispatch_native_change = [&](const std::function<bool(const ProjectChangeEvent &)> &matches) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        ProjectChangeBatch batch;
        bool found = false;
        while (std::chrono::steady_clock::now() < deadline)
        {
            batch = session.take_change_batch();
            found = std::any_of(batch.events.begin(), batch.events.end(), matches);
            if (found)
                break;
            SDL_Delay(5);
        }
        require(found, "a native Project change must settle within three seconds");
        const auto commit_frame = [&](const ProjectChangeBatch &frameBatch) {
            std::size_t callbacks = 0;
            const auto committed = projectPanel.apply_changes(frameBatch,
                [&](const ProjectChangeEvent &event, const std::filesystem::path &path) {
                    ++callbacks;
                    apply_project_resource_change(resources, preview, document, path, event.directory);
                });
            require(committed == callbacks && committed <= ProjectPanel::MaxOwnerChangePathsPerFrame,
                    "each owner frame must account for its bounded native Project path commits");
            return committed;
        };
        work::reset();
        const auto firstFrameCommits = commit_frame(batch);
        const std::size_t continuationLimit = projectPanel.pending_change_paths();
        ProjectChangeBatch continuation = batch;
        continuation.events.clear();
        for (std::size_t frame = 0;
             frame < continuationLimit && projectPanel.pending_change_paths() != 0; ++frame)
            require(commit_frame(continuation) > 0,
                    "deferred native Project paths must make progress on each owner frame");
        require(firstFrameCommits > 0 && projectPanel.pending_change_paths() == 0,
                "native Project paths must drain through the bounded application owner queue");
        wait_for_preview_assets(preview, document);
        return work::snapshot();
    };
    const auto listing = changes.begin_directory_listing(cachedFolder);
    require(listing.requestId != 0 && changes.complete_directory_listing(listing, true),
            "the cached Project folder listing must start from a complete snapshot");
    const auto pendingListing = changes.begin_directory_listing(cachedFolder);
    require(changes.directory_listing_current(pendingListing),
            "a Project refresh token must be current before the native file event");

    const auto priorTime = std::filesystem::last_write_time(texturePath);
    const auto temporaryReplacement = cachedFolder / "texture.replacement.png";
    write_solid_png(temporaryReplacement, 8, 230, 8);
    std::filesystem::last_write_time(temporaryReplacement, priorTime);
    std::filesystem::rename(temporaryReplacement, texturePath);
    const auto eventDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    ProjectChangeBatch changed;
    while (std::chrono::steady_clock::now() < eventDeadline)
    {
        changed = session.take_change_batch();
        if (std::any_of(changed.events.begin(), changed.events.end(), [&](const auto &event) {
                return event.path == texturePath && event.kind != ProjectChangeKind::Removed;
            }))
            break;
        SDL_Delay(5);
    }
    std::vector<std::filesystem::path> changedPaths;
    for (const auto &event : changed.events)
    {
        if (!event.affectedPaths.empty())
            changedPaths.insert(changedPaths.end(), event.affectedPaths.begin(), event.affectedPaths.end());
        else
        {
            changedPaths.push_back(event.path);
            if (!event.oldPath.empty())
                changedPaths.push_back(event.oldPath);
        }
    }
    require(std::find(changedPaths.begin(), changedPaths.end(), texturePath) != changedPaths.end(),
            "the native watcher must report an atomic same-metadata replacement in the cached Project folder");
    std::size_t ownerPathCommits = 0;
    work::reset();
    const auto firstFrameCommits = projectPanel.apply_changes(changed,
        [&](const ProjectChangeEvent &event, const std::filesystem::path &path) {
            ++ownerPathCommits;
            apply_project_resource_change(resources, preview, document, path, event.directory);
        });
    const std::size_t remainingFrameBudget = projectPanel.pending_change_paths();
    ProjectChangeBatch continuation;
    continuation.projectGeneration = changed.projectGeneration;
    for (std::size_t frame = 0;
         frame < remainingFrameBudget && projectPanel.pending_change_paths() != 0;
         ++frame)
    {
        const auto processed = projectPanel.apply_changes(continuation,
            [&](const ProjectChangeEvent &event, const std::filesystem::path &path) {
                ++ownerPathCommits;
                apply_project_resource_change(resources, preview, document, path, event.directory);
            });
        require(processed > 0, "deferred native watcher paths must make progress on each owner frame");
    }
    require(firstFrameCommits > 0 && ownerPathCommits >= firstFrameCommits &&
                projectPanel.pending_change_paths() == 0,
            "the application Project frame must commit the event paths through the bounded owner queue");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    require(work::snapshot()[static_cast<std::size_t>(work::Counter::OwnerAssetChangePaths)] == ownerPathCommits,
            "the enabled owner work counter must match Scene resource paths committed from the native event");
#endif
    require(!changes.directory_listing_current(pendingListing),
            "the native event must invalidate the cached Project folder's in-flight listing revision");
    wait_for_preview_assets(preview, document);
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "the Scene must render after watcher-driven owner-thread resource adoption");
    const auto replacedPixel = read_preview_pixel(renderer, preview.target(), 80, 60);
    require(replacedPixel[1] > replacedPixel[0] * 4 && replacedPixel[1] > replacedPixel[2] * 4,
            "the Scene must show replacement pixels without a manual refresh or later child write");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    require(work::snapshot()[static_cast<std::size_t>(work::Counter::TextureUploads)] == 1,
            "one referenced replacement must produce one prepared upload without unrelated texture reloads");
#endif
    require(document.root == authoredBefore && document.valueRevision == valueRevisionBefore &&
                document.structureRevision == structureRevisionBefore &&
                selection.current() == originalSelection &&
                resources.resolve(unrelatedPath, ResourceKind::Texture).logicalVersion == unrelatedVersionBefore,
            "watcher-driven preview adoption must preserve authored history inputs, selection, and unrelated resources");

    const auto unusedPath = cachedFolder / "unused-added.png";
    write_solid_png(unusedPath, 210, 180, 20);
    const auto unusedWork = dispatch_native_change([&](const ProjectChangeEvent &event) {
        return event_mentions(event, unusedPath);
    });
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    require(unusedWork[static_cast<std::size_t>(work::Counter::TextureUploads)] == 0,
            "an unrelated Project addition must not reload the referenced Scene texture");
#else
    (void)unusedWork;
#endif

    const auto completeImagePath = temporary.path() / "complete-yellow.png";
    write_solid_png(completeImagePath, 230, 220, 8);
    std::ifstream completeImageInput(completeImagePath, std::ios::binary);
    const std::vector<char> completeImage((std::istreambuf_iterator<char>(completeImageInput)),
                                          std::istreambuf_iterator<char>());
    require(completeImage.size() > 16, "partial-write fixture must contain a complete PNG");
    {
        std::ofstream partial(texturePath, std::ios::binary | std::ios::trunc);
        partial.write(completeImage.data(), static_cast<std::streamsize>(completeImage.size() / 2));
        require(static_cast<bool>(partial), "the partial Scene texture write must reach disk");
    }
    SDL_Delay(40);
    {
        std::ofstream remainder(texturePath, std::ios::binary | std::ios::app);
        remainder.write(completeImage.data() + completeImage.size() / 2,
                        static_cast<std::streamsize>(completeImage.size() - completeImage.size() / 2));
        require(static_cast<bool>(remainder), "the partial Scene texture write must complete");
    }
    std::filesystem::last_write_time(texturePath, priorTime);
    const auto partialWork = dispatch_native_change([&](const ProjectChangeEvent &event) {
        return event_mentions(event, texturePath) && event.kind != ProjectChangeKind::Removed;
    });
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "the Scene must render after a coalesced partial write completes");
    const auto partialPixel = read_preview_pixel(renderer, preview.target(), 80, 60);
    require(partialPixel[0] > partialPixel[2] * 4 && partialPixel[1] > partialPixel[2] * 4,
            "a same-metadata partial write must adopt the complete yellow resource version");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    require(partialWork[static_cast<std::size_t>(work::Counter::TextureUploads)] == 1,
            "one same-metadata referenced edit must produce exactly one prepared texture upload");
#else
    (void)partialWork;
#endif

    require(std::filesystem::remove(texturePath), "the referenced Scene texture must delete");
    const auto deletedWork = dispatch_native_change([&](const ProjectChangeEvent &event) {
        return event_mentions(event, texturePath) && event.kind == ProjectChangeKind::Removed;
    });
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "deleting the referenced file must preserve the last-good Scene presentation");
    const auto lastGoodPixel = read_preview_pixel(renderer, preview.target(), 80, 60);
    require(lastGoodPixel == partialPixel && resources.resolve("assets/cached/texture.png").status ==
                ResourceStatus::Missing,
            "deletion must expose a missing reference while retaining the last-good preview pixels");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    require(deletedWork[static_cast<std::size_t>(work::Counter::TextureUploads)] == 0,
            "deletion failure must not upload an unrelated replacement texture");
#else
    (void)deletedWork;
#endif

    write_solid_png(texturePath, 8, 8, 240);
    work::reset();
    require(preview.retry_failed_assets(document) == 1,
            "Refresh Assets must retry the exhausted referenced texture without waiting for another file event");
    wait_for_preview_assets(preview, document);
    const auto repairedWork = work::snapshot();
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "recreating a missing referenced texture must refresh the Scene preview");
    const auto repairedPixel = read_preview_pixel(renderer, preview.target(), 80, 60);
    require(repairedPixel[2] > repairedPixel[0] * 4 && repairedPixel[2] > repairedPixel[1] * 4,
            "the Scene must show repaired resource pixels without changing the authored reference");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    require(repairedWork[static_cast<std::size_t>(work::Counter::TextureUploads)] == 1,
            "explicitly retrying one repaired texture must produce one prepared upload");
#else
    (void)repairedWork;
#endif
    require(resources.resolve(unrelatedPath, ResourceKind::Texture).logicalVersion == unrelatedVersionBefore,
            "explicitly retrying the repaired texture must leave unrelated Scene resource versions unchanged");
    require(document.root == authoredBefore && document.valueRevision == valueRevisionBefore &&
                document.structureRevision == structureRevisionBefore && selection.current() == originalSelection,
            "add, edit, delete and repair notifications must preserve Scene history and valid selection");
    preview.release();
}

void test_real_tree_events_and_root_replacement()
{
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "project" / "assets";
    const auto outside = temporary.path() / "outside";
    std::filesystem::create_directories(root / "cached");
    std::filesystem::create_directories(outside);
    std::ofstream(root / "cached" / "initial.png") << "initial";
    std::ofstream(outside / "external.png") << "outside";

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<ProjectChangeEvent> events;
    auto watcher = make_project_watch_service();
    std::string error;
    require(static_cast<bool>(watcher) && watcher->supported(), "the host must provide a native watcher");
    require(watcher->start(ProjectGeneration{18}, root,
        [&](ProjectChangeEvent event) {
            {
                std::lock_guard lock(mutex);
                events.push_back(std::move(event));
            }
            condition.notify_all();
        }, error), "native watch must start after registering existing directories: " + error);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::RootAvailable && event.path == root;
            }), "background watch discovery must publish readiness before callers rely on native coverage");

    const auto inserted = root / "cached" / "added.png";
    std::ofstream(inserted) << "first";
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.projectGeneration == ProjectGeneration{18} && event.path == inserted &&
                       (event.kind == ProjectChangeKind::Added || event.kind == ProjectChangeKind::Modified);
            }), "a cached-folder addition must arrive through native notifications within three seconds");

    const auto clearEvents = [&] {
        std::lock_guard lock(mutex);
        events.clear();
    };
    const auto insertedTime = std::filesystem::last_write_time(inserted);
    clearEvents();
    std::ofstream(inserted, std::ios::trunc) << "other";
    std::filesystem::last_write_time(inserted, insertedTime);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.path == inserted && event.kind == ProjectChangeKind::Modified;
            }), "same-size writes with restored timestamps must still arrive through native notifications");

    clearEvents();
    {
        std::ofstream partial(inserted, std::ios::app);
        partial << "partial";
        partial.flush();
        require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                    return event.path == inserted && event.kind == ProjectChangeKind::Modified;
                }), "an open partial write must produce an early content notification");
        clearEvents();
        partial << "-complete";
        partial.flush();
    }
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.path == inserted && event.kind == ProjectChangeKind::Modified;
            }), "completion of a partial write must produce a later content notification");

    const auto replacement = root / "cached" / "renamed.png";
    std::filesystem::rename(inserted, replacement);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::Renamed && event.path == replacement &&
                       event.oldPath == inserted;
            }), "paired native rename halves must preserve the old and new names");

#if defined(__linux__)
    const auto atomicReplacement = root / "cached" / "replacement.tmp";
    std::ofstream(atomicReplacement) << "atomic replacement";
    clearEvents();
    std::filesystem::rename(atomicReplacement, replacement);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.path == replacement && event.kind != ProjectChangeKind::Removed;
            }), "atomic replacement must invalidate the cached destination path");
#endif
    clearEvents();
    std::filesystem::remove(replacement);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.path == replacement && event.kind == ProjectChangeKind::Removed;
            }), "deleting a cached asset must reach the project watcher");

    const auto incoming = temporary.path() / "project" / "incoming";
    std::filesystem::create_directories(incoming / "nested");
    std::ofstream(incoming / "nested" / "seed.png") << "seed";
    const auto movedTree = root / "moved-tree";
    std::filesystem::rename(incoming, movedTree);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::Added && event.path == movedTree && event.directory;
            }), "moving a populated directory into the tree must register its subtree");
    const auto movedChild = movedTree / "nested" / "after-move.png";
    std::ofstream(movedChild) << "after move";
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.path == movedChild && event.kind == ProjectChangeKind::Added;
            }), "new child events inside a moved-in populated directory must be observed");

    const auto aliasTarget = root / "cached" / "alias-target";
    std::filesystem::create_directories(aliasTarget);
    std::ofstream(aliasTarget / "seed.png") << "seed";
    std::error_code containedAliasError;
    std::filesystem::create_directory_symlink(aliasTarget, root / "cached" / "alias-folder",
                                               containedAliasError);
    if (!containedAliasError)
    {
        const auto aliasEntry = wait_for_event(mutex, condition, events,
            [&](const ProjectChangeEvent &event) { return event.path == root / "cached" / "alias-folder"; });
        require(aliasEntry, "a contained cached-folder alias must remain visible to the project watcher");
        const auto aliasTargetChange = aliasTarget / "after-alias.png";
        std::ofstream(aliasTargetChange) << "changed through an alias target";
        require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                    return event.path == aliasTargetChange && event.kind == ProjectChangeKind::Added;
                }), "canonical subtree changes beneath a contained folder alias must remain monitored");
    }

    const auto internalTree = root / "cached" / "internal-tree";
    std::filesystem::create_directories(internalTree / "nested");
    std::ofstream(internalTree / "nested" / "seed.png") << "seed";
    const auto observedTree = wait_for_event(mutex, condition, events,
        [&](const ProjectChangeEvent &event) {
            return event.path == internalTree && event.kind == ProjectChangeKind::Added;
        });
    require(observedTree, "new nested directories must be installed before their contents are enumerated");
    const auto watchedDirectoriesBeforeRename = watcher->watched_directories();
    const auto renamedTree = root / "cached" / "renamed-tree";
    std::filesystem::rename(internalTree, renamedTree);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::Renamed && event.path == renamedTree &&
                       event.oldPath == internalTree;
            }), "in-tree directory renames must preserve their old and new paths");
    require(watcher->watched_directories() == watchedDirectoriesBeforeRename,
            "renaming an in-tree directory must keep the same set of active subtree watches");
    const auto renamedChild = renamedTree / "nested" / "after-rename.png";
    std::ofstream(renamedChild) << "after rename";
    const bool renamedChildObserved = wait_for_event(mutex, condition, events,
        [&](const ProjectChangeEvent &event) {
            return event.path == renamedChild && event.kind == ProjectChangeKind::Added;
        });
    if (!renamedChildObserved)
    {
        throw std::runtime_error("watch paths must follow a renamed directory subtree; watched=" +
            std::to_string(watcher->watched_directories()) + " before-rename=" +
            std::to_string(watchedDirectoriesBeforeRename) + "; recent events: " +
            recent_watch_events(mutex, events));
    }

    std::error_code linkError;
    std::filesystem::create_directory_symlink(outside, root / "escape", linkError);
    if (!linkError)
    {
        const auto symlinkEvent = wait_for_event(mutex, condition, events,
            [&](const ProjectChangeEvent &event) { return event.path == root / "escape"; });
        require(symlinkEvent, "a symlink entry itself must remain discoverable");
        std::ofstream(outside / "changed.png") << "outside changed";
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::lock_guard lock(mutex);
        require(std::none_of(events.begin(), events.end(), [&](const ProjectChangeEvent &event) {
                    return event.path == root / "escape" / "changed.png";
                }), "recursive watching must not follow an escaping directory symlink");
    }

    const auto movedRoot = temporary.path() / "project" / "assets-old";
    std::filesystem::rename(root, movedRoot);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::RootUnavailable && event.path == root;
            }), "root replacement must emit explicit unavailable coverage");
    std::filesystem::create_directories(root);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::RootAvailable && event.path == root;
            }), "a recreated project root must explicitly restore native notification coverage");
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::Overflow && event.path == root;
            }), "recreated roots must regain monitoring and request reconciliation");
    const auto afterReplacement = root / "after-replacement.png";
    std::ofstream(afterReplacement) << "replacement";
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.path == afterReplacement && event.kind == ProjectChangeKind::Added;
            }), "the replacement root must report ordinary child changes after rearming");
    watcher->stop();
    require(watcher->watched_directories() == 0,
            "stopping a watcher must join its worker and release every native watch");
}

void test_linux_watcher_recovers_when_started_with_missing_root()
{
#if defined(__linux__)
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto root = project / "assets";
    std::filesystem::create_directories(project);

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<ProjectChangeEvent> events;
    auto watcher = make_project_watch_service();
    std::string error;
    require(watcher->start(ProjectGeneration{19}, root,
                [&](ProjectChangeEvent event) {
                    {
                        std::lock_guard lock(mutex);
                        events.push_back(std::move(event));
                    }
                    condition.notify_all();
                }, error),
            "a missing Linux root can retain a parent watch for later recovery: " + error);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::RootUnavailable && event.path == root;
            }), "startup reports the absent root while keeping its parent notification active");
    require(watcher->watched_directories() == 2,
            "the missing root retains parent and recovery-ancestor watches until recreation");

    std::filesystem::create_directories(root);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::RootAvailable && event.path == root;
            }), "a missing root is recursively watched after its parent reports recreation");
    const auto added = root / "after-recovery.png";
    std::ofstream(added) << "recovered";
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.path == added && event.kind == ProjectChangeKind::Added;
            }), "the recovered root reports ordinary child additions");
    watcher->stop();
    require(watcher->watched_directories() == 0,
            "stopping the recovered watcher releases parent and root watches");
#endif
}

void test_linux_watcher_recovers_when_root_parent_is_replaced()
{
#if defined(__linux__)
    TemporaryDirectory temporary;
    const auto parent = temporary.path() / "project";
    const auto root = parent / "assets";
    const auto movedParent = temporary.path() / "project-old";
    std::filesystem::create_directories(root);

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<ProjectChangeEvent> events;
    auto watcher = make_project_watch_service();
    std::string error;
    require(watcher->start(ProjectGeneration{21}, root,
                [&](ProjectChangeEvent event) {
                    {
                        std::lock_guard lock(mutex);
                        events.push_back(std::move(event));
                    }
                    condition.notify_all();
                }, error),
            "a root whose parent may move must start with a recovery-ancestor watch: " + error);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::RootAvailable && event.path == root;
            }), "the root must be covered before replacing its parent");
    const auto clearEvents = [&] {
        std::lock_guard lock(mutex);
        events.clear();
    };
    clearEvents();

    std::filesystem::rename(parent, movedParent);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::RootUnavailable && event.path == root;
            }), "moving away the immediate parent must publish root unavailability");

    clearEvents();
    std::filesystem::create_directories(root);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::RootAvailable && event.path == root;
            }), "recreating the immediate parent and root must restore native coverage");
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::Overflow && event.path == root;
            }), "recovered coverage must request a complete inventory reconciliation");

    const auto added = root / "after-parent-recovery.png";
    std::ofstream(added) << "recovered parent";
    const bool recoveredChildObserved = wait_for_event(mutex, condition, events,
        [&](const ProjectChangeEvent &event) {
            return event.path == added && (event.kind == ProjectChangeKind::Added ||
                                             event.kind == ProjectChangeKind::Modified);
        });
    if (!recoveredChildObserved)
        throw std::runtime_error("the recovered root must report later child additions; watched=" +
            std::to_string(watcher->watched_directories()) + "; recent events: " +
            recent_watch_events(mutex, events));
    watcher->stop();
    require(watcher->watched_directories() == 0,
            "stopping a parent-recovered watcher must release every directory reservation");
#endif
}

void test_linux_watcher_recovers_when_started_before_root_parent_exists()
{
#if defined(__linux__)
    TemporaryDirectory temporary;
    const auto parent = temporary.path() / "late-project";
    const auto root = parent / "assets";
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<ProjectChangeEvent> events;
    auto watcher = make_project_watch_service();
    std::string error;
    require(watcher->start(ProjectGeneration{22}, root,
                [&](ProjectChangeEvent event) {
                    {
                        std::lock_guard lock(mutex);
                        events.push_back(std::move(event));
                    }
                    condition.notify_all();
                }, error),
            "a missing immediate root parent can retain its nearest existing ancestor watch: " + error);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::RootUnavailable && event.path == root;
            }), "startup must report an absent root while retaining ancestor recovery coverage");
    const auto clearEvents = [&] {
        std::lock_guard lock(mutex);
        events.clear();
    };

    clearEvents();
    std::filesystem::create_directories(parent);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::RootUnavailable && event.path == root;
            }), "creating the immediate parent must arm its watch while the root remains absent");

    clearEvents();
    std::filesystem::create_directories(root);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::RootAvailable && event.path == root;
            }), "creating the root beneath a newly armed parent must restore coverage");
    const auto added = root / "after-late-parent.png";
    std::ofstream(added) << "recovered late parent";
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.path == added && (event.kind == ProjectChangeKind::Added ||
                                                event.kind == ProjectChangeKind::Modified);
            }), "the root created after its parent must report later child changes");
    watcher->stop();
    require(watcher->watched_directories() == 0,
            "stopping a late-parent watcher must release every directory reservation");
#endif
}

void test_nested_root_parent_does_not_hide_project_root_changes()
{
#if defined(__linux__)
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    const auto manifest = project / "CMakeLists.txt";
    std::ofstream(manifest) << "project(watcher-root-events)\n";

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<ProjectChangeEvent> events;
    auto watcher = make_project_watch_service();
    std::string error;
    require(watcher->start(ProjectGeneration{20}, project,
                [&](ProjectChangeEvent event) {
                    {
                        std::lock_guard lock(mutex);
                        events.push_back(std::move(event));
                    }
                    condition.notify_all();
                }, error, ProjectWatchOptions{.sourceInputsOnly = true,
                    .includedSubtrees = {assets}, .additionalRoots = {assets}}),
            "the project and nested asset roots share one Linux inotify queue: " + error);
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.kind == ProjectChangeKind::RootAvailable && event.path == project;
            }), "the project root is watched before changing its direct children");

    {
        std::ofstream changedManifest(manifest, std::ios::app);
        changedManifest << "# direct project-root edit\n";
    }
    require(wait_for_event(mutex, condition, events, [&](const ProjectChangeEvent &event) {
                return event.path == manifest && event.kind == ProjectChangeKind::Modified;
            }), "a nested root's parent watch must not consume direct project-root edits");
    watcher->stop();
#endif
}

void test_resource_aliases_versions_and_unresolved_paths()
{
    TemporaryDirectory temporary;
    const auto assets = temporary.path() / "assets";
    const auto outside = temporary.path() / "outside";
    std::filesystem::create_directories(assets);
    std::filesystem::create_directories(outside);
    const auto target = assets / "target.png";
    std::ofstream(target) << "target";
    std::ofstream(outside / "secret.png") << "outside";
    std::filesystem::create_symlink("target.png", assets / "alias-a.png");
    std::filesystem::create_symlink("target.png", assets / "alias-b.png");
    std::filesystem::create_symlink("missing.png", assets / "broken.png");
    std::filesystem::create_symlink("cycle-b.png", assets / "cycle-a.png");
    std::filesystem::create_symlink("cycle-a.png", assets / "cycle-b.png");
    std::filesystem::create_symlink(outside / "secret.png", assets / "escape.png");

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{23}, assets);
    const auto direct = resources.resolve("target.png", ResourceKind::Texture);
    const auto aliasA = resources.resolve("alias-a.png", ResourceKind::Texture);
    const auto aliasB = resources.resolve("alias-b.png", ResourceKind::Texture);
    require(direct.status == ResourceStatus::Resolved && aliasA.status == ResourceStatus::Resolved &&
                aliasB.status == ResourceStatus::Resolved && direct.canonicalPath == aliasA.canonicalPath &&
                direct.canonicalPath == aliasB.canonicalPath,
            "contained symlink aliases must resolve to one canonical asset version");
    require(resources.resolve("broken.png").status == ResourceStatus::BrokenSymlink &&
                resources.resolve("escape.png").status == ResourceStatus::OutsideRoot &&
                resources.resolve("cycle-a.png").status == ResourceStatus::SymlinkCycle,
            "broken, escaping and cyclic links must remain visible but unresolved");

    const DocumentIdentity firstDocument{ProjectGeneration{23}, TabId{4}, DocumentGeneration{8}};
    const DocumentIdentity secondDocument{ProjectGeneration{23}, TabId{5}, DocumentGeneration{8}};
    resources.register_consumer("target.png", ResourceKind::Texture,
                                {firstDocument, 10, 11, "sprite"});
    resources.register_consumer("alias-a.png", ResourceKind::Texture,
                                {firstDocument, 12, 13, "sprite"});
    resources.register_consumer("alias-b.png", ResourceKind::Texture,
                                {secondDocument, 20, 21, "thumbnail"});
    const ResourceConsumer enginePathConsumer{secondDocument, 21, 22, "sprite"};
    resources.register_consumer("assets/target.png", ResourceKind::Texture, enginePathConsumer);
    require(resources.resolve("assets/target.png").canonicalPath == direct.canonicalPath &&
                resources.affected_consumers(target).size() == 4,
            "canonical invalidation must fan out to every logical alias consumer");
    resources.register_consumer("nested/child.png", ResourceKind::Texture,
                                {firstDocument, 30, 31, "nested"});
    require(resources.affected_consumers(assets / "nested", true).size() == 1,
            "directory uncertainty must reveal consumers below the affected subtree");

    const auto canonicalTree = assets / "canonical-tree";
    const auto aliasTree = assets / "alias-tree";
    std::filesystem::create_directories(canonicalTree);
    std::ofstream(canonicalTree / "child.png") << "child";
    std::error_code directoryAliasError;
    std::filesystem::create_directory_symlink(canonicalTree, aliasTree, directoryAliasError);
    if (!directoryAliasError)
    {
        const auto aliasChild = aliasTree / "child.png";
        const ResourceConsumer aliasChildConsumer{firstDocument, 40, 41, "nested-alias"};
        resources.register_consumer(aliasChild, ResourceKind::Texture, aliasChildConsumer);
        const auto aliasChildBefore = resources.resolve(aliasChild, ResourceKind::Texture);
        require(aliasChildBefore.status == ResourceStatus::Resolved &&
                    resources.affected_consumers(canonicalTree, true).size() == 1,
                "a cached child reached through a directory alias must join its canonical subtree");

        const auto movedCanonicalTree = assets / "moved-canonical-tree";
        std::filesystem::rename(canonicalTree, movedCanonicalTree);
        const auto removedTargetConsumers = resources.affected_consumers(canonicalTree, true);
        require(std::find(removedTargetConsumers.begin(), removedTargetConsumers.end(), aliasChildConsumer) !=
                    removedTargetConsumers.end(),
                "canonical parent removal must still fan out through cached child aliases after the path disappears");
        resources.invalidate_subtree(canonicalTree, resources.uncertainty_epoch() + 1);
        const auto brokenChild = resources.resolve(aliasChild, ResourceKind::Texture);
        require(brokenChild.status == ResourceStatus::BrokenSymlink &&
                    brokenChild.logicalVersion > aliasChildBefore.logicalVersion,
                "canonical subtree uncertainty must invalidate the aliased child even while its target is missing");

        std::filesystem::create_directories(canonicalTree);
        std::ofstream(canonicalTree / "child.png") << "recreated";
        resources.invalidate_subtree(canonicalTree, resources.uncertainty_epoch() + 1);
        const auto repairedChild = resources.resolve(aliasChild, ResourceKind::Texture);
        require(repairedChild.status == ResourceStatus::Resolved &&
                    repairedChild.canonicalPath == std::filesystem::canonical(canonicalTree / "child.png"),
                "recreating a contained canonical parent must repair its cached logical child alias");

        std::filesystem::remove(aliasTree);
        const auto removedAliasConsumers = resources.affected_consumers(aliasTree, true);
        require(std::find(removedAliasConsumers.begin(), removedAliasConsumers.end(), aliasChildConsumer) !=
                    removedAliasConsumers.end(),
                "removing a cached alias folder must invalidate every logical descendant consumer");
        resources.invalidate_subtree(aliasTree, resources.uncertainty_epoch() + 1);
        std::filesystem::create_directory_symlink(canonicalTree, aliasTree);
        require(resources.resolve(aliasChild, ResourceKind::Texture).status == ResourceStatus::Resolved,
                "recreating the contained alias must restore its cached child resource");
    }
    resources.unregister_consumer("assets/target.png", enginePathConsumer);
    const auto priorLogicalVersion = resources.resolve("target.png").logicalVersion;
    resources.invalidate(target);
    const auto changed = resources.resolve("target.png");
    const auto changedAlias = resources.resolve("alias-a.png");
    require(changed.logicalVersion > priorLogicalVersion &&
                changed.canonicalVersion > direct.canonicalVersion &&
                changedAlias.canonicalVersion == changed.canonicalVersion,
            "a notification must advance logical and canonical versions even if file metadata is unchanged");

    const auto staleRequest = resources.begin_request("target.png");
    const auto currentRequest = resources.begin_request("target.png");
    require(!resources.accepts(staleRequest) && resources.accepts(currentRequest),
            "an explicit refresh must supersede earlier prepared work");
    require(resources.join_request("target.png") == currentRequest,
            "concurrent dependency demand for an active content version must share its token");
    require(resources.set_preparation_state(currentRequest, ResourcePreparationState::Preparing) &&
                resources.set_preparation_state(currentRequest,
                                                ResourcePreparationState::AwaitingOwnerCommit) &&
                resources.set_preparation_state(currentRequest, ResourcePreparationState::Ready) &&
                resources.is_ready(currentRequest) && resources.join_request("target.png") == currentRequest,
            "a ready content version must retain its shared request token for later consumers");
    resources.invalidate(target);
    require(!resources.accepts(currentRequest) &&
                resources.resolve("target.png").preparation == ResourcePreparationState::Dirty &&
                !resources.set_preparation_state(currentRequest, ResourcePreparationState::Ready),
            "an event during preparation must return the resource to Dirty and reject stale adoption");
    const auto preUncertainty = resources.begin_request("alias-a.png");
    resources.invalidate_subtree(assets, 9);
    require(resources.uncertainty_epoch() == 9 && !resources.accepts(preUncertainty) &&
                resources.resolve("alias-a.png").logicalVersion > changedAlias.logicalVersion,
            "subtree uncertainty must lazily advance versions and reject stale prepared results");

    std::filesystem::rename(target, assets / "renamed.png");
    resources.invalidate(target);
    require(resources.resolve("target.png").status == ResourceStatus::Missing &&
                resources.affected_consumers("target.png").size() == 3,
            "renames must leave authored logical references unresolved until explicit repair");
    resources.unregister_document(firstDocument);
    require(resources.affected_consumers("target.png").size() == 1,
            "closing a document must release its consumers and retain other tabs' references");
}

void test_initially_broken_contained_alias_dependencies()
{
    TemporaryDirectory temporary;
    const auto assets = temporary.path() / "assets";
    const auto targetA = assets / "target-a.png";
    const auto targetB = assets / "target-b.png";
    const auto alias = assets / "outer.png";
    const auto firstMissing = assets / "links" / "middle.png";
    const auto secondMissing = assets / "alternate-links" / "middle.png";
    std::filesystem::create_directories(assets);
    std::filesystem::create_directories(firstMissing.parent_path());
    std::ofstream(targetA) << "first target";
    std::ofstream(targetB) << "second target";
    std::filesystem::create_symlink("links/middle.png", alias);

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{81}, assets);
    const DocumentIdentity document{ProjectGeneration{81}, TabId{12}, DocumentGeneration{3}};
    const ResourceConsumer consumer{document, 10, 11, "sprite.texture"};
    resources.register_consumer(alias, ResourceKind::Texture, consumer);
    const auto initiallyBroken = resources.resolve(alias, ResourceKind::Texture);
    require(initiallyBroken.status == ResourceStatus::BrokenSymlink &&
                initiallyBroken.lastGoodCanonicalPath.empty(),
            "a first-demand contained link with a missing intermediate target must remain visibly broken");
    const auto staleRequest = resources.begin_request(alias);
    require(resources.affected_consumers(firstMissing, false) == std::vector<ResourceConsumer>{consumer} &&
                resources.affected_consumers(firstMissing.parent_path(), true) ==
                    std::vector<ResourceConsumer>{consumer},
            "first demand must retain the unresolved target and its directory subtree before successful resolve");

    std::filesystem::create_directories(firstMissing.parent_path());
    std::filesystem::create_symlink("../target-a.png", firstMissing);
    const auto repairedTargetConsumers = resources.affected_consumers(firstMissing, false);
    require(repairedTargetConsumers == std::vector<ResourceConsumer>{consumer},
            "creating the missing intermediate link must find its unresolved logical consumer");
    resources.invalidate(firstMissing);
    require(!resources.accepts(staleRequest),
            "repairing an unresolved dependency must reject a request made against the broken version");
    const auto firstResolved = resources.resolve(alias, ResourceKind::Texture);
    require(firstResolved.status == ResourceStatus::Resolved &&
                firstResolved.canonicalPath == std::filesystem::canonical(targetA),
            "creating only the intermediate target link must resolve the cached logical resource");

    std::filesystem::remove(alias);
    std::filesystem::create_symlink("alternate-links/middle.png", alias);
    resources.invalidate(alias);
    const auto retargetedBroken = resources.resolve(alias, ResourceKind::Texture);
    require(retargetedBroken.status == ResourceStatus::BrokenSymlink &&
                retargetedBroken.lastGoodCanonicalPath == std::filesystem::canonical(targetA) &&
                resources.affected_consumers(secondMissing.parent_path(), false) ==
                    std::vector<ResourceConsumer>{consumer},
            "a retarget to a new missing chain must preserve last-good content and observe its new target prefix");
    std::filesystem::create_directories(secondMissing.parent_path());
    std::filesystem::create_symlink("../target-b.png", secondMissing);
    resources.invalidate(secondMissing.parent_path());
    const auto secondResolved = resources.resolve(alias, ResourceKind::Texture);
    require(secondResolved.status == ResourceStatus::Resolved &&
                secondResolved.canonicalPath == std::filesystem::canonical(targetB) &&
                resources.affected_consumers(firstMissing, false).empty(),
            "successful adoption must bind the repaired target and retire the obsolete missing-chain dependency");
}

void test_initially_rejected_contained_alias_dependencies()
{
    const auto verify_repair = [](const std::string &name, ResourceStatus initialStatus,
                                  bool cycle) {
        TemporaryDirectory temporary;
        const auto assets = temporary.path() / "assets";
        const auto links = assets / "links";
        const auto outer = assets / "outer.png";
        const auto middle = links / "middle.png";
        const auto target = assets / "target.png";
        const auto outsideTarget = temporary.path() / "outside" / "secret.png";
        std::filesystem::create_directories(links);
        std::ofstream(target) << "contained target";
        std::filesystem::create_directories(outsideTarget.parent_path());
        std::ofstream(outsideTarget) << "must never be exposed";
        std::filesystem::create_symlink("links/middle.png", outer);
        std::filesystem::create_symlink(cycle ? std::filesystem::path("../outer.png") : outsideTarget,
                                        middle);

        ResourceIndex resources;
        resources.set_project(ProjectGeneration{82}, assets);
        const DocumentIdentity document{ProjectGeneration{82}, TabId{13}, DocumentGeneration{4}};
        const ResourceConsumer consumer{document, 12, 13, "sprite.texture"};
        resources.register_consumer(outer, ResourceKind::Texture, consumer);
        const auto initial = resources.resolve(outer, ResourceKind::Texture);
        require(initial.status == initialStatus && initial.canonicalPath.empty(),
                name + " first-demand alias must remain rejected without exposing an outside/cyclic target");
        const auto staleRequest = resources.begin_request(outer);
        require(resources.affected_consumers(middle, false) == std::vector<ResourceConsumer>{consumer},
                name + " first demand must retain the safely observed intermediate link dependency");

        std::filesystem::remove(middle);
        std::filesystem::create_symlink("../target.png", middle);
        require(resources.affected_consumers(middle, false) == std::vector<ResourceConsumer>{consumer},
                name + " repair event must still find its consumer before invalidation");
        resources.invalidate(middle);
        require(!resources.accepts(staleRequest),
                name + " intermediate-link repair must reject a request from the rejected version");
        const auto repaired = resources.resolve(outer, ResourceKind::Texture);
        require(repaired.status == ResourceStatus::Resolved &&
                    repaired.canonicalPath == std::filesystem::canonical(target),
                name + " repair must adopt only the newly contained target");
    };

    verify_repair("cyclic", ResourceStatus::SymlinkCycle, true);
    verify_repair("escaping", ResourceStatus::OutsideRoot, false);
}

#if !defined(_WIN32)
void test_initially_unreadable_contained_alias_dependencies()
{
    TemporaryDirectory temporary;
    const auto assets = temporary.path() / "assets";
    const auto links = assets / "links";
    const auto restricted = assets / "restricted";
    const auto repaired = assets / "repaired";
    const auto unreadableMiddle = links / "unreadable-middle";
    const auto unreadableAlias = assets / "unreadable-alias";
    const auto lastGoodMiddle = links / "last-good-middle";
    const auto lastGoodAlias = assets / "last-good-alias";
    std::filesystem::create_directories(links);
    std::filesystem::create_directories(restricted);
    std::filesystem::create_directories(repaired);
    std::filesystem::create_directories(assets / "last-good");
    std::ofstream(restricted / "sprite.png") << "inaccessible";
    std::ofstream(repaired / "sprite.png") << "repaired";
    std::ofstream(assets / "last-good" / "sprite.png") << "last good";
    std::ofstream(repaired / "last-good-sprite.png") << "new last good";
    std::filesystem::create_directory_symlink("links/unreadable-middle", unreadableAlias);
    std::filesystem::create_directory_symlink("../restricted", unreadableMiddle);
    std::filesystem::create_directory_symlink("links/last-good-middle", lastGoodAlias);
    std::filesystem::create_directory_symlink("../last-good", lastGoodMiddle);

    ScopedDirectoryPermissions restriction(restricted, std::filesystem::perms::none);
    ResourceIndex resources;
    resources.set_project(ProjectGeneration{83}, assets);
    const DocumentIdentity document{ProjectGeneration{83}, TabId{14}, DocumentGeneration{5}};
    const ResourceConsumer firstConsumer{document, 12, 13, "sprite.texture"};
    const ResourceConsumer retainedConsumer{document, 22, 23, "sprite.texture"};
    const auto firstResource = unreadableAlias / "sprite.png";
    const auto retainedResource = lastGoodAlias / "sprite.png";
    resources.register_consumer(firstResource, ResourceKind::Texture, firstConsumer);
    resources.register_consumer(retainedResource, ResourceKind::Texture, retainedConsumer);

    const auto unreadable = resources.resolve(firstResource, ResourceKind::Texture);
    require(unreadable.status == ResourceStatus::Unreadable && unreadable.canonicalPath.empty() &&
                unreadable.lastGoodCanonicalPath.empty(),
            "the permission fixture must be unreadable on first demand without exposing its target");
    const auto staleFirstRequest = resources.begin_request(firstResource);
    require(resources.affected_consumers(unreadableMiddle, false) ==
                std::vector<ResourceConsumer>{firstConsumer},
            "first-demand unreadable resolution must retain its observed intermediate link");

    const auto lastGood = resources.resolve(retainedResource, ResourceKind::Texture);
    require(lastGood.status == ResourceStatus::Resolved &&
                lastGood.canonicalPath == std::filesystem::canonical(assets / "last-good" / "sprite.png"),
            "the second alias must establish a usable last-good resource before becoming unreadable");
    std::filesystem::remove(lastGoodMiddle);
    std::filesystem::create_directory_symlink("../restricted", lastGoodMiddle);
    require(resources.affected_consumers(lastGoodMiddle, false) ==
                std::vector<ResourceConsumer>{retainedConsumer},
            "a previously resolved alias must retain its middle-link dependency through access loss");
    resources.invalidate(lastGoodMiddle);
    const auto temporarilyUnreadable = resources.resolve(retainedResource, ResourceKind::Texture);
    require(temporarilyUnreadable.status == ResourceStatus::Unreadable &&
                temporarilyUnreadable.lastGoodCanonicalPath == lastGood.canonicalPath,
            "an unreadable replacement must preserve the prior accepted resource identity");

    std::filesystem::remove(unreadableMiddle);
    std::filesystem::create_directory_symlink("../repaired", unreadableMiddle);
    require(resources.affected_consumers(unreadableMiddle, false) ==
                std::vector<ResourceConsumer>{firstConsumer},
            "repairing only the accessible first-demand intermediate link must find its consumer");
    resources.invalidate(unreadableMiddle);
    require(!resources.accepts(staleFirstRequest),
            "repairing the unreadable alias must reject prepared work from the inaccessible version");
    const auto repairedFirst = resources.resolve(firstResource, ResourceKind::Texture);
    require(repairedFirst.status == ResourceStatus::Resolved &&
                repairedFirst.canonicalPath == std::filesystem::canonical(repaired / "sprite.png"),
            "the unreadable first-demand alias must resolve after only its middle link is repaired");

    std::filesystem::remove(lastGoodMiddle);
    std::filesystem::create_directory_symlink("../repaired", lastGoodMiddle);
    resources.invalidate(lastGoodMiddle);
    const auto restoredLastGood = resources.resolve(retainedResource, ResourceKind::Texture);
    require(restoredLastGood.status == ResourceStatus::Resolved &&
                restoredLastGood.canonicalPath == std::filesystem::canonical(repaired / "sprite.png"),
            "a retained alias must adopt its repaired target without losing resource validity");
}
#endif

void test_healthy_inventory_deltas_do_not_refresh_unrelated_resources()
{
    TemporaryDirectory temporary;
    const auto assets = temporary.path() / "assets";
    std::filesystem::create_directories(assets);
    const auto referenced = assets / "referenced.png";
    const auto unused = assets / "unused.png";
    std::ofstream(referenced) << "referenced";

    ProjectChanges changes;
    changes.start(ProjectGeneration{72}, assets);
    ResourceIndex resources;
    resources.set_project(ProjectGeneration{72}, assets);
    const DocumentIdentity document{ProjectGeneration{72}, TabId{5}, DocumentGeneration{1}};
    const ResourceConsumer consumer{document, 7, 8, "sprite.texture"};
    resources.register_consumer(referenced, ResourceKind::Texture, consumer);
    const auto initialTime = std::filesystem::last_write_time(referenced);
    const std::vector<std::pair<std::filesystem::path, std::string>> initial{
        {referenced, "f:" + std::filesystem::canonical(referenced).generic_string() + ":" +
            std::to_string(std::filesystem::file_size(referenced)) + ":" +
            std::to_string(initialTime.time_since_epoch().count())}};
    auto snapshot = changes.begin_root_snapshot();
    require(changes.complete_root_snapshot(snapshot, initial, true),
            "the targeted-delta fixture must accept an initial complete inventory");
    auto batch = changes.take_batch(256, std::chrono::steady_clock::now(), true);
    require(batch.events.size() == 1 && batch.events.front().directory,
            "the initial inventory must establish its conservative root baseline");
    (void)resources.affected_consumers(assets, true);

    std::ofstream(unused) << "not referenced by the open Scene";
    const auto unusedTime = std::filesystem::last_write_time(unused);
    const std::vector<std::pair<std::filesystem::path, std::string>> expanded{
        initial.front(),
        {unused, "f:" + std::filesystem::canonical(unused).generic_string() + ":" +
            std::to_string(std::filesystem::file_size(unused)) + ":" +
            std::to_string(unusedTime.time_since_epoch().count())}};
    snapshot = changes.begin_root_snapshot();
    require(changes.complete_root_snapshot(snapshot, expanded, true),
            "a healthy consistency cycle must accept the newly observed unused asset");
    batch = changes.take_batch(256, std::chrono::steady_clock::now(), true);
    require(batch.events.size() == 1 && batch.events.front().path == unused &&
                batch.events.front().kind == ProjectChangeKind::Added && !batch.events.front().directory &&
                resources.affected_consumers(unused).empty(),
            "a missed ordinary addition must produce only its path delta and no unrelated Scene reload demand");
}

void test_directory_alias_non_directory_invalidation()
{
    TemporaryDirectory temporary;
    const auto assets = temporary.path() / "assets";
    const auto firstTarget = assets / "first-target";
    const auto secondTarget = assets / "second-target";
    const auto alias = assets / "scene-alias";
    std::filesystem::create_directories(firstTarget);
    std::filesystem::create_directories(secondTarget);
    for (const auto *name : {"sprite.png", "clip.animset", "frame.png"})
    {
        std::ofstream(firstTarget / name) << "first " << name;
        std::ofstream(secondTarget / name) << "second " << name;
    }
    std::filesystem::create_directory_symlink(firstTarget.filename(), alias);

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{73}, assets);
    const DocumentIdentity document{ProjectGeneration{73}, TabId{7}, DocumentGeneration{1}};
    const std::vector<std::pair<std::filesystem::path, ResourceConsumer>> dependencies{
        {alias / "sprite.png", {document, 10, 11, "sprite.texture"}},
        {alias / "clip.animset", {document, 20, 21, "animator.clip"}},
        {alias / "frame.png", {document, 20, 21, "animation.frame.0"}},
    };
    std::vector<std::uint64_t> initialVersions;
    for (const auto &[path, consumer] : dependencies)
    {
        resources.register_consumer(path, consumer.role == "animator.clip"
                                              ? ResourceKind::Animation : ResourceKind::Texture,
                                    consumer);
        const auto resolution = resources.resolve(path);
        require(resolution.status == ResourceStatus::Resolved,
                "all aliased Scene resource kinds must resolve before retargeting");
        initialVersions.push_back(resolution.logicalVersion);
    }
    const auto unrelated = assets / "unrelated.png";
    std::ofstream(unrelated) << "unrelated";
    resources.register_consumer(unrelated, ResourceKind::Texture,
                                {document, 30, 31, "sprite.texture"});
    const auto unrelatedVersion = resources.resolve(unrelated).logicalVersion;

    const auto changed_consumers = [&] {
        const auto consumers = resources.affected_consumers(alias, false);
        require(consumers.size() == dependencies.size(),
                "directory-link events with directory=false must discover sprite, clip and frame consumers");
        return consumers;
    };
    const auto apply_non_directory_change = [&] {
        (void)changed_consumers();
        resources.invalidate(alias);
    };
    const auto require_target = [&](const std::filesystem::path &target, std::size_t versionOffset) {
        for (std::size_t i = 0; i < dependencies.size(); ++i)
        {
            const auto resolution = resources.resolve(dependencies[i].first);
            require(resolution.status == ResourceStatus::Resolved &&
                        resolution.canonicalPath == std::filesystem::canonical(target / dependencies[i].first.filename()) &&
                        resolution.logicalVersion > initialVersions[i] + versionOffset,
                    "every cached logical descendant must re-resolve to the current canonical target");
        }
        require(resources.resolve(unrelated).logicalVersion == unrelatedVersion,
                "directory-link invalidation must leave unrelated Scene resources untouched");
    };

#if defined(__linux__)
    const auto replacementLink = assets / "scene-alias-next";
    std::filesystem::create_directory_symlink(secondTarget.filename(), replacementLink);
    std::filesystem::rename(replacementLink, alias);
    apply_non_directory_change();
    require_target(secondTarget, 0);
#else
    std::filesystem::remove(alias);
    apply_non_directory_change();
    std::filesystem::create_directory_symlink(secondTarget.filename(), alias);
    apply_non_directory_change();
    require_target(secondTarget, 0);
#endif

    std::filesystem::remove(alias);
    apply_non_directory_change();
    for (std::size_t i = 0; i < dependencies.size(); ++i)
    {
        const auto resolution = resources.resolve(dependencies[i].first);
        require(resolution.status == ResourceStatus::Missing &&
                    resolution.lastGoodCanonicalPath == std::filesystem::canonical(
                        secondTarget / dependencies[i].first.filename()),
                "unlinking the directory alias must preserve last-good descendant mappings");
    }
    std::filesystem::create_directory_symlink(firstTarget.filename(), alias);
    apply_non_directory_change();
    require_target(firstTarget, 1);

    std::filesystem::remove(alias);
    std::filesystem::create_directory_symlink("missing-target", alias);
    apply_non_directory_change();
    for (const auto &[path, consumer] : dependencies)
    {
        (void)consumer;
        const auto resolution = resources.resolve(path);
        require(resolution.status == ResourceStatus::BrokenSymlink &&
                    resolution.lastGoodCanonicalPath == std::filesystem::canonical(
                        firstTarget / path.filename()),
                "a broken retarget must retain the last-good canonical descendant until repair");
    }
    std::filesystem::remove(alias);
    std::filesystem::create_directory_symlink(secondTarget.filename(), alias);
    apply_non_directory_change();
    require_target(secondTarget, 2);
}

void test_transitive_alias_hop_invalidation()
{
    TemporaryDirectory temporary;
    const auto assets = temporary.path() / "assets";
    const auto firstTarget = assets / "target-a";
    const auto secondTarget = assets / "target-b";
    const auto middleAlias = assets / "middle-alias";
    const auto outerAlias = assets / "outer-alias";
    std::filesystem::create_directories(firstTarget);
    std::filesystem::create_directories(secondTarget);
    for (const auto *name : {"sprite.png", "clip.animset", "frame.png"})
    {
        std::ofstream(firstTarget / name) << "first " << name;
        std::ofstream(secondTarget / name) << "second " << name;
    }
    std::filesystem::create_directory_symlink(firstTarget.filename(), middleAlias);
    std::filesystem::create_directory_symlink(middleAlias.filename(), outerAlias);

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{74}, assets);
    const DocumentIdentity document{ProjectGeneration{74}, TabId{8}, DocumentGeneration{1}};
    const std::vector<std::pair<std::filesystem::path, ResourceConsumer>> dependencies{
        {outerAlias / "sprite.png", {document, 10, 11, "sprite.texture"}},
        {outerAlias / "clip.animset", {document, 20, 21, "animator.clip"}},
        {outerAlias / "frame.png", {document, 20, 21, "animation.frame.0"}},
    };
    std::vector<std::uint64_t> initialVersions;
    std::vector<ResourceRequestToken> staleRequests;
    for (const auto &[path, consumer] : dependencies)
    {
        resources.register_consumer(path, consumer.role == "animator.clip"
                                              ? ResourceKind::Animation : ResourceKind::Texture,
                                    consumer);
        const auto resolution = resources.resolve(path);
        require(resolution.status == ResourceStatus::Resolved &&
                    resolution.canonicalPath == std::filesystem::canonical(
                        firstTarget / path.filename()),
                "nested contained links must resolve through both outer and intermediate aliases");
        initialVersions.push_back(resolution.logicalVersion);
        staleRequests.push_back(resources.begin_request(path));
    }

    const auto changed_consumers = [&] {
        const auto consumers = resources.affected_consumers(middleAlias, false);
        require(consumers.size() == dependencies.size(),
                "an intermediate directory-link event with directory=false must find every outer alias consumer");
        return consumers;
    };
    const auto replacementLink = assets / "middle-alias-next";
    std::filesystem::create_directory_symlink(secondTarget.filename(), replacementLink);
#if defined(__linux__)
    std::filesystem::rename(replacementLink, middleAlias);
#else
    std::filesystem::remove(middleAlias);
    std::filesystem::rename(replacementLink, middleAlias);
#endif
    (void)changed_consumers();
    resources.invalidate(middleAlias);
    for (const auto &request : staleRequests)
        require(!resources.accepts(request),
                "an intermediate alias retarget must reject prepared Scene results from the old target");

    const auto require_target = [&](const std::filesystem::path &target) {
        for (std::size_t i = 0; i < dependencies.size(); ++i)
        {
            const auto resolution = resources.resolve(dependencies[i].first);
            require(resolution.status == ResourceStatus::Resolved &&
                        resolution.canonicalPath == std::filesystem::canonical(
                            target / dependencies[i].first.filename()) &&
                        resolution.logicalVersion > initialVersions[i],
                    "every outer logical alias must adopt its current intermediate target");
        }
    };
    require_target(secondTarget);
    require(resources.affected_consumers(firstTarget, true).empty(),
            "successful intermediate retarget must retire obsolete canonical reverse mappings");

    std::filesystem::remove(middleAlias);
    (void)changed_consumers();
    resources.invalidate(middleAlias);
    for (const auto &[path, consumer] : dependencies)
    {
        (void)consumer;
        const auto resolution = resources.resolve(path);
        require(resolution.status == ResourceStatus::BrokenSymlink &&
                    resolution.lastGoodCanonicalPath == std::filesystem::canonical(
                        secondTarget / path.filename()),
                "removing an intermediate link must retain each descendant's last-good target");
    }

    std::filesystem::create_directory_symlink(firstTarget.filename(), middleAlias);
    (void)changed_consumers();
    resources.invalidate(middleAlias);
    require_target(firstTarget);
    require(resources.affected_consumers(secondTarget, true).empty(),
            "repairing the intermediate link must retire its previous target dependency");

    const auto alternateMiddleAlias = assets / "alternate-middle-alias";
    const auto outerReplacement = assets / "outer-alias-next";
    std::filesystem::create_directory_symlink(secondTarget.filename(), alternateMiddleAlias);
    std::filesystem::create_directory_symlink(alternateMiddleAlias.filename(), outerReplacement);
#if defined(__linux__)
    std::filesystem::rename(outerReplacement, outerAlias);
#else
    std::filesystem::remove(outerAlias);
    std::filesystem::rename(outerReplacement, outerAlias);
#endif
    const auto outerConsumers = resources.affected_consumers(outerAlias, false);
    require(outerConsumers.size() == dependencies.size(),
            "retargeting the outer link must find consumers through its retained dependency");
    resources.invalidate(outerAlias);
    require_target(secondTarget);
    require(resources.affected_consumers(middleAlias, false).empty() &&
                resources.affected_consumers(alternateMiddleAlias, false).size() == dependencies.size(),
            "successful retarget must retire the obsolete intermediate hop and retain the new one");
}

void test_alias_parent_directory_invalidation()
{
    TemporaryDirectory temporary;
    const auto assets = temporary.path() / "assets";
    const auto targetA = assets / "target-a";
    const auto targetB = assets / "target-b";
    const auto targetC = assets / "target-c";
    const auto links = assets / "links";
    const auto outerAlias = assets / "outer-alias";
    const auto unrelated = assets / "unrelated.png";
    std::filesystem::create_directories(targetA);
    std::filesystem::create_directories(targetB);
    std::filesystem::create_directories(targetC);
    std::filesystem::create_directories(links);
    for (const auto *name : {"sprite.png", "clip.animset", "frame.png"})
    {
        std::ofstream(targetA / name) << "A " << name;
        std::ofstream(targetB / name) << "B " << name;
        std::ofstream(targetC / name) << "C " << name;
    }
    std::ofstream(unrelated) << "unrelated";
    std::filesystem::create_directory_symlink("../target-a", links / "middle");
    std::filesystem::create_directory_symlink("links/middle", outerAlias);

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{75}, assets);
    const DocumentIdentity document{ProjectGeneration{75}, TabId{9}, DocumentGeneration{1}};
    const std::vector<std::pair<std::filesystem::path, ResourceConsumer>> dependencies{
        {outerAlias / "sprite.png", {document, 10, 11, "sprite.texture"}},
        {outerAlias / "clip.animset", {document, 20, 21, "animator.clip"}},
        {outerAlias / "frame.png", {document, 20, 21, "animation.frame.0"}},
    };
    std::vector<std::uint64_t> initialVersions;
    std::vector<ResourceRequestToken> staleRequests;
    for (const auto &[path, consumer] : dependencies)
    {
        const auto kind = consumer.role == "animator.clip"
            ? ResourceKind::Animation : ResourceKind::Texture;
        resources.register_consumer(path, kind, consumer);
        const auto resolution = resources.resolve(path, kind);
        require(resolution.status == ResourceStatus::Resolved &&
                    resolution.canonicalPath == std::filesystem::canonical(
                        targetA / path.filename()),
                "nested alias dependencies must retain the intermediate link beneath its parent");
        initialVersions.push_back(resolution.logicalVersion);
        staleRequests.push_back(resources.begin_request(path));
    }
    resources.register_consumer(unrelated, ResourceKind::Texture,
                                {document, 90, 91, "unrelated.texture"});
    const auto unrelatedVersion = resources.resolve(unrelated).logicalVersion;

    const auto require_parent_consumers = [&] {
        const auto consumers = resources.affected_consumers(links, true);
        require(consumers.size() == dependencies.size() &&
                    std::none_of(consumers.begin(), consumers.end(), [](const ResourceConsumer &consumer) {
                        return consumer.objectId == 90;
                    }),
                "a directory event for the intermediate hop's parent must fan out only to retained aliases");
    };

    const auto movedLinks = assets / "links-moved";
    std::filesystem::rename(links, movedLinks);
    require_parent_consumers();
    resources.invalidate(links);
    for (const auto &request : staleRequests)
        require(!resources.accepts(request),
                "a parent-directory rename must reject prepared Scene results using the old symlink chain");
    for (const auto &[path, consumer] : dependencies)
    {
        (void)consumer;
        const auto resolution = resources.resolve(path);
        require(resolution.status == ResourceStatus::BrokenSymlink &&
                    resolution.lastGoodCanonicalPath == std::filesystem::canonical(
                        targetA / path.filename()),
                "a renamed intermediate parent must preserve each last-good resource identity");
    }

    std::filesystem::create_directories(links);
    std::filesystem::create_directory_symlink("../target-b", links / "middle");
    require_parent_consumers();
    resources.invalidate(links);
    for (std::size_t index = 0; index < dependencies.size(); ++index)
    {
        const auto resolution = resources.resolve(dependencies[index].first);
        require(resolution.status == ResourceStatus::Resolved &&
                    resolution.canonicalPath == std::filesystem::canonical(
                        targetB / dependencies[index].first.filename()) &&
                    resolution.logicalVersion > initialVersions[index],
                "recreating the parent directory must adopt its new intermediate target");
    }
    require(resources.affected_consumers(targetA, true).empty(),
            "successful parent recreation must retire old canonical reverse mappings");

    const auto stagedLinks = assets / "links-next";
    std::filesystem::create_directories(stagedLinks);
    std::filesystem::create_directory_symlink("../target-c", stagedLinks / "middle");
    std::filesystem::remove_all(links);
    std::filesystem::rename(stagedLinks, links);
    require_parent_consumers();
    resources.invalidate(links);
    for (const auto &[path, consumer] : dependencies)
    {
        (void)consumer;
        require(resources.resolve(path).canonicalPath == std::filesystem::canonical(
                    targetC / path.filename()),
                "replacing the intermediate parent directory must re-resolve every outer alias");
    }
    require(resources.affected_consumers(targetB, true).empty() &&
                resources.resolve(unrelated).logicalVersion == unrelatedVersion,
            "parent replacement must retire the previous target without touching unrelated resources");
}

void test_bounded_fallback_reference_checks()
{
    TemporaryDirectory temporary;
    const auto assets = temporary.path() / "assets";
    std::filesystem::create_directories(assets);
    const auto firstPath = assets / "a.png";
    const auto secondPath = assets / "b.png";
    std::ofstream(firstPath) << "first";
    std::ofstream(secondPath) << "before";

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{31}, assets);
    const DocumentIdentity document{ProjectGeneration{31}, TabId{2}, DocumentGeneration{1}};
    resources.register_consumer("a.png", ResourceKind::Texture,
                                {document, 1, 2, "sprite"});
    resources.register_consumer("b.png", ResourceKind::Texture,
                                {document, 3, 4, "sprite"});
    resources.invalidate("b.png");
    work::reset();
    const auto initiallyResolved = resources.resolve("b.png");
    const auto initialVersion = initiallyResolved.logicalVersion;
    const auto afterInitialResolve = work::snapshot();
    std::ofstream(secondPath, std::ios::trunc) << "after with different size";
    const auto cachedResolution = resources.resolve("b.png");
    require(cachedResolution.logicalVersion == initialVersion &&
                cachedResolution.size == initiallyResolved.size,
            "unchanged-frame reference queries must reuse their versioned resolution without restatting the path");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    const auto afterCachedResolve = work::snapshot();
    require(afterCachedResolve[static_cast<std::size_t>(work::Counter::ValidityQueries)] ==
                afterInitialResolve[static_cast<std::size_t>(work::Counter::ValidityQueries)],
            "cached displayed-reference validity must perform zero filesystem resolutions on unchanged frames");
#else
    (void)afterInitialResolve;
#endif

    require(resources.inspect_fallback_paths(0).empty(),
            "a zero path-stat budget must not inspect any references");
    require(resources.inspect_fallback_paths(1).empty(),
            "one fallback unit must inspect no more than the first registered reference");
    auto changed = resources.inspect_fallback_paths(1);
    require(changed.size() == 1 && changed.front().logicalPath == secondPath &&
                changed.front().consumers.size() == 1 &&
                resources.resolve("b.png").logicalVersion > initialVersion,
            "the bounded round-robin fallback must detect and invalidate a changed referenced file");
    require(resources.inspect_fallback_paths(1).empty(),
            "unchanged references must not be repeatedly invalidated between native notifications");

    std::filesystem::remove(secondPath);
    changed = resources.inspect_fallback_paths(1);
    require(changed.size() == 1 && changed.front().logicalPath == secondPath &&
                resources.resolve("b.png").status == ResourceStatus::Missing &&
                changed.front().consumers.size() == 1,
            "fallback checks must preserve unresolved consumers when a referenced file disappears");

    ResourceIndex roundRobin;
    roundRobin.set_project(ProjectGeneration{32}, assets);
    const DocumentIdentity largeDocument{ProjectGeneration{32}, TabId{3}, DocumentGeneration{1}};
    std::filesystem::create_directories(assets / "roundrobin");
    std::filesystem::path lastPath;
    for (int index = 0; index < 520; ++index)
    {
        std::ostringstream name;
        name << "roundrobin/item-" << std::setw(4) << std::setfill('0') << index << ".png";
        const auto path = assets / name.str();
        std::ofstream(path) << "x";
        roundRobin.register_consumer(name.str(), ResourceKind::Texture,
                                     {largeDocument, index + 1, index + 1000, "thumbnail"});
        if (index == 519)
            lastPath = path;
    }
    std::ofstream(lastPath, std::ios::trunc) << "a changed-size referenced image";
    require(roundRobin.inspect_fallback_paths(512).empty(),
            "fallback inspection must respect its 512-reference per-pass bound");
    changed = roundRobin.inspect_fallback_paths(512);
    require(std::any_of(changed.begin(), changed.end(), [&](const ResourceFallbackChange &change) {
                return change.logicalPath == lastPath && change.consumers.size() == 1;
            }),
            "round-robin fallback must reach references beyond the first 512 entries");
}

void test_visible_audio_reference_fallback_demand()
{
    TemporaryDirectory temporary;
    const auto assets = temporary.path() / "assets";
    std::filesystem::create_directories(assets / "audio");
    const auto audioPath = assets / "audio" / "music.ogg";
    std::ofstream(audioPath, std::ios::binary) << "original audio";

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{92}, assets);
    const DocumentIdentity document{ProjectGeneration{92}, TabId{10}, DocumentGeneration{5}};
    const ResourceConsumer audioConsumer{document, 70, 71, "inspector-asset:Audio:s8:filePath;"};
    resources.begin_visible_reference_frame();
    resources.register_visible_reference("assets/audio/music.ogg", ResourceKind::Audio, audioConsumer);
    resources.end_visible_reference_frame();
    require(resources.affected_consumers(audioPath) == std::vector<ResourceConsumer>{audioConsumer},
            "a visible Audio reference must create fallback demand without a Scene preview proxy");

    work::reset();
    resources.begin_visible_reference_frame();
    resources.register_visible_reference("assets/audio/music.ogg", ResourceKind::Audio, audioConsumer);
    resources.end_visible_reference_frame();
    const auto idleQueries = work::snapshot()[static_cast<std::size_t>(work::Counter::ValidityQueries)];
    resources.begin_visible_reference_frame();
    resources.register_visible_reference("assets/audio/music.ogg", ResourceKind::Audio, audioConsumer);
    resources.end_visible_reference_frame();
    require(work::snapshot()[static_cast<std::size_t>(work::Counter::ValidityQueries)] == idleQueries,
            "an unchanged visible Audio reference must reuse its cached resolution on healthy frames");

    require(std::filesystem::remove(audioPath), "the visible Audio test asset must delete");
    auto fallback = resources.inspect_fallback_paths(1);
    require(fallback.size() == 1 && fallback.front().logicalPath == audioPath &&
                fallback.front().consumers == std::vector<ResourceConsumer>{audioConsumer} &&
                resources.resolve("assets/audio/music.ogg", ResourceKind::Audio).status == ResourceStatus::Missing,
            "bounded fallback must detect a silently deleted visible Audio reference and retain its owner");

    std::ofstream(audioPath, std::ios::binary) << "restored audio file";
    fallback = resources.inspect_fallback_paths(1);
    require(fallback.size() == 1 && fallback.front().logicalPath == audioPath &&
                resources.resolve("assets/audio/music.ogg", ResourceKind::Audio).status == ResourceStatus::Resolved,
            "bounded fallback must detect a silently restored visible Audio reference");

    resources.begin_visible_reference_frame();
    resources.end_visible_reference_frame();
    require(resources.affected_consumers(audioPath).empty(),
            "a reference demand must retire when the field is no longer visible");
    require(std::filesystem::remove(audioPath), "the retired Audio test asset must delete");
    require(resources.inspect_fallback_paths(1).empty(),
            "retired reference-only paths must no longer consume degraded fallback work");

    const auto demandedRoot = assets / "audio-demand";
    std::filesystem::create_directories(demandedRoot);
    ResourceIndex manyResources;
    manyResources.set_project(ProjectGeneration{93}, assets);
    const DocumentIdentity manyDocument{ProjectGeneration{93}, TabId{11}, DocumentGeneration{1}};
    std::filesystem::path lastAudioPath;
    manyResources.begin_visible_reference_frame();
    for (int index = 0; index < 520; ++index)
    {
        std::ostringstream name;
        name << "audio-demand/item-" << std::setw(4) << std::setfill('0') << index << ".ogg";
        const auto path = assets / name.str();
        std::ofstream(path, std::ios::binary) << "stable audio";
        manyResources.register_visible_reference(name.str(), ResourceKind::Audio,
            {manyDocument, index + 1, index + 1000, "inspector-asset:Audio:s8:filePath;"});
        if (index == 519)
            lastAudioPath = path;
    }
    manyResources.end_visible_reference_frame();
    std::ofstream(lastAudioPath, std::ios::binary | std::ios::trunc) << "changed audio size";
    require(manyResources.inspect_fallback_paths(512).empty(),
            "visible Audio fallback must keep each pass within its 512-reference budget");
    fallback = manyResources.inspect_fallback_paths(512);
    require(std::any_of(fallback.begin(), fallback.end(), [&](const ResourceFallbackChange &change) {
                return change.logicalPath == lastAudioPath && change.consumers.size() == 1;
            }),
            "round-robin fallback must reach visible reference-only demands beyond the first 512 paths");
    manyResources.unregister_document(manyDocument);
    require(manyResources.affected_consumers(lastAudioPath).empty(),
            "closing a Scene tab must release its visible reference-only fallback demands");
}

void test_recovery_inventory_revalidates_equal_metadata_and_replacement()
{
    TemporaryDirectory temporary;
    const auto assets = temporary.path() / "assets";
    std::filesystem::create_directories(assets);
    const auto texture = assets / "same-metadata.png";
    std::ofstream(texture, std::ios::binary) << "old!";
    const auto originalTime = std::filesystem::last_write_time(texture);
    const std::vector<std::pair<std::filesystem::path, std::string>> inventory{
        {texture, "4:" + std::to_string(originalTime.time_since_epoch().count())}};

    ProjectChanges changes;
    changes.start(ProjectGeneration{44}, assets);
    ResourceIndex resources;
    resources.set_project(ProjectGeneration{44}, assets);
    const DocumentIdentity document{ProjectGeneration{44}, TabId{8}, DocumentGeneration{3}};
    resources.register_consumer("same-metadata.png", ResourceKind::Texture,
                                {document, 90, 91, "sprite.texture"});
    const auto initial = resources.resolve("same-metadata.png", ResourceKind::Texture);
    auto snapshot = changes.begin_root_snapshot();
    require(changes.complete_root_snapshot(snapshot, inventory, true),
            "the integration fixture must accept an initial complete inventory");
    auto batch = changes.take_batch(256, std::chrono::steady_clock::now(), true);
    require(batch.events.size() == 1 && batch.events.front().directory &&
                batch.events.front().path == assets &&
                resources.affected_consumers(assets, true).size() == 1,
            "root reconciliation must expose every referenced resource to consumer refresh");
    resources.invalidate(assets);
    resources.invalidate_subtree(assets, resources.uncertainty_epoch() + 1);
    const auto initialRefresh = resources.resolve("same-metadata.png", ResourceKind::Texture);
    require(initialRefresh.logicalVersion > initial.logicalVersion &&
                changes.take_batch(256, std::chrono::steady_clock::now(), true).coverage ==
                    ProjectCoverageState::Current,
            "Current coverage must follow the root event that refreshes its lazy consumer");

    snapshot = changes.begin_root_snapshot();
    require(changes.complete_root_snapshot(snapshot, inventory, true) &&
                changes.take_batch(256, std::chrono::steady_clock::now(), true).events.empty(),
            "an unchanged healthy-consistency scan must leave valid resources alone");

    const auto staleRequest = resources.begin_request("same-metadata.png");
    std::ofstream(texture, std::ios::binary | std::ios::trunc) << "new!";
    std::filesystem::last_write_time(texture, originalTime);
    changes.submit(ProjectChangeEvent{.kind = ProjectChangeKind::Overflow,
                                     .path = assets,
                                     .directory = true,
                                     .projectGeneration = ProjectGeneration{44}});
    batch = changes.take_batch(256, std::chrono::steady_clock::now(), true);
    require(batch.rootDirty, "overflow must put the project into recovery before scanning");
    resources.invalidate_subtree(assets, batch.uncertaintyEpoch);
    snapshot = changes.begin_root_snapshot();
    require(snapshot.recoveryRequired && changes.complete_root_snapshot(snapshot, inventory, true),
            "an equal-metadata recovery scan must still accept a complete inventory");
    batch = changes.take_batch(256, std::chrono::steady_clock::now(), true);
    require(batch.coverage == ProjectCoverageState::Reconciling && batch.events.size() == 1 &&
                batch.events.front().path == assets,
            "equal-metadata uncertainty must publish a subtree invalidation before Current");
    const auto consumers = resources.affected_consumers(assets, true);
    resources.invalidate(assets);
    resources.invalidate_subtree(assets, resources.uncertainty_epoch() + 1);
    const auto recovered = resources.resolve("same-metadata.png", ResourceKind::Texture);
    require(consumers.size() == 1 && recovered.logicalVersion > initialRefresh.logicalVersion &&
                !resources.accepts(staleRequest),
            "recovery must advance resource versions and reject stale work even when metadata is identical");
    require(changes.take_batch(256, std::chrono::steady_clock::now(), true).coverage ==
                ProjectCoverageState::Current,
            "coverage may return to Current after the consumer invalidation event has been handled");

    const auto staleRootToken = changes.begin_root_snapshot();
    const auto replacementRoot = temporary.path() / "replacement-assets";
    std::filesystem::create_directories(replacementRoot);
    const auto replacementFile = replacementRoot / "next.png";
    std::ofstream(replacementFile) << "next";
    const auto replacementTime = std::filesystem::last_write_time(replacementFile);
    const std::vector<std::pair<std::filesystem::path, std::string>> replacementInventory{
        {replacementFile, "4:" + std::to_string(replacementTime.time_since_epoch().count())}};
    changes.start(ProjectGeneration{45}, replacementRoot);
    resources.set_project(ProjectGeneration{45}, replacementRoot);
    require(!changes.complete_root_snapshot(staleRootToken, inventory, true),
            "a pre-replacement scan must not adopt into the new project root");
    snapshot = changes.begin_root_snapshot();
    require(changes.complete_root_snapshot(snapshot, replacementInventory, true),
            "the replacement root must accept its own complete inventory");
    batch = changes.take_batch(256, std::chrono::steady_clock::now(), true);
    require(batch.events.size() == 1 && batch.events.front().projectGeneration == ProjectGeneration{45} &&
                batch.events.front().path == replacementRoot,
            "root replacement must refresh resources under the new project generation");
}

void test_project_session_wakes_owner_on_native_completion()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    std::ofstream(project / "CMakeLists.txt") << "project(wake-test)\n";

    ProjectSession session(project, assets);
    std::mutex mutex;
    std::condition_variable condition;
    bool woke = false;
    session.set_wake_callback([&] {
        {
            std::lock_guard lock(mutex);
            woke = true;
        }
        condition.notify_one();
    });
    std::ofstream(assets / "wake.png") << "wake";
    std::unique_lock lock(mutex);
    require(condition.wait_for(lock, std::chrono::seconds(3), [&] { return woke; }),
            "native watcher completions must wake the owner loop instead of waiting for a polling delay");
}

void test_source_notifications_are_separate_from_asset_changes()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    const auto source = project / "src";
    std::filesystem::create_directories(assets);
    std::filesystem::create_directories(source);
    std::ofstream(project / "CMakeLists.txt") << "project(source_watch)\n";

    ProjectSession session(project, assets);
    require(session.source_notifications_available(),
            "the source-root watcher must report native coverage when the host watcher is available");
#if defined(__linux__)
    require(ProjectSessionTestProbe::watcher_owner_count(session) == 1,
            "nested Linux source and asset roots must use one native watcher owner");
#endif
    const auto beforeSourceWrite = session.source_change_revision();
    std::ofstream(source / "main.cpp") << "int main() { return 0; }\n";
    const auto sourceDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (session.source_change_revision() == beforeSourceWrite &&
           std::chrono::steady_clock::now() < sourceDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(session.source_change_revision() > beforeSourceWrite,
            "source-root edits must advance a content uncertainty revision promptly");

    const auto beforeManifestWrite = session.source_change_revision();
    {
        std::ofstream manifest(project / "CMakeLists.txt", std::ios::app);
        manifest << "# direct project-root configuration edit\n";
    }
    const auto manifestDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (session.source_change_revision() == beforeManifestWrite &&
           std::chrono::steady_clock::now() < manifestDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(session.source_change_revision() > beforeManifestWrite,
            "a direct project-root CMakeLists.txt edit must advance source freshness with nested assets watched");

    const auto afterSourceWrite = session.source_change_revision();
    const auto scenePath = assets / "scene.json";
    std::ofstream(scenePath) << "[]\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    require(session.source_change_revision() == afterSourceWrite,
            "asset-root notifications must not trigger source identity hashing or rebuild state");
    bool assetChangePublished = false;
    const auto assetDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!assetChangePublished && std::chrono::steady_clock::now() < assetDeadline)
    {
        const auto batch = session.take_change_batch();
        assetChangePublished = std::any_of(batch.events.begin(), batch.events.end(), [&](const auto &event) {
            return event.path == scenePath || event.oldPath == scenePath ||
                   std::find(event.affectedPaths.begin(), event.affectedPaths.end(), scenePath) !=
                       event.affectedPaths.end();
        });
        if (!assetChangePublished)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(assetChangePublished,
            "the shared Linux watcher owner must still route asset changes to ProjectChanges");

    const auto beforeBuildOutput = session.source_change_revision();
    std::filesystem::create_directories(project / "build");
    std::ofstream(project / "build" / "generated.cpp") << "generated\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    require(session.source_change_revision() == beforeBuildOutput,
            "ignored build outputs must not trigger source freshness work");

    std::filesystem::remove_all(assets);
    bool assetRootUnavailable = false;
    const auto unavailableDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!assetRootUnavailable && std::chrono::steady_clock::now() < unavailableDeadline)
    {
        const auto batch = session.take_change_batch();
        assetRootUnavailable = batch.coverage == ProjectCoverageState::RootUnavailable ||
            std::any_of(batch.events.begin(), batch.events.end(), [](const auto &event) {
                return event.kind == ProjectChangeKind::RootUnavailable;
            });
        if (!assetRootUnavailable)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(assetRootUnavailable && session.source_notifications_available(),
            "removing the nested asset root must degrade asset coverage without stopping source monitoring");

    std::filesystem::create_directories(assets);
    bool assetRootRestored = false;
    const auto restoredDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!assetRootRestored && std::chrono::steady_clock::now() < restoredDeadline)
    {
        const auto batch = session.take_change_batch();
        assetRootRestored = batch.coverage != ProjectCoverageState::RootUnavailable;
        if (!assetRootRestored)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(assetRootRestored && session.source_notifications_available(),
            "recreating the nested asset root must restore asset coverage while preserving source coverage");
    require(session.source_change_revision() == afterSourceWrite,
            "asset-root lifecycle changes must not invalidate source freshness identity");

#if defined(__linux__)
    const auto movedProject = temporary.path() / "project-old";
    std::filesystem::rename(project, movedProject);
    bool sourceRootUnavailable = false;
    bool projectAssetRootUnavailable = false;
    const auto projectUnavailableDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while ((!sourceRootUnavailable || !projectAssetRootUnavailable) &&
           std::chrono::steady_clock::now() < projectUnavailableDeadline)
    {
        sourceRootUnavailable = !session.source_notifications_available();
        const auto batch = session.take_change_batch();
        projectAssetRootUnavailable = projectAssetRootUnavailable ||
            batch.coverage == ProjectCoverageState::RootUnavailable ||
            std::any_of(batch.events.begin(), batch.events.end(), [](const auto &event) {
                return event.kind == ProjectChangeKind::RootUnavailable;
            });
        if (!sourceRootUnavailable || !projectAssetRootUnavailable)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(sourceRootUnavailable && projectAssetRootUnavailable,
            "losing the project root must publish degraded source and nested-asset status");

    std::filesystem::create_directories(assets);
    std::ofstream(project / "CMakeLists.txt") << "project(source_watch_recovered)\n";
    bool sourceRootRestored = false;
    bool projectAssetRootRestored = false;
    const auto projectRestoredDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while ((!sourceRootRestored || !projectAssetRootRestored) &&
           std::chrono::steady_clock::now() < projectRestoredDeadline)
    {
        sourceRootRestored = sourceRootRestored || session.source_notifications_available();
        (void)session.take_change_batch();
        projectAssetRootRestored =
            session.project_changes().coverage() != ProjectCoverageState::RootUnavailable;
        if (!sourceRootRestored || !projectAssetRootRestored)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(sourceRootRestored && projectAssetRootRestored,
            "recreating the project root must restore source notifications and nested asset coverage");
#endif
}

void test_disjoint_roots_share_watcher_owner()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = temporary.path() / "external-assets";
    const auto source = project / "src";
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(assets);
    std::ofstream(project / "CMakeLists.txt") << "project(disjoint_watch)\n";

    ProjectSession session(project, assets);
    require(session.source_notifications_available() &&
                ProjectSessionTestProbe::watcher_owner_count(session) == 1,
            "disjoint source and asset roots must share one native watcher owner");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const auto beforeSourceWrite = session.source_change_revision();
    std::ofstream(source / "main.cpp") << "int main() { return 0; }\n";
    const auto sourceDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (session.source_change_revision() == beforeSourceWrite &&
           std::chrono::steady_clock::now() < sourceDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(session.source_change_revision() > beforeSourceWrite,
            "source changes remain visible through the shared disjoint-root owner");

    const auto scenePath = assets / "scene.json";
    std::ofstream(scenePath) << "[]\n";
    bool assetEventPublished = false;
    const auto assetDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!assetEventPublished && std::chrono::steady_clock::now() < assetDeadline)
    {
        const auto batch = session.take_change_batch();
        assetEventPublished = std::any_of(batch.events.begin(), batch.events.end(), [&](const auto &event) {
            return event.path == scenePath || event.oldPath == scenePath;
        });
        if (!assetEventPublished)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(assetEventPublished,
            "the shared watcher owner routes disjoint asset events to ProjectChanges");

    std::filesystem::remove_all(assets);
    bool assetUnavailable = false;
    const auto unavailableDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!assetUnavailable && std::chrono::steady_clock::now() < unavailableDeadline)
    {
        const auto batch = session.take_change_batch();
        assetUnavailable = batch.coverage == ProjectCoverageState::RootUnavailable;
        if (!assetUnavailable)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(assetUnavailable && session.source_notifications_available(),
            "loss of a disjoint asset root must not degrade source monitoring");

    const auto afterAssetLoss = session.source_change_revision();
    std::ofstream(source / "continued.cpp") << "int continued() { return 1; }\n";
    const auto continuedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (session.source_change_revision() == afterAssetLoss &&
           std::chrono::steady_clock::now() < continuedDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(session.source_change_revision() > afterAssetLoss,
            "source changes continue while the independent asset root is unavailable");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto afterContinuedSourceChange = session.source_change_revision();

    std::filesystem::create_directories(assets);
    bool assetRestored = false;
    const auto restoredDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!assetRestored && std::chrono::steady_clock::now() < restoredDeadline)
    {
        const auto batch = session.take_change_batch();
        assetRestored = batch.coverage != ProjectCoverageState::RootUnavailable;
        if (!assetRestored)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(assetRestored && session.source_notifications_available(),
            "recreated disjoint asset roots regain coverage without replacing the shared owner");
    require(session.source_change_revision() == afterContinuedSourceChange,
            "asset-root recovery must not invalidate source freshness identity");
}

void test_equal_roots_share_watcher_owner()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project-and-assets";
    const auto source = project / "src";
    std::filesystem::create_directories(source);
    std::ofstream(project / "CMakeLists.txt") << "project(shared_root_watch)\n";

    ProjectSession session(project, project);
    require(session.source_notifications_available() &&
                ProjectSessionTestProbe::watcher_owner_count(session) == 1,
            "equal source and asset roots must use one native watcher owner");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto beforeSourceWrite = session.source_change_revision();
    std::ofstream(source / "main.cpp") << "int main() { return 0; }\n";
    const auto sourceDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (session.source_change_revision() == beforeSourceWrite &&
           std::chrono::steady_clock::now() < sourceDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(session.source_change_revision() > beforeSourceWrite,
            "source files must remain freshness-relevant when both roots are equal");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto afterSourceWrite = session.source_change_revision();
    const auto assetPath = project / "content.png";
    std::ofstream(assetPath) << "texture bytes";
    bool assetChangePublished = false;
    const auto assetDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!assetChangePublished && std::chrono::steady_clock::now() < assetDeadline)
    {
        const auto batch = session.take_change_batch();
        assetChangePublished = std::any_of(batch.events.begin(), batch.events.end(), [&](const auto &event) {
            return event.path == assetPath || event.oldPath == assetPath ||
                   std::find(event.affectedPaths.begin(), event.affectedPaths.end(), assetPath) !=
                       event.affectedPaths.end();
        });
        if (!assetChangePublished)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    require(assetChangePublished && session.source_change_revision() == afterSourceWrite,
            "the shared equal-root owner must publish non-source asset changes without changing source freshness");
}

void test_linux_watcher_discovery_respects_directory_cap()
{
#if defined(__linux__)
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "wide-project";
    std::filesystem::create_directories(root);
    for (std::size_t index = 0; index <= ProjectChanges::MaxWatchedDirectories; ++index)
        std::filesystem::create_directories(root / ("directory-" + std::to_string(index)));

    auto watcher = make_project_watch_service();
    std::string error;
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<ProjectChangeEvent> events;
    const bool started = watcher->start(ProjectGeneration{71}, root,
        [&](ProjectChangeEvent event) {
            {
                std::lock_guard lock(mutex);
                events.push_back(std::move(event));
            }
            condition.notify_all();
        }, error);
    require(started && error.empty(),
            "watch startup must return while bounded recursive discovery runs on its worker");
    {
        std::unique_lock lock(mutex);
        require(condition.wait_for(lock, std::chrono::seconds(6), [&] {
                    return std::any_of(events.begin(), events.end(), [](const auto &event) {
                        return event.kind == ProjectChangeKind::NotificationsUnavailable;
                    });
                }), "background discovery must reject a fan-out larger than its bounded pending-directory queue");
    }
    watcher->stop();
    require(watcher->watched_directories() == 0,
            "stopping background discovery must cancel it and release partial native watches");

    auto cancelledWatcher = make_project_watch_service();
    std::atomic<std::size_t> callbackCount{0};
    require(cancelledWatcher->start(ProjectGeneration{72}, root,
                [&](ProjectChangeEvent) { callbackCount.fetch_add(1, std::memory_order_relaxed); }, error),
            "a second bounded watch discovery must start without enumerating on the caller thread");
    cancelledWatcher->stop();
    const auto stoppedCallbackCount = callbackCount.load(std::memory_order_acquire);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    require(cancelledWatcher->watched_directories() == 0 &&
                callbackCount.load(std::memory_order_acquire) == stoppedCallbackCount,
            "stop must cancel/join discovery and prevent callbacks after teardown");

    const auto firstRoot = temporary.path() / "first-project";
    const auto secondRoot = temporary.path() / "second-project";
    std::filesystem::create_directories(firstRoot);
    std::filesystem::create_directories(secondRoot);
    auto sharedBudget = std::make_shared<ProjectWatchBudget>(3);
    auto firstWatcher = make_project_watch_service();
    std::mutex firstMutex;
    std::condition_variable firstCondition;
    std::vector<ProjectChangeEvent> firstEvents;
    require(firstWatcher->start(ProjectGeneration{73}, firstRoot,
                [&](ProjectChangeEvent event) {
                    {
                        std::lock_guard lock(firstMutex);
                        firstEvents.push_back(std::move(event));
                    }
                    firstCondition.notify_all();
                }, error, ProjectWatchOptions{.budget = sharedBudget}),
            "the first project watcher must start with the shared directory budget");
    {
        std::unique_lock lock(firstMutex);
        require(firstCondition.wait_for(lock, std::chrono::seconds(3), [&] {
                    return std::any_of(firstEvents.begin(), firstEvents.end(), [](const auto &event) {
                        return event.kind == ProjectChangeKind::RootAvailable;
                    });
                }), "the first watcher must acquire its parent and root directory reservations");
    }
    require(sharedBudget->active() == 3 && firstWatcher->watched_directories() == 3,
            "the first watcher must account for its tree, parent, and recovery-ancestor registrations");

    auto secondWatcher = make_project_watch_service();
    std::mutex secondMutex;
    std::condition_variable secondCondition;
    std::vector<ProjectChangeEvent> secondEvents;
    require(secondWatcher->start(ProjectGeneration{74}, secondRoot,
                [&](ProjectChangeEvent event) {
                    {
                        std::lock_guard lock(secondMutex);
                        secondEvents.push_back(std::move(event));
                    }
                    secondCondition.notify_all();
                }, error, ProjectWatchOptions{.budget = sharedBudget}),
            "the second project watcher must start its bounded background discovery");
    {
        std::unique_lock lock(secondMutex);
        require(secondCondition.wait_for(lock, std::chrono::seconds(3), [&] {
                    return std::any_of(secondEvents.begin(), secondEvents.end(), [](const auto &event) {
                        return event.kind == ProjectChangeKind::NotificationsUnavailable;
                    });
                }), "the second watcher must report when the combined watch budget is exhausted");
    }
    require(sharedBudget->active() == 3 &&
                firstWatcher->watched_directories() + secondWatcher->watched_directories() <=
                    sharedBudget->limit(),
            "asset and source watcher instances must not exceed their shared project-wide budget");
    secondWatcher->stop();
    firstWatcher->stop();
    require(sharedBudget->active() == 0,
            "stopping both watcher owners must return every shared reservation");
#endif
}
} // namespace

int main()
{
    test_real_tree_events_and_root_replacement();
    test_linux_watcher_recovers_when_started_with_missing_root();
    test_linux_watcher_recovers_when_root_parent_is_replaced();
    test_linux_watcher_recovers_when_started_before_root_parent_exists();
    test_nested_root_parent_does_not_hide_project_root_changes();
    test_resource_aliases_versions_and_unresolved_paths();
    test_initially_broken_contained_alias_dependencies();
    test_initially_rejected_contained_alias_dependencies();
#if !defined(_WIN32)
    test_initially_unreadable_contained_alias_dependencies();
#endif
    test_directory_alias_non_directory_invalidation();
    test_transitive_alias_hop_invalidation();
    test_alias_parent_directory_invalidation();
    test_bounded_fallback_reference_checks();
    test_visible_audio_reference_fallback_demand();
    test_recovery_inventory_revalidates_equal_metadata_and_replacement();
    test_project_session_wakes_owner_on_native_completion();
    test_source_notifications_are_separate_from_asset_changes();
    test_disjoint_roots_share_watcher_owner();
    test_equal_roots_share_watcher_owner();
    test_healthy_inventory_deltas_do_not_refresh_unrelated_resources();
    test_linux_watcher_discovery_respects_directory_cap();
    test_native_change_reaches_project_listing_and_scene_preview();
    return 0;
}
