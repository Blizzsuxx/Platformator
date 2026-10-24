#include "test_support.h"
#include <iostream>
#include <platformator/assetreference.h>
#include <platformator/scene.h>
#include <platformator/scriptcomponent.h>
#include <platformator/scriptregistration.h>
#include <SDL3/SDL.h>

using namespace test_support;
namespace fs = std::filesystem;
namespace
{
struct StartupTexture : ::Behavior
{
    AssetReference<TextureWrapper> texture;
    SERIALIZABLE_SCRIPT(StartupTexture, texture);
};
}
int main()
{
    headlessEnvironment();
    const auto previous = fs::current_path();
    const auto directory = fs::absolute("asset-root-fixtures");
    const auto root = directory / "Data";
    try
    {
        fs::create_directories(root / "textures");
        fs::create_directories(root / "scenes");
        fs::create_directories(root / "audio");
        fs::create_directories(root / "animations");
        const auto source = fs::path(__FILE__).parent_path().parent_path() / "assets";
        for (const auto &name : {"MiXeD.PNG", "missing.png"})
            fs::copy_file(source / "textures/missing.png", root / "textures" / name,
                          fs::copy_options::overwrite_existing);
        fs::copy_file(source / "audio/missing.wav", root / "audio/missing.wav", fs::copy_options::overwrite_existing);
        fs::copy_file(source / "animations/missing.animset", root / "animations/missing.animset",
                      fs::copy_options::overwrite_existing);
        RuntimeOptions options;
        options.assetRoot = fs::relative(root, previous).string();
        {
            platformator::Runtime runtime(options);
            require(runtime.getAssetRoot() == fs::weakly_canonical(root),
                    "Explicit relative root must normalize once at startup");
            AssetReference<TextureWrapper> texture(runtime.loadTexture("assets/textures/MiXeD.PNG"));
            require(texture && runtime.loadTexture("textures/MiXeD.PNG") == texture.get(),
                    "Case-preserving logical/legacy paths share a cache entry");
            AssetReference<TextureWrapper> fallback(runtime.loadTexture("assets/absent.png"));
            require(fallback && fallback->getFilePath() == "assets/textures/missing.png",
                    "Missing optional textures use the packaged fallback");
            AssetReference<AudioWrapper> audio(runtime.loadAudio("assets/absent.wav"));
            AssetReference<AnimationClip> animation(runtime.loadAnimationClip("assets/absent.animset"));
            require(audio && animation && !animation->getFrames().empty(),
                    "All intentional fallback assets must be usable without Mario");
            fs::create_directories(directory / "Elsewhere");
            fs::current_path(directory / "Elsewhere");
            require(runtime.loadTexture("assets/textures/MiXeD.PNG") == texture.get(),
                    "Changing cwd cannot alter an active root");
            const auto scene = root / "scenes/Case.scene";
            writeScene(scene, "case");
            nlohmann::json document;
            {
                std::ifstream file(scene);
                file >> document;
            }
            document[0]["components"] = {{{"id", 1},
                                          {"type", SPRITE},
                                          {"textureFilePath", "../textures/MiXeD.PNG"},
                                          {"flip", 0},
                                          {"width", 20},
                                          {"height", 20},
                                          {"sourceRectEnabled", false}}};
            document[0]["components"].push_back(
                {{"id", 2},
                 {"type", SCRIPT},
                 {"behaviors", {{{"type", "StartupTexture"}, {"texture", "../textures/MiXeD.PNG"}}}}});
            {
                std::ofstream file(scene);
                file << document;
            }
            runtime.loadScene("assets/scenes/Case.scene");
            require(runtime.getGameObject("case"),
                    "Scene-relative legacy resources must resolve inside the configured root");
            auto *behavior = runtime.getGameObject("case")->getComponent<ScriptComponent>()->getBehavior<StartupTexture>();
            require(!behavior->texture, "Behavior assets remain unresolved until startup");
            runtime.simulateFrame(dt);
            require(behavior->texture.get() == texture.get(),
                    "Startup resolution must retain scene-relative paths and filename case after cwd changes");
            document[0]["components"][0]["textureFilePath"] = "assets/absent-required.png";
            {
                std::ofstream file(scene);
                file << document;
            }
            bool failed = false;
            try
            {
                runtime.loadScene("assets/scenes/Case.scene");
            }
            catch (const std::exception &)
            {
                failed = true;
            }
            require(failed && runtime.getGameObject("case"), "Required scene resources cannot fall back silently");
            for (const auto &escape : {"assets/../outside.png", "../../outside.png"})
            {
                failed = false;
                try
                {
                    runtime.loadTexture(escape);
                }
                catch (const std::exception &)
                {
                    failed = true;
                }
                require(failed, "Logical/legacy paths cannot escape the configured root");
            }
        }
        fs::current_path(previous);
        {
            platformator::Runtime runtime;
            require(runtime.getAssetRoot() == fs::weakly_canonical(fs::path(SDL_GetBasePath()) / "assets"),
                    "Default root must use executable/assets without searching for demo folders");
        }
        fs::remove_all(directory);
        std::cout << "[PASS] explicit/default asset roots\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        fs::current_path(previous);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
