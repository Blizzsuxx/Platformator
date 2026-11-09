#include "projectchanges.h"

#include <algorithm>
#include <stdexcept>

using namespace platformator_editor;

namespace
{
void require_change(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

ProjectChangeEvent event(ProjectChangeKind kind, std::filesystem::path path,
                         bool directory = false, ProjectGeneration generation = ProjectGeneration{1})
{
    ProjectChangeEvent result;
    result.kind = kind;
    result.path = std::move(path);
    result.directory = directory;
    result.projectGeneration = generation;
    return result;
}

ProjectChangeBatch drain(ProjectChanges &changes)
{
    return changes.take_batch(256, std::chrono::steady_clock::now(), true);
}

bool contains_path(const std::vector<std::filesystem::path> &paths,
                  const std::filesystem::path &expected)
{
    return std::find(paths.begin(), paths.end(), expected) != paths.end();
}

void test_added_modified_snapshot_overlap_and_recreation()
{
    const auto root = std::filesystem::path("/tmp/project-changes-added-modified");
    ProjectChanges changes;
    changes.start(ProjectGeneration{1}, root);
    const auto added = root / "textures" / "tile.png";
    changes.submit(event(ProjectChangeKind::Added, added));
    const auto snapshot = changes.begin_root_snapshot();
    changes.submit(event(ProjectChangeKind::Modified, added));
    require_change(changes.complete_root_snapshot(snapshot, true),
                   "snapshot overlap fixture must complete");
    const auto replayed = drain(changes);
    require_change(replayed.events.size() == 1 && replayed.events.front().path == added &&
                       replayed.events.front().kind == ProjectChangeKind::Added,
                   "Added→Modified must refresh its sequence so a later write survives snapshot completion");

    ProjectChanges recreation;
    recreation.start(ProjectGeneration{2}, root);
    recreation.submit(event(ProjectChangeKind::Added, added, false, ProjectGeneration{2}));
    const auto recreateSnapshot = recreation.begin_root_snapshot();
    recreation.submit(event(ProjectChangeKind::Removed, added, false, ProjectGeneration{2}));
    recreation.submit(event(ProjectChangeKind::Added, added, false, ProjectGeneration{2}));
    require_change(recreation.complete_root_snapshot(recreateSnapshot, true),
                   "recreated-file snapshot must complete");
    const auto recreated = drain(recreation);
    require_change(recreated.events.size() == 1 && recreated.events.front().kind == ProjectChangeKind::Added &&
                       recreated.events.front().path == added &&
                       contains_path(recreated.events.front().affectedPaths, added),
                   "add/remove/recreate must retain the final addition after the snapshot boundary");
}

void test_project_change_sequences_survive_coalescing()
{
    const auto root = std::filesystem::path("/tmp/project-changes-sequences");
    const auto image = root / "tile.png";
    ProjectChanges changes;
    changes.start(ProjectGeneration{7}, root);

    auto first = event(ProjectChangeKind::Modified, image, false, ProjectGeneration{7});
    first.affectedPaths = {image, root / "alias" / "tile.png"};
    changes.submit(std::move(first));
    const auto firstBatch = drain(changes);
    require_change(firstBatch.events.size() == 1 && firstBatch.events.front().sequence != 0 &&
                       firstBatch.events.front().affectedPaths.size() == 2,
                   "one submitted event must retain one identity across all affected alias paths");

    changes.submit(event(ProjectChangeKind::Modified, image, false, ProjectGeneration{7}));
    changes.submit(event(ProjectChangeKind::Modified, image, false, ProjectGeneration{7}));
    const auto coalesced = drain(changes);
    require_change(coalesced.events.size() == 1 &&
                       coalesced.events.front().sequence > firstBatch.events.front().sequence,
                   "a later coalesced write must supersede the earlier event identity");
}

void test_retry_schedule_keeps_deadline_until_failure()
{
    using Clock = RecoveryRetrySchedule::Clock;
    const auto start = Clock::time_point{} + std::chrono::seconds(50);
    RecoveryRetrySchedule schedule;
    schedule.request_now(start);
    require_change(schedule.due(start), "a newly requested recovery must be immediately eligible");
    const auto firstDeadline = schedule.retry_after_failure(start);
    require_change(firstDeadline == start + std::chrono::milliseconds(200) && schedule.failures() == 1,
                   "the first persistent recovery failure must schedule a bounded delayed retry");
    for (int frame = 1; frame <= 100; ++frame)
        require_change(!schedule.due(start + std::chrono::milliseconds(frame)) &&
                           schedule.deadline() == firstDeadline && schedule.failures() == 1,
                       "dirty frames must not reset the retry deadline or create scan storms");
    const auto secondDeadline = schedule.retry_after_failure(firstDeadline);
    require_change(secondDeadline == firstDeadline + std::chrono::milliseconds(400) &&
                       schedule.failures() == 2,
                   "repeated failures must apply exponential backoff");
    auto nextFailure = secondDeadline;
    auto priorDeadline = nextFailure;
    for (int failure = 3; failure <= 9; ++failure)
    {
        priorDeadline = nextFailure;
        nextFailure = schedule.retry_after_failure(nextFailure);
    }
    require_change(schedule.failures() == 9 &&
                       nextFailure - priorDeadline == std::chrono::seconds(30),
                   "recovery retries must remain bounded at the configured thirty-second ceiling");
    schedule.schedule_success(nextFailure, std::chrono::seconds(60));
    require_change(schedule.failures() == 0 && !schedule.due(nextFailure) &&
                       schedule.due(nextFailure + std::chrono::seconds(60)),
                   "successful recovery must clear backoff and honor the healthy scan interval");
}

void test_complete_inventory_reconciliation_and_replay()
{
    const auto root = std::filesystem::path("/tmp/project-changes-inventory");
    const auto first = root / "first.png";
    const auto second = root / "second.png";
    const auto third = root / "third.png";
    const std::vector<std::pair<std::filesystem::path, std::string>> initial{{first, "4:10"}};
    ProjectChanges changes;
    changes.start(ProjectGeneration{1}, root);
    auto snapshot = changes.begin_root_snapshot();
    require_change(snapshot.recoveryRequired &&
                       changes.complete_root_snapshot(snapshot, initial, true) &&
                       changes.coverage() == ProjectCoverageState::Reconciling,
                   "the first complete inventory must publish a reconciliation barrier before Current");
    auto batch = drain(changes);
    require_change(batch.events.size() == 1 && batch.events.front().directory &&
                       batch.events.front().path == root && changes.coverage() == ProjectCoverageState::Current,
                   "startup reconciliation must invalidate the accepted root after complete coverage");

    snapshot = changes.begin_root_snapshot();
    changes.submit(event(ProjectChangeKind::Added, third));
    const std::vector<std::pair<std::filesystem::path, std::string>> expanded{
        {first, "4:10"}, {second, "6:20"}};
    require_change(changes.complete_root_snapshot(snapshot, expanded, true),
                   "a changed complete inventory must be accepted");
    batch = changes.take_batch(1, std::chrono::steady_clock::now(), true);
    require_change(batch.events.size() == 1 && batch.events.front().path == third &&
                       batch.events.front().kind == ProjectChangeKind::Added &&
                       changes.coverage() == ProjectCoverageState::Reconciling,
                   "in-flight notifications must replay before the inventory barrier can declare Current");
    batch = changes.take_batch(1, std::chrono::steady_clock::now(), true);
    require_change(batch.events.size() == 1 && batch.events.front().path == second &&
                       batch.events.front().kind == ProjectChangeKind::Added &&
                       changes.coverage() == ProjectCoverageState::Current,
                   "healthy reconciliation must publish the changed file after replay without invalidating the root");

    snapshot = changes.begin_root_snapshot();
    require_change(!snapshot.recoveryRequired && changes.complete_root_snapshot(snapshot, expanded, true) &&
                       drain(changes).events.empty(),
                   "an unchanged healthy consistency scan must not trigger a subtree reload");

    const auto emptyDirectory = root / "empty";
    snapshot = changes.begin_root_snapshot();
    const std::vector<std::pair<std::filesystem::path, std::string>> withEmptyDirectory{
        {first, "4:10"}, {second, "6:20"}, {emptyDirectory, "d:" + emptyDirectory.generic_string()}};
    require_change(changes.complete_root_snapshot(snapshot, withEmptyDirectory, true),
                   "a complete inventory must retain empty directory identities");
    batch = drain(changes);
    require_change(batch.events.size() == 1 && batch.events.front().path == emptyDirectory &&
                       batch.events.front().kind == ProjectChangeKind::Added && batch.events.front().directory,
                   "a missed empty-folder addition must produce a directory-scoped delta");

    snapshot = changes.begin_root_snapshot();
    const std::vector<std::pair<std::filesystem::path, std::string>> removed{{second, "6:20"}};
    require_change(changes.complete_root_snapshot(snapshot, removed, true),
                   "a complete inventory that misses prior files and folders must be accepted");
    batch = drain(changes);
    require_change(batch.events.size() == 2 &&
                       std::any_of(batch.events.begin(), batch.events.end(), [&](const ProjectChangeEvent &change) {
                           return change.path == first && change.kind == ProjectChangeKind::Removed && !change.directory;
                       }) &&
                       std::any_of(batch.events.begin(), batch.events.end(), [&](const ProjectChangeEvent &change) {
                           return change.path == emptyDirectory && change.kind == ProjectChangeKind::Removed &&
                                  change.directory;
                       }),
                   "a healthy scan must repair missed file/folder deletions with exact path scopes");

    snapshot = changes.begin_root_snapshot();
    const std::vector<std::pair<std::filesystem::path, std::string>> partial{{third, "3:30"}};
    require_change(!changes.complete_root_snapshot(snapshot, partial, false) &&
                       changes.coverage() == ProjectCoverageState::Degraded,
                   "an incomplete scan must not adopt its partial prefix");
    snapshot = changes.begin_root_snapshot();
    require_change(snapshot.recoveryRequired && changes.complete_root_snapshot(snapshot, removed, true),
                   "the last accepted inventory must remain recoverable after a partial scan");
    drain(changes);
    snapshot = changes.begin_root_snapshot();
    require_change(changes.complete_root_snapshot(snapshot, partial, true),
                   "a later complete scan must compare against the previous complete baseline");
    batch = drain(changes);
    require_change(batch.events.size() == 2 &&
                       std::any_of(batch.events.begin(), batch.events.end(), [&](const ProjectChangeEvent &change) {
                           return change.path == third && change.kind == ProjectChangeKind::Added;
                       }) &&
                       std::any_of(batch.events.begin(), batch.events.end(), [&](const ProjectChangeEvent &change) {
                           return change.path == second && change.kind == ProjectChangeKind::Removed;
                       }),
                   "a later complete scan must detect changes omitted by a failed prefix without broad invalidation");

    const auto staleToken = changes.begin_root_snapshot();
    const auto replacementRoot = std::filesystem::path("/tmp/project-changes-inventory-replaced");
    changes.start(ProjectGeneration{2}, replacementRoot);
    require_change(!changes.complete_root_snapshot(staleToken, partial, true),
                   "a previous root generation must not adopt inventory into its replacement");
    const auto replacementToken = changes.begin_root_snapshot();
    require_change(replacementToken.recoveryRequired &&
                       changes.complete_root_snapshot(replacementToken,
                           {{replacementRoot / "first.png", "4:10"}}, true),
                   "a replaced root must build an independent complete inventory");
    batch = drain(changes);
    require_change(batch.events.size() == 1 &&
                       batch.events.front().projectGeneration == ProjectGeneration{2} &&
                       batch.events.front().path == replacementRoot,
                   "root replacement must publish reconciliation under its new project identity");
}

void test_directory_listing_revision_rejects_stale_subtree_results()
{
    const auto root = std::filesystem::path("/tmp/project-changes-directory-revisions");
    ProjectChanges changes;
    changes.start(ProjectGeneration{1}, root);
    const auto initialSnapshot = changes.begin_root_snapshot();
    require_change(changes.complete_root_snapshot(initialSnapshot, true),
                   "directory listing revision fixture must start current");
    const auto directory = root / "nested";
    const auto childDirectory = directory / "child";
    const auto oldListing = changes.begin_directory_listing(childDirectory);
    changes.submit(event(ProjectChangeKind::Modified, directory, true));
    require_change(!changes.directory_listing_current(oldListing),
                   "a directory invalidation must reject in-flight listings below that subtree");
    const auto newListing = changes.begin_directory_listing(childDirectory);
    require_change(changes.directory_listing_current(newListing) &&
                       !changes.complete_directory_listing(oldListing, true) &&
                       changes.complete_directory_listing(newListing, true),
                   "only the newest request at the current invalidation revision may be accepted");

    const auto beforeOverflow = changes.begin_directory_listing(childDirectory);
    changes.submit(event(ProjectChangeKind::Overflow, {}, false));
    require_change(!changes.directory_listing_current(beforeOverflow) &&
                       !changes.complete_directory_listing(beforeOverflow, true),
                   "root uncertainty must invalidate every active targeted listing token");
    const auto retry = changes.begin_directory_listing(childDirectory);
    require_change(changes.directory_listing_current(retry) &&
                       !changes.complete_directory_listing(retry, false) &&
                       changes.coverage() == ProjectCoverageState::Degraded,
                   "an incomplete targeted scan must report degraded coverage without accepting a prefix");
}

void test_quiet_window_and_maximum_batch_delay()
{
    ProjectChanges changes;
    const auto root = std::filesystem::path("/tmp/project-changes-batching");
    changes.start(ProjectGeneration{1}, root);
    const auto snapshot = changes.begin_root_snapshot();
    require_change(changes.complete_root_snapshot(snapshot, true),
                   "batch timing must start from complete coverage");
    const auto epoch = std::chrono::steady_clock::time_point{} + std::chrono::seconds(10);
    changes.submit(event(ProjectChangeKind::Added, root / "first.png"), epoch);
    require_change(changes.next_batch_deadline() == epoch + std::chrono::milliseconds(200),
                   "the event inbox must expose the next quiet-window deadline to the outer wait loop");
    require_change(changes.take_batch(256, epoch + std::chrono::milliseconds(199)).events.empty(),
                   "a normal delta batch must wait for the 200 ms quiet window");
    require_change(changes.take_batch(256, epoch + std::chrono::milliseconds(200)).events.size() == 1,
                   "a quiet batch must be available after 200 ms");

    changes.submit(event(ProjectChangeKind::Added, root / "second.png"), epoch);
    changes.submit(event(ProjectChangeKind::Added, root / "third.png"),
                   epoch + std::chrono::milliseconds(900));
    require_change(changes.next_batch_deadline() == epoch + std::chrono::seconds(1),
                   "the exposed event deadline must honor the one-second maximum batch age");
    require_change(changes.take_batch(256, epoch + std::chrono::milliseconds(1000)).events.size() == 2,
                   "continuous edits must flush by the one-second maximum batch age");
}

void test_startup_replay_and_request_order()
{
    ProjectChanges changes;
    const auto root = std::filesystem::path("/tmp/project-changes-startup");
    changes.start(ProjectGeneration{1}, root);
    require_change(changes.coverage() == ProjectCoverageState::Starting,
                   "starting a project must begin with explicit Starting coverage");

    changes.submit(event(ProjectChangeKind::Modified, root / "before-snapshot.png"));
    const auto snapshot = changes.begin_root_snapshot();
    changes.submit(event(ProjectChangeKind::Added, root / "during-snapshot.png"));
    require_change(changes.complete_root_snapshot(snapshot, true) &&
                       changes.coverage() == ProjectCoverageState::Current,
                   "a complete latest snapshot must become Current");
    const auto replayed = drain(changes);
    require_change(replayed.events.size() == 1 &&
                       replayed.events[0].path == root / "during-snapshot.png",
                   "events after snapshot start must replay after the initial listing completes");

    const auto directory = root / "textures";
    const auto oldListing = changes.begin_directory_listing(directory);
    changes.submit(event(ProjectChangeKind::Modified, directory / "tile.png"));
    const auto newListing = changes.begin_directory_listing(directory);
    require_change(!changes.complete_directory_listing(oldListing, true) &&
                       changes.complete_directory_listing(newListing, true),
                   "old listing completions must lose to the newest request and invalidation revision");

    const auto staleSnapshot = changes.begin_root_snapshot();
    const auto newestSnapshot = changes.begin_root_snapshot();
    require_change(!changes.complete_root_snapshot(staleSnapshot, true) &&
                       changes.complete_root_snapshot(newestSnapshot, true),
                   "an older root scan must never replace the newest complete snapshot");
}

void test_rename_directory_moves_and_bounds()
{
    ProjectChanges changes;
    const auto root = std::filesystem::path("/tmp/project-changes-moves");
    changes.start(ProjectGeneration{1}, root);
    const auto snapshot = changes.begin_root_snapshot();
    require_change(changes.complete_root_snapshot(snapshot, true), "test root must reconcile");

    ProjectChangeEvent rename = event(ProjectChangeKind::Renamed, root / "new" / "tile.png");
    rename.oldPath = root / "old" / "tile.png";
    changes.submit(rename);
    changes.submit(event(ProjectChangeKind::Modified, root / "new" / "tile.png"));
    const auto renamed = drain(changes);
    require_change(renamed.events.size() == 1 && renamed.events[0].kind == ProjectChangeKind::Renamed &&
                       renamed.events[0].oldPath == root / "old" / "tile.png" &&
                       renamed.events[0].path == root / "new" / "tile.png" &&
                       contains_path(renamed.events[0].affectedPaths, root / "old" / "tile.png") &&
                       contains_path(renamed.events[0].affectedPaths, root / "new" / "tile.png"),
                   "Rename→Modify must retain both paths for consumers and parent listings");
    require_change(changes.directory_revision(root / "old") > 0 &&
                       changes.directory_revision(root / "new") > 0,
                   "Rename→Modify must invalidate both containing directories");

    ProjectChangeEvent movedDirectory = event(ProjectChangeKind::Renamed,
        root / "populated", true);
    movedDirectory.oldPath = root / "incoming";
    changes.submit(std::move(movedDirectory));
    require_change(changes.directory_revision(root) > 0 &&
                       changes.directory_revision(root / "populated") > 0,
                   "directory moves must invalidate their containing listing and subtree revision");
    drain(changes);

    ProjectChanges renameChain;
    renameChain.start(ProjectGeneration{3}, root);
    const auto chainSnapshot = renameChain.begin_root_snapshot();
    require_change(renameChain.complete_root_snapshot(chainSnapshot, true),
                   "rename-chain fixture must begin with complete coverage");
    const auto first = root / "first" / "tile.png";
    const auto middle = root / "middle" / "tile.png";
    const auto final = root / "final" / "tile.png";
    ProjectChangeEvent firstRename = event(ProjectChangeKind::Renamed, middle, false, ProjectGeneration{3});
    firstRename.oldPath = first;
    renameChain.submit(firstRename);
    ProjectChangeEvent secondRename = event(ProjectChangeKind::Renamed, final, false, ProjectGeneration{3});
    secondRename.oldPath = middle;
    renameChain.submit(secondRename);
    const auto chained = drain(renameChain);
    require_change(chained.events.size() == 1 && chained.events.front().kind == ProjectChangeKind::Renamed &&
                       chained.events.front().oldPath == first && chained.events.front().path == final &&
                       contains_path(chained.events.front().affectedPaths, first) &&
                       contains_path(chained.events.front().affectedPaths, middle) &&
                       contains_path(chained.events.front().affectedPaths, final),
                   "cross-directory rename chains must retain their original, intermediate and final paths");
    require_change(renameChain.directory_revision(first.parent_path()) > 0 &&
                       renameChain.directory_revision(middle.parent_path()) > 0 &&
                       renameChain.directory_revision(final.parent_path()) > 0,
                   "cross-directory rename chains must invalidate every affected parent listing");

    changes.submit(event(ProjectChangeKind::Added, root / "same.png"));
    changes.submit(event(ProjectChangeKind::Removed, root / "same.png"));
    require_change(changes.queued_paths() == 0,
                   "an add followed by removal before commit must coalesce away");

    ProjectChanges bounded(1, ProjectChanges::MaxQueuedBytes);
    bounded.start(ProjectGeneration{2}, root);
    bounded.submit(event(ProjectChangeKind::Added, root / "a.png", false, ProjectGeneration{2}));
    bounded.submit(event(ProjectChangeKind::Added, root / "b.png", false, ProjectGeneration{2}));
    require_change(bounded.coverage() == ProjectCoverageState::Degraded && bounded.queued_paths() == 0,
                   "queue path overflow must mark the root dirty instead of dropping events silently");
}

void test_overflow_retry_failure_and_stale_generation()
{
    ProjectChanges changes;
    const auto root = std::filesystem::path("/tmp/project-changes-overflow");
    changes.start(ProjectGeneration{7}, root);
    auto snapshot = changes.begin_root_snapshot();
    require_change(changes.complete_root_snapshot(snapshot, true), "test root must reconcile before overflow");
    const auto firstEpoch = changes.uncertainty_epoch();

    changes.submit(event(ProjectChangeKind::Modified, root / "same-metadata.png", false,
                         ProjectGeneration{7}));
    changes.submit(event(ProjectChangeKind::Modified, root / "same-metadata.png", false,
                         ProjectGeneration{7}));
    const auto sameMetadata = drain(changes);
    require_change(sameMetadata.events.size() == 1 &&
                       sameMetadata.events[0].kind == ProjectChangeKind::Modified,
                   "repeated writes with unchanged metadata must remain an invalidation event");

    changes.submit(event(ProjectChangeKind::Modified, root / "pending.png", false,
                         ProjectGeneration{7}));
    changes.submit(event(ProjectChangeKind::Overflow, {}, false, ProjectGeneration{7}));
    const auto overflow = drain(changes);
    require_change(overflow.rootDirty && overflow.events.empty() &&
                       overflow.coverage == ProjectCoverageState::Degraded &&
                       overflow.uncertaintyEpoch > firstEpoch,
                   "native overflow must discard incomplete deltas and require root reconciliation");
    snapshot = changes.begin_root_snapshot();
    require_change(!changes.complete_root_snapshot(snapshot, false) &&
                       changes.coverage() == ProjectCoverageState::Degraded,
                   "a failed retry must not promote incomplete state to Current");
    changes.report_retry_failure(root / "textures");
    changes.report_retry_failure(root / "textures");
    require_change(changes.retry_failures(root / "textures") == 2,
                   "failed targeted retries must remain observable for bounded backoff");

    changes.start(ProjectGeneration{8}, root);
    changes.submit(event(ProjectChangeKind::Added, root / "stale.png", false, ProjectGeneration{7}));
    require_change(changes.queued_paths() == 0 && changes.project_generation() == ProjectGeneration{8},
                   "old project-generation events must be rejected after project replacement");
    changes.submit(event(ProjectChangeKind::RootUnavailable, root, true, ProjectGeneration{8}));
    require_change(changes.coverage() == ProjectCoverageState::RootUnavailable &&
                       drain(changes).rootDirty,
                   "root replacement or disappearance must remain explicitly unavailable until reconciled");
    changes.set_notifications_available(false);
    changes.submit(event(ProjectChangeKind::RootAvailable, root, true, ProjectGeneration{8}));
    changes.submit(event(ProjectChangeKind::Overflow, root, true, ProjectGeneration{8}));
    snapshot = changes.begin_root_snapshot();
    require_change(changes.complete_root_snapshot(snapshot, true) &&
                       changes.coverage() == ProjectCoverageState::Current,
                   "a rearmed root monitor must regain Current only after a complete reconciliation");
    changes.stop();
    require_change(changes.coverage() == ProjectCoverageState::Stopped,
                   "stopped project watchers must not report Current coverage");

    ProjectChanges pollingFallback;
    pollingFallback.start(ProjectGeneration{9}, root);
    pollingFallback.set_notifications_available(false);
    const auto fallbackSnapshot = pollingFallback.begin_root_snapshot();
    require_change(pollingFallback.complete_root_snapshot(fallbackSnapshot, true) &&
                       pollingFallback.coverage() == ProjectCoverageState::Degraded &&
                       !drain(pollingFallback).rootDirty,
                   "a complete fallback snapshot must remain visibly degraded without claiming active notification coverage");
}
} // namespace

int main()
{
    test_startup_replay_and_request_order();
    test_added_modified_snapshot_overlap_and_recreation();
    test_project_change_sequences_survive_coalescing();
    test_retry_schedule_keeps_deadline_until_failure();
    test_complete_inventory_reconciliation_and_replay();
    test_directory_listing_revision_rejects_stale_subtree_results();
    test_rename_directory_moves_and_bounds();
    test_overflow_retry_failure_and_stale_generation();
    test_quiet_window_and_maximum_batch_delay();
    return 0;
}
