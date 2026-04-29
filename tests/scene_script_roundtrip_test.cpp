#include <cstdlib>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <toml++/toml.hpp>

#include "animator.h"
#include "audio.h"
#include "behaviorfactoryregistry.h"
#include "constants.h"
#include "gamemanager.h"
#include "scene.h"
#include "scriptcomponent.h"

namespace scene_roundtrip_test_support
{
    class NamespacedSceneBehavior : public Behavior
    {
    };
}

REGISTER_BEHAVIOR(scene_roundtrip_test_support::NamespacedSceneBehavior);

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

        throw std::runtime_error("Scene round-trip test could not locate resource '" + relativePath.generic_string() + "'.");
    }

    void cleanupAllGameObjects(GameManager &gameManager)
    {
        while (!gameManager.getGameObjects().empty())
        {
            std::vector<GameObject *> snapshot = gameManager.getGameObjects();
            for (GameObject *gameObject : snapshot)
            {
                gameManager.destroyGameObject(gameObject);
            }
            gameManager.simulateFrame(FRAME_TIME);
        }
    }

    class AnnotatedSceneBehavior : public Behavior
    {
    public:
        SERIALIZABLE_BEHAVIOR(AnnotatedSceneBehavior);

        SERIALIZED_FIELD(std::string, displayName);
        SERIALIZED_FIELD(float, speed);
        SERIALIZED_FIELD(bool, allowDash);
        SERIALIZED_FIELD(double, precision);
        SERIALIZED_FIELD(Eigen::Vector2f, offset);
        SERIALIZED_FIELD(platformator::GameObjectRef, targetObject);
        SERIALIZED_FIELD(platformator::ComponentRef<Audio>, emitter);
        SERIALIZED_FIELD(platformator::TextureAssetRef, icon);
        SERIALIZED_FIELD(platformator::AnimationClipRef, idleClip);
        SERIALIZED_FIELD(platformator::AudioAssetRef, sound);
    };

    REGISTER_BEHAVIOR(AnnotatedSceneBehavior);

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

    const toml::table *findSavedObject(const toml::array &objects, const std::string &name)
    {
        for (const toml::node &node : objects)
        {
            const toml::table *objectTable = node.as_table();
            if (objectTable == nullptr)
            {
                continue;
            }

            const toml::node *nameNode = objectTable->get("name");
            if (nameNode != nullptr && nameNode->is_string() && nameNode->value_exact<std::string>().value_or("") == name)
            {
                return objectTable;
            }
        }

        return nullptr;
    }

    void testSceneVersion2RoundTrip()
    {
        GameManager &gameManager = GameManager::getInstance();
        cleanupAllGameObjects(gameManager);

        const std::filesystem::path scenePath = std::filesystem::current_path() / "scene_v2_roundtrip.scene";
        const std::filesystem::path animationClipPath = std::filesystem::current_path() / "scene_v2_roundtrip.animset";
        const std::filesystem::path texturePath = findWorkspaceRelativePath(std::filesystem::path("assets") / "ball.png");
        const std::filesystem::path audioPath = findWorkspaceRelativePath(std::filesystem::path("examples") / "mario" / "assets" / "audio" / "jump.wav");

        const std::string sceneRelativeTexturePath = std::filesystem::relative(texturePath, scenePath.parent_path()).generic_string();
        const std::string sceneRelativeAudioPath = std::filesystem::relative(audioPath, scenePath.parent_path()).generic_string();
        const std::string sceneRelativeAnimsetPath = std::filesystem::relative(animationClipPath, scenePath.parent_path()).generic_string();
        const std::string animsetRelativeTexturePath = std::filesystem::relative(texturePath, animationClipPath.parent_path()).generic_string();

        auto cleanupFiles = [&]()
        {
            std::error_code errorCode;
            std::filesystem::remove(scenePath, errorCode);
            std::filesystem::remove(animationClipPath, errorCode);
        };

        cleanupFiles();

        try
        {
            std::ofstream animationFile(animationClipPath);
            require(animationFile.is_open(), "Scene v2 round-trip test failed to create the temporary animation clip file.");
            animationFile << "format = \"platformator_animset\"\n";
            animationFile << "version = 1\n";
            animationFile << "name = \"idle\"\n";
            animationFile << "fps = 1\n";
            animationFile << "loop = true\n";
            animationFile << "size = [32, 32]\n";
            animationFile << "frames = [\n";
            animationFile << "    \"" << animsetRelativeTexturePath << "\",\n";
            animationFile << "]\n";
            animationFile.close();

            std::ofstream sceneFile(scenePath);
            require(sceneFile.is_open(), "Scene v2 round-trip test failed to create the temporary scene file.");
            sceneFile << "format = \"platformator_scene\"\n";
            sceneFile << "version = 2\n\n";

            sceneFile << "[[objects]]\n";
            sceneFile << "id = 100\n";
            sceneFile << "name = \"Target Dummy\"\n\n";
            sceneFile << "[objects.audio]\n";
            sceneFile << "id = 300\n";
            sceneFile << "gain = 0.4\n\n";

            sceneFile << "[[objects]]\n";
            sceneFile << "id = 101\n";
            sceneFile << "name = \"Main Camera\"\n";
            sceneFile << "children = [102]\n\n";
            sceneFile << "[objects.camera]\n";
            sceneFile << "id = 200\n";
            sceneFile << "viewport = [0, 0, 640, 480]\n\n";

            sceneFile << "[[objects]]\n";
            sceneFile << "id = 103\n";
            sceneFile << "name = \"Sprite Only\"\n\n";
            sceneFile << "[objects.sprite]\n";
            sceneFile << "id = 204\n";
            sceneFile << "path = \"" << sceneRelativeTexturePath << "\"\n";
            sceneFile << "flip = \"none\"\n";
            sceneFile << "size = [32, 32]\n";
            sceneFile << "sourceRect = [0, 0, 16, 16]\n\n";

            sceneFile << "[[objects]]\n";
            sceneFile << "id = 102\n";
            sceneFile << "name = \"Scripted Object\"\n";
            sceneFile << "position = [12, 34]\n";
            sceneFile << "scale = [1.5, 2.0]\n\n";
            sceneFile << "[objects.sprite]\n";
            sceneFile << "id = 202\n";
            sceneFile << "path = \"" << sceneRelativeTexturePath << "\"\n";
            sceneFile << "flip = \"horizontal\"\n";
            sceneFile << "size = [32, 32]\n";
            sceneFile << "sourceRect = [0, 0, 16, 16]\n\n";
            sceneFile << "[objects.animator]\n";
            sceneFile << "id = 201\n";
            sceneFile << "playbackSpeed = 1.5\n";
            sceneFile << "clip = \"" << sceneRelativeAnimsetPath << "\"\n";
            sceneFile << "playing = true\n\n";
            sceneFile << "[objects.script]\n";
            sceneFile << "id = 203\n\n";
            sceneFile << "[[objects.script.behaviors]]\n";
            sceneFile << "type = \"AnnotatedSceneBehavior\"\n";
            sceneFile << "displayName = \"Player One\"\n";
            sceneFile << "speed = 123.5\n";
            sceneFile << "allowDash = true\n";
            sceneFile << "precision = 987.654321\n";
            sceneFile << "offset = [11.25, -4.5]\n";
            sceneFile << "targetObject = { gameObject = 100 }\n";
            sceneFile << "emitter = { component = 300 }\n";
            sceneFile << "icon = \"" << sceneRelativeTexturePath << "\"\n";
            sceneFile << "idleClip = \"" << sceneRelativeAnimsetPath << "\"\n";
            sceneFile << "sound = \"" << sceneRelativeAudioPath << "\"\n";
            sceneFile.close();

            Scene scene(scenePath.string());
            gameManager.loadScene(scene);
            gameManager.simulateFrame(FRAME_TIME);

            GameObject *mainCamera = gameManager.getGameObject("Main Camera");
            GameObject *spriteOnlyObject = gameManager.getGameObject("Sprite Only");
            GameObject *targetDummy = gameManager.getGameObject("Target Dummy");
            GameObject *scriptedObject = gameManager.getGameObject("Scripted Object");
            require(mainCamera != nullptr, "Scene v2 round-trip test failed to load the main camera object.");
            require(spriteOnlyObject != nullptr, "Scene v2 round-trip test failed to load the sprite-only object.");
            require(targetDummy != nullptr, "Scene v2 round-trip test failed to load the target object.");
            require(scriptedObject != nullptr, "Scene v2 round-trip test failed to load the scripted object.");
            require(scriptedObject->getId() == 102, "Scene v2 round-trip test failed to restore the scripted object id.");
            require(mainCamera->getChildren().size() == 1 && mainCamera->getChildren()[0] == scriptedObject,
                    "Scene v2 round-trip test failed to restore child object references.");

            Audio *targetAudio = targetDummy->getComponent<Audio>();
            require(targetAudio != nullptr && targetAudio->getId() == 300,
                    "Scene v2 round-trip test failed to restore the referenced audio component id.");

            Sprite *spriteOnly = spriteOnlyObject->getComponent<Sprite>();
            require(spriteOnly != nullptr && spriteOnly->getId() == 204,
                    "Scene v2 round-trip test failed to restore the sprite-only component id.");
            require(spriteOnly->hasSourceRect() && std::abs(spriteOnly->getSourceRect()->w - 16.0f) <= 1e-5f,
                    "Scene v2 round-trip test failed to restore sprite source rect state.");

            Sprite *sprite = scriptedObject->getComponent<Sprite>();
            require(sprite != nullptr && sprite->getId() == 202,
                    "Scene v2 round-trip test failed to restore the sprite component id.");
            require(sprite->getFlip() == SDL_FLIP_HORIZONTAL,
                    "Scene v2 round-trip test failed to restore sprite flip mode.");

            Animator *animator = scriptedObject->getComponent<Animator>();
            require(animator != nullptr && animator->getId() == 201,
                    "Scene v2 round-trip test failed to restore the animator component id.");
            require(std::abs(animator->getPlaybackSpeed() - 1.5f) <= 1e-5f,
                    "Scene v2 round-trip test failed to restore animator playback speed.");
            require(animator->getCurrentAnimationClip() != nullptr && animator->getCurrentAnimationClip()->getName() == "idle",
                    "Scene v2 round-trip test failed to restore the animator clip asset.");

            ScriptComponent *scriptComponent = scriptedObject->getComponent<ScriptComponent>();
            require(scriptComponent != nullptr && scriptComponent->getId() == 203,
                    "Scene v2 round-trip test failed to restore the script component id.");

            AnnotatedSceneBehavior *behavior = findBehavior<AnnotatedSceneBehavior>(scriptComponent);
            require(behavior != nullptr, "Scene v2 round-trip test failed to instantiate the annotated behavior.");
            require(behavior->displayName == "Player One", "Scene v2 round-trip test failed to deserialize the string field.");
            require(std::abs(behavior->speed - 123.5f) <= 1e-5f, "Scene v2 round-trip test failed to deserialize the float field.");
            require(behavior->allowDash, "Scene v2 round-trip test failed to deserialize the bool field.");
            require(std::abs(behavior->precision - 987.654321) <= 1e-12, "Scene v2 round-trip test failed to deserialize the double field.");
            require((behavior->offset - Eigen::Vector2f(11.25f, -4.5f)).norm() <= 1e-5f,
                    "Scene v2 round-trip test failed to deserialize the Vector2f field.");
            require(behavior->targetObject.id == 100 && behavior->targetObject.get() == targetDummy,
                    "Scene v2 round-trip test failed to resolve the game object reference field.");
            require(behavior->emitter.id == 300 && behavior->emitter.get() == targetAudio,
                    "Scene v2 round-trip test failed to resolve the component reference field.");
            require(behavior->icon.path == sceneRelativeTexturePath && behavior->icon.get() != nullptr,
                    "Scene v2 round-trip test failed to resolve the texture asset field.");
            require(behavior->idleClip.path == sceneRelativeAnimsetPath && behavior->idleClip.get() != nullptr,
                    "Scene v2 round-trip test failed to resolve the animation clip asset field.");
            require(behavior->sound.path == sceneRelativeAudioPath && behavior->sound.get() != nullptr,
                    "Scene v2 round-trip test failed to resolve the audio asset field.");

            gameManager.saveScene(scene);

            const toml::table savedDocument = toml::parse_file(scenePath.string());
            require(savedDocument["version"].value_or(0) == 2,
                    "Scene v2 round-trip test failed to save the new scene version.");

            const toml::array *savedObjects = savedDocument.get_as<toml::array>("objects");
            require(savedObjects != nullptr, "Scene v2 round-trip test failed to save the objects array.");

            const toml::table *savedMainCamera = findSavedObject(*savedObjects, "Main Camera");
            require(savedMainCamera != nullptr, "Scene v2 round-trip test failed to save the main camera object.");
            const toml::array *savedChildren = savedMainCamera->get_as<toml::array>("children");
            require(savedChildren != nullptr && savedChildren->size() == 1 && savedChildren->get(0)->value_or<int64_t>(0) == 102,
                    "Scene v2 round-trip test failed to preserve child ids.");

            const toml::table *savedScriptedObject = findSavedObject(*savedObjects, "Scripted Object");
            require(savedScriptedObject != nullptr, "Scene v2 round-trip test failed to save the scripted object.");
            const toml::table *savedScript = savedScriptedObject->get_as<toml::table>("script");
            require(savedScript != nullptr, "Scene v2 round-trip test failed to save the explicit script component block.");
            const toml::array *savedBehaviors = savedScript->get_as<toml::array>("behaviors");
            require(savedBehaviors != nullptr && !savedBehaviors->empty(),
                    "Scene v2 round-trip test failed to save script behaviors.");
            const toml::table *savedBehavior = savedBehaviors->get(0)->as_table();
            require(savedBehavior != nullptr, "Scene v2 round-trip test failed to save the behavior table.");
            require(savedBehavior->get("displayName") != nullptr,
                    "Scene v2 round-trip test failed to preserve annotated field casing when saving behavior data.");
            const toml::table *savedTargetRef = savedBehavior->get_as<toml::table>("targetObject");
            require(savedTargetRef != nullptr && savedTargetRef->get("gameObject")->value_or<int64_t>(0) == 100,
                    "Scene v2 round-trip test failed to serialize the game object reference field using ids.");
            const toml::table *savedEmitterRef = savedBehavior->get_as<toml::table>("emitter");
            require(savedEmitterRef != nullptr && savedEmitterRef->get("component")->value_or<int64_t>(0) == 300,
                    "Scene v2 round-trip test failed to serialize the component reference field using ids.");

            cleanupAllGameObjects(gameManager);
            gameManager.loadScene(scene);
            gameManager.simulateFrame(FRAME_TIME);

            scriptedObject = gameManager.getGameObject("Scripted Object");
            require(scriptedObject != nullptr && scriptedObject->getId() == 102,
                    "Scene v2 round-trip test failed to reload the saved scripted object.");
            scriptComponent = scriptedObject->getComponent<ScriptComponent>();
            behavior = findBehavior<AnnotatedSceneBehavior>(scriptComponent);
            require(behavior != nullptr && behavior->targetObject.get() != nullptr && behavior->emitter.get() != nullptr,
                    "Scene v2 round-trip test failed to reload serialized references from the saved scene.");

            cleanupAllGameObjects(gameManager);
            cleanupFiles();
        }
        catch (const std::exception &)
        {
            cleanupAllGameObjects(gameManager);
            cleanupFiles();
            throw;
        }
    }

    void testSceneVersion2RejectsInvalidIds()
    {
        GameManager &gameManager = GameManager::getInstance();
        cleanupAllGameObjects(gameManager);

        const std::filesystem::path scenePath = std::filesystem::current_path() / "scene_v2_invalid_ids.scene";

        auto cleanupFile = [&]()
        {
            std::error_code errorCode;
            std::filesystem::remove(scenePath, errorCode);
        };

        cleanupFile();

        try
        {
            {
                std::ofstream sceneFile(scenePath);
                require(sceneFile.is_open(), "Scene invalid-id test failed to create the temporary scene file.");
                sceneFile << "format = \"platformator_scene\"\n";
                sceneFile << "version = 2\n\n";
                sceneFile << "[[objects]]\n";
                sceneFile << "id = 0\n";
                sceneFile << "name = \"Zero Id\"\n";
            }

            bool threw = false;
            try
            {
                Scene scene(scenePath.string());
                gameManager.loadScene(scene);
            }
            catch (const std::exception &exception)
            {
                threw = true;
                require(std::string(exception.what()).find("must be positive") != std::string::npos,
                        "Scene invalid-id test failed to reject id 0 with a positive-id error.");
            }
            require(threw, "Scene invalid-id test expected object id 0 to be rejected.");

            cleanupFile();

            {
                std::ofstream sceneFile(scenePath);
                require(sceneFile.is_open(), "Scene duplicate-id test failed to create the temporary scene file.");
                sceneFile << "format = \"platformator_scene\"\n";
                sceneFile << "version = 2\n\n";
                sceneFile << "[[objects]]\n";
                sceneFile << "id = 400\n";
                sceneFile << "name = \"First\"\n\n";
                sceneFile << "[objects.audio]\n";
                sceneFile << "id = 900\n";
                sceneFile << "gain = 0.5\n\n";
                sceneFile << "[[objects]]\n";
                sceneFile << "id = 401\n";
                sceneFile << "name = \"Second\"\n\n";
                sceneFile << "[objects.script]\n";
                sceneFile << "id = 900\n";
            }

            threw = false;
            try
            {
                Scene scene(scenePath.string());
                gameManager.loadScene(scene);
            }
            catch (const std::exception &exception)
            {
                threw = true;
                require(std::string(exception.what()).find("duplicates id 900") != std::string::npos,
                        "Scene duplicate-id test failed to reject duplicate component ids.");
            }
            require(threw, "Scene duplicate-id test expected duplicate component ids to be rejected.");

            cleanupAllGameObjects(gameManager);
            cleanupFile();
        }
        catch (const std::exception &)
        {
            cleanupAllGameObjects(gameManager);
            cleanupFile();
            throw;
        }
    }

    void testFailedSceneLoadDoesNotLeaveStartedBehaviors()
    {
        GameManager &gameManager = GameManager::getInstance();
        cleanupAllGameObjects(gameManager);

        const std::filesystem::path scenePath = std::filesystem::current_path() / "scene_v2_failed_load.scene";

        auto cleanupFile = [&]()
        {
            std::error_code errorCode;
            std::filesystem::remove(scenePath, errorCode);
        };

        cleanupFile();

        try
        {
            std::ofstream sceneFile(scenePath);
            require(sceneFile.is_open(), "Scene failed-load test failed to create the temporary scene file.");
            sceneFile << "format = \"platformator_scene\"\n";
            sceneFile << "version = 2\n\n";
            sceneFile << "[[objects]]\n";
            sceneFile << "id = 700\n";
            sceneFile << "name = \"Broken Scripted Object\"\n\n";
            sceneFile << "[objects.script]\n";
            sceneFile << "id = 701\n\n";
            sceneFile << "[[objects.script.behaviors]]\n";
            sceneFile << "type = \"scene_roundtrip_test_support::NamespacedSceneBehavior\"\n\n";
            sceneFile << "[[objects.script.behaviors]]\n";
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
                        "Scene failed-load test failed to report the unknown behavior type.");
            }

            require(threw, "Scene failed-load test expected scene loading to fail.");
            require(gameManager.getGameObjects().empty(), "Scene failed-load test should not leave partially loaded game objects registered.");

            gameManager.simulateFrame(FRAME_TIME);
            require(gameManager.getGameObjects().empty(), "Scene failed-load test left runtime state behind after a failed load.");

            cleanupAllGameObjects(gameManager);
            cleanupFile();
        }
        catch (const std::exception &)
        {
            cleanupAllGameObjects(gameManager);
            cleanupFile();
            throw;
        }
    }

    void testSceneVersion1StillLoads()
    {
        GameManager &gameManager = GameManager::getInstance();
        cleanupAllGameObjects(gameManager);

        try
        {
            Scene scene(findWorkspaceRelativePath(std::filesystem::path("assets") / "scenes" / "default.scene").string());
            gameManager.loadScene(scene);

            GameObject *ball = gameManager.getGameObject("Ball");
            GameObject *mainCamera = gameManager.getGameObject("Main Camera");
            require(ball != nullptr, "Scene v1 compatibility test failed to load the Ball object from the default scene.");
            require(ball->getComponent<Rigidbody>() != nullptr, "Scene v1 compatibility test failed to restore Ball rigidbody state.");
            require(ball->getComponent<BoxCollider>() != nullptr, "Scene v1 compatibility test failed to restore Ball collider state.");
            require(ball->getComponent<Sprite>() != nullptr, "Scene v1 compatibility test failed to restore Ball sprite state.");
            require(mainCamera != nullptr && mainCamera->getComponent<Camera>() != nullptr,
                    "Scene v1 compatibility test failed to restore the main camera component.");

            cleanupAllGameObjects(gameManager);
        }
        catch (const std::exception &)
        {
            cleanupAllGameObjects(gameManager);
            throw;
        }
    }

    struct TestCase
    {
        const char *name;
        void (*run)();
    };
}

int main()
{
    configureHeadlessEnvironment();
    GameManager::getInstance();

    static const TestCase testCases[] = {
        {"scene_version2_round_trip", testSceneVersion2RoundTrip},
        {"scene_version2_rejects_invalid_ids", testSceneVersion2RejectsInvalidIds},
        {"scene_failed_load_clears_pending_starts", testFailedSceneLoadDoesNotLeaveStartedBehaviors},
        {"scene_version1_still_loads", testSceneVersion1StillLoads},
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
        std::cerr << failureCount << " scene serialization test(s) failed.\n";
        return 1;
    }

    return 0;
}