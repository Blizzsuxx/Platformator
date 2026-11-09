#include "test_support.h"

#include "document.h"
#include "editorwork.h"
#include "projectchanges.h"
#include "validation.h"
#include "workspace.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace
{
void run_validation_rejects_stale_navigation_and_deleted_targets()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);

    auto oldDocument = make_default_document(project, assets, 1);
    oldDocument.projectGeneration = 9;
    oldDocument.tabId = TabId{3};
    oldDocument.root[0]["components"].push_back(component(
        41, 3, Json{{"colliderType", 1}, {"width", -1.0}, {"height", 16.0},
                    {"offset", {{"x", 0.0}, {"y", 0.0}}}, {"trigger", false},
                    {"collisionGroup", 1}, {"collisionMask", 1}}));
    ++oldDocument.structureRevision;
    rebuild_document_indexes(oldDocument);

    auto currentDocument = oldDocument;
    currentDocument.documentGeneration = 2;
    currentDocument.tabId = TabId{4};
    currentDocument.root[0]["components"].erase(currentDocument.root[0]["components"].begin() + 1);
    ++currentDocument.structureRevision;
    ++currentDocument.valueRevision;
    rebuild_document_indexes(currentDocument);

    ValidationService service;
    const auto staleRequest = service.request(oldDocument);
    const auto currentRequest = service.request(currentDocument);
    require(staleRequest != 0 && currentRequest > staleRequest,
            "validation requests must receive increasing nonzero identities");
    require(!service.take_current(oldDocument),
            "a result for the old tab must not be adopted after navigation changes the latest request");

    std::optional<ValidationCompletion> completion;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!completion && std::chrono::steady_clock::now() < deadline)
    {
        completion = service.take_current(currentDocument);
        if (!completion)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(completion.has_value(), "latest validation must settle within three seconds");
    require(completion->requestId == currentRequest && completion->document.tabId == currentDocument.tabId &&
                completion->document.documentGeneration ==
                    DocumentGeneration{currentDocument.documentGeneration} &&
                completion->structureRevision == currentDocument.structureRevision &&
                completion->valueRevision == currentDocument.valueRevision,
            "published validation must carry the exact latest document and revision identity");
    require(!service.pending() && service.is_current(currentDocument),
            "the latest validation result must become current after publication");
    require(std::none_of(completion->result.issues.begin(), completion->result.issues.end(),
                         [](const ValidationIssue &issue) { return issue.componentId == 41; }),
            "a deleted component target from the stale result must not survive the newer validation");
}

void run_validation_snapshot_limits_are_explicit()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    auto document = make_default_document(project, assets, 1);
    for (int index = 0; index < 100'001; ++index)
        document.indexes.objectsById.emplace(index, ObjectLocation{});

    ValidationService service;
    const auto request = service.request(document);
    const auto completion = service.take_current(document);
    require(request != 0 && completion && completion->result.blocksRun &&
                !completion->result.issues.empty() &&
                completion->result.issues.front().message.find("bounded validation snapshot limits") !=
                    std::string::npos,
            "over-limit validation requests must fail with an explicit blocking diagnostic");
}

void run_asset_invalidation_revalidates_unchanged_document_revisions()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    const auto image = assets / "sprite.png";
    {
        std::ofstream stream(image, std::ios::binary);
        stream << "placeholder";
    }
    auto document = make_default_document(project, assets, 1);
    document.root[0]["components"].push_back(component(
        51, 6, Json{{"textureFilePath", "assets/sprite.png"}, {"flip", 0},
                    {"width", 16.0}, {"height", 16.0}, {"sourceRectEnabled", false}}));
    ++document.structureRevision;
    rebuild_document_indexes(document);

    ValidationService service;
    service.request(document);
    const auto wait_for_current = [&]() -> std::optional<ValidationCompletion> {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        std::optional<ValidationCompletion> result;
        while (!result && std::chrono::steady_clock::now() < deadline)
        {
            result = service.take_current(document);
            if (!result)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return result;
    };
    const auto before = wait_for_current();
    require(before && std::none_of(before->result.issues.begin(), before->result.issues.end(),
                                   [](const ValidationIssue &issue) {
                                       return issue.componentId == 51 &&
                                              issue.fieldPath == "textureFilePath";
                                   }),
            "an existing referenced asset must pass the first validation revision");

    std::filesystem::remove(image);
    service.request_changed(document, {0}); // Asset changes do not advance authored revisions.
    const auto after = wait_for_current();
    require(after && std::any_of(after->result.issues.begin(), after->result.issues.end(),
                                 [](const ValidationIssue &issue) {
                                     return issue.componentId == 51 &&
                                            issue.fieldPath == "textureFilePath" && issue.blocksRun;
                                 }),
            "an asset invalidation must schedule a fresh blocking result without a scene edit");
}

void run_local_validation_merges_only_changed_object_results()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    const auto existingImage = assets / "present.png";
    {
        std::ofstream stream(existingImage, std::ios::binary);
        stream << "placeholder";
    }

    auto document = make_default_document(project, assets, 1);
    document.root[0]["children"].push_back(object(71, "Local target"));
    document.root[0]["children"][0]["components"].push_back(component(
        72, 6, Json{{"textureFilePath", "assets/present.png"}, {"flip", 0},
                    {"width", 16.0}, {"height", 16.0}, {"sourceRectEnabled", false}}));
    document.root.push_back(object(81, "Unrelated invalid target"));
    document.root[1]["components"].push_back(component(
        82, 6, Json{{"textureFilePath", "assets/missing.png"}, {"flip", 0},
                    {"width", 16.0}, {"height", 16.0}, {"sourceRectEnabled", false}}));
    ++document.structureRevision;
    rebuild_document_indexes(document);

    ValidationService service;
    service.request(document);
    const auto wait_for_current = [&](const DocumentSession &current) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        std::optional<ValidationCompletion> result;
        while (!result && std::chrono::steady_clock::now() < deadline)
        {
            result = service.take_current(current);
            if (!result)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(result.has_value(), "targeted validation must settle within three seconds");
        return std::move(*result);
    };
    const auto initial = wait_for_current(document);
    require(std::any_of(initial.result.issues.begin(), initial.result.issues.end(),
                        [](const ValidationIssue &issue) {
                            return issue.objectId == 81 && issue.componentId == 82 &&
                                   issue.fieldPath == "textureFilePath";
                        }),
            "the full baseline must retain the unrelated missing asset issue");

    document.root[0]["children"][0]["scale"]["x"] = 0.0;
    ++document.valueRevision;
    service.request_changed(document, {71});
    const auto changed = wait_for_current(document);
    require(std::any_of(changed.result.issues.begin(), changed.result.issues.end(),
                        [](const ValidationIssue &issue) {
                            return issue.objectId == 71 && issue.fieldPath == "scale.x" && issue.blocksRun;
                        }),
            "a local edit must publish new issues for its affected nested object");
    require(std::any_of(changed.result.issues.begin(), changed.result.issues.end(),
                        [](const ValidationIssue &issue) {
                            return issue.objectId == 81 && issue.componentId == 82 &&
                                   issue.fieldPath == "textureFilePath";
                        }),
            "local publication must preserve issues for unaffected objects");

    document.root[0]["children"][0]["scale"]["x"] = 1.0;
    ++document.valueRevision;
    service.request_changed(document, {71, 71});
    const auto repaired = wait_for_current(document);
    require(std::none_of(repaired.result.issues.begin(), repaired.result.issues.end(),
                         [](const ValidationIssue &issue) {
                             return issue.objectId == 71 && issue.fieldPath == "scale.x";
                         }) &&
                std::any_of(repaired.result.issues.begin(), repaired.result.issues.end(),
                            [](const ValidationIssue &issue) {
                                return issue.objectId == 81 && issue.componentId == 82 &&
                                       issue.fieldPath == "textureFilePath";
                            }),
            "revalidating one object must remove its repaired issue and retain unrelated issues");
}

void run_f6_and_run_barriers_require_latest_current_validation()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    auto document = make_default_document(project, assets, 1);
    document.root[0]["scale"]["x"] = 0.0;
    ++document.valueRevision;

    ValidationService service;
    ValidationResult result;
    service.request(document);
    const auto wait_for_current = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        std::optional<ValidationCompletion> completion;
        while (!completion && std::chrono::steady_clock::now() < deadline)
        {
            completion = service.take_current(document);
            if (!completion)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(completion.has_value(), "F6/Run validation barrier must settle within three seconds");
        result = std::move(completion->result);
    };

    require(!validation_allows_run(service, document, result),
            "Run must remain blocked while validation for the current revision is pending");
    wait_for_current();
    require(!validation_allows_run(service, document, result),
            "a current blocking issue must prevent Run after the barrier settles");

    document.root[0]["scale"]["x"] = 1.0;
    ++document.valueRevision;
    service.request_changed(document, {0});
    require(!validation_allows_run(service, document, result),
            "an old blocking result must not authorize Run after a newer edit");
    wait_for_current();
    require(validation_allows_run(service, document, result),
            "Run may proceed only after the latest valid revision has completed");

    // F6 explicitly invalidates the published revision before queuing a fresh full
    // validation, even when no authored revision changed.
    service.cancel();
    require(!validation_allows_run(service, document, result),
            "explicit F6 revalidation must withhold Run authorization until completion");
    service.request(document);
    require(!validation_allows_run(service, document, result) || service.is_current(document),
            "the F6 barrier must be pending or already complete, never backed by a cancelled result");
    wait_for_current();
    require(validation_allows_run(service, document, result),
            "a completed explicit F6 validation must restore Run authorization for valid data");
}

void run_authorization_requires_the_adopted_validation_completion()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    auto document = make_default_document(project, assets, 1);
    ValidationService service;
    const auto wait_for_publication = [&](const DocumentSession &current) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while ((!service.is_current(current) || service.pending()) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(service.is_current(current) && !service.pending(),
                "validation publication must settle within three seconds");
    };
    const auto take_current = [&](const DocumentSession &current) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        std::optional<ValidationCompletion> completion;
        while (!completion && std::chrono::steady_clock::now() < deadline)
        {
            completion = service.take_current(current);
            if (!completion)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(completion.has_value(), "the exact published validation must be adoptable");
        return std::move(*completion);
    };

    service.request(document);
    const auto initial = take_current(document);
    require(validation_allows_run(service, document, initial.result),
            "an adopted valid completion may authorize Run");

    document.root[0]["scale"]["x"] = 0.0;
    ++document.valueRevision;
    service.request_changed(document, {0});
    wait_for_publication(document); // Deliberately leave ready unconsumed for this frame boundary.
    require(!validation_allows_run(service, document, initial.result),
            "publication before UI consumption must not authorize with the previous valid result");
    const auto blocking = take_current(document);
    require(!validation_allows_run(service, document, blocking.result),
            "the adopted current blocking result must prevent Run");

    document.root[0]["scale"]["x"] = 1.0;
    ++document.valueRevision;
    service.request_changed(document, {0});
    wait_for_publication(document);
    require(!validation_allows_run(service, document, blocking.result),
            "a stale blocking result must not stand in for a newer valid publication");
    const auto repaired = take_current(document);
    require(validation_allows_run(service, document, repaired.result),
            "adopting the newer valid completion must restore Run authorization");

    const auto previousRequestId = repaired.result.requestId;
    service.request_changed(document, {0}); // Asset-only changes retain authored revisions.
    wait_for_publication(document);
    require(!validation_allows_run(service, document, repaired.result),
            "asset-only validation must replace the authorization identity at unchanged revisions");
    const auto assetRevalidated = take_current(document);
    require(assetRevalidated.result.requestId > previousRequestId &&
                validation_allows_run(service, document, assetRevalidated.result),
            "the exact completed asset-only validation may authorize Run");

    auto replacement = document;
    ++replacement.documentGeneration;
    service.request(replacement);
    require(!validation_allows_run(service, replacement, assetRevalidated.result),
            "a result from the replaced tab generation must not authorize Run");
    const auto replacementResult = take_current(replacement);
    require(validation_allows_run(service, replacement, replacementResult.result),
            "the replacement tab may Run after its own completion is adopted");
}

void run_root_recovery_validation_is_revision_driven()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    auto document = make_default_document(project, assets, 1);
    document.projectGeneration = 41;
    document.tabId = TabId{9};

    ProjectChanges changes;
    changes.start(ProjectGeneration{41}, project);
    const auto firstBatch = changes.take_batch(256, std::chrono::steady_clock::now(), true);
    const auto repeatedBatch = changes.take_batch(256, std::chrono::steady_clock::now(), true);
    require(firstBatch.rootDirty && repeatedBatch.rootDirty &&
                firstBatch.uncertaintyEpoch == repeatedBatch.uncertaintyEpoch,
            "unchanged recovery coverage must retain one uncertainty epoch across owner frames");

    ValidationService service;
    ValidationRecoveryGate gate;
    require(gate.should_request_full(firstBatch.rootDirty, firstBatch.projectGeneration,
                                    firstBatch.uncertaintyEpoch),
            "a new root uncertainty revision must request one full validation");
    const auto firstRequest = service.request(document, true);
    for (int frame = 0; frame < 200; ++frame)
        require(!gate.should_request_full(repeatedBatch.rootDirty, repeatedBatch.projectGeneration,
                                         repeatedBatch.uncertaintyEpoch),
                "persistent dirty coverage must not schedule another full request every frame");
    require(!gate.should_request_full(false, ProjectGeneration{41}, firstBatch.uncertaintyEpoch),
            "a clean coverage frame must not schedule recovery validation");

    std::optional<ValidationCompletion> firstCompletion;
    const auto firstDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!firstCompletion && std::chrono::steady_clock::now() < firstDeadline)
    {
        firstCompletion = service.take_current(document);
        if (!firstCompletion)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(firstCompletion && firstCompletion->requestId == firstRequest &&
                service.is_current(document),
            "repeated degraded-coverage frames must allow the original recovery result to publish");

    ProjectChangeEvent overflow;
    overflow.kind = ProjectChangeKind::Overflow;
    overflow.projectGeneration = ProjectGeneration{41};
    changes.submit(std::move(overflow));
    const auto nextBatch = changes.take_batch(256, std::chrono::steady_clock::now(), true);
    require(nextBatch.rootDirty && nextBatch.uncertaintyEpoch > firstBatch.uncertaintyEpoch &&
                gate.should_request_full(nextBatch.rootDirty, nextBatch.projectGeneration,
                                         nextBatch.uncertaintyEpoch),
            "a later uncertainty epoch must request a fresh validation");
    const auto secondRequest = service.request(document, true);
    std::optional<ValidationCompletion> secondCompletion;
    const auto secondDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!secondCompletion && std::chrono::steady_clock::now() < secondDeadline)
    {
        secondCompletion = service.take_current(document);
        if (!secondCompletion)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(secondCompletion && secondCompletion->requestId == secondRequest &&
                secondRequest > firstRequest,
            "each new uncertainty epoch must replace the prior authorization identity once");
    require(gate.should_request_full(true, ProjectGeneration{42}, nextBatch.uncertaintyEpoch),
            "a project replacement must establish its own recovery-validation revision");
}

void run_hidden_audio_validation_dependencies_recover_from_events_and_fallback()
{
    work::reset();
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    const auto audioPath = assets / "audio" / "music.wav";
    std::filesystem::create_directories(audioPath.parent_path());
    std::ofstream(audioPath, std::ios::binary) << "audio fixture";

    auto document = make_default_document(project, assets, 1);
    document.projectGeneration = 8;
    document.tabId = TabId{22};
    document.root = Json::array({object(70, "Hidden audio owner", Json::array({
        component(71, 1, Json{{"filePath", "assets/audio/music.wav"}, {"gain", 1.0},
                              {"loopCount", 0}, {"autoPlay", false}})}))});
    ++document.structureRevision;
    rebuild_document_indexes(document);
    const Json authoredBefore = document.root;
    const auto structureBefore = document.structureRevision;
    const auto valueBefore = document.valueRevision;

    ValidationService service;
    const auto await_current = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        std::optional<ValidationCompletion> completion;
        while (std::chrono::steady_clock::now() < deadline)
        {
            completion = service.take_current(document);
            if (completion)
                return std::move(*completion);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("hidden Audio validation did not settle within three seconds");
    };
    const auto has_audio_error = [](const ValidationResult &result) {
        return std::any_of(result.issues.begin(), result.issues.end(), [](const ValidationIssue &issue) {
            return issue.objectId == 70 && issue.componentId == 71 &&
                   issue.fieldPath == "filePath" && issue.blocksRun;
        });
    };

    service.request(document);
    const auto initial = await_current();
    require(validation_allows_run(service, document, initial.result) &&
                initial.result.assetDependencies.size() == 1 &&
                initial.result.assetDependencies.front().kind == ResourceKind::Audio &&
                initial.result.assetDependencies.front().logicalPath == audioPath,
            "validation must retain an unselected Audio dependency without decoding its file");

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{document.projectGeneration}, assets);
    const auto dependency = initial.result.assetDependencies.front();
    const DocumentIdentity identity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                    DocumentGeneration{document.documentGeneration}};
    ResourceConsumer consumer{identity, dependency.objectId, dependency.componentId,
                              "validation.asset"};
    resources.register_consumer(dependency.logicalPath, dependency.kind, consumer);
    const auto event_object_ids = [&] {
        return validation_object_ids_for_resource_consumers(
            resources.affected_consumers(audioPath), identity);
    };
    require(event_object_ids() == std::vector<int>{70},
            "hidden Audio validation dependencies must route native changes to their owner object");

    const auto revalidate_after_native_event = [&] {
        const auto changed = event_object_ids();
        require(changed == std::vector<int>{70},
                "a native or alias change must retain the hidden Audio validation consumer");
        resources.invalidate(audioPath);
        resources.resolve(audioPath, ResourceKind::Audio);
        service.request_changed(document, changed);
        return await_current();
    };
    require(std::filesystem::remove(audioPath), "the Audio fixture must be removable");
    auto missing = revalidate_after_native_event();
    require(has_audio_error(missing.result) && !validation_allows_run(service, document, missing.result),
            "deleting a hidden Audio dependency must publish a current Run-blocking diagnostic");

    std::ofstream(audioPath, std::ios::binary) << "restored audio fixture";
    auto restored = revalidate_after_native_event();
    require(!has_audio_error(restored.result) && validation_allows_run(service, document, restored.result),
            "restoring a hidden Audio dependency must clear the current Run blocker");

    require(resources.inspect_fallback_paths(1).empty(),
            "degraded fallback must establish a stable baseline for the hidden Audio dependency");
    require(std::filesystem::remove(audioPath), "the Audio fixture must be removable for fallback coverage");
    const auto fallback = resources.inspect_fallback_paths(1);
    require(fallback.size() == 1 && fallback.front().logicalPath == audioPath,
            "degraded fallback must detect a hidden Audio dependency that changed without a native event");
    const auto fallbackObjects = validation_object_ids_for_resource_consumers(
        fallback.front().consumers, identity);
    service.request_changed(document, fallbackObjects);
    missing = await_current();
    require(fallbackObjects == std::vector<int>{70} && has_audio_error(missing.result) &&
                !validation_allows_run(service, document, missing.result),
            "fallback-dispatched Audio deletion must refresh diagnostics and block Run");

    std::ofstream(audioPath, std::ios::binary) << "fallback restoration";
    const auto restoredFallback = resources.inspect_fallback_paths(1);
    require(restoredFallback.size() == 1,
            "degraded fallback must detect hidden Audio restoration");
    const auto restoredObjects = validation_object_ids_for_resource_consumers(
        restoredFallback.front().consumers, identity);
    service.request_changed(document, restoredObjects);
    restored = await_current();
    require(restoredObjects == std::vector<int>{70} && !has_audio_error(restored.result) &&
                validation_allows_run(service, document, restored.result),
            "fallback-dispatched Audio restoration must recover Run authorization");

    require(document.root == authoredBefore && document.structureRevision == structureBefore &&
                document.valueRevision == valueBefore,
            "external dependency changes must leave authored data and its history revisions untouched");
    document.root[0]["components"][0]["filePath"] = "assets/audio/alternate.wav";
    ++document.valueRevision;
    service.request_changed(document, {70});
    const auto edited = await_current();
    require(edited.incremental && edited.refreshedObjectIds == std::vector<int>{70} &&
                edited.result.assetDependencies.size() == 1 &&
                edited.result.assetDependencies.front().logicalPath == assets / "audio" / "alternate.wav",
            "local validation must replace only the edited object's dependency paths");

    Json expectedEdited = authoredBefore;
    expectedEdited[0]["components"][0]["filePath"] = "assets/audio/alternate.wav";
    const auto work = work::snapshot();
    require(document.root == expectedEdited && document.structureRevision == structureBefore &&
                document.valueRevision == valueBefore + 1 && work[static_cast<std::size_t>(
                    work::Counter::ImageDecodes)] == 0 &&
                work[static_cast<std::size_t>(work::Counter::TextureUploads)] == 0,
            "dependency validation must not edit authored data, change history, decode audio, or reload unrelated textures");
}

void run_deleted_field_issue_is_removed_from_local_publication()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    std::filesystem::create_directories(assets);
    auto document = make_default_document(project, assets, 1);
    document.root[0]["components"].push_back(component(
        91, 6, Json{{"textureFilePath", ""}, {"flip", 0}, {"width", 16.0},
                    {"height", 16.0}, {"sourceRectEnabled", true},
                    {"sourceRect", {{"x", "invalid"}, {"y", 0.0}, {"w", 16.0}, {"h", 16.0}}}}));
    ++document.structureRevision;
    rebuild_document_indexes(document);

    ValidationService service;
    service.request(document);
    const auto wait_for_current = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        std::optional<ValidationCompletion> completion;
        while (!completion && std::chrono::steady_clock::now() < deadline)
        {
            completion = service.take_current(document);
            if (!completion)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(completion.has_value(), "deleted-field validation must settle within three seconds");
        return std::move(completion->result);
    };
    const auto initial = wait_for_current();
    require(std::any_of(initial.issues.begin(), initial.issues.end(),
                        [](const ValidationIssue &issue) {
                            return issue.componentId == 91 && issue.fieldPath == "sourceRect.x";
                        }),
            "the baseline must publish the malformed enabled source-rectangle field");

    document.root[0]["components"][1]["sourceRectEnabled"] = false;
    document.root[0]["components"][1].erase("sourceRect");
    ++document.valueRevision;
    service.request_changed(document, {0});
    const auto repaired = wait_for_current();
    require(std::none_of(repaired.issues.begin(), repaired.issues.end(),
                         [](const ValidationIssue &issue) {
                             return issue.componentId == 91 && issue.fieldPath.starts_with("sourceRect");
                         }),
            "a deleted field no longer consulted by the document must not retain a stale issue");
}

void run_external_scene_identity_requires_a_run_decision()
{
    TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    const auto externalScenes = temporary.path() / "outside assets";
    std::filesystem::create_directories(assets);
    std::filesystem::create_directories(externalScenes);
    const auto path = externalScenes / "external.scene";
    write_json(path, Json::array({object(61, "Loaded copy")}));

    DocumentWorkspace workspace(project, assets);
    const auto opened = workspace.open_document(path, UnsavedDecision::Discard);
    require(opened && workspace.source_file_status().state == SourceFileState::Current,
            "a saved scene outside the asset root must load with a current identity");
    workspace.document_for_edit().root[0]["name"] = "Local work";
    ++workspace.document_for_edit().valueRevision;
    write_json(path, Json::array({object(61, "External writer changed this scene")}));
    const auto changed = workspace.source_file_status();
    require(changed.state == SourceFileState::Changed && !source_file_allows_run(changed),
            "an unreviewed source-file change must hold Run even when authored revisions are unchanged");

    const Json localDraft = workspace.document().root;
    const auto conflict = workspace.save();
    require(conflict.status == WorkspaceActionStatus::Failed && workspace.dirty() &&
                workspace.document().root == localDraft &&
                source_file_allows_run(changed, true, changed.diskIdentity),
            "Keep Work must preserve a dirty draft and allow the user-reviewed version to continue Run");
    const auto reload = workspace.reload_source_file();
    require(reload && !workspace.dirty() &&
                workspace.document().root[0]["name"] == "External writer changed this scene" &&
                source_file_allows_run(workspace.source_file_status()),
            "Reload must adopt the disk version and clear the conflict before Run continues");
}
} // namespace

int main()
{
    try
    {
        run_validation_rejects_stale_navigation_and_deleted_targets();
        run_validation_snapshot_limits_are_explicit();
        run_asset_invalidation_revalidates_unchanged_document_revisions();
        run_local_validation_merges_only_changed_object_results();
        run_f6_and_run_barriers_require_latest_current_validation();
        run_authorization_requires_the_adopted_validation_completion();
        run_root_recovery_validation_is_revision_driven();
        run_hidden_audio_validation_dependencies_recover_from_events_and_fallback();
        run_deleted_field_issue_is_removed_from_local_publication();
        run_external_scene_identity_requires_a_run_decision();
        std::cout << "diagnostics tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "diagnostics tests failed: " << error.what() << '\n';
        return 1;
    }
}
