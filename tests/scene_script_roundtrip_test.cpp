#include <cstdlib>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "animationasset.h"
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

        std::string getTypeName() const override
        {
            return "TestSerializedBehavior";
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

        const platformator_behavior_detail::AnimationSetAssetReference &getIdleAnimset() const
        {
            return idleAnimset;
        }

        const platformator_behavior_detail::AnimationSetAssetReference &getRunAnimset() const
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
        platformator_behavior_detail::AnimationSetAssetReference idleAnimset;
        platformator_behavior_detail::AnimationSetAssetReference runAnimset;
        platformator_behavior_detail::AudioAssetReference sound;
    };

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
        static bool registered = false;
        if (!registered)
        {
            BehaviorFactoryRegistry::getInstance().registerBehavior<TestSerializedBehavior>("TestSerializedBehavior");
            registered = true;
        }

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
            idleAnimationSetFile << "animations {\n";
            idleAnimationSetFile << "    clip {\n";
            idleAnimationSetFile << "        name \"idle\"\n";
            idleAnimationSetFile << "        fps 1\n";
            idleAnimationSetFile << "        loop true\n";
            idleAnimationSetFile << "        size 32 32\n";
            idleAnimationSetFile << "        frame \"" << idleAnimationTexturePath << "\"\n";
            idleAnimationSetFile << "    }\n";
            idleAnimationSetFile << "}\n";
            idleAnimationSetFile.close();

            std::ofstream runAnimationSetFile(runAnimationSetPath);
            require(runAnimationSetFile.is_open(), "Scene script round-trip test failed to create the temporary run animation set file.");
            runAnimationSetFile << "animations {\n";
            runAnimationSetFile << "    clip {\n";
            runAnimationSetFile << "        name \"run\"\n";
            runAnimationSetFile << "        fps 2\n";
            runAnimationSetFile << "        loop true\n";
            runAnimationSetFile << "        size 32 32\n";
            runAnimationSetFile << "        frame \"" << runAnimationTexturePath << "\"\n";
            runAnimationSetFile << "    }\n";
            runAnimationSetFile << "}\n";
            runAnimationSetFile.close();

            std::ofstream sceneFile(scenePath);
            require(sceneFile.is_open(), "Scene script round-trip test failed to create the temporary scene file.");

            sceneFile << "object {\n";
            sceneFile << "    name \"Audio Emitter\"\n";
            sceneFile << "    audio {\n";
            sceneFile << "        gain 0.4\n";
            sceneFile << "    }\n";
            sceneFile << "}\n\n";
            sceneFile << "object {\n";
            sceneFile << "    name \"Scripted Object\"\n";
            sceneFile << "    animator {\n";
            sceneFile << "        playbackSpeed 1\n";
            sceneFile << "    }\n";
            sceneFile << "    script {\n";
            sceneFile << "        type \"TestSerializedBehavior\"\n";
            sceneFile << "        displayName \"Player One\"\n";
            sceneFile << "        speed 123.5\n";
            sceneFile << "        allowDash true\n";
            sceneFile << "        precision 987.654321\n";
            sceneFile << "        offset 11.25,-4.5\n";
            sceneFile << "        target \"Target Dummy\"\n";
            sceneFile << "        emitter \"Audio Emitter\"\n";
            sceneFile << "        icon \"" << sceneTexturePath << "\"\n";
            sceneFile << "        idleAnimset \"" << sceneIdleAnimationSetPath << "\"\n";
            sceneFile << "        runAnimset \"" << sceneRunAnimationSetPath << "\"\n";
            sceneFile << "        sound \"" << sceneAudioPath << "\"\n";
            sceneFile << "    }\n";
            sceneFile << "}\n";
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

            TestSerializedBehavior *behavior = nullptr;
            for (Behavior *candidate : scriptComponent->getBehaviors())
            {
                behavior = dynamic_cast<TestSerializedBehavior *>(candidate);
                if (behavior != nullptr)
                {
                    break;
                }
            }
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

            require(saved.find("displayname \"Player One\"") != std::string::npos,
                    "Scene script round-trip test failed to preserve quoted string script properties.");
            require(saved.find("speed 123.5") != std::string::npos,
                    "Scene script round-trip test failed to preserve raw numeric script properties.");
            require(saved.find("allowdash true") != std::string::npos,
                    "Scene script round-trip test failed to preserve raw boolean script properties.");
            require(saved.find("precision 987.654321") != std::string::npos,
                    "Scene script round-trip test failed to preserve raw double script properties.");
            require(saved.find("offset 11.25,-4.5") != std::string::npos,
                    "Scene script round-trip test failed to preserve raw Vector2f script properties.");
            require(saved.find("target \"Target Dummy\"") != std::string::npos,
                    "Scene script round-trip test failed to preserve named object reference properties.");
            require(saved.find("emitter \"Audio Emitter\"") != std::string::npos,
                    "Scene script round-trip test failed to preserve named component reference properties.");
            require(saved.find("icon \"" + sceneTexturePath + "\"") != std::string::npos,
                    "Scene script round-trip test failed to preserve texture asset reference properties.");
            require(saved.find("idleanimset \"" + sceneIdleAnimationSetPath + "\"") != std::string::npos,
                    "Scene script round-trip test failed to preserve the idle animset script property.");
            require(saved.find("runanimset \"" + sceneRunAnimationSetPath + "\"") != std::string::npos,
                    "Scene script round-trip test failed to preserve the run animset script property.");
            require(saved.find("sound \"" + sceneAudioPath + "\"") != std::string::npos,
                    "Scene script round-trip test failed to preserve audio asset reference properties.");
            require(saved.find("animator {") != std::string::npos,
                    "Scene script round-trip test failed to preserve the explicit animator component.");
            require(saved.find("asset \"" + sceneIdleAnimationSetPath + "\"") == std::string::npos,
                    "Scene script round-trip test incorrectly wrote animset references into the animator block.");
            require(saved.find("play \"") == std::string::npos,
                    "Scene script round-trip test incorrectly wrote runtime clip playback state into the animator block.");
            require(saved.find("speed \"123.5\"") == std::string::npos,
                    "Scene script round-trip test incorrectly quoted a numeric script property.");
            require(saved.find("allowdash \"true\"") == std::string::npos,
                    "Scene script round-trip test incorrectly quoted a boolean script property.");
            require(saved.find("precision \"987.654321\"") == std::string::npos,
                    "Scene script round-trip test incorrectly quoted a double script property.");
            require(saved.find("offset \"11.25,-4.5\"") == std::string::npos,
                    "Scene script round-trip test incorrectly quoted a Vector2f script property.");

            cleanup();
        }
        catch (const std::exception &)
        {
            cleanup();
            throw;
        }
    }

    void testLegacyAnimatorMigration()
    {
        GameManager &gameManager = GameManager::getInstance();
        std::filesystem::path scenePath = std::filesystem::current_path() / "scene_legacy_animator_migration.scene";
        std::filesystem::path idleAnimationSetPath = std::filesystem::current_path() / "scene_legacy_animator_idle.animset";
        const std::filesystem::path textureAssetPath = findWorkspaceRelativePath(std::filesystem::path("examples") / "mario" / "assets" / "player" / "idle_0.png");
        const std::string sceneTexturePath = std::filesystem::relative(textureAssetPath, scenePath.parent_path()).generic_string();
        const std::string sceneIdleAnimationSetPath = std::filesystem::relative(idleAnimationSetPath, scenePath.parent_path()).generic_string();
        const std::string idleAnimationTexturePath = std::filesystem::relative(textureAssetPath, idleAnimationSetPath.parent_path()).generic_string();

        auto cleanup = [&]()
        {
            destroyNamedObject(gameManager, "Legacy Asset Animator");
            destroyNamedObject(gameManager, "Legacy Clip Animator");

            std::error_code errorCode;
            std::filesystem::remove(scenePath, errorCode);
            std::filesystem::remove(idleAnimationSetPath, errorCode);
        };

        cleanup();

        try
        {
            std::ofstream idleAnimationSetFile(idleAnimationSetPath);
            require(idleAnimationSetFile.is_open(), "Legacy animator migration test failed to create the temporary animation set file.");
            idleAnimationSetFile << "animations {\n";
            idleAnimationSetFile << "    play \"idle\"\n";
            idleAnimationSetFile << "    clip {\n";
            idleAnimationSetFile << "        name \"idle\"\n";
            idleAnimationSetFile << "        fps 4\n";
            idleAnimationSetFile << "        loop true\n";
            idleAnimationSetFile << "        size 32 32\n";
            idleAnimationSetFile << "        frame \"" << idleAnimationTexturePath << "\"\n";
            idleAnimationSetFile << "    }\n";
            idleAnimationSetFile << "}\n";
            idleAnimationSetFile.close();

            std::ofstream sceneFile(scenePath);
            require(sceneFile.is_open(), "Legacy animator migration test failed to create the temporary scene file.");
            sceneFile << "object {\n";
            sceneFile << "    name \"Legacy Asset Animator\"\n";
            sceneFile << "    sprite {\n";
            sceneFile << "        path \"" << sceneTexturePath << "\"\n";
            sceneFile << "        size 32 32\n";
            sceneFile << "    }\n";
            sceneFile << "    animator {\n";
            sceneFile << "        asset \"" << sceneIdleAnimationSetPath << "\"\n";
            sceneFile << "        play \"idle\"\n";
            sceneFile << "        playbackSpeed 1.5\n";
            sceneFile << "    }\n";
            sceneFile << "}\n\n";
            sceneFile << "object {\n";
            sceneFile << "    name \"Legacy Clip Animator\"\n";
            sceneFile << "    sprite {\n";
            sceneFile << "        path \"" << sceneTexturePath << "\"\n";
            sceneFile << "        size 32 32\n";
            sceneFile << "    }\n";
            sceneFile << "    animator {\n";
            sceneFile << "        playbackSpeed 0.75\n";
            sceneFile << "        play \"legacyIdle\"\n";
            sceneFile << "        clip {\n";
            sceneFile << "            name \"legacyIdle\"\n";
            sceneFile << "            fps 4\n";
            sceneFile << "            loop true\n";
            sceneFile << "            size 32 32\n";
            sceneFile << "            frame \"" << sceneTexturePath << "\"\n";
            sceneFile << "        }\n";
            sceneFile << "    }\n";
            sceneFile << "}\n";
            sceneFile.close();

            Scene scene(scenePath.string());
            gameManager.loadScene(scene);
            gameManager.simulateFrame(FRAME_TIME);

            GameObject *legacyAssetAnimatorObject = gameManager.getGameObject("Legacy Asset Animator");
            require(legacyAssetAnimatorObject != nullptr, "Legacy animator migration test failed to load the legacy asset animator object.");
            Animator *legacyAssetAnimator = legacyAssetAnimatorObject->getComponent<Animator>();
            require(legacyAssetAnimator != nullptr, "Legacy animator migration test failed to preserve the explicit animator component for the asset-based object.");
            require(legacyAssetAnimator->getCurrentClipName() == "idle",
                    "Legacy animator migration test failed to select the requested legacy asset animation.");
            require(legacyAssetAnimator->getIsPlaying(),
                    "Legacy animator migration test failed to leave the legacy asset animation playing after load.");
            require(std::abs(legacyAssetAnimator->getPlaybackSpeed() - 1.5f) <= 1e-5f,
                    "Legacy animator migration test failed to preserve animator playback speed while migrating legacy asset data.");

            GameObject *legacyClipAnimatorObject = gameManager.getGameObject("Legacy Clip Animator");
            require(legacyClipAnimatorObject != nullptr, "Legacy animator migration test failed to load the legacy inline clip object.");
            Animator *legacyClipAnimator = legacyClipAnimatorObject->getComponent<Animator>();
            require(legacyClipAnimator != nullptr, "Legacy animator migration test failed to preserve the explicit animator component for the inline clip object.");
            require(std::abs(legacyClipAnimator->getPlaybackSpeed() - 0.75f) <= 1e-5f,
                    "Legacy animator migration test failed to preserve animator playback speed while migrating legacy inline clip data.");

            gameManager.saveScene(scene);

            std::ifstream savedSceneFile(scenePath);
            require(savedSceneFile.is_open(), "Legacy animator migration test failed to reopen the saved scene file.");

            std::stringstream savedSceneContents;
            savedSceneContents << savedSceneFile.rdbuf();
            const std::string saved = savedSceneContents.str();

            require(saved.find("animator {") != std::string::npos,
                    "Legacy animator migration test failed to preserve explicit animator components during migration.");
            require(saved.find("playbackSpeed 1.5") != std::string::npos,
                    "Legacy animator migration test failed to preserve migrated playback speed for the legacy asset object.");
            require(saved.find("playbackSpeed 0.75") != std::string::npos,
                    "Legacy animator migration test failed to preserve migrated playback speed for the legacy inline clip object.");
            require(saved.find("asset \"") == std::string::npos,
                    "Legacy animator migration test incorrectly preserved deprecated animator asset properties after migration.");
            require(saved.find("play \"") == std::string::npos,
                    "Legacy animator migration test incorrectly preserved deprecated animator play properties after migration.");
            require(saved.find("clip {") == std::string::npos,
                    "Legacy animator migration test incorrectly preserved deprecated inline animator clips after migration.");

            cleanup();
        }
        catch (const std::exception &)
        {
            cleanup();
            throw;
        }
    }
} // namespace

int main()
{
    configureHeadlessEnvironment();
    GameManager::getInstance();

    try
    {
        testSceneScriptRoundTrip();
        testLegacyAnimatorMigration();
        std::cout << "[PASS] scene_script_roundtrip_test\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "[FAIL] scene_script_roundtrip_test: " << exception.what() << '\n';
        return 1;
    }
}