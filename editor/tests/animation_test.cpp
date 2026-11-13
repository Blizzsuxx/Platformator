#include "animationeditor.h"
#include "assetdocument.h"
#include "document.h"
#include "documentmanager.h"
#include "preview.h"
#include "panels/animation.h"
#include "panels/inspector.h"
#include "panels/project.h"
#include "resourceindex.h"
#include "test_support.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3_image/SDL_image.h>
#include <platformator/runtime.h>
#include <platformator/runtimeoptions.h>
#include "platformator/animationclip.h"
#include "platformator/animator.h"
#include "platformator/sprite.h"
#include "platformator/gameobject.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>


using namespace platformator_editor;

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class AnimationAddFrameGui
{
  public:
    AnimationAddFrameGui()
    {
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {900.0f, 900.0f};
        io.DeltaTime = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        io.Fonts->Build();
    }

    ~AnimationAddFrameGui() { ImGui::DestroyContext(); }

    ImGuiID frame(AnimationPanel &panel, AssetDocument &document,
                  const std::filesystem::path &assetRoot,
                  ImVec2 mouse = {-10.0f, -10.0f}, bool leftDown = false,
                  ImGuiID *addFrameId = nullptr, const char *buttonLabel = "Add Frame",
                  bool *frameListDrawn = nullptr)
    {
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, leftDown);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({800.0f, 860.0f}, ImGuiCond_Always);
        ImGui::Begin("animation-add-frame-test", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
        ImGui::BeginDisabled(false);
        panel.draw(document, assetRoot);
        ImGui::EndDisabled();
        if (frameListDrawn != nullptr)
        {
            *frameListDrawn = false;
            for (const ImGuiWindow *window : ImGui::GetCurrentContext()->Windows)
                if (std::string_view(window->Name).find("##frame-list") != std::string_view::npos &&
                    window->LastFrameActive == ImGui::GetFrameCount())
                    *frameListDrawn = true;
        }
        if (addFrameId != nullptr)
            *addFrameId = ImGui::GetID(buttonLabel);
        ImGui::End();
        ImGui::Render();
        return ImGui::GetHoveredID();
    }

    ImVec2 find_add_frame(AnimationPanel &panel, AssetDocument &document,
                          const std::filesystem::path &assetRoot, const char *buttonLabel = "Add Frame")
    {
        ImGuiID button = 0;
        frame(panel, document, assetRoot, {-10.0f, -10.0f}, false, &button, buttonLabel);
        for (float y = 0.0f; y <= 700.0f; y += 2.0f)
            for (const float x : {55.0f, 150.0f, 220.0f, 340.0f})
                if (frame(panel, document, assetRoot, {x, y}) == button)
                    return {x, y};
        throw std::runtime_error(std::string("could not locate animation button: ") + buttonLabel);
    }

    void click_add_frame(AnimationPanel &panel, AssetDocument &document,
                         const std::filesystem::path &assetRoot)
    {
        const ImVec2 point = find_add_frame(panel, document, assetRoot);
        frame(panel, document, assetRoot, point, true);
        frame(panel, document, assetRoot, point, false);
    }
};

void test_animation_toggles_keep_panel_visible()
{
    platformator_editor_test::TemporaryDirectory temporary;
    AnimationAddFrameGui gui;
    Json clip{{"frames", Json::array({make_default_animation_frame()})},
              {"loop", true}, {"framesPerSecond", 4.0}, {"width", 16.0}, {"height", 16.0}};
    AssetDocument document(clip);
    AnimationPanel panel;
    const auto toggle = [&](const char *label) {
        const ImVec2 point = gui.find_add_frame(panel, document, temporary.path(), label);
        gui.frame(panel, document, temporary.path(), point, true);
        bool frameListDrawn = false;
        gui.frame(panel, document, temporary.path(), point, false, nullptr, label, &frameListDrawn);
        require(frameListDrawn,
                "animation checkbox commits must still draw the frame list on the click frame");
    };
    toggle("Use Source Rectangle");
    require(document.root()["frames"][0]["hasSourceRect"] == true,
            "the source rectangle checkbox must enable the selected frame region");
    toggle("Use Source Rectangle");
    require(document.root()["frames"][0]["hasSourceRect"] == false,
            "the source rectangle checkbox must disable the selected frame region");
    toggle("Loop");
    require(document.root()["loop"] == false,
            "Loop must toggle without dropping the rest of the panel");
}

void write_runtime_texture(const std::filesystem::path &path)
{
    SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
    require(surface != nullptr, "runtime animation test must create a frame texture");
    const bool saved = SDL_FillSurfaceRect(surface, nullptr,
        SDL_MapSurfaceRGBA(surface, 180, 80, 30, 255)) &&
        IMG_SavePNG(surface, path.string().c_str());
    SDL_DestroySurface(surface);
    require(saved, "runtime animation test must save a valid frame texture");
}

Json runtime_default_frame(std::string texture, double duration)
{
    Json frame = make_default_animation_frame();
    frame["duration"] = duration;
    frame["textureWrapperFilePath"] = std::move(texture);
    return frame;
}

void test_shared_frame_duration_rules()
{
    require(is_valid_animation_frame_duration(0.25),
            "a positive finite frame duration is valid");
    require(!is_valid_animation_frame_duration(0.0) &&
                !is_valid_animation_frame_duration(-0.1) &&
                !is_valid_animation_frame_duration(std::numeric_limits<double>::infinity()) &&
                !is_valid_animation_frame_duration(std::numeric_limits<double>::quiet_NaN()),
            "zero, negative, and nonfinite frame durations are invalid");
}

void test_clip_document_validation_and_serialization()
{
    nlohmann::json valid = {
        {"frames", nlohmann::json::array({
            {{"duration", 0.25}, {"textureWrapperFilePath", "walk.png"},
             {"hasSourceRect", false}, {"sourceRect", {{"x", 0.0}, {"y", 0.0},
                                                            {"w", 0.0}, {"h", 0.0}}},
             {"vendorFrameField", 8}},
            {{"duration", 0.25}, {"textureWrapperFilePath", "run.png"},
             {"hasSourceRect", false}, {"sourceRect", {{"x", 0.0}, {"y", 0.0},
                                                            {"w", 0.0}, {"h", 0.0}}}}})},
        {"loop", true}, {"filePath", ""},
        {"unknownClipField", { {"retain", true} }}};
    const auto accepted = validate_animation_clip_document(valid);
    require(accepted.valid(), "unknown asset members must not affect clip timing validation");

    auto zeroDuration = valid;
    zeroDuration["frames"][0]["duration"] = 0.0;
    const auto zeroIssues = validate_animation_clip_document(zeroDuration);
    require(!zeroIssues.valid() && zeroIssues.issues.front().jsonPointer == "/frames/0/duration",
            "validation must reject a zero frame duration by JSON pointer");

    auto invalidDuration = valid;
    invalidDuration["frames"][1]["duration"] = -0.5;
    const auto durationIssues = validate_animation_clip_document(invalidDuration);
    require(!durationIssues.valid() && durationIssues.issues.front().jsonPointer == "/frames/1/duration",
            "validation must identify a negative frame duration by JSON pointer");

    auto missingDuration = valid;
    missingDuration["frames"][0].erase("duration");
    require(!validate_animation_clip_document(missingDuration).valid(),
            "missing frame timing must be reported without discarding other JSON members");

    auto malformedSchema = valid;
    malformedSchema["frames"][1].erase("hasSourceRect");
    const auto schemaIssues = validate_animation_clip_document(malformedSchema);
    require(std::any_of(schemaIssues.issues.begin(), schemaIssues.issues.end(), [](const auto &issue) {
                return issue.jsonPointer == "/frames/1/hasSourceRect";
            }),
            "missing runtime-required frame schema must be reported at its JSON pointer");

    const auto hasIssue = [](const AnimationClipValidation &validation, const std::string &pointer) {
        return std::any_of(validation.issues.begin(), validation.issues.end(), [&](const auto &issue) {
            return issue.jsonPointer == pointer;
        });
    };
    auto missingLoop = valid;
    missingLoop.erase("loop");
    require(hasIssue(validate_animation_clip_document(missingLoop), "/loop"),
            "missing runtime root loop data must be field-addressed");
    auto wrongFilePathType = valid;
    wrongFilePathType["filePath"] = nullptr;
    require(hasIssue(validate_animation_clip_document(wrongFilePathType), "/filePath"),
            "wrong-kind runtime root filePath data must be field-addressed");

    AnimationClip clip({AnimationFrame(nullptr, 0.25f)}, true);
    const nlohmann::json serialized = clip;
    require(serialized["frames"][0]["duration"] == 0.25f,
            "engine serialization retains a positive frame duration");

    AnimationClip invalidClip({AnimationFrame(nullptr, -0.1f)}, true);
    bool rejected = false;
    try
    {
        const nlohmann::json ignored = invalidClip;
        (void)ignored;
    }
    catch (const std::runtime_error &)
    {
        rejected = true;
    }
    require(rejected, "engine serialization must reject invalid duration data before writing it");
}

void test_root_schema_save_rejection_preserves_animation_draft()
{
    platformator_editor_test::TemporaryDirectory temporary;
    const auto project = temporary.path() / "project";
    const auto assets = project / "assets";
    const auto animationDirectory = assets / "animations";
    std::filesystem::create_directories(animationDirectory);
    std::ofstream(project / "CMakeLists.txt") << "project(animation-root-schema-test)\n";
    const auto path = animationDirectory / "missing-filepath.animset";
    Json malformed{{"frames", Json::array()},
                   {"loop", true},
                   {"vendor", Json{{"kept", true}}}};
    platformator_editor_test::write_json(path, malformed);

    DocumentManager documents(project, assets);
    const auto id = documents.open_animation(path);
    require(id.has_value(), "malformed root metadata must remain editable after opening");
    auto *tab = documents.find_animation_tab(*id);
    require(tab != nullptr && tab->document().set("/loop", false).changed,
            "a malformed animation draft must retain unrelated edits");
    const Json draft = tab->document().root();
    const auto history = tab->document().history_token();
    const auto savedHistory = tab->document().saved_history_token();
    const auto blocked = documents.save_animation_tab(*id);
    require(blocked.status == WorkspaceActionStatus::Failed &&
                blocked.message.find("/filePath") != std::string::npos &&
                tab->document().root() == draft && tab->document().dirty() &&
                tab->document().history_token() == history &&
                tab->document().saved_history_token() == savedHistory &&
                platformator_editor_test::read_json(path) == malformed,
            "a root-schema save failure must identify the field and preserve disk, draft, history and saved state");
}

void test_frame_order_and_history_are_lossless()
{
    const Json initial = {
        {"frames", Json::array({
            {{"duration", 0.2}, {"textureWrapperFilePath", "assets/a.png"},
             {"hasSourceRect", false}, {"sourceRect", {{"x", 0.0}, {"y", 0.0},
                                                           {"w", 0.0}, {"h", 0.0}}},
             {"vendor", {"id", 4}}},
            {{"duration", 0.4}, {"textureWrapperFilePath", "assets/b.png"},
             {"hasSourceRect", false}, {"sourceRect", {{"x", 0.0}, {"y", 0.0},
                                                           {"w", 0.0}, {"h", 0.0}}},
             {"opaque", true}}})},
        {"framesPerSecond", 8.0}, {"loop", true}, {"width", 16}, {"height", 24},
        {"name", "ordered"}, {"filePath", ""},
        {"clipVendorData", Json::array({1, 2, 3})}};
    AssetDocument document(initial);

    require(move_animation_frame(document, 0, 1).changed,
            "reordering a frame must commit one asset edit");
    require(document.root()["frames"][0]["textureWrapperFilePath"] == "assets/b.png" &&
                document.root()["frames"][1]["vendor"] == initial["frames"][0]["vendor"] &&
                document.root()["clipVendorData"] == initial["clipVendorData"],
            "reordering must retain each unknown frame member and clip-level content");
    require(document.undo().changed && document.root() == initial,
            "one Undo must restore the complete ordered asset object");
    require(document.redo().changed && document.root()["frames"][0]["opaque"] == true,
            "one Redo must restore the exact frame order and opaque values");
    require(!move_animation_frame(document, 99, 0).error.empty(),
            "out-of-range frame moves must leave the document unchanged");

    const auto add = add_animation_frame(document,
        Json{{"duration", 0.1}, {"textureWrapperFilePath", "assets/c.png"}, {"newFrameField", 17}});
    require(add.changed && document.root()["frames"].size() == 3,
            "adding a frame must add one authored array entry");
    require(document.root()["frames"][2]["hasSourceRect"] == false &&
                document.root()["frames"][2]["sourceRect"] ==
                    Json{{"x", 0.0}, {"y", 0.0}, {"w", 0.0}, {"h", 0.0}} &&
                document.root()["frames"][2]["newFrameField"] == 17,
            "frame creation must fill runtime defaults and retain caller-supplied unknown members");
    require(document.undo().changed && document.root()["frames"].size() == 2,
            "the added frame must be a single Undo step");
    require(remove_animation_frame(document, 0).changed && document.root()["frames"].size() == 1,
            "removing a frame must preserve the remaining entries");
}

void test_playback_and_scrubbing_are_view_only()
{
    const Json clip = {
        {"frames", Json::array({
            {{"duration", 0.25}}, {{"duration", 0.25}}, {{"duration", 0.5}}})},
        {"loop", true}};
    require(animation_frame_at_time(clip, 0.1) == 0 &&
                animation_frame_at_time(clip, 0.4) == 1 &&
                animation_frame_at_time(clip, 0.8) == 2 &&
                animation_frame_at_time(clip, 1.0) == 0,
            "playback must use explicit frame durations and loop boundaries");

    auto nonLooping = clip;
    nonLooping["loop"] = false;
    require(animation_frame_at_time(nonLooping, 20.0) == 2,
            "non-looping playback must hold on the final frame");

    AssetDocument document(clip);
    const auto originalHistoryToken = document.history_token();
    AnimationPanel panel;
    panel.scrub_to(clip, 1);
    require(panel.selected_frame() == 1 && document.root() == clip &&
                document.history_token() == originalHistoryToken,
            "scrubbing must only change panel view state");
    panel.set_playing(true);
    panel.advance_playback(clip, 0.51);
    require(panel.selected_frame() == 2 && document.root() == clip &&
                document.history_token() == originalHistoryToken,
            "playback must advance the view without editing authored data");
    panel.set_playing(false);
    panel.advance_playback(clip, 0.5);
    require(panel.selected_frame() == 2 && document.root() == clip &&
                document.history_token() == originalHistoryToken,
            "paused playback must leave the playhead and asset unchanged");

    using PlaybackClock = std::chrono::steady_clock;
    const auto now = PlaybackClock::time_point{} + std::chrono::seconds(12);
    AnimationPanel scheduled;
    scheduled.scrub_to(clip, 0);
    scheduled.set_playing(true);
    const auto firstDeadline = scheduled.next_playback_deadline(clip, now);
    require(firstDeadline && *firstDeadline == now + std::chrono::milliseconds(250),
            "an active preview must schedule its first frame boundary while the editor is idle");
    scheduled.advance_playback(clip, 0.25);
    const auto secondDeadline = scheduled.next_playback_deadline(clip, now + std::chrono::milliseconds(250));
    require(scheduled.selected_frame() == 1 && secondDeadline &&
                *secondDeadline == now + std::chrono::milliseconds(500),
            "each idle wake must schedule the next authored frame boundary");
    scheduled.set_playing(false);
    require(!scheduled.next_playback_deadline(clip, now),
            "Pause must retire the idle playback deadline");

    AnimationPanel heldLastFrame;
    heldLastFrame.scrub_to(nonLooping, 2);
    heldLastFrame.set_playing(true);
    heldLastFrame.advance_playback(nonLooping, 0.5);
    require(heldLastFrame.selected_frame() == 2 &&
                !heldLastFrame.next_playback_deadline(nonLooping, now),
            "a non-looping preview at its final frame must not keep waking the idle editor");
}

void test_animation_semantics_are_bounded_and_actions_are_safe()
{
    Json clip = {
        {"frames", Json::array()}, {"framesPerSecond", 8.0}, {"loop", true},
        {"width", 16.0}, {"height", 16.0}, {"name", "semantic"}, {"filePath", ""}};
    for (int index = 0; index < 1000; ++index)
    {
        Json frame = make_default_animation_frame();
        frame["duration"] = 0.125;
        frame["textureWrapperFilePath"] = "assets/frame.png";
        clip["frames"].push_back(std::move(frame));
    }
    AssetDocument document(clip);
    AnimationPanel panel;
    panel.scrub_to(clip, 500);
    const auto nodes = panel.semantic_nodes(document, {}, "animation/7");
    require(nodes.size() <= 48,
            "animation semantics must virtualize long frame lists to a bounded node set");
    const auto node_named = [&](std::string_view id) -> const SemanticNode * {
        const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const auto &node) {
            return node.id == id;
        });
        return found == nodes.end() ? nullptr : &*found;
    };
    const auto *root = node_named("animation/7/window");
    const auto *frame = node_named("animation/7/frame/500");
    const auto *moveUp = node_named("animation/7/move-up");
    require(root != nullptr && root->role == SemanticRole::Window &&
                frame != nullptr && frame->role == SemanticRole::ListItem && frame->selected &&
                moveUp != nullptr && moveUp->enabled,
            "animation semantics must report role, selected frame and enabled actions");

    SemanticAction changeDuration;
    changeDuration.nodeId = "animation/7/duration";
    changeDuration.action = "set_value";
    changeDuration.value = "0.5";
    std::string error;
    require(panel.dispatch_semantic_action(document, {}, changeDuration, "animation/7", error) &&
                document.root()["frames"][500]["duration"] == 0.5,
            "a valid semantic duration action must use the asset document edit path");
    require(document.undo().changed && document.root()["frames"][500]["duration"] == 0.125,
            "a semantic authored action must create one normal Undo step");

    changeDuration.value = "nan";
    require(!panel.dispatch_semantic_action(document, {}, changeDuration, "animation/7", error) &&
                !error.empty() && document.root()["frames"][500]["duration"] == 0.125,
            "nonfinite semantic numeric input must be rejected without changing the asset");

    SemanticAction select;
    select.nodeId = "animation/7/frame/501";
    select.action = "select";
    const auto historyToken = document.history_token();
    require(panel.dispatch_semantic_action(document, {}, select, "animation/7", error) &&
                panel.selected_frame() == 501 && document.history_token() == historyToken,
            "a semantic frame selection must update view state without dirtying the asset");

    SemanticAction add;
    add.nodeId = "animation/7/add-frame";
    add.action = "activate";
    require(panel.dispatch_semantic_action(document, {}, add, "animation/7", error) &&
                document.root()["frames"].size() == 1001 && panel.selected_frame() == 1000,
            "semantic frame creation must append once and select the new frame safely");
}

void test_animation_source_region_has_semantics_and_one_undo()
{
    platformator_editor_test::TemporaryDirectory temporary;
    const auto root = temporary.path();
    const auto assetRoot = root / "configured-content-root";
    std::filesystem::create_directories(assetRoot);
    std::ofstream(assetRoot / "sheet.png", std::ios::binary) << "fixture";
    const Json initial{
        {"frames", Json::array({Json{{"duration", 0.25},
                                     {"textureWrapperFilePath", "assets/sheet.png"},
                                     {"hasSourceRect", true},
                                     {"sourceRect", Json{{"x", 0.0}, {"y", 0.0},
                                                          {"w", 32.0}, {"h", 16.0}}},
                                     {"unknownFrameValue", "keep"}}})},
        {"framesPerSecond", 4.0}, {"loop", true}, {"width", 32.0},
        {"height", 16.0}, {"name", "source-region"}, {"filePath", ""}};
    AssetDocument document(initial);
    AnimationPanel panel;
    std::filesystem::path preparedPath;
    const AnimationPanel::PreparedTexture prepared = [](const std::filesystem::path &) {
        return AnimationPanel::PreparedTextureInfo{nullptr, 64, 32};
    };
    const AnimationPanel::PreparedTexture recordingPrepared = [&](const std::filesystem::path &path) {
        preparedPath = path;
        return prepared(path);
    };
    const auto nodes = panel.semantic_nodes(document, assetRoot, "animation/9", recordingPrepared);
    const auto sourceX = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &node) {
        return node.id == "animation/9/source-region/x";
    });
    const auto sourceWidth = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &node) {
        return node.id == "animation/9/source-region/width";
    });
    require(sourceX != nodes.end() && sourceX->role == SemanticRole::TextField &&
                sourceX->value == "0.000000" && sourceX->maximum == 32.0 &&
                sourceWidth != nodes.end() && sourceWidth->value == "32.000000",
            "accessible animation source region reports bounded pixel coordinates and stored crop values");
    require(preparedPath == assetRoot / "sheet.png",
            "serialized assets/... frame paths must resolve against the configured disjoint asset root");

    SemanticAction moveSourceX{
        .nodeId = "animation/9/source-region/x", .action = "set_value", .value = "8"};
    std::string error;
    require(panel.dispatch_semantic_action(document, assetRoot, moveSourceX, "animation/9", error) &&
                document.root()["frames"][0]["hasSourceRect"] == true &&
                document.root()["frames"][0]["sourceRect"]["x"] == 8.0 &&
                document.root()["frames"][0]["sourceRect"]["w"] == 32.0 &&
                document.root()["frames"][0]["unknownFrameValue"] == "keep",
            "accessible source-region edits preserve image bounds and unknown frame data");
    require(document.undo().changed && document.root() == initial,
            "accessible source-region edits commit as one exact asset history step");

    moveSourceX.value = "65";
    require(!panel.dispatch_semantic_action(document, root, moveSourceX, "animation/9", error) &&
                !error.empty() && document.root() == initial,
            "out-of-image semantic source-region edits are rejected without changing the asset");
}

void test_bounded_preview_projection_and_resource_dependencies()
{
    const auto root = std::filesystem::temp_directory_path() / "platformator-animation-preview-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "assets");
    std::ofstream(root / "assets" / "frame.png", std::ios::binary) << "preview";

    Json clip = {
        {"frames", Json::array({
            {{"duration", 0.25}, {"textureWrapperFilePath", "assets/frame.png"}},
            {{"duration", 0.25}, {"textureWrapperFilePath", "assets/frame.png"}},
            {{"duration", 0.5}, {"textureWrapperFilePath", "assets/frame.png"}}})},
        {"loop", true},
        {"unknown", Json{{"preserved", true}}}};
    const auto preview = prepare_animation_preview(clip, root / "assets");
    require(preview.valid && preview.frames.size() == 3 &&
                preview.frames[0].texturePath == root / "assets" / "frame.png" &&
                preview.frameEndTimes.back() == 1.0,
            "prepared preview keeps bounded frame resources and shared resolved timing");
    require(animation_preview_frame_at_time(preview, 0.24) == 0 &&
                animation_preview_frame_at_time(preview, 0.25) == 1 &&
                animation_preview_frame_at_time(preview, 0.5) == 2 &&
                animation_preview_frame_at_time(preview, 1.0) == 0,
            "preview playback uses explicit duration, FPS fallback and loop boundaries");
    auto nonLoop = preview;
    nonLoop.loop = false;
    require(animation_preview_frame_at_time(nonLoop, 20.0) == 2,
            "non-looping previews hold their final frame");
    require(clip["unknown"]["preserved"] == true,
            "preview projection leaves the authored JSON and unknown data untouched");

    Json oversized = clip;
    while (oversized["frames"].size() <= AnimationPreviewDocument::MaximumFrames)
        oversized["frames"].push_back(Json{{"duration", 0.25}});
    const auto tooManyFrames = prepare_animation_preview(oversized, root / "assets");
    require(!tooManyFrames.valid && tooManyFrames.frames.empty() &&
                tooManyFrames.error.find("512-frame") != std::string::npos &&
                oversized["frames"].size() == AnimationPreviewDocument::MaximumFrames + 1,
            "preview frame limits reject rather than truncating saved document content");
    Json oversizedProjection = {
        {"frames", Json::array({{{"duration", 1.0},
                                 {"textureWrapperFilePath", std::string(AnimationPreviewDocument::MaximumBytes, 'a')}}})},
        {"framesPerSecond", 1.0}, {"loop", false}};
    require(!prepare_animation_preview(oversizedProjection, root / "assets").valid &&
                oversizedProjection["frames"].size() == 1,
            "the projected preview byte cap rejects without modifying authored frames");

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{5}, root / "assets");
    const DocumentIdentity identity{ProjectGeneration{5}, TabId{19}, DocumentGeneration{19}};
    const ResourceConsumer consumer{identity, 1, std::nullopt, "caller-supplied-role"};
    resources.begin_visible_reference_frame();
    resources.register_visible_animation_texture(root / "assets" / "frame.png", consumer);
    const auto affected = resources.affected_consumers(root / "assets" / "frame.png");
    require(affected.size() == 1 && affected.front().document == identity &&
                affected.front().role == "animation.preview.frame",
            "visible animation frame textures join the resource invalidation graph");
    resources.end_visible_reference_frame();
    resources.begin_visible_reference_frame();
    resources.end_visible_reference_frame();
    require(resources.affected_consumers(root / "assets" / "frame.png").empty(),
            "animation texture demand retires when the frame is no longer visible");

    std::filesystem::remove_all(root);
}

void test_texture_compatibility_is_contained_and_typed()
{
    const auto root = std::filesystem::temp_directory_path() / "platformator-animation-panel-test";
    std::filesystem::remove_all(root);
    const auto assetRoot = root / "configured-content-root";
    std::filesystem::create_directories(assetRoot);
    {
        std::ofstream image(assetRoot / "sprite.PNG", std::ios::binary);
        image << "fixture";
    }
    {
        std::ofstream text(assetRoot / "notes.txt", std::ios::binary);
        text << "fixture";
    }
    {
        std::ofstream image(assetRoot / "sprite.qoi", std::ios::binary);
        image << "fixture";
    }
    {
        std::ofstream image(assetRoot / "sprite.dds", std::ios::binary);
        image << "fixture";
    }
    require(animation_texture_is_compatible(assetRoot, "assets/sprite.PNG"),
            "existing image files inside the configured asset root must be assignable");
    require(animation_texture_is_compatible(assetRoot, "assets/sprite.qoi") &&
                !animation_texture_is_compatible(assetRoot, "assets/sprite.dds") &&
                !animation_texture_is_compatible(assetRoot, "assets/notes.txt") &&
                !animation_texture_is_compatible(assetRoot, "../outside.png") &&
                !animation_texture_is_compatible(assetRoot, "assets/missing.png"),
            "shared image formats are assignable while unsupported, escaping and missing files are rejected");
    const auto outside = root / "outside.png";
    std::ofstream(outside, std::ios::binary) << "outside";
    std::error_code symlinkError;
    std::filesystem::create_symlink(assetRoot / "sprite.PNG", assetRoot / "contained.png", symlinkError);
    if (!symlinkError)
    {
        require(animation_texture_is_compatible(assetRoot, "assets/contained.png"),
                "contained image symlinks must remain compatible");
        std::filesystem::create_symlink(outside, assetRoot / "escaping.png", symlinkError);
        if (!symlinkError)
            require(!animation_texture_is_compatible(assetRoot, "assets/escaping.png"),
                    "image symlinks resolving outside the configured root must be rejected");
    }
    std::filesystem::remove_all(root);
}

void test_reordered_animation_asset_saves_reopens_and_prepares_identically()
{
    using platformator_editor_test::TemporaryDirectory;

    TemporaryDirectory temporary;
    const auto assetRoot = temporary.path() / "assets";
    std::filesystem::create_directories(assetRoot);
    const auto assetPath = assetRoot / "walk.animset";
    const Json initial = {
        {"frames", Json::array({
            {{"duration", 0.25}, {"textureWrapperFilePath", "assets/sheet.png"},
             {"hasSourceRect", true}, {"sourceRect", {{"x", 4}, {"y", 0}, {"w", 4}, {"h", 4}}},
             {"vendorFrame", {{"tag", "first"}}}},
            {{"duration", 0.25}, {"textureWrapperFilePath", "assets/idle.png"},
             {"hasSourceRect", false}, {"sourceRect", {{"x", 0}, {"y", 0}, {"w", 0}, {"h", 0}}}}})},
        {"loop", false},
        {"vendorClip", {{"preserve", true}}}};

    AssetDocument document(initial);
    require(move_animation_frame(document, 0, 1).changed,
            "frame strip reorder must be a saved asset edit");
    const auto saved = document.save(assetPath);
    require(saved.succeeded() && !document.dirty(),
            "animation frame reorder must save through the asset document writer");

    const auto reopened = load_asset_document(assetPath);
    require(reopened && reopened.document->root() == document.root() &&
                reopened.document->root()["frames"][0]["textureWrapperFilePath"] == "assets/idle.png" &&
                reopened.document->root()["frames"][1]["vendorFrame"]["tag"] == "first" &&
                reopened.document->root()["vendorClip"]["preserve"] == true,
            "save and reopen must retain reordered frame payloads and unknown members exactly");

    const auto preview = prepare_animation_preview(reopened.document->root(), assetRoot);
    if (!preview.valid)
        throw std::runtime_error("reopened animation preview rejected the saved asset: " + preview.error);
    require(preview.valid && preview.frames.size() == 2 && preview.frames[0].durationSeconds == 0.25 &&
                preview.frames[0].texturePath.filename().string() == "idle.png" &&
                preview.frames[1].durationSeconds == 0.25 &&
                preview.frames[1].texturePath.filename().string() == "sheet.png" && !preview.loop,
            "reopened frames must prepare for preview in authored order with shared duration fallback");
}

void test_add_frame_ui_semantics_save_reload_and_runtime_consumption()
{
    using platformator_editor_test::TemporaryDirectory;

    TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    const auto animationDirectory = assetRoot / "animations";
    std::filesystem::create_directories(animationDirectory);
    std::ofstream(projectRoot / "CMakeLists.txt") << "project(animation-add-frame-test)\n";

    RuntimeOptions runtimeOptions;
    runtimeOptions.windowSettings.width = 64;
    runtimeOptions.windowSettings.height = 64;
    runtimeOptions.assetRoot = assetRoot.string();
    platformator::Runtime runtime(runtimeOptions);
    const auto texturePath = assetRoot / "frame.png";
    write_runtime_texture(texturePath);

    const auto animationPath = animationDirectory / "added.animset";
    Json initialFrame = runtime_default_frame("assets/frame.png", 0.125);
    initialFrame["vendorFrame"] = Json{{"kept", true}};
    const Json initial{
        {"frames", Json::array({initialFrame})},
        {"framesPerSecond", 4.0}, {"loop", true}, {"width", 2.0}, {"height", 2.0},
        {"name", "add-frame"}, {"filePath", animationPath.string()},
        {"vendorClip", Json{{"kept", "yes"}}}};

    AssetDocument uiDocument(initial);
    AnimationPanel uiPanel;
    AnimationAddFrameGui gui;
    gui.click_add_frame(uiPanel, uiDocument, assetRoot);
    // Exercise real GUI removal, including the outer disabled scope used by the host.
    const ImVec2 removePoint = gui.find_add_frame(uiPanel, uiDocument, assetRoot, "Remove Frame");
    gui.frame(uiPanel, uiDocument, assetRoot, removePoint, true);
    gui.frame(uiPanel, uiDocument, assetRoot, removePoint, false);
    require(uiDocument.root()["frames"].size() == 1 && uiPanel.selected_frame() == 0,
            "removing an empty frame must leave ImGui scopes balanced and retain the previous frame");
    gui.click_add_frame(uiPanel, uiDocument, assetRoot);
    const ImVec2 regionToggle = gui.find_add_frame(uiPanel, uiDocument, assetRoot, "Use Source Rectangle");
    gui.frame(uiPanel, uiDocument, assetRoot, regionToggle, true);
    gui.frame(uiPanel, uiDocument, assetRoot, regionToggle, false);
    require(uiDocument.root()["frames"][1]["hasSourceRect"] == true &&
                uiDocument.root()["frames"][1]["sourceRect"]["w"].get<double>() > 0.0,
            "source rectangle toggle must initialize valid bounds");
    require(uiDocument.undo().changed && uiDocument.root()["frames"][1]["hasSourceRect"] == false,
            "one Undo must restore the disabled source rectangle and its prior bounds");
    const Json afterUiAdd = uiDocument.root();
    require(afterUiAdd["frames"].size() == 2 && uiPanel.selected_frame() == 1 &&
                afterUiAdd["frames"][1]["duration"] == 0.1 &&
                afterUiAdd["frames"][1]["textureWrapperFilePath"] == "" &&
                afterUiAdd["frames"][1]["hasSourceRect"] == false &&
                afterUiAdd["frames"][1]["sourceRect"] ==
                    Json{{"x", 0.0}, {"y", 0.0}, {"w", 0.0}, {"h", 0.0}},
            "the real Add Frame button must append a valid uncropped empty frame and select it");
    require(uiDocument.undo().changed && uiDocument.root() == initial &&
                uiDocument.redo().changed && uiDocument.root() == afterUiAdd,
            "one Undo and Redo must reverse and restore the exact UI Add Frame edit");
    const auto uiSave = uiDocument.save(animationPath);
    require(uiSave.succeeded(), "the UI-created animation frame must save cleanly");
    auto reopenedUi = load_asset_document(animationPath);
    require(reopenedUi && reopenedUi.document->root() == afterUiAdd &&
                validate_animation_clip_document(reopenedUi.document->root()).valid(),
            "save and reopen must retain a runtime-compatible UI-created frame");

    AssetDocument semanticDocument(std::move(*reopenedUi.document));
    const Json beforeSemanticAdd = semanticDocument.root();
    AnimationPanel semanticPanel;
    std::string error;
    SemanticAction add{
        .nodeId = "animation/add/add-frame",
        .action = "activate"};
    require(semanticPanel.dispatch_semantic_action(
                semanticDocument, assetRoot, add, "animation/add", error) && error.empty(),
            "the semantic Add Frame action must use the same runtime-compatible construction path");
    const Json afterSemanticAdd = semanticDocument.root();
    require(afterSemanticAdd["frames"].size() == 3 &&
                afterSemanticAdd["frames"][2]["hasSourceRect"] == false &&
                afterSemanticAdd["frames"][2]["sourceRect"]["w"] == 0.0 &&
                afterSemanticAdd["vendorClip"] == initial["vendorClip"],
            "semantic frame creation must preserve clip data and initialize the complete runtime schema");
    require(semanticDocument.undo().changed && semanticDocument.root() == beforeSemanticAdd &&
                semanticDocument.redo().changed && semanticDocument.root() == afterSemanticAdd,
            "one Undo and Redo must reverse and restore exactly one semantic Add Frame edit");

    SemanticAction textureDraft{
        .nodeId = "animation/add/texture-path",
        .action = "set_value",
        .value = "assets/frame.png"};
    require(semanticPanel.dispatch_semantic_action(
                semanticDocument, assetRoot, textureDraft, "animation/add", error) &&
                semanticDocument.root() == afterSemanticAdd,
            "entering an assigned texture draft must not write partial authored data");
    SemanticAction assignTexture{
        .nodeId = "animation/add/assign-texture",
        .action = "activate"};
    require(semanticPanel.dispatch_semantic_action(
                semanticDocument, assetRoot, assignTexture, "animation/add", error) &&
                semanticDocument.root()["frames"][2]["textureWrapperFilePath"] == "assets/frame.png",
            "the semantic texture assignment must commit the compatible contained image reference");
    require(!semanticDocument.root()["frames"][2].contains("vendorFrame"),
            "the newly authored semantic frame must not invent an unknown frame member");

    const auto semanticSave = semanticDocument.save(animationPath);
    require(semanticSave.succeeded(), "the semantically authored animation must save cleanly");
    auto reopened = load_asset_document(animationPath);
    require(reopened && validate_animation_clip_document(reopened.document->root()).valid() &&
                reopened.document->root()["frames"].size() == 3 &&
                reopened.document->root()["frames"][0]["vendorFrame"]["kept"] == true &&
                reopened.document->root()["vendorClip"]["kept"] == "yes",
            "reopen must preserve frame order, unknown members and runtime-required frame fields");
    const auto preview = prepare_animation_preview(reopened.document->root(), assetRoot);
    require(preview.valid && preview.frames.size() == 3 &&
                preview.frames[0].texturePath == texturePath &&
                preview.frames[1].texturePath.empty() &&
                preview.frames[2].texturePath == texturePath &&
                preview.frames[0].durationSeconds == 0.125 &&
                preview.frames[1].durationSeconds == 0.1 &&
                preview.frames[2].durationSeconds == 0.1,
            "saved preview order and durations must preserve explicit timing and empty/assigned paths");

    AnimationClip *loaded = runtime.loadAnimationClip("assets/animations/added.animset");
    require(loaded != nullptr && loaded->frames.size() == 3 &&
                std::abs(loaded->frames[0].duration - 0.125f) < 1e-6f &&
                std::abs(loaded->frames[1].duration - 0.1f) < 1e-6f &&
                std::abs(loaded->frames[2].duration - 0.1f) < 1e-6f &&
                loaded->frames[0].getTextureWrapper() != nullptr &&
                loaded->frames[1].getTextureWrapper() == nullptr &&
                loaded->frames[2].getTextureWrapper() != nullptr &&
                !loaded->frames[1].region.hasSourceRect && loaded->frames[1].region.sourceRect.w == 0.0f,
            "ordinary Runtime loading must preserve explicit durations and consume empty/assigned frames");

    const auto invalidPath = animationDirectory / "invalid-frame-schema.animset";
    Json invalid = initial;
    invalid["frames"][0].erase("sourceRect");
    platformator_editor_test::write_json(invalidPath, invalid);
    DocumentManager documents(projectRoot, assetRoot);
    const auto invalidId = documents.open_animation(invalidPath);
    require(invalidId.has_value(), "malformed animation drafts must remain recoverable when opened");
    auto *invalidTab = documents.find_animation_tab(*invalidId);
    require(invalidTab != nullptr && invalidTab->document().set("/loop", false).changed,
            "the malformed animation draft must retain unrelated edits");
    const auto blockedSave = documents.save_animation_tab(*invalidId);
    require(blockedSave.status == WorkspaceActionStatus::Failed &&
                blockedSave.message.find("/frames/0/sourceRect") != std::string::npos &&
                invalidTab->document().dirty() &&
                platformator_editor_test::read_json(invalidPath) == invalid,
            "Save must report malformed runtime frame schema and leave disk and draft intact");
    const auto blockedSaveAll = documents.save_all();
    require(blockedSaveAll.action.status == WorkspaceActionStatus::Failed &&
                blockedSaveAll.failedTab == invalidId && invalidTab->document().dirty(),
            "Save All must apply the same animation runtime-schema gate without losing work");
}

void test_authored_sprite_dimensions_preserved_across_animation_and_preview()
{
    platformator_editor_test::TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    std::filesystem::create_directories(assetRoot / "animations");

    RuntimeOptions runtimeOptions;
    runtimeOptions.windowSettings.width = 64;
    runtimeOptions.windowSettings.height = 64;
    runtimeOptions.assetRoot = assetRoot.string();
    platformator::Runtime runtime(runtimeOptions);

    // Part A: Animator::applyCurrentFrame preserves positive authored Sprite width & height
    GameObject *gameObject = runtime.createGameObject();
    gameObject->addComponent<Sprite>();
    Sprite *sprite = gameObject->getComponent<Sprite>();
    sprite->setWidth(48.0f);
    sprite->setHeight(64.0f);

    std::vector<AnimationFrame> frames;
    AnimationFrame frame1;
    frame1.duration = 0.1f;
    frame1.region.setSourceRect(SDL_FRect{0.0f, 0.0f, 16.0f, 16.0f});
    frames.push_back(frame1);

    AnimationFrame frame2;
    frame2.duration = 0.1f;
    frame2.region.setSourceRect(SDL_FRect{16.0f, 0.0f, 32.0f, 32.0f});
    frames.push_back(frame2);

    AnimationClip clip(frames, true, "");

    gameObject->addComponent<Animator>();
    Animator *animator = gameObject->getComponent<Animator>();
    animator->play(&clip);

    require(sprite->getWidth() == 48.0f && sprite->getHeight() == 64.0f,
            "Animator::applyCurrentFrame must preserve positive authored Sprite width and height");

    animator->setCurrentFrameIndex(1);
    require(sprite->getWidth() == 48.0f && sprite->getHeight() == 64.0f,
            "Advancing animation frame must preserve positive authored Sprite width and height");

    // Part B: EditorPreview preservation
    const auto animPath = assetRoot / "animations" / "test.animset";
    Json clipJson = Json::object();
    clipJson["frames"] = Json::array({
        Json{{"duration", 0.1}, {"hasSourceRect", true},
             {"sourceRect", Json{{"x", 0.0}, {"y", 0.0}, {"w", 12.0}, {"h", 12.0}}},
             {"textureWrapperFilePath", ""}}
    });
    clipJson["framesPerSecond"] = 8.0;
    clipJson["loop"] = true;
    clipJson["width"] = 16.0;
    clipJson["height"] = 16.0;
    clipJson["name"] = "test";
    clipJson["filePath"] = animPath.string();
    platformator_editor_test::write_json(animPath, clipJson);

    Json sceneRoots = Json::array({
        Json{
            {"id", 100},
            {"name", "Hero"},
            {"tag", ""},
            {"active", true},
            {"position", Json{{"x", 0.0}, {"y", 0.0}}},
            {"scale", Json{{"x", 1.0}, {"y", 1.0}}},
            {"rotation", 0.0},
            {"components", Json::array({
                Json{
                    {"id", 101},
                    {"type", 6}, // Sprite
                    {"enabled", true},
                    {"width", 50.0},
                    {"height", 75.0},
                    {"textureFilePath", ""},
                    {"flip", 0},
                    {"sourceRectEnabled", false},
                    {"sourceRect", nullptr}
                },
                Json{
                    {"id", 102},
                    {"type", 0}, // Animator
                    {"enabled", true},
                    {"animationClipFilePath", "assets/animations/test.animset"},
                    {"currentFrameIndex", 0},
                    {"playbackSpeed", 1.0},
                    {"playing", true}
                }
            })},
            {"children", Json::array()}
        }
    });

    auto parsed = parse_document(std::move(sceneRoots), 1, std::nullopt, projectRoot, assetRoot);
    require(static_cast<bool>(parsed), ("document must parse cleanly: " + parsed.error).c_str());
    DocumentSession document = std::move(*parsed.document);

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{document.projectGeneration}, assetRoot);

    EditorPreview preview(runtime);
    preview.set_resource_index(&resources);
    preview.reconcile(document, true);

    const auto *info = preview.object_info(100);
    require(info != nullptr, "preview must have object info for Hero");
    require(info->width == 50.0f && info->height == 75.0f,
            "EditorPreview must preserve positive authored Sprite width and height");
}

void test_animation_clip_optional_dimensions_validation_and_deserialization()
{
    platformator_editor_test::TemporaryDirectory temporary;
    const auto assetRoot = temporary.path() / "assets";
    std::filesystem::create_directories(assetRoot);

    RuntimeOptions runtimeOptions;
    runtimeOptions.windowSettings.width = 64;
    runtimeOptions.windowSettings.height = 64;
    runtimeOptions.assetRoot = assetRoot.string();
    platformator::Runtime runtime(runtimeOptions);

    Json minimal = {
        {"frames", Json::array({
            Json{{"duration", 0.1}, {"hasSourceRect", false},
                 {"sourceRect", Json{{"x", 0.0}, {"y", 0.0}, {"w", 0.0}, {"h", 0.0}}},
                 {"textureWrapperFilePath", ""}}
        })},
        {"loop", true},
        {"filePath", "test.animset"}
    };

    const auto validationValid = validate_animation_clip_document(minimal);
    require(validationValid.valid(),
            "AnimationClip document without width/height/name/framesPerSecond must validate cleanly");

    AnimationClip clip;
    from_json(minimal, clip);
    require(clip.getFrames().size() == 1 && clip.getLoop() && clip.getFilePath() == "test.animset",
            "AnimationClip deserialized from minimal schema must preserve frames, loop, and filePath");
}

void test_project_panel_create_animation_clip_and_open()
{
    platformator_editor_test::TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    const auto animDir = assetRoot / "animations";
    std::filesystem::create_directories(animDir);

    ProjectContext context;
    context.set_roots(projectRoot, assetRoot);
    ProjectChanges changes;
    ProjectScanService scans(1);
    ProjectPanel projectPanel(context, changes, scans, nullptr);

    std::filesystem::path openedCallbackPath;
    projectPanel.set_open_animation_callback([&](const std::filesystem::path &p) {
        openedCallbackPath = p;
    });

    const bool created = projectPanel.execute_context_create("Animation Clip", animDir);
    require(created, "ProjectPanel::execute_context_create('Animation Clip') must succeed");

    const auto clipPath = animDir / "New Animation.animset";
    require(std::filesystem::exists(clipPath),
            "New Animation.animset must exist on disk after creation");
    require(openedCallbackPath == clipPath,
            "ProjectPanel must invoke open_animation_callback with created clip path");

    const Json diskJson = platformator_editor_test::read_json(clipPath);
    require(diskJson.is_object() && diskJson.contains("frames") && diskJson["frames"].is_array() &&
            diskJson["loop"] == true && diskJson["filePath"] == clipPath.string(),
            "Created .animset must contain valid minimal runtime payload");

    DocumentManager documents(projectRoot, assetRoot);
    const auto tabId = documents.open_animation(clipPath);
    require(tabId.has_value(),
            "DocumentManager::open_animation must open the newly created .animset without error");

    const auto *tab = documents.find_animation_tab(*tabId);
    require(tab != nullptr && !tab->document().dirty(),
            "Opened animation tab must exist and be initially clean");

    // Verify collision suffixing on second creation
    const bool created2 = projectPanel.execute_context_create("Animation Clip", animDir);
    require(created2, "Second execute_context_create('Animation Clip') must succeed");
    const auto collisionPath = animDir / "New Animation (1).animset";
    require(std::filesystem::exists(collisionPath),
            "Second creation must generate New Animation (1).animset");
    const auto tabId2 = documents.open_animation(collisionPath);
    require(tabId2.has_value(), "Collision-suffixed .animset must open cleanly");
}

void test_inspector_edit_clip_resolves_assets_prefix()
{
    platformator_editor_test::TemporaryDirectory temporary;
    const auto projectRoot = temporary.path() / "project";
    const auto assetRoot = projectRoot / "assets";
    const auto animDir = assetRoot / "animations";
    std::filesystem::create_directories(animDir);
    const auto clipPath = animDir / "walk.animset";
    platformator_editor_test::write_json(clipPath, Json{
        {"frames", Json::array()}, {"loop", true}, {"filePath", "assets/animations/walk.animset"}});

    Json sceneRoots = Json::array({
        Json{
            {"id", 10}, {"name", "Player"}, {"tag", ""}, {"active", true},
            {"position", Json{{"x", 0.0}, {"y", 0.0}}},
            {"scale", Json{{"x", 1.0}, {"y", 1.0}}},
            {"rotation", 0.0},
            {"components", Json::array({
                Json{{"id", 20}, {"type", 0}, {"enabled", true},
                     {"animationClipFilePath", "assets/animations/walk.animset"},
                     {"currentFrameIndex", 0}, {"playbackSpeed", 1.0}, {"playing", false}}
            })},
            {"children", Json::array()}}
    });
    auto parsed = parse_document(std::move(sceneRoots), 1, std::nullopt, projectRoot, assetRoot);
    require(static_cast<bool>(parsed), "scene document must parse cleanly");
    DocumentSession document = std::move(*parsed.document);
    SelectionModel selection;
    Selection targetSelection;
    targetSelection.objectId = 10;
    targetSelection.selectedObjectIds = {10};
    selection.set(document, std::move(targetSelection));

    InspectorPanel inspector;
    std::filesystem::path openedPath;
    inspector.set_open_animation_callback([&](const std::filesystem::path &path) {
        openedPath = path;
    });
    EditCommandService commands;
    const auto nodes = inspector.semantic_nodes(document, selection.current(), "inspector", nullptr);
    const auto editAction = std::find_if(nodes.begin(), nodes.end(), [](const SemanticNode &node) {
        return node.id.ends_with("/edit-clip");
    });
    require(editAction != nodes.end(), "Inspector semantic nodes must expose Edit animation clip action");
    require(inspector.dispatch_semantic_action(
                document, selection,
                SemanticAction{.nodeId = editAction->id, .action = "activate"},
                "inspector", nullptr, &commands),
            "Inspector EditClip semantic action must succeed");
    require(openedPath == clipPath,
            "Inspector EditClip must strip leading assets/ prefix instead of duplicating assetRoot");
}
} // namespace

int main()
{
    try
    {
        test_animation_toggles_keep_panel_visible();
        test_shared_frame_duration_rules();
        test_clip_document_validation_and_serialization();
        test_root_schema_save_rejection_preserves_animation_draft();
        test_frame_order_and_history_are_lossless();
        test_playback_and_scrubbing_are_view_only();
        test_animation_semantics_are_bounded_and_actions_are_safe();
        test_animation_source_region_has_semantics_and_one_undo();
        test_bounded_preview_projection_and_resource_dependencies();
        test_texture_compatibility_is_contained_and_typed();
        test_reordered_animation_asset_saves_reopens_and_prepares_identically();
        test_add_frame_ui_semantics_save_reload_and_runtime_consumption();
        test_authored_sprite_dimensions_preserved_across_animation_and_preview();
        test_animation_clip_optional_dimensions_validation_and_deserialization();
        test_project_panel_create_animation_clip_and_open();
        test_inspector_edit_clip_resolves_assets_prefix();
        std::cout << "animation timing tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception &error)
    {
        std::cerr << "animation timing tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
