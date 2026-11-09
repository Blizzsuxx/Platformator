#include "panels/spriteeditor.h"

#include "document.h"
#include "selection.h"
#include "test_support.h"
#include "assetdocument.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace platformator_editor;
using namespace platformator_editor_test;

int main()
{
    try
    {
        SpriteSourceRegion region{4.0, 2.0, 8.0, 6.0};
        std::string error;
        require(validate_sprite_source_region(region, 32, 16, error),
                "finite positive regions inside original source dimensions must validate");
        require(!validate_sprite_source_region({-1.0, 0.0, 2.0, 2.0}, 32, 16, error) &&
                    !validate_sprite_source_region({0.0, 0.0, 0.0, 2.0}, 32, 16, error) &&
                    !validate_sprite_source_region({28.0, 0.0, 8.0, 2.0}, 32, 16, error) &&
                    !validate_sprite_source_region({0.0, 15.0, 2.0, 2.0}, 32, 16, error) &&
                    !validate_sprite_source_region({0.0, 0.0, std::numeric_limits<double>::infinity(), 1.0},
                                                   32, 16, error) &&
                    !validate_sprite_source_region({0.0, 0.0, 1.0, 1.0}, 0, 16, error),
                "negative, empty, nonfinite, out-of-image, or unavailable bounds must reject");

        DocumentSession document = make_default_document("/project", "/project/assets", 7);
        auto &components = document.root[0]["components"];
        components.push_back(Json{{"id", 21}, {"type", 6},
                                  {"textureFilePath", "assets/old.png"},
                                  {"sourceRectEnabled", false},
                                  {"sourceRect", Json{{"x", 0.0}, {"y", 0.0},
                                                       {"w", 4.0}, {"h", 4.0},
                                                       {"vendorMetadata", "retain"}}}});
        ++document.structureRevision;
        rebuild_document_indexes(document);
        const Json before = document.root;
        EditCommandService commands;
        auto rejected = commit_sprite_source_region(commands, document, 0, 21,
            "assets/sprites/hero.png", {28.0, 0.0, 8.0, 2.0}, 32, 16);
        require(rejected.status == CommandStatus::Rejected && document.root == before &&
                    commands.history_status().undoCommands == 0,
                "invalid source regions must not dirty the document or create history");
        rejected = commit_sprite_source_region(commands, document, 0, 21,
            "assets/../outside.png", region, 32, 16);
        require(rejected.status == CommandStatus::Rejected && document.root == before,
                "a source region cannot assign a texture outside the project asset root");

        const auto committed = commit_sprite_source_region(commands, document, 0, 21,
            "assets/sprites/hero.png", region, 32, 16);
        require(committed.status == CommandStatus::Changed &&
                    commands.history_status().undoCommands == 1 &&
                    document.root[0]["components"][1]["textureFilePath"] == "assets/sprites/hero.png" &&
                    document.root[0]["components"][1]["sourceRectEnabled"] == true &&
                    document.root[0]["components"][1]["sourceRect"] ==
                        Json{{"x", 4.0}, {"y", 2.0}, {"w", 8.0}, {"h", 6.0},
                             {"vendorMetadata", "retain"}},
                "texture assignment and source pixels must commit together without losing unknown rectangle fields");

        SelectionModel selection;
        const auto undone = commands.undo(document, selection);
        require(undone && document.root == before && commands.history_status().canRedo,
                "one Undo must restore the exact prior texture and source rectangle");
        const auto redone = commands.redo(document, selection);
        require(redone && document.root[0]["components"][1]["sourceRect"]["w"] == 8.0 &&
                    document.root[0]["components"][1]["textureFilePath"] == "assets/sprites/hero.png",
                "Redo must restore the committed image-pixel region exactly");

        const auto projectRoot = std::filesystem::temp_directory_path() /
                                 "platformator-animation-region-test";
        std::filesystem::remove_all(projectRoot);
        const auto assetRoot = projectRoot / "assets";
        std::filesystem::create_directories(assetRoot);
        {
            std::ofstream image(assetRoot / "sheet.png", std::ios::binary);
            image << "fixture";
        }
        const Json animationBefore = {
            {"frames", Json::array({{{"duration", 0.2},
                                     {"textureWrapperFilePath", "assets/old.png"},
                                     {"sourceRect", {{"x", 0}, {"y", 0}, {"w", 2}, {"h", 2},
                                                      {"vendor", "retain"}}}}})},
            {"framesPerSecond", 5.0}, {"loop", true}, {"vendorClip", 9}};
        AssetDocument animation(animationBefore);
        const auto invalidRegion = commit_animation_frame_source_region(
            {animation, 0}, "assets/sheet.png", assetRoot, {28, 0, 8, 2}, 32, 16);
        require(!invalidRegion.error.empty() && animation.root() == animationBefore &&
                    !animation.can_undo(),
                "invalid frame regions must preserve the asset and its history");
        const auto invalidTexture = commit_animation_frame_source_region(
            {animation, 0}, "assets/../outside.png", assetRoot, region, 32, 16);
        require(!invalidTexture.error.empty() && animation.root() == animationBefore,
                "frame regions must reject paths outside the project asset root");
        const auto frameCommit = commit_animation_frame_source_region(
            {animation, 0}, "assets/sheet.png", assetRoot, region, 32, 16);
        require(frameCommit.changed && animation.can_undo() &&
                    animation.root()["frames"][0]["textureWrapperFilePath"] == "assets/sheet.png" &&
                    animation.root()["frames"][0]["hasSourceRect"] == true &&
                    animation.root()["frames"][0]["sourceRect"] ==
                        Json{{"x", 4.0}, {"y", 2.0}, {"w", 8.0}, {"h", 6.0},
                             {"vendor", "retain"}},
                "animation frame texture and image-pixel bounds must commit atomically and retain unknown fields");
        require(animation.undo().changed && animation.root() == animationBefore &&
                    animation.redo().changed && animation.root()["frames"][0]["sourceRect"]["w"] == 8.0,
                "one asset Undo/Redo must restore the exact prior and committed frame regions");
        std::filesystem::remove_all(projectRoot);

        std::cout << "sprite region tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "sprite region tests failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
