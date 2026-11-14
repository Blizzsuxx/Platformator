#include "audiopreview.h"
#include "layersettings.h"
#include "test_support.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace platformator_editor;
using platformator_editor_test::require;

namespace platformator_editor
{
struct AudioPreviewServiceTestProbe
{
    static bool warm_queued(const AudioPreviewService &service)
    {
        return service.warm_queued_for_testing();
    }
};
} // namespace platformator_editor

namespace
{
struct ScopedTempDir
{
    std::filesystem::path dir;
    explicit ScopedTempDir(const char *name)
    {
        dir = std::filesystem::temp_directory_path() /
            (std::string("platformator_") + name + "_" + std::to_string(std::rand()));
        std::filesystem::create_directories(dir);
    }
    ~ScopedTempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    const std::filesystem::path &path() const noexcept { return dir; }
};

void write_minimal_wav(const std::filesystem::path &path, int sampleRate = 22050, int numSamples = 2205)
{
    std::ofstream out(path, std::ios::binary);
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(numSamples * 2);
    const std::uint32_t riffSize = 36 + dataBytes;
    const std::uint16_t audioFormat = 1;
    const std::uint16_t channels = 1;
    const std::uint32_t byteRate = static_cast<std::uint32_t>(sampleRate * 2);
    const std::uint16_t blockAlign = 2;
    const std::uint16_t bitsPerSample = 16;
    const std::uint32_t fmtSize = 16;

    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char *>(&riffSize), 4);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    out.write(reinterpret_cast<const char *>(&fmtSize), 4);
    out.write(reinterpret_cast<const char *>(&audioFormat), 2);
    out.write(reinterpret_cast<const char *>(&channels), 2);
    out.write(reinterpret_cast<const char *>(&sampleRate), 4);
    out.write(reinterpret_cast<const char *>(&byteRate), 4);
    out.write(reinterpret_cast<const char *>(&blockAlign), 2);
    out.write(reinterpret_cast<const char *>(&bitsPerSample), 2);
    out.write("data", 4);
    out.write(reinterpret_cast<const char *>(&dataBytes), 4);
    std::vector<std::uint8_t> pcm(dataBytes, 0);
    out.write(reinterpret_cast<const char *>(pcm.data()), static_cast<std::streamsize>(pcm.size()));
}

bool wait_until_ready(AudioPreviewService &service, std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (service.state() == AudioPlaybackState::Loading &&
           std::chrono::steady_clock::now() < deadline)
    {
        service.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    service.poll();
    return service.state() != AudioPlaybackState::Loading;
}

void test_audio_preview_lifecycle()
{
    ScopedTempDir temp("audiopreview_test");
    const auto wavA = temp.path() / "a.wav";
    const auto wavB = temp.path() / "b.wav";
    const auto txt = temp.path() / "note.txt";
    write_minimal_wav(wavA);
    write_minimal_wav(wavB);
    {
        std::ofstream out(txt);
        out << "not audio";
    }

    require(is_supported_audio_extension(wavA.extension().string()), ".wav must be supported audio");
    require(is_supported_audio_extension(".OGG"), ".OGG must be supported audio case-insensitively");
    require(!is_supported_audio_extension(txt.extension().string()), ".txt must not be supported audio");

    AudioPreviewService service;
    require(service.state() == AudioPlaybackState::Stopped && !service.active_path().has_value(),
            "Fresh AudioPreviewService must not be playing");

    std::atomic<int> wakeCount = 0;
    service.set_wake_callback([&] { ++wakeCount; });
    service.warm();
    require(AudioPreviewServiceTestProbe::warm_queued(service),
            "warmup must record its queued state until it is superseded or completed");
    require(service.toggle_or_play(wavA) &&
                !AudioPreviewServiceTestProbe::warm_queued(service),
            "a playback request must clear the queued warmup state when superseding it");
    service.stop();
    service.warm();
    const auto warmDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (wakeCount.load() == 0 && std::chrono::steady_clock::now() < warmDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(wakeCount.load() > 0,
            "mixer warmup completion must wake the editor instead of waiting for its idle timeout");
    service.poll();

    // Non-audio file must fail cleanly without crashing.
    require(!service.toggle_or_play(txt), "Unsupported extension must return false");

    // Play A -> activePath == wavA (under dummy audio driver, mixer initializes cleanly).
    require(service.toggle_or_play(wavA), "wavA preview request must be accepted");
    require(service.toggle_or_play(wavA), "toggling the same loading clip must cancel its request");
    require(service.state() == AudioPlaybackState::Stopped && !service.active_path().has_value(),
            "toggling a loading clip must stop it without waiting for worker completion");
    require(service.toggle_or_play(wavA), "wavA preview request must be accepted after cancellation");
    require(wait_until_ready(service), "wavA preview setup must finish or report a bounded failure");
    const bool playedA = service.state() == AudioPlaybackState::Playing;
    if (playedA)
    {
        require(service.state() == AudioPlaybackState::Playing && service.is_playing(wavA),
                "is_playing must match played wavA");
        // Play B -> switches cleanly to B.
        require(service.toggle_or_play(wavB), "Switching to wavB must be accepted");
        require(wait_until_ready(service), "wavB preview setup must finish or report a bounded failure");
        require(service.state() == AudioPlaybackState::Playing,
                "wavB preview must play after its asynchronous setup completes");
        require(service.is_playing(wavB) && !service.is_playing(wavA),
                "active_path must switch to wavB");
        // Toggle B again -> stops playback cleanly.
        service.toggle_or_play(wavB);
        require(service.state() == AudioPlaybackState::Stopped && !service.active_path().has_value(),
                "Toggling same active clip must stop playback");
    }
    service.poll();
    service.shutdown();
    require(service.state() == AudioPlaybackState::Stopped, "Shutdown must leave service stopped");
}

void test_audio_warm_failure_resets_queue()
{
    SDL_Environment *environment = SDL_GetEnvironment();
    const char *oldDriver = SDL_GetEnvironmentVariable(environment, "SDL_AUDIODRIVER");
    const std::optional<std::string> savedDriver = oldDriver != nullptr
        ? std::optional<std::string>(oldDriver) : std::nullopt;
    require(SDL_SetEnvironmentVariable(environment, "SDL_AUDIODRIVER",
                                       "platformator-invalid-audio-driver", true),
            "test must configure an unavailable audio driver");

    AudioPreviewService service;
    std::atomic<int> wakeCount = 0;
    service.set_wake_callback([&] { ++wakeCount; });
    service.warm();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (wakeCount.load() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    service.poll();
    require(wakeCount.load() > 0 && !service.last_error().empty() &&
                !AudioPreviewServiceTestProbe::warm_queued(service),
            "a failed mixer warmup must publish its error and clear warmQueued");

    const bool restored = savedDriver
        ? SDL_SetEnvironmentVariable(environment, "SDL_AUDIODRIVER", savedDriver->c_str(), true)
        : SDL_UnsetEnvironmentVariable(environment, "SDL_AUDIODRIVER");
    require(restored, "test must restore the prior audio-driver setting");
    service.warm();
    require(AudioPreviewServiceTestProbe::warm_queued(service),
            "warmup must be retryable after a failed initialization");
    service.shutdown();
}

void test_layer_settings_roundtrip()
{
    ScopedTempDir temp("layersettings_test");
    LayerSettings settings = default_layer_settings();
    std::string error;

    require(!set_layer_name(settings, 0, "Custom", error), "Layer 0 name must be locked");
    require(set_layer_name(settings, 1, "  Player  ", error), "Layer 1 name must trim and succeed");
    require(settings.names[1] == "Player", "Trimmed layer name must be stored");
    require(!set_layer_name(settings, 2, "Player", error), "Duplicate layer name must fail");

    set_layer_collision(settings, 1, 2, false);
    require(!can_layers_collide(settings, 1, 2) && !can_layers_collide(settings, 2, 1),
            "Layer collision matrix must be symmetric");

    const auto saveResult = save_layer_settings(temp.path(), settings);
    require(saveResult.succeeded, "Saving layers.json must succeed");
    const auto loadResult = load_layer_settings(temp.path());
    require(loadResult.present && loadResult.valid, "Loading layers.json must succeed");
    require(loadResult.settings.names[1] == "Player", "Loaded layer 1 name must persist");
    require(!can_layers_collide(loadResult.settings, 1, 2), "Loaded collision matrix must persist");

    require(layer_to_collision_group(1) == 2ULL &&
                collision_group_to_layer(2ULL) == std::optional<std::uint8_t>{1} &&
                !collision_group_to_layer(3ULL).has_value(),
            "Layer index and single-bit collisionGroup conversion must round-trip cleanly");
    const std::uint64_t mask1 = layer_to_collision_mask(loadResult.settings, 1);
    require((mask1 & (std::uint64_t{1} << 2)) == 0 && (mask1 & (std::uint64_t{1} << 1)) != 0,
            "Layer mask helper must mirror symmetric collision matrix bits");

    const auto recoverySaved = save_layer_sync_recovery(temp.path(), loadResult.settings,
        "A closed asset still needs collider mask synchronization.");
    const auto recoveryLoaded = load_layer_sync_recovery(temp.path());
    require(recoverySaved.succeeded && recoveryLoaded.pending && recoveryLoaded.valid &&
                recoveryLoaded.settings == loadResult.settings &&
                recoveryLoaded.message.find("closed asset") != std::string::npos,
            "layer collider synchronization recovery must persist settings and an actionable reason");
    require(clear_layer_sync_recovery(temp.path()).succeeded &&
                !load_layer_sync_recovery(temp.path()).pending,
            "a completed layer synchronization must clear its durable recovery marker");
    const auto projectA = temp.path() / "project-a";
    const auto projectB = temp.path() / "project-b";
    LayerSettings settingsB = settings;
    require(set_layer_name(settingsB, 3, "Enemies", error),
            "the second project can keep an independent layer draft");
    require(save_layer_sync_recovery(projectA, settings, "Project A retry").succeeded &&
                save_layer_sync_recovery(projectB, settingsB, "Project B retry").succeeded &&
                load_layer_sync_recovery(projectA).settings == settings &&
                load_layer_sync_recovery(projectB).settings == settingsB,
            "durable layer synchronization markers must remain isolated per project across switches");

    const auto draftProject = temp.path() / "draft-project";
    LayerSettings previouslySaved = settings;
    require(set_layer_name(previouslySaved, 4, "PreviouslySaved", error) &&
                save_layer_settings(draftProject, previouslySaved).succeeded &&
                save_layer_sync_recovery(draftProject, previouslySaved,
                                         "An earlier committed synchronization is pending.").succeeded,
            "layer draft recovery fixture must begin with persisted settings and committed sync work");
    LayerSettings discardedDraft = previouslySaved;
    require(set_layer_name(discardedDraft, 5, "DraftOnly", error),
            "draft-only layer name must be valid");
    set_layer_collision(discardedDraft, 4, 5, false);
    require(prepare_layer_sync_recovery(draftProject, discardedDraft,
                                        "Prepared before replacing layers.json.").succeeded,
            "a draft can be prepared without replacing the committed recovery settings");
    const auto persistedPath = layer_settings_path(draftProject);
    const auto backupPath = draftProject / ".platformator" / "layers.saved.json";
    std::filesystem::rename(persistedPath, backupPath);
    std::filesystem::create_directory(persistedPath);
    const auto failedReplacement = save_layer_settings(draftProject, discardedDraft);
    require(!failedReplacement.succeeded,
            "a directory obstruction must make layers.json replacement fail for the regression case");
    require(discard_prepared_layer_sync_recovery(draftProject).succeeded,
            "Discard must clear only the uncommitted prepared record");
    std::filesystem::remove(persistedPath);
    std::filesystem::rename(backupPath, persistedPath);
    const auto afterDiscard = load_layer_sync_recovery(draftProject);
    const auto afterDiscardSettings = load_layer_settings(draftProject);
    require(afterDiscard.pending && afterDiscard.valid &&
                afterDiscard.settings == previouslySaved && afterDiscardSettings.valid &&
                afterDiscardSettings.settings == previouslySaved &&
                afterDiscardSettings.settings.names[5].empty() &&
                can_layers_collide(afterDiscardSettings.settings, 4, 5),
            "Discard and restart must retain only previously saved settings and committed sync work");
    const auto repairedAssets = draftProject / "assets" / "scenes";
    std::filesystem::create_directories(repairedAssets);
    const auto repairedScene = repairedAssets / "retry.scene";
    {
        std::ofstream sceneFile(repairedScene, std::ios::binary);
        sceneFile << nlohmann::json::array({nlohmann::json{{"id", 1}, {"components",
            nlohmann::json::array({nlohmann::json{{"id", 2}, {"type", 3},
                {"collisionGroup", layer_to_collision_group(4)}, {"collisionMask", 0}}})}}}).dump(2)
                  << '\n';
    }
    const auto restartedRetry = sync_project_files_with_layer_settings(
        draftProject / "assets", afterDiscard.settings);
    const auto retryCleared = restartedRetry.succeeded()
        ? clear_layer_sync_recovery(draftProject) : LayerSettingsSaveResult{};
    require(restartedRetry.succeeded() && restartedRetry.updatedFiles == 1 &&
                retryCleared.succeeded &&
                !load_layer_sync_recovery(draftProject).pending &&
                load_layer_settings(draftProject).settings == previouslySaved,
            "after destination repair, a restarted retry must synchronize only the committed saved settings; "
            "updated=" + std::to_string(restartedRetry.updatedFiles) + " failures=" +
                std::to_string(restartedRetry.failures.size()) +
                (restartedRetry.failures.empty() ? std::string{} :
                    " first=" + restartedRetry.failures.front().path.string() + ":" +
                        restartedRetry.failures.front().message));

    require(prepare_layer_sync_recovery(draftProject, discardedDraft,
                                            "Crash window after settings replacement.").succeeded &&
                save_layer_settings(draftProject, discardedDraft).succeeded,
            "the crash-window fixture must persist settings after preparing recovery");
    const auto afterRestart = load_layer_sync_recovery(draftProject);
    require(afterRestart.pending && afterRestart.valid && afterRestart.settings == discardedDraft,
            "restart must promote a prepared recovery record only after matching settings were persisted");

    nlohmann::json invalidLayers{{"version", 1}, {"layers", settings.names},
                                 {"collisionMatrix", settings.collisionMatrix}};
    invalidLayers["layers"][1] = "  dEfAuLt  ";
    {
        std::ofstream output(layer_settings_path(temp.path()), std::ios::binary | std::ios::trunc);
        output << invalidLayers.dump(2) << '\n';
    }
    const auto duplicateDefault = load_layer_settings(temp.path());
    require(duplicateDefault.present && !duplicateDefault.valid &&
                duplicateDefault.message.find("slots 0 and 1") != std::string::npos &&
                duplicateDefault.settings.names[0] == DefaultLayerName &&
                duplicateDefault.settings.names[1].empty(),
            "loaded custom layer names must reject trimmed, case-insensitive duplicates of reserved slot 0");

    nlohmann::json scene = nlohmann::json::array({
        nlohmann::json{{"id", 1}, {"components", nlohmann::json::array({
            nlohmann::json{{"id", 2}, {"type", 3}, {"collisionGroup", 2ULL}, {"collisionMask", 0xFFFFFFFFULL}}
        })}}
    });
    require(sync_colliders_with_layer_settings(scene, loadResult.settings) &&
                scene[0]["components"][0]["collisionMask"].get<std::uint64_t>() == mask1,
            "sync_colliders_with_layer_settings must update single-layer collider masks");
}

bool run_timing_capture(const std::optional<std::filesystem::path> &fixtureDirectory = std::nullopt)
{
    constexpr int WarmupRepetitions = 3;
    constexpr int MeasuredRepetitions = 5;
    constexpr double MaximumAllowedMilliseconds = 250.0;
    ScopedTempDir temp("audiopreview_timing");
    const auto root = fixtureDirectory.value_or(temp.path());
    if (!fixtureDirectory)
        write_minimal_wav(root / "wav.wav", 22050, 22050 * 2);
    const std::vector<std::pair<std::string, std::filesystem::path>> formats{
        {"wav", root / "wav.wav"}, {"ogg", root / "ogg.ogg"},
        {"mp3", root / "mp3.mp3"}, {"flac", root / "flac.flac"}};

    std::cout << "format,stage,sample,submission_ms,first_mixer_output_ms,submission_succeeded,mixer_output_delivered\n";
    bool allSucceeded = true;
    double maximumMilliseconds = 0.0;
    const auto measure = [&](std::string_view format, std::string_view stage, int sample,
                             const std::filesystem::path &audioPath,
                             AudioPreviewService &service, bool record) {
        const auto started = std::chrono::steady_clock::now();
        const bool succeeded = service.toggle_or_play(audioPath);
        const auto submitted = std::chrono::steady_clock::now();
        const double submissionMs = std::chrono::duration<double, std::milli>(submitted - started).count();
        bool outputDelivered = false;
        while (succeeded && service.mixer_output_position_frames() <= 0 &&
               std::chrono::steady_clock::now() - started <
                   std::chrono::milliseconds(static_cast<int>(MaximumAllowedMilliseconds)))
        {
            service.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto outputTime = std::chrono::steady_clock::now();
        outputDelivered = succeeded && service.mixer_output_position_frames() > 0;
        const double outputMs = outputDelivered
            ? std::chrono::duration<double, std::milli>(outputTime - started).count() : 0.0;
        maximumMilliseconds = std::max(maximumMilliseconds, submissionMs);
        allSucceeded = allSucceeded && succeeded && submissionMs <= MaximumAllowedMilliseconds &&
                       outputDelivered && outputMs <= MaximumAllowedMilliseconds;
        if (record)
            std::cout << format << ',' << stage << ',' << sample << ',' << submissionMs << ','
                      << (outputDelivered ? std::to_string(outputMs) : std::string{}) << ','
                      << (succeeded ? "true" : "false") << ','
                      << (outputDelivered ? "true" : "false") << '\n';
        if (!succeeded)
            std::cerr << format << ' ' << stage << " sample " << sample
                      << " failed: " << service.last_error() << '\n';
        else if (!outputDelivered)
            std::cerr << format << ' ' << stage << " sample " << sample
                      << " submitted but produced no mixer output position within the limit\n";
        service.stop();
    };

    for (const auto &[format, audioPath] : formats)
    {
        if (!std::filesystem::is_regular_file(audioPath))
        {
            allSucceeded = false;
            std::cerr << "missing " << format << " timing fixture: " << audioPath.string() << '\n';
            continue;
        }
        for (int sample = 0; sample < MeasuredRepetitions; ++sample)
        {
            AudioPreviewService coldService;
            measure(format, "cold", sample + 1, audioPath, coldService, true);
        }

        AudioPreviewService warmService;
        for (int sample = 0; sample < WarmupRepetitions; ++sample)
        {
            measure(format, "warmup", sample + 1, audioPath, warmService, false);
        }
        for (int sample = 0; sample < MeasuredRepetitions; ++sample)
            measure(format, "warm", sample + 1, audioPath, warmService, true);
        warmService.shutdown();
    }

    std::cerr << "max synchronous play submission: " << maximumMilliseconds
              << " ms (limit " << MaximumAllowedMilliseconds << " ms)\n";
    return allSucceeded;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--timing-capture")
        return run_timing_capture() ? 0 : 2;
    if (argc == 3 && std::string_view(argv[1]) == "--timing-capture")
        return run_timing_capture(std::filesystem::path(argv[2])) ? 0 : 2;
    if (argc != 1)
    {
        std::cerr << "usage: platformator_editor_audiopreview_test [--timing-capture [fixture-directory]]\n";
        return 2;
    }
    test_audio_warm_failure_resets_queue();
    test_audio_preview_lifecycle();
    test_layer_settings_roundtrip();
    std::cout << "audiopreview_test passed\n";
    return 0;
}
