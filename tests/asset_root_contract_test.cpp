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
    AssetReference<AnimationClip> clip;
    SERIALIZABLE_SCRIPT(StartupTexture, texture, clip);
};
} // namespace
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
        fs::create_directories(root / "animations/nested");
        const auto source = fs::path(__FILE__).parent_path().parent_path() / "assets";
        for (const auto &name : {"MiXeD.PNG", "missing.png"})
            fs::copy_file(source / "textures/missing.png", root / "textures" / name,
                          fs::copy_options::overwrite_existing);
        fs::copy_file(source / "audio/missing.wav", root / "audio/missing.wav", fs::copy_options::overwrite_existing);
        fs::copy_file(source / "animations/missing.animset", root / "animations/missing.animset",
                      fs::copy_options::overwrite_existing);
        nlohmann::json clipDocument;
        {
            std::ifstream file(source / "animations/missing.animset");
            file >> clipDocument;
        }
        clipDocument["frames"][0]["textureWrapperFilePath"] = "assets/textures/MiXeD.PNG";
        {
            std::ofstream file(root / "animations/nested/Valid.animset");
            file << clipDocument;
        }
        RuntimeOptions options;
        options.assetRoot = fs::relative(root, previous).string();
        {
            platformator::Runtime runtime(options);
            require(runtime.getAssetRoot() == fs::weakly_canonical(root),
                    "Explicit relative root must normalize once at startup");
            AssetReference<TextureWrapper> texture(runtime.loadTexture("assets/textures/MiXeD.PNG"));
            require(texture && runtime.loadTexture("assets\\textures\\MiXeD.PNG") == texture.get(),
                    "Assets paths preserve filename case and normalize separators");
            const auto rejects = [](auto &&action) {
                bool failed = false;
                try
                {
                    action();
                }
                catch (const std::exception &)
                {
                    failed = true;
                }
                require(failed, "Unsupported asset paths must fail instead of using a fallback");
            };
            for (const auto &invalid : {"textures/MiXeD.PNG", "../textures/MiXeD.PNG", "./assets/textures/MiXeD.PNG",
                                        "assets/scenes/../textures/MiXeD.PNG", "assets/../outside.png", "assets"})
                rejects([&] { runtime.loadTexture(invalid); });
            rejects([&] { runtime.loadTexture((root / "textures/MiXeD.PNG").string()); });
            rejects([&] { runtime.loadAudio("audio/missing.wav"); });
            rejects([&] { runtime.loadAnimationClip("animations/missing.animset"); });
            fs::copy_file(source / "textures/missing.png", directory / "outside.png",
                          fs::copy_options::overwrite_existing);
            fs::remove(root / "outside-link.png");
            fs::create_symlink(directory / "outside.png", root / "outside-link.png");
            rejects([&] { runtime.loadTexture("assets/outside-link.png"); });
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
                                          {"textureFilePath", "assets/textures/MiXeD.PNG"},
                                          {"flip", 0},
                                          {"width", 20},
                                          {"height", 20},
                                          {"sourceRectEnabled", false}}};
            document[0]["components"].push_back({{"id", 2},
                                                 {"type", SCRIPT},
                                                 {"behaviors",
                                                  {{{"type", "StartupTexture"},
                                                    {"texture", "assets/textures/MiXeD.PNG"},
                                                    {"clip", "assets/animations/nested/Valid.animset"}}}}});
            {
                std::ofstream file(scene);
                file << document;
            }
            runtime.loadScene("assets/scenes/Case.scene");
            require(runtime.getGameObject("case"), "Scene component assets must resolve inside the configured root");
            auto *behavior =
                runtime.getGameObject("case")->getComponent<ScriptComponent>()->getBehavior<StartupTexture>();
            require(!behavior->texture, "Behavior assets remain unresolved until startup");
            runtime.simulateFrame(dt);
            require(behavior->texture.get() == texture.get() && behavior->clip &&
                        behavior->clip->getFrames()[0].textureWrapper == texture.get(),
                    "Behavior and nested animation assets use the same root after cwd changes");
            runtime.saveScene(scene.string());
            {
                nlohmann::json saved;
                std::ifstream file(scene);
                file >> saved;
                bool checked = false;
                for (const auto &component : saved[0]["components"])
                    if (component["type"] == SCRIPT)
                    {
                        const auto &fields = component["behaviors"][0];
                        require(fields["texture"] == "assets/textures/MiXeD.PNG" &&
                                    fields["clip"] == "assets/animations/nested/Valid.animset",
                                "Serialization preserves canonical case-sensitive assets paths");
                        checked = true;
                    }
                require(checked, "Saved scene must include the behavior asset fields");
            }
            auto *committed = runtime.getGameObject("case");
            document[0]["components"][0]["textureFilePath"] = "../textures/MiXeD.PNG";
            {
                std::ofstream file(scene);
                file << document;
            }
            rejects([&] { runtime.loadScene(scene.string()); });
            require(runtime.getGameObject("case") == committed,
                    "Relative component assets reject preparation without replacing the scene");
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
            document[0]["components"][0]["textureFilePath"] = "assets/textures/MiXeD.PNG";
            document[0]["components"][1]["behaviors"][0]["texture"] = "../textures/MiXeD.PNG";
            {
                std::ofstream file(scene);
                file << document;
            }
            runtime.loadScene(scene.string());
            rejects([&] { runtime.simulateFrame(dt); });
            require(runtime.getGameObject("case"), "Relative behavior assets fail at startup after commit");
            clipDocument["frames"][0]["textureWrapperFilePath"] = "../../textures/MiXeD.PNG";
            {
                std::ofstream file(root / "animations/nested/Invalid.animset");
                file << clipDocument;
            }
            rejects([&] { runtime.loadAnimationClip("assets/animations/nested/Invalid.animset"); });
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
