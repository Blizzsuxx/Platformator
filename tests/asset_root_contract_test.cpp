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
            try
            {
                runtime.loadTexture("assets/../original-request.png");
                throw std::runtime_error("Invalid traversal unexpectedly accepted");
            }
            catch (const std::invalid_argument &error)
            {
                require(std::string(error.what()).find("assets/../original-request.png") != std::string::npos,
                        "Invalid asset diagnostics retain the original requested path");
            }
            rejects([&] { runtime.loadAudio("audio/missing.wav"); });
            rejects([&] { runtime.loadAnimationClip("animations/missing.animset"); });
            fs::copy_file(source / "textures/missing.png", directory / "outside.png",
                          fs::copy_options::overwrite_existing);
            fs::remove(root / "outside-link.png");
            fs::create_symlink(directory / "outside.png", root / "outside-link.png");
            rejects([&] { runtime.loadTexture("assets/outside-link.png"); });
            const auto missing = [](const std::string &path, auto &&load) {
                try
                {
                    load(path);
                    throw std::runtime_error("Missing asset was unexpectedly accepted: " + path);
                }
                catch (const std::runtime_error &error)
                {
                    require(std::string(error.what()).find(path) != std::string::npos,
                            "Missing asset diagnostic must include the original request");
                    require(std::string(error.what()).find("unexpectedly accepted") == std::string::npos,
                            "Missing asset must fail rather than use a fallback");
                }
            };
            missing("assets/absent.png", [&](const auto &path) { runtime.loadTexture(path); });
            missing("assets/absent.wav", [&](const auto &path) { runtime.loadAudio(path); });
            missing("assets/absent.animset", [&](const auto &path) { runtime.loadAnimationClip(path); });
            for (const auto &name : {"broken.png", "broken.wav", "broken.animset"})
            {
                std::ofstream broken(root / name);
                broken << "not a valid asset";
            }
            missing("assets/broken.png", [&](const auto &path) { runtime.loadTexture(path); });
            missing("assets/broken.wav", [&](const auto &path) { runtime.loadAudio(path); });
            missing("assets/broken.animset", [&](const auto &path) { runtime.loadAnimationClip(path); });
            AssetReference<TextureWrapper> explicitMissing(runtime.loadTexture("assets/textures/missing.png"));
            require(static_cast<bool>(explicitMissing), "Explicit formerly-fallback file remains loadable");
            require(runtime.loadTexture("") == nullptr && runtime.loadAudio("") == nullptr &&
                        runtime.loadAnimationClip("") == nullptr,
                    "Empty paths still represent no asset");

            fs::copy_file(source / "textures/missing.png", root / "textures/temporary.png",
                          fs::copy_options::overwrite_existing);
            {
                AssetReference<TextureWrapper> held(runtime.loadTexture("assets/textures/temporary.png"));
                auto *expected = held.get();
                require(runtime.loadTexture("assets\\textures\\temporary.png") == expected,
                        "Validated aliases share the same resource");
                fs::remove(root / "textures/temporary.png");
                require(runtime.loadTexture("assets/textures/temporary.png") == expected &&
                            runtime.loadTexture("assets\\textures\\temporary.png") == expected,
                        "Previously resolved aliases reuse retained resources after deletion");
            }
            missing("assets/textures/temporary.png", [&](const auto &path) { runtime.loadTexture(path); });
            missing("assets\\textures\\temporary.png", [&](const auto &path) { runtime.loadTexture(path); });
            fs::copy_file(source / "audio/missing.wav", root / "audio/temporary.wav",
                          fs::copy_options::overwrite_existing);
            {
                AssetReference<AudioWrapper> held(runtime.loadAudio("assets/audio/temporary.wav"));
                auto *expected = held.get();
                fs::remove(root / "audio/temporary.wav");
                require(runtime.loadAudio("assets/audio/temporary.wav") == expected,
                        "Retained audio request survives file deletion");
            }
            missing("assets/audio/temporary.wav", [&](const auto &path) { runtime.loadAudio(path); });
            fs::copy_file(root / "animations/nested/Valid.animset", root / "animations/nested/temporary.animset",
                          fs::copy_options::overwrite_existing);
            {
                AssetReference<AnimationClip> held(runtime.loadAnimationClip("assets/animations/nested/temporary.animset"));
                auto *expected = held.get();
                fs::remove(root / "animations/nested/temporary.animset");
                require(runtime.loadAnimationClip("assets/animations/nested/temporary.animset") == expected,
                        "Retained animation request survives file deletion");
            }
            missing("assets/animations/nested/temporary.animset", [&](const auto &path) { runtime.loadAnimationClip(path); });
            fs::create_symlink(root / "textures/MiXeD.PNG", root / "cached-link.png");
            {
                AssetReference<TextureWrapper> link(runtime.loadTexture("assets/cached-link.png"));
                auto *expected = link.get();
                fs::remove(root / "cached-link.png");
                fs::create_symlink(directory / "outside.png", root / "cached-link.png");
                require(runtime.loadTexture("assets/cached-link.png") == expected,
                        "Previously validated request remains stable after symlink change");
                rejects([&] { runtime.loadTexture("assets\\cached-link.png"); });
            }
            fs::remove(root / "cached-link.png");
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
            runtime.simulateFrame(dt);
            auto *script = runtime.getGameObject("case")->getComponent<ScriptComponent>();
            require(script->getBehaviors().size() == 1 && script->getPendingBehaviors().empty() &&
                        !script->getBehavior<StartupTexture>()->texture,
                    "Invalid behavior asset paths log startup errors and leave the behavior runnable and unresolved");
            clipDocument["frames"][0]["textureWrapperFilePath"] = "../../textures/MiXeD.PNG";
            {
                std::ofstream file(root / "animations/nested/Invalid.animset");
                file << clipDocument;
            }
            rejects([&] { runtime.loadAnimationClip("assets/animations/nested/Invalid.animset"); });
            clipDocument["frames"][0]["textureWrapperFilePath"] = "assets/textures/missing-nested.png";
            {
                std::ofstream file(root / "animations/nested/MissingFrame.animset");
                file << clipDocument;
            }
            try
            {
                runtime.loadAnimationClip("assets/animations/nested/MissingFrame.animset");
                throw std::runtime_error("Missing nested frame unexpectedly accepted");
            }
            catch (const std::exception &error)
            {
                require(std::string(error.what()).find("assets/textures/missing-nested.png") != std::string::npos,
                        "Nested frame failure identifies the missing frame request");
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
