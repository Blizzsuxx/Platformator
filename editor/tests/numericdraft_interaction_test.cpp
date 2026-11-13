#include "assetdocument.h"
#include "documentmanager.h"
#include "panels/animation.h"
#include "panels/spriteeditor.h"
#include "test_support.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace platformator_editor
{
struct AddressNumericDraftTestProbe
{
    static void check_steps()
    {
        AddressNumericDraft draft;
        draft.reset_to("precision", 0.1);
        for (int i = 0; i < 6; ++i)
            draft.finish_step("precision", 0.1, 0.05, false, false, true, true);
        if (std::string(draft.buffer_.data()) != "0.40")
            throw std::runtime_error("step display must omit binary arithmetic tails");
        const auto committed = draft.finish_step("precision", 0.1, 0.05, false, false, false, false);
        if (committed.kind != NumericDraftOutcomeKind::Commit || std::abs(committed.value - 0.4) > 1e-14)
            throw std::runtime_error("stepping must commit the numeric value once on release");
        draft.finish_step("precision", committed.value, 0.05, true, true, false, false);
        if (std::string(draft.buffer_.data()) != "0.35")
            throw std::runtime_error("decrement display must omit binary arithmetic tails");
        const double precise = 0.12345678901234567;
        draft.reset_to("precision", precise);
        draft.format(precise, true);
        if (draft.finish_text().kind != NumericDraftOutcomeKind::None)
            throw std::runtime_error("unchanged precise text must not create an edit");
    }
};
}

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::string animation_address(const AssetDocument &document,
                              const std::filesystem::path &assetRoot,
                              std::string_view pointer)
{
    const std::string source = document.source_path()
        ? document.source_path()->generic_string() : std::string("untitled");
    const std::string root = assetRoot.generic_string();
    return std::to_string(document.generation()) + ":" + std::to_string(source.size()) + ":" +
        source + ":" + std::to_string(root.size()) + ":" + root + ":" +
        std::string(pointer);
}

std::string region_address(const AssetDocument &document, std::size_t frameIndex,
                           const std::filesystem::path &assetRoot,
                           std::string_view textureReference, std::string_view field)
{
    const std::string source = document.source_path()
        ? document.source_path()->generic_string() : std::string("untitled");
    const std::string root = assetRoot.generic_string();
    return std::to_string(document.generation()) + ":" + std::to_string(source.size()) + ":" +
        source + ":" + std::to_string(root.size()) + ":" + root + ":" +
        std::to_string(frameIndex) + ":" + std::to_string(textureReference.size()) + ":" +
        std::string(textureReference) + ":" + std::string(field);
}

ImGuiID item_id(std::string_view address, const char *label)
{
    const std::string scope(address);
    ImGui::PushID(scope.c_str());
    const ImGuiID id = ImGui::GetID(label);
    ImGui::PopID();
    return id;
}

class GuiHarness
{
  public:
    GuiHarness()
    {
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {900.0f, 900.0f};
        io.DeltaTime = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        io.Fonts->Build();
    }

    ~GuiHarness() { ImGui::DestroyContext(); }

    template <typename Draw>
    ImGuiID frame(Draw &&draw, ImVec2 mouse = {-10.0f, -10.0f}, bool leftDown = false)
    {
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, leftDown);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({800.0f, 860.0f}, ImGuiCond_Always);
        ImGui::Begin("numeric-draft-test", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
        draw();
        ImGui::End();
        ImGui::Render();
        return ImGui::GetHoveredID();
    }

    template <typename Draw>
    ImVec2 find_at_x(Draw &&draw, ImGuiID target, float x,
                     float yBegin = 0.0f, float yEnd = 850.0f)
    {
        for (float y = yBegin; y <= yEnd; y += 2.0f)
        {
            if (frame(draw, {x, y}) == target)
                return {x, y};
        }
        throw std::runtime_error("could not locate the requested numeric ImGui field");
    }

    template <typename Draw>
    ImVec2 find_anywhere(Draw &&draw, ImGuiID target)
    {
        for (float y = 0.0f; y <= 850.0f; y += 4.0f)
        {
            for (float x = 8.0f; x <= 790.0f; x += 12.0f)
            {
                if (frame(draw, {x, y}) == target)
                    return {x, y};
            }
        }
        throw std::runtime_error("could not locate the requested numeric ImGui field");
    }

    template <typename Draw>
    ImGuiID widget_id(Draw &&draw, std::string_view address, const char *label)
    {
        ImGuiID result = 0;
        frame([&] {
            result = item_id(address, label);
            draw();
        });
        return result;
    }

    template <typename Draw>
    void click(Draw &&draw, ImVec2 point)
    {
        frame(draw, point, true);
        frame(draw, point, false);
    }

    template <typename Draw>
    void replace_text(Draw &&draw, ImVec2 point, std::string_view value,
                      bool enter, bool escape = false)
    {
        ImGuiIO &io = ImGui::GetIO();
        for (const unsigned char character : value)
            io.AddInputCharacter(character);
        frame(draw, point);
        if (escape)
        {
            io.AddKeyEvent(ImGuiKey_Escape, true);
            frame(draw, point);
            io.AddKeyEvent(ImGuiKey_Escape, false);
            frame(draw, point);
        }
        else if (enter)
        {
            io.AddKeyEvent(ImGuiKey_Enter, true);
            frame(draw, point);
            io.AddKeyEvent(ImGuiKey_Enter, false);
            frame(draw, point);
        }
    }
};

void draw_panel(GuiHarness &gui, AnimationPanel &panel, AssetDocument &document,
                const std::filesystem::path &assetRoot,
                ImVec2 mouse = {-10.0f, -10.0f}, bool down = false)
{
    gui.frame([&] { panel.draw(document, assetRoot); }, mouse, down);
}

void test_animation_numeric_transactions()
{
    GuiHarness gui;
    const auto assetRoot = std::filesystem::temp_directory_path() / "platformator-numeric-assets";
    const Json original = {
        {"frames", Json::array({{{"duration", 0.25}, {"textureWrapperFilePath", ""}}})},
        {"loop", true}};
    AssetDocument document(original, 19);
    AnimationPanel panel;
    const auto draw = [&] { panel.draw(document, assetRoot); };
    const ImGuiID durationId = gui.widget_id(draw,
        animation_address(document, assetRoot, "/frames/0/duration"),
        "Duration (seconds)");
    const ImVec2 durationPoint = gui.find_at_x(draw, durationId, 60.0f);

    gui.click(draw, durationPoint);
    gui.replace_text(draw, durationPoint, "123456789", false);
    require(document.root()["frames"][0]["duration"] == 0.25 && !document.can_undo(),
            "typing a valid duration draft must leave authored state and history unchanged");
    ImGuiID textureId = 0;
    gui.frame([&] {
        textureId = ImGui::GetID("Clear##frame-texture");
        draw();
    });
    const ImVec2 texturePoint = gui.find_anywhere(draw, textureId);
    gui.click(draw, texturePoint);
    require(document.root()["frames"][0]["duration"].get<double>() > 0.25 && document.can_undo(),
            "focus loss must commit one complete duration edit");
    require(document.undo().changed && document.root()["frames"][0]["duration"] == 0.25 &&
                !document.can_undo(),
            "one Undo must restore a focus-loss duration commit");

    gui.click(draw, durationPoint);
    ImGuiIO &io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_Enter, true);
    draw_panel(gui, panel, document, assetRoot, durationPoint);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    draw_panel(gui, panel, document, assetRoot, durationPoint);
    require(document.root()["frames"][0]["duration"] == 0.25 && !document.can_undo(),
            "committing an unchanged duration must add no history");
    gui.click(draw, durationPoint);
    gui.replace_text(draw, durationPoint, "2", true);
    require(document.root()["frames"][0]["duration"].get<double>() != 0.25 && document.can_undo(),
            "the animation frame duration control must commit one typed numeric edit");
    require(document.undo().changed && document.root()["frames"][0]["duration"] == 0.25 &&
                !document.can_undo(),
            "one Undo must restore an animation duration edit");

    gui.click(draw, durationPoint);
    gui.replace_text(draw, durationPoint, "91", false, true);
    require(document.root()["frames"][0]["duration"] == 0.25 && !document.can_undo(),
            "Escape must cancel an active duration draft without history");

    gui.click(draw, durationPoint);
    gui.replace_text(draw, durationPoint, "-", false);
    gui.click(draw, texturePoint);
    require(document.root()["frames"][0]["duration"] == 0.25 && !document.can_undo(),
            "an incomplete duration draft must be rejected when focus leaves the field");

    const ImGuiID plusId = gui.widget_id(draw,
        animation_address(document, assetRoot, "/frames/0/duration"), "+");
    const ImVec2 plusPoint = gui.find_anywhere(draw, plusId);
    gui.frame(draw, plusPoint, true);
    for (int repeat = 0; repeat < 30; ++repeat)
        draw_panel(gui, panel, document, assetRoot, plusPoint, true);
    require(document.root()["frames"][0]["duration"] == 0.25 && !document.can_undo(),
            "held step repeats must remain a draft until the button is released");
    draw_panel(gui, panel, document, assetRoot, plusPoint, false);
    require(document.root()["frames"][0]["duration"].get<double>() > 0.25 && document.can_undo(),
            "a held step interaction must commit once on release");
    require(document.undo().changed && document.root()["frames"][0]["duration"] == 0.25 &&
                !document.can_undo(),
            "one Undo must reverse the entire held step interaction");
}

void test_source_region_numeric_transactions_and_selection_scope(SDL_Texture *texture)
{
    GuiHarness gui;
    TemporaryDirectory temporary;
    const auto assetRoot = temporary.path() / "assets";
    std::filesystem::create_directories(assetRoot);
    std::ofstream(assetRoot / "sheet.png", std::ios::binary) << "fixture";
    constexpr std::string_view reference = "assets/sheet.png";
    const Json rect{{"x", 0.0}, {"y", 0.0}, {"w", 16.0}, {"h", 16.0}};
    AssetDocument document(Json{{"frames", Json::array({
        {{"duration", 0.25}, {"textureWrapperFilePath", reference},
         {"hasSourceRect", true}, {"sourceRect", rect}},
        {{"duration", 0.25}, {"textureWrapperFilePath", reference},
         {"hasSourceRect", true}, {"sourceRect", rect}}})},
        {"framesPerSecond", 4.0}, {"loop", true}}, 23);
    AnimationFrameRegionEditor editor;
    const auto draw = [&](std::size_t frameIndex) {
        editor.draw(document, frameIndex, assetRoot, reference, texture, 32, 32);
    };
    const auto drawZero = [&] { draw(0); };
    const auto drawFrame = [&](std::size_t frameIndex) {
        gui.frame([&] { draw(frameIndex); });
    };

    const ImGuiID xId = gui.widget_id(drawZero,
        region_address(document, 0, assetRoot, reference, "x"), "##X");
    const ImVec2 xPoint = gui.find_at_x(drawZero, xId, 60.0f);
    const ImGuiID yId = gui.widget_id(drawZero,
        region_address(document, 0, assetRoot, reference, "y"), "##Y");
    const ImVec2 yPoint = gui.find_at_x(drawZero, yId, 250.0f);
    const ImGuiID widthId = gui.widget_id(drawZero,
        region_address(document, 0, assetRoot, reference, "width"), "##Width");
    const ImVec2 widthPoint = gui.find_at_x(drawZero, widthId, 80.0f);
    const ImGuiID heightId = gui.widget_id(drawZero,
        region_address(document, 0, assetRoot, reference, "height"), "##Height");
    const ImVec2 heightPoint = gui.find_at_x(drawZero, heightId, 280.0f);
    require(std::abs(xPoint.y - yPoint.y) < 1.0f &&
                std::abs(widthPoint.y - heightPoint.y) < 1.0f && widthPoint.y > xPoint.y,
            "source-region fields must use an XY row followed by a paired dimensions row");
    gui.click(drawZero, xPoint);
    gui.replace_text(drawZero, xPoint, "5", false);
    require(document.root()["frames"][0]["sourceRect"]["x"] == 0.0 &&
                !document.can_undo(),
            "typing a source-region number must remain an uncommitted draft");

    drawFrame(1);
    drawFrame(0);
    require(document.root()["frames"][0]["sourceRect"]["x"] == 0.0 &&
                !document.can_undo(),
            "changing frames must discard the old frame's source-region draft");

    const ImGuiID currentXId = gui.widget_id(drawZero,
        region_address(document, 0, assetRoot, reference, "x"), "##X");
    const ImVec2 currentXPoint = gui.find_at_x(drawZero, currentXId, 60.0f);
    gui.click(drawZero, currentXPoint);
    gui.replace_text(drawZero, currentXPoint, "3", true);
    require(document.root()["frames"][0]["sourceRect"]["x"] == 3.0 && document.can_undo(),
            "the source-region X control must commit its validated numeric edit");
    require(document.undo().changed && document.root()["frames"][0]["sourceRect"]["x"] == 0.0 &&
                !document.can_undo(),
            "one Undo must reverse the complete source-region numeric edit");
}

void test_tab_and_project_transition_cancellation()
{
    TemporaryDirectory temporary;
    const auto projectA = temporary.path() / "project-a";
    const auto assetA = projectA / "assets";
    const auto projectB = temporary.path() / "project-b";
    const auto assetB = projectB / "content";
    std::filesystem::create_directories(assetA);
    std::filesystem::create_directories(assetB);
    std::ofstream(projectA / "CMakeLists.txt") << "project(fixture)\n";
    std::ofstream(projectB / "CMakeLists.txt") << "project(fixture)\n";
    const Json clip = {
        {"frames", Json::array({{{"duration", 0.25}, {"textureWrapperFilePath", ""}}})},
        {"loop", true}};
    const auto firstPath = assetA / "first.animset";
    const auto secondPath = assetA / "second.animset";
    std::ofstream(firstPath, std::ios::binary) << clip.dump();
    std::ofstream(secondPath, std::ios::binary) << clip.dump();

    DocumentManager manager(projectA, assetA, ProjectGeneration{31});
    const auto first = manager.open_animation(firstPath);
    const auto second = manager.open_animation(secondPath);
    require(first && second && manager.activate_animation_tab(*first),
            "animation tab transition fixture opens both clip tabs");
    auto *tab = manager.find_animation_tab(*first);
    require(tab != nullptr, "the first animation tab remains available");

    GuiHarness gui;
    const auto draw = [&] {
        tab->panel().draw(tab->document(), tab->asset_root());
    };
    const ImGuiID durationId = gui.widget_id(draw,
        animation_address(tab->document(), assetA, "/frames/0/duration"),
        "Duration (seconds)");
    const ImVec2 durationPoint = gui.find_at_x(draw, durationId, 60.0f);
    gui.click(draw, durationPoint);
    gui.replace_text(draw, durationPoint, "12", false);
    require(tab->document().root()["frames"][0]["duration"] == 0.25 &&
                !tab->document().can_undo(),
            "typing before switching tabs must remain a draft");
    require(manager.activate_animation_tab(*second) &&
                manager.activate_animation_tab(*first),
            "the active animation tab can move away and back");
    gui.frame(draw);
    require(tab->document().root()["frames"][0]["duration"] == 0.25 &&
                !tab->document().can_undo(),
            "switching animation tabs must cancel the prior tab's numeric draft");

    gui.click(draw, durationPoint);
    gui.replace_text(draw, durationPoint, "3", true);
    require(tab->document().root()["frames"][0]["duration"].get<double>() != 0.25 &&
                tab->document().can_undo(),
            "the field can start a fresh numeric edit after tab cancellation");
    require(tab->document().undo().changed && !tab->document().can_undo(),
            "the post-switch edit remains a single independent history entry");

    gui.click(draw, durationPoint);
    gui.replace_text(draw, durationPoint, "12", false);
    const auto rootChange = manager.set_project_roots(projectB, assetB, ProjectGeneration{32});
    require(static_cast<bool>(rootChange),
            "project-root transition commits for an animation tab");
    gui.frame([&] { tab->panel().draw(tab->document(), tab->asset_root()); });
    require(tab->document().root()["frames"][0]["duration"] == 0.25 &&
                !tab->document().can_undo(),
            "changing project roots must cancel all animation numeric drafts");
}
} // namespace

int main()
{
    try
    {
        AddressNumericDraftTestProbe::check_steps();
        const bool ownsSdlVideo = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
        if (ownsSdlVideo)
            require(SDL_InitSubSystem(SDL_INIT_VIDEO), "SDL video initializes for source region input");
        SDL_Surface *surface = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
        require(surface != nullptr, "source region test surface is created");
        SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
        require(renderer != nullptr, "source region test software renderer is created");
        SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                                  SDL_TEXTUREACCESS_STATIC, 32, 32);
        require(texture != nullptr, "source region test texture is created");

        test_animation_numeric_transactions();
        test_source_region_numeric_transactions_and_selection_scope(texture);
        test_tab_and_project_transition_cancellation();

        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
        if (ownsSdlVideo)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        std::cout << "numeric draft interaction tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception &error)
    {
        std::cerr << "numeric draft interaction tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
