#include <cstdlib>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "animationclip.h"
#include "animator.h"
#include "audio.h"
#include "behaviorfactoryregistry.h"
#include "constants.h"
#include "gamemanager.h"
#include "scene.h"
#include "scriptcomponent.h"

namespace
{
        void require(bool condition, const std::string &message)
        {
                if (!condition)
                {
                        throw std::runtime_error(message);
                }
        }

        void configureHeadlessEnvironment()
        {
                setenv("SDL_VIDEODRIVER", "dummy", 1);
                setenv("SDL_AUDIODRIVER", "dummy", 1);
                setenv("SDL_RENDER_DRIVER", "software", 1);
        }

        void drainDeletion(GameManager &gameManager)
        {
                gameManager.simulateFrame(FRAME_TIME);
        }

        std::filesystem::path findWorkspaceRelativePath(const std::filesystem::path &relativePath)
        {
                std::filesystem::path searchDirectory = std::filesystem::current_path();

                while (true)
                {
                        std::filesystem::path candidate = searchDirectory / relativePath;
                        if (std::filesystem::exists(candidate))
                        {
                                return candidate.lexically_normal();
                        }

                        std::filesystem::path parentDirectory = searchDirectory.parent_path();
                        if (parentDirectory == searchDirectory)
                        {
                                break;
                        }

                        searchDirectory = parentDirectory;
                }

                throw std::runtime_error("Scene script round-trip test could not locate resource '" + relativePath.generic_string() + "'.");
        }

        class TestSerializedBehavior : public Behavior
        {
        public:
                TestSerializedBehavior()
                    : displayName(""), speed(0.0f), allowDash(false), precision(0.0), offset(Eigen::Vector2f::Zero()), target(), emitter(), icon(), idleAnimset(), runAnimset(), sound()
                {
                }

                BEHAVIOR_FIELDS(
                    TestSerializedBehavior,
                    BEHAVIOR_FIELD(displayName),
                    BEHAVIOR_FIELD(speed),
                    BEHAVIOR_FIELD(allowDash),
                    BEHAVIOR_FIELD(precision),
                    BEHAVIOR_FIELD(offset),
                    BEHAVIOR_FIELD(target),
                    BEHAVIOR_FIELD(emitter),
                    BEHAVIOR_FIELD(icon),
                    BEHAVIOR_FIELD(idleAnimset),
                    BEHAVIOR_FIELD(runAnimset),
                    BEHAVIOR_FIELD(sound));

                void start() override
                {
                        Animator *animator = getGameObject()->getComponent<Animator>();
                        require(animator != nullptr, "Scene script round-trip test expected an explicit animator component.");

                        require(animator->play(idleAnimset.get()),
                                "Scene script round-trip test failed to play the initial script-owned animset directly through the animator.");
                }

                const std::string &getDisplayName() const
                {
                        return displayName;
                }

                float getSpeed() const
                {
                        return speed;
                }

                bool getAllowDash() const
                {
                        return allowDash;
                }

                double getPrecision() const
                {
                        return precision;
                }

                const Eigen::Vector2f &getOffset() const
                {
                        return offset;
                }

                const platformator_behavior_detail::NamedGameObjectReference &getTarget() const
                {
                        return target;
                }

                const platformator_behavior_detail::NamedComponentReference<Audio> &getEmitter() const
                {
                        return emitter;
                }

                const platformator_behavior_detail::TextureAssetReference &getIcon() const
                {
                        return icon;
                }

                const platformator_behavior_detail::AnimationClipReference &getIdleAnimset() const
                {
                        return idleAnimset;
                }

                const platformator_behavior_detail::AnimationClipReference &getRunAnimset() const
                {
                        return runAnimset;
                }

                const platformator_behavior_detail::AudioAssetReference &getSound() const
                {
                        return sound;
                }

        private:
                std::string displayName;
                float speed;
                bool allowDash;
                double precision;
                Eigen::Vector2f offset;
                platformator_behavior_detail::NamedGameObjectReference target;
                platformator_behavior_detail::NamedComponentReference<Audio> emitter;
                platformator_behavior_detail::TextureAssetReference icon;
                platformator_behavior_detail::AnimationClipReference idleAnimset;
                platformator_behavior_detail::AnimationClipReference runAnimset;
                platformator_behavior_detail::AudioAssetReference sound;
        };

        class StrictBoolBehavior : public Behavior
        {
        public:
                StrictBoolBehavior() : allowDash(false)
                {
                }

                BEHAVIOR_FIELDS(
                    StrictBoolBehavior,
                    BEHAVIOR_FIELD(allowDash));

                bool getAllowDash() const
                {
                        return allowDash;
                }

        private:
                bool allowDash;
        };

        class ReferenceResolutionBehavior : public Behavior
        {
        public:
                ReferenceResolutionBehavior()
                    : target(), emitter(), icon(), idleAnimset(), sound()
                {
                }

                BEHAVIOR_FIELDS(
                    ReferenceResolutionBehavior,
                    BEHAVIOR_FIELD(target),
                    BEHAVIOR_FIELD(emitter),
                    BEHAVIOR_FIELD(icon),
                    BEHAVIOR_FIELD(idleAnimset),
                    BEHAVIOR_FIELD(sound));

                const platformator_behavior_detail::NamedGameObjectReference &getTarget() const
                {
                        return target;
                }

                const platformator_behavior_detail::NamedComponentReference<Audio> &getEmitter() const
                {
                        return emitter;
                }

                const platformator_behavior_detail::TextureAssetReference &getIcon() const
                {
                        return icon;
                }

                const platformator_behavior_detail::AnimationClipReference &getIdleAnimset() const
                {
                        return idleAnimset;
                }

                const platformator_behavior_detail::AudioAssetReference &getSound() const
                {
                        return sound;
                }

        private:
                platformator_behavior_detail::NamedGameObjectReference target;
                platformator_behavior_detail::NamedComponentReference<Audio> emitter;
                platformator_behavior_detail::TextureAssetReference icon;
                platformator_behavior_detail::AnimationClipReference idleAnimset;
                platformator_behavior_detail::AudioAssetReference sound;
        };

        template <typename T>
        T *findBehavior(ScriptComponent *scriptComponent)
        {
                for (Behavior *candidate : scriptComponent->getBehaviors())
                {
                        if (T *typedBehavior = dynamic_cast<T *>(candidate))
                        {
                                return typedBehavior;
                        }
                }

                return nullptr;
        }

        void registerSceneScriptTestBehaviors()
        {
                static bool registered = false;
                if (registered)
                {
                        return;
                }

                BehaviorFactoryRegistry &registry = BehaviorFactoryRegistry::getInstance();
                registry.registerBehavior<TestSerializedBehavior>("TestSerializedBehavior");
                registry.registerBehavior<StrictBoolBehavior>("StrictBoolBehavior");
                registry.registerBehavior<ReferenceResolutionBehavior>("ReferenceResolutionBehavior");
                registered = true;
        }

        void destroyNamedObject(GameManager &gameManager, const std::string &name)
        {
                GameObject *gameObject = gameManager.getGameObject(name);
                if (gameObject != nullptr)
                {
                        gameManager.destroyGameObject(gameObject);
                        drainDeletion(gameManager);
                }
        }

        void testSceneScriptRoundTrip()
        {
                registerSceneScriptTestBehaviors();

                GameManager &gameManager = GameManager::getInstance();
                std::filesystem::path scenePath = std::filesystem::current_path() / "scene_script_roundtrip_regression.scene";
                std::filesystem::path idleAnimationSetPath = std::filesystem::current_path() / "scene_script_roundtrip_idle.animset";
                std::filesystem::path runAnimationSetPath = std::filesystem::current_path() / "scene_script_roundtrip_run.animset";
                const std::filesystem::path audioAssetPath = findWorkspaceRelativePath(std::filesystem::path("examples") / "mario" / "assets" / "audio" / "jump.wav");
                const std::filesystem::path textureAssetPath = findWorkspaceRelativePath(std::filesystem::path("examples") / "mario" / "assets" / "player" / "idle_0.png");
                const std::filesystem::path runTextureAssetPath = findWorkspaceRelativePath(std::filesystem::path("examples") / "mario" / "assets" / "player" / "run_0.png");
                const std::string sceneAudioPath = std::filesystem::relative(audioAssetPath, scenePath.parent_path()).generic_string();
                const std::string sceneTexturePath = std::filesystem::relative(textureAssetPath, scenePath.parent_path()).generic_string();
                const std::string sceneIdleAnimationSetPath = std::filesystem::relative(idleAnimationSetPath, scenePath.parent_path()).generic_string();
                const std::string sceneRunAnimationSetPath = std::filesystem::relative(runAnimationSetPath, scenePath.parent_path()).generic_string();
                const std::string idleAnimationTexturePath = std::filesystem::relative(textureAssetPath, idleAnimationSetPath.parent_path()).generic_string();
                const std::string runAnimationTexturePath = std::filesystem::relative(runTextureAssetPath, runAnimationSetPath.parent_path()).generic_string();

                auto cleanup = [&]()
                {
                        destroyNamedObject(gameManager, "Scripted Object");
                        destroyNamedObject(gameManager, "Audio Emitter");

                        std::error_code errorCode;
                        std::filesystem::remove(scenePath, errorCode);
                        std::filesystem::remove(idleAnimationSetPath, errorCode);
                        std::filesystem::remove(runAnimationSetPath, errorCode);
                };

                cleanup();

                try
                {
                        std::ofstream idleAnimationSetFile(idleAnimationSetPath);
                        require(idleAnimationSetFile.is_open(), "Scene script round-trip test failed to create the temporary idle animation set file.");
                        idleAnimationSetFile << "format = \"platformator_animset\"\n";
                        idleAnimationSetFile << "version = 1\n";
                        idleAnimationSetFile << "name = \"idle\"\n";
                        idleAnimationSetFile << "fps = 1\n";
                        idleAnimationSetFile << "loop = true\n";
                        idleAnimationSetFile << "size = [32, 32]\n";
                        idleAnimationSetFile << "frames = [\n";
                        idleAnimationSetFile << "    \"" << idleAnimationTexturePath << "\",\n";
                        idleAnimationSetFile << "]\n";
                        idleAnimationSetFile.close();

                        std::ofstream runAnimationSetFile(runAnimationSetPath);
                        require(runAnimationSetFile.is_open(), "Scene script round-trip test failed to create the temporary run animation set file.");
                        runAnimationSetFile << "format = \"platformator_animset\"\n";
                        runAnimationSetFile << "version = 1\n";
                        runAnimationSetFile << "name = \"run\"\n";
                        runAnimationSetFile << "fps = 2\n";
                        runAnimationSetFile << "loop = true\n";
                        runAnimationSetFile << "size = [32, 32]\n";
                        runAnimationSetFile << "frames = [\n";
                        runAnimationSetFile << "    \"" << runAnimationTexturePath << "\",\n";
                        runAnimationSetFile << "]\n";
                        runAnimationSetFile.close();

                        std::ofstream sceneFile(scenePath);
                        require(sceneFile.is_open(), "Scene script round-trip test failed to create the temporary scene file.");

                        sceneFile << "format = \"platformator_scene\"\n";
                        sceneFile << "version = 1\n\n";
                        sceneFile << "[[objects]]\n";
                        sceneFile << "name = \"Audio Emitter\"\n\n";
                        sceneFile << "[objects.audio]\n";
                        sceneFile << "gain = 0.4\n\n";
                        sceneFile << "[[objects]]\n";
                        sceneFile << "name = \"Scripted Object\"\n\n";
                        sceneFile << "[objects.animator]\n";
                        sceneFile << "playbackSpeed = 1\n\n";
                        sceneFile << "[[objects.scripts]]\n";
                        sceneFile << "type = \"TestSerializedBehavior\"\n";
                        sceneFile << "displayName = \"Player One\"\n";
                        sceneFile << "speed = 123.5\n";
                        sceneFile << "allowDash = true\n";
                        sceneFile << "precision = 987.654321\n";
                        sceneFile << "offset = [11.25, -4.5]\n";
                        sceneFile << "target = \"Target Dummy\"\n";
                        sceneFile << "emitter = \"Audio Emitter\"\n";
                        sceneFile << "icon = \"" << sceneTexturePath << "\"\n";
                        sceneFile << "idleAnimset = \"" << sceneIdleAnimationSetPath << "\"\n";
                        sceneFile << "runAnimset = \"" << sceneRunAnimationSetPath << "\"\n";
                        sceneFile << "sound = \"" << sceneAudioPath << "\"\n";
                        sceneFile.close();

                        Scene scene(scenePath.string());
                        gameManager.loadScene(scene);
                        gameManager.simulateFrame(FRAME_TIME);

                        GameObject *scriptedObject = gameManager.getGameObject("Scripted Object");
                        require(scriptedObject != nullptr, "Scene script round-trip test expected one loaded object.");

                        ScriptComponent *scriptComponent = scriptedObject->getComponent<ScriptComponent>();
                        require(scriptComponent != nullptr, "Scene script round-trip test failed to load the script component.");
                        Animator *animator = scriptedObject->getComponent<Animator>();
                        require(animator != nullptr, "Scene script round-trip test failed to load the explicit animator component.");
                        require(animator->getCurrentClipName() == "idle",
                                "Scene script round-trip test failed to restore the animator's current clip.");
                        require(animator->getIsPlaying(),
                                "Scene script round-trip test failed to leave the animator playing after the initial script-owned animset started.");

                        TestSerializedBehavior *behavior = findBehavior<TestSerializedBehavior>(scriptComponent);
                        require(behavior != nullptr, "Scene script round-trip test failed to load the test behavior.");
                        require(behavior->getDisplayName() == "Player One",
                                "Scene script round-trip test failed to deserialize the string property.");
                        require(std::abs(behavior->getSpeed() - 123.5f) <= 1e-5f,
                                "Scene script round-trip test failed to deserialize the float property.");
                        require(behavior->getAllowDash(),
                                "Scene script round-trip test failed to deserialize the bool property.");
                        require(std::abs(behavior->getPrecision() - 987.654321) <= 1e-12,
                                "Scene script round-trip test failed to deserialize the double property.");
                        require((behavior->getOffset() - Eigen::Vector2f(11.25f, -4.5f)).norm() <= 1e-5f,
                                "Scene script round-trip test failed to deserialize the Vector2f property.");
                        require(behavior->getTarget().name == "Target Dummy",
                                "Scene script round-trip test failed to deserialize the named object reference property.");
                        require(behavior->getEmitter().name == "Audio Emitter",
                                "Scene script round-trip test failed to deserialize the named component reference property.");
                        require(behavior->getEmitter().get() != nullptr,
                                "Scene script round-trip test failed to resolve the named component reference after behavior start.");
                        require(behavior->getIcon().path == sceneTexturePath,
                                "Scene script round-trip test failed to deserialize the texture asset reference path.");
                        require(behavior->getIcon().get() != nullptr,
                                "Scene script round-trip test failed to resolve the texture asset reference after behavior start.");
                        require(behavior->getIdleAnimset().path == sceneIdleAnimationSetPath,
                                "Scene script round-trip test failed to deserialize the idle animset script property.");
                        require(behavior->getIdleAnimset().get() != nullptr,
                                "Scene script round-trip test failed to resolve the idle animset script property after behavior start.");
                        require(behavior->getRunAnimset().path == sceneRunAnimationSetPath,
                                "Scene script round-trip test failed to deserialize the run animset script property.");
                        require(behavior->getRunAnimset().get() != nullptr,
                                "Scene script round-trip test failed to resolve the run animset script property after behavior start.");
                        require(animator->play(behavior->getRunAnimset().get()),
                                "Scene script round-trip test failed to switch clips by playing a different script-owned animset directly through the animator.");
                        require(animator->getCurrentClipName() == "run",
                                "Scene script round-trip test failed to switch the animator to the requested runtime clip.");
                        require(animator->getCurrentFrameIndex() == 0,
                                "Scene script round-trip test failed to restart playback from the first frame of the requested animset.");
                        require(behavior->getSound().path == sceneAudioPath,
                                "Scene script round-trip test failed to deserialize the audio asset reference path.");
                        require(behavior->getSound().get() != nullptr,
                                "Scene script round-trip test failed to resolve the audio asset reference after behavior start.");

                        gameManager.saveScene(scene);

                        std::ifstream savedSceneFile(scenePath);
                        require(savedSceneFile.is_open(), "Scene script round-trip test failed to reopen the saved scene file.");

                        std::stringstream savedSceneContents;
                        savedSceneContents << savedSceneFile.rdbuf();
                        const std::string saved = savedSceneContents.str();

                        require(saved.find("displayname = \"Player One\"") != std::string::npos,
                                "Scene script round-trip test failed to preserve quoted string script properties.");
                        require(saved.find("speed = 123.5") != std::string::npos,
                                "Scene script round-trip test failed to preserve raw numeric script properties.");
                        require(saved.find("allowdash = true") != std::string::npos,
                                "Scene script round-trip test failed to preserve raw boolean script properties.");
                        require(saved.find("precision = 987.654321") != std::string::npos,
                                "Scene script round-trip test failed to preserve raw double script properties.");
                        require(saved.find("offset = [11.25, -4.5]") != std::string::npos,
                                "Scene script round-trip test failed to preserve raw Vector2f script properties.");
                        require(saved.find("target = \"Target Dummy\"") != std::string::npos,
                                "Scene script round-trip test failed to preserve named object reference properties.");
                        require(saved.find("emitter = \"Audio Emitter\"") != std::string::npos,
                                "Scene script round-trip test failed to preserve named component reference properties.");
                        require(saved.find("icon = \"" + sceneTexturePath + "\"") != std::string::npos,
                                "Scene script round-trip test failed to preserve texture asset reference properties.");
                        require(saved.find("idleanimset = \"" + sceneIdleAnimationSetPath + "\"") != std::string::npos,
                                "Scene script round-trip test failed to preserve the idle animset script property.");
                        require(saved.find("runanimset = \"" + sceneRunAnimationSetPath + "\"") != std::string::npos,
                                "Scene script round-trip test failed to preserve the run animset script property.");
                        require(saved.find("sound = \"" + sceneAudioPath + "\"") != std::string::npos,
                                "Scene script round-trip test failed to preserve audio asset reference properties.");
                        require(saved.find("[objects.animator]") != std::string::npos,
                                "Scene script round-trip test failed to preserve the explicit animator component.");
                        require(saved.find("asset = \"" + sceneIdleAnimationSetPath + "\"") == std::string::npos,
                                "Scene script round-trip test incorrectly wrote animset references into the animator block.");
                        require(saved.find("play = \"") == std::string::npos,
                                "Scene script round-trip test incorrectly wrote runtime clip playback state into the animator block.");
                        require(saved.find("speed = \"123.5\"") == std::string::npos,
                                "Scene script round-trip test incorrectly quoted a numeric script property.");
                        require(saved.find("allowdash = \"true\"") == std::string::npos,
                                "Scene script round-trip test incorrectly quoted a boolean script property.");
                        require(saved.find("precision = \"987.654321\"") == std::string::npos,
                                "Scene script round-trip test incorrectly quoted a double script property.");
                        require(saved.find("offset = \"11.25,-4.5\"") == std::string::npos,
                                "Scene script round-trip test incorrectly quoted a Vector2f script property.");

                        cleanup();
                }
                catch (const std::exception &)
                {
                        cleanup();
                        throw;
                }
        }

        void testSceneScriptExactTyping()
        {
                registerSceneScriptTestBehaviors();

                GameManager &gameManager = GameManager::getInstance();
                std::filesystem::path scenePath = std::filesystem::current_path() / "scene_script_exact_typing.scene";

                auto cleanup = [&]()
                {
                        destroyNamedObject(gameManager, "Strict Scripted Object");

                        std::error_code errorCode;
                        std::filesystem::remove(scenePath, errorCode);
                };

                cleanup();

                try
                {
                        std::ofstream sceneFile(scenePath);
                        require(sceneFile.is_open(), "Scene script exact-typing test failed to create the temporary scene file.");

                        sceneFile << "format = \"platformator_scene\"\n";
                        sceneFile << "version = 1\n\n";
                        sceneFile << "[[objects]]\n";
                        sceneFile << "name = \"Strict Scripted Object\"\n\n";
                        sceneFile << "[[objects.scripts]]\n";
                        sceneFile << "type = \"StrictBoolBehavior\"\n";
                        sceneFile << "allowDash = 1\n";
                        sceneFile.close();

                        bool threw = false;
                        try
                        {
                                Scene scene(scenePath.string());
                                gameManager.loadScene(scene);
                        }
                        catch (const std::exception &exception)
                        {
                                threw = true;
                                require(std::string(exception.what()).find("expected a bool") != std::string::npos,
                                        "Scene script exact-typing test failed to report a typed bool mismatch.");
                        }

                        require(threw,
                                "Scene script exact-typing test expected loading to reject an integer for a bool field.");

                        cleanup();
                }
                catch (const std::exception &)
                {
                        cleanup();
                        throw;
                }
        }

        void testSceneScriptMissingReferencesAndAssets()
        {
                registerSceneScriptTestBehaviors();

                GameManager &gameManager = GameManager::getInstance();
                std::filesystem::path scenePath = std::filesystem::current_path() / "scene_script_missing_references.scene";
                const std::string missingIconPath = (std::filesystem::path("missing") / "icon.png").generic_string();
                const std::string missingAnimsetPath = (std::filesystem::path("missing") / "idle.animset").generic_string();
                const std::string missingAudioPath = (std::filesystem::path("missing") / "sound.wav").generic_string();

                auto cleanup = [&]()
                {
                        destroyNamedObject(gameManager, "Reference Test Object");

                        std::error_code errorCode;
                        std::filesystem::remove(scenePath, errorCode);
                };

                cleanup();

                try
                {
                        std::ofstream sceneFile(scenePath);
                        require(sceneFile.is_open(), "Scene script missing-reference test failed to create the temporary scene file.");

                        sceneFile << "format = \"platformator_scene\"\n";
                        sceneFile << "version = 1\n\n";
                        sceneFile << "[[objects]]\n";
                        sceneFile << "name = \"Reference Test Object\"\n\n";
                        sceneFile << "[[objects.scripts]]\n";
                        sceneFile << "type = \"ReferenceResolutionBehavior\"\n";
                        sceneFile << "target = \"Missing Target\"\n";
                        sceneFile << "emitter = \"Missing Emitter\"\n";
                        sceneFile << "icon = \"" << missingIconPath << "\"\n";
                        sceneFile << "idleAnimset = \"" << missingAnimsetPath << "\"\n";
                        sceneFile << "sound = \"" << missingAudioPath << "\"\n";
                        sceneFile.close();

                        Scene scene(scenePath.string());
                        gameManager.loadScene(scene);
                        gameManager.simulateFrame(FRAME_TIME);

                        GameObject *referenceObject = gameManager.getGameObject("Reference Test Object");
                        require(referenceObject != nullptr,
                                "Scene script missing-reference test failed to load the scripted object.");

                        ScriptComponent *scriptComponent = referenceObject->getComponent<ScriptComponent>();
                        require(scriptComponent != nullptr,
                                "Scene script missing-reference test failed to load the script component.");

                        ReferenceResolutionBehavior *behavior = findBehavior<ReferenceResolutionBehavior>(scriptComponent);
                        require(behavior != nullptr,
                                "Scene script missing-reference test failed to load the reference-resolution behavior.");
                        require(behavior->getTarget().name == "Missing Target",
                                "Scene script missing-reference test failed to preserve the missing named object reference.");
                        require(gameManager.getGameObject(behavior->getTarget().name) == nullptr,
                                "Scene script missing-reference test unexpectedly resolved a missing named object.");
                        require(behavior->getEmitter().name == "Missing Emitter",
                                "Scene script missing-reference test failed to preserve the missing named component reference.");
                        require(behavior->getEmitter().get() == nullptr,
                                "Scene script missing-reference test unexpectedly resolved a missing named component.");
                        require(behavior->getIcon().path == missingIconPath,
                                "Scene script missing-reference test failed to preserve the missing texture asset path.");
                        require(behavior->getIcon().get() == nullptr,
                                "Scene script missing-reference test unexpectedly resolved a missing texture asset.");
                        require(behavior->getIdleAnimset().path == missingAnimsetPath,
                                "Scene script missing-reference test failed to preserve the missing animation asset path.");
                        require(behavior->getIdleAnimset().get() == nullptr,
                                "Scene script missing-reference test unexpectedly resolved a missing animation asset.");
                        require(behavior->getSound().path == missingAudioPath,
                                "Scene script missing-reference test failed to preserve the missing audio asset path.");
                        require(behavior->getSound().get() == nullptr,
                                "Scene script missing-reference test unexpectedly resolved a missing audio asset.");

                        cleanup();
                }
                catch (const std::exception &)
                {
                        cleanup();
                        throw;
                }
        }

        void testSceneScriptUnknownBehaviorType()
        {
                GameManager &gameManager = GameManager::getInstance();
                std::filesystem::path scenePath = std::filesystem::current_path() / "scene_script_unknown_behavior.scene";

                auto cleanup = [&]()
                {
                        destroyNamedObject(gameManager, "Unknown Behavior Object");

                        std::error_code errorCode;
                        std::filesystem::remove(scenePath, errorCode);
                };

                cleanup();

                try
                {
                        std::ofstream sceneFile(scenePath);
                        require(sceneFile.is_open(), "Scene script unknown-type test failed to create the temporary scene file.");

                        sceneFile << "format = \"platformator_scene\"\n";
                        sceneFile << "version = 1\n\n";
                        sceneFile << "[[objects]]\n";
                        sceneFile << "name = \"Unknown Behavior Object\"\n\n";
                        sceneFile << "[[objects.scripts]]\n";
                        sceneFile << "type = \"MissingBehaviorType\"\n";
                        sceneFile.close();

                        bool threw = false;
                        try
                        {
                                Scene scene(scenePath.string());
                                gameManager.loadScene(scene);
                        }
                        catch (const std::exception &exception)
                        {
                                threw = true;
                                require(std::string(exception.what()).find("Unknown behavior type 'MissingBehaviorType'") != std::string::npos,
                                        "Scene script unknown-type test failed to report the missing behavior type.");
                        }

                        require(threw,
                                "Scene script unknown-type test expected loading to fail for an unregistered behavior type.");

                        cleanup();
                }
                catch (const std::exception &)
                {
                        cleanup();
                        throw;
                }
        }

        struct TestCase
        {
                const char *name;
                void (*run)();
        };

} // namespace

int main()
{
        configureHeadlessEnvironment();
        GameManager::getInstance();

        static const TestCase testCases[] = {
            {"scene_script_round_trip", testSceneScriptRoundTrip},
            {"scene_script_exact_typing", testSceneScriptExactTyping},
            {"scene_script_missing_references_and_assets", testSceneScriptMissingReferencesAndAssets},
            {"scene_script_unknown_behavior_type", testSceneScriptUnknownBehaviorType},
        };

        size_t failureCount = 0;
        for (const TestCase &testCase : testCases)
        {
                try
                {
                        testCase.run();
                        std::cout << "[PASS] " << testCase.name << '\n';
                }
                catch (const std::exception &exception)
                {
                        ++failureCount;
                        std::cerr << "[FAIL] " << testCase.name << ": " << exception.what() << '\n';
                }
        }

        if (failureCount != 0)
        {
                std::cerr << failureCount << " scene script regression test(s) failed.\n";
                return 1;
        }

        return 0;
}