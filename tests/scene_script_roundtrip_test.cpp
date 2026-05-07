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

#include <json.hpp>

#include "animator.h"
#include "audio.h"
#include "boxcollider.h"
#include "camera.h"
#include "constants.h"
#include "jsonhelpers.h"
#include "platformator/assetreference.h"
#include "platformator/objectreference.h"
#include "platformator/runtime.h"
#include "rigidbody.h"
#include "scene.h"
#include "scriptcomponent.h"
#include "sprite.h"

namespace scene_roundtrip_test_support
{
    class NamespacedSceneBehavior : public Behavior
    {
    public:
        SERIALIZABLE_SCRIPT(scene_roundtrip_test_support::NamespacedSceneBehavior);
    };
}

namespace
{
    using platformator::Runtime;

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

    void cleanupAllGameObjects(Runtime &gameManager)
    {
        gameManager.clearScene();
    }

    class AnnotatedSceneBehavior : public Behavior
    {
    public:
        AnnotatedSceneBehavior()
            : displayName(), speed(0.0f), allowDash(false), precision(0.0), offset(Eigen::Vector2f::Zero()), targetObject(), emitter(), icon(), idleClip(), sound()
        {
        }

        std::string displayName;
        float speed;
        bool allowDash;
        double precision;
        Eigen::Vector2f offset;
        ObjectReference<GameObject> targetObject;
        ObjectReference<Audio> emitter;
        platformator::TextureAssetRef icon;
        platformator::AnimationClipRef idleClip;
        platformator::AudioAssetRef sound;

        SERIALIZABLE_SCRIPT(AnnotatedSceneBehavior,
                            displayName,
                            speed,
                            allowDash,
                            precision,
                            offset,
                            targetObject,
                            emitter,
                            icon,
                            idleClip,
                            sound);
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

    nlohmann::json makeVectorJson(float x, float y)
    {
        return nlohmann::json{{"x", x}, {"y", y}};
    }

    nlohmann::json makeRectJson(float x, float y, float w, float h)
    {
        return nlohmann::json{{"x", x}, {"y", y}, {"w", w}, {"h", h}};
    }

    nlohmann::json makeGameObjectJson(int id, const std::string &name)
    {
        return nlohmann::json{{"id", id},
                              {"rotation", 0.0f},
                              {"active", true},
                              {"position", makeVectorJson(0.0f, 0.0f)},
                              {"scale", makeVectorJson(1.0f, 1.0f)},
                              {"name", name},
                              {"tag", ""},
                              {"children", nlohmann::json::array()},
                              {"components", nlohmann::json::array()}};
    }

    const nlohmann::json *findSavedObject(const nlohmann::json &objects, const std::string &name)
    {
        for (const nlohmann::json &objectJson : objects)
        {
            if (!objectJson.is_object())
            {
                continue;
            }

            const auto nameIt = objectJson.find("name");
            if (nameIt != objectJson.end() && nameIt->is_string() && nameIt->get<std::string>() == name)
            {
                return &objectJson;
            }
        }

        return nullptr;
    }

    const nlohmann::json *findSavedComponent(const nlohmann::json &components, ComponentType componentType)
    {
        for (const nlohmann::json &componentJson : components)
        {
            if (!componentJson.is_object())
            {
                continue;
            }

            const auto typeIt = componentJson.find("type");
            if (typeIt != componentJson.end() && typeIt->is_number_integer() && typeIt->get<int>() == static_cast<int>(componentType))
            {
                return &componentJson;
            }
        }

        return nullptr;
    }

    void writeJsonFile(const std::filesystem::path &path, const nlohmann::json &document, const std::string &errorMessage)
    {
        if (path.has_parent_path())
        {
            std::error_code directoryError;
            std::filesystem::create_directories(path.parent_path(), directoryError);
        }

        std::ofstream file(path);
        require(file.is_open(), errorMessage);
        file << document.dump(4);
    }

    void testSceneVersion2RoundTrip()
    {
        Runtime &gameManager = Runtime::current();
        cleanupAllGameObjects(gameManager);

        const std::filesystem::path scenePath = std::filesystem::current_path() / "scene_v2_roundtrip.scene";
        const std::filesystem::path animationClipPath = std::filesystem::path("assets") / "test_runtime" / "scene_v2_roundtrip.animset";
        const std::filesystem::path texturePath = std::filesystem::path("assets") / "ball.png";
        const std::filesystem::path audioPath = std::filesystem::path("assets") / "audio" / "jump.wav";

        const std::string textureAssetPath = texturePath.generic_string();
        const std::string audioAssetPath = audioPath.generic_string();
        const std::string animationClipAssetPath = animationClipPath.generic_string();

        auto cleanupFiles = [&]()
        {
            std::error_code errorCode;
            std::filesystem::remove(scenePath, errorCode);
            std::filesystem::remove(animationClipPath, errorCode);
        };

        cleanupFiles();

        try
        {
            const nlohmann::json animationClipJson = {
                {"frames", nlohmann::json::array({{{"duration", 0.0f},
                                                   {"sourceRect", makeRectJson(0.0f, 0.0f, 0.0f, 0.0f)},
                                                   {"hasSourceRect", false},
                                                   {"textureWrapperFilePath", textureAssetPath}}})},
                {"framesPerSecond", 1.0},
                {"loop", true},
                {"width", 32.0f},
                {"height", 32.0f},
                {"name", "idle"},
                {"filePath", animationClipAssetPath}};
            writeJsonFile(animationClipPath, animationClipJson,
                          "Scene v2 round-trip test failed to create the temporary animation clip file.");

            nlohmann::json targetDummyJson = makeGameObjectJson(100, "Target Dummy");
            targetDummyJson["components"].push_back({{"id", 300},
                                                     {"filePath", ""},
                                                     {"gain", 0.4f},
                                                     {"loopCount", 0},
                                                     {"type", static_cast<int>(ComponentType::AUDIO)},
                                                     {"autoPlay", false}});

            nlohmann::json mainCameraJson = makeGameObjectJson(101, "Main Camera");
            mainCameraJson["components"].push_back({{"id", 200},
                                                    {"width", 640.0f},
                                                    {"height", 480.0f},
                                                    {"type", static_cast<int>(ComponentType::CAMERA)}});

            nlohmann::json spriteOnlyJson = makeGameObjectJson(103, "Sprite Only");
            spriteOnlyJson["components"].push_back({{"id", 204},
                                                    {"textureFilePath", textureAssetPath},
                                                    {"flip", static_cast<int>(SDL_FLIP_NONE)},
                                                    {"width", 32.0f},
                                                    {"height", 32.0f},
                                                    {"sourceRectEnabled", true},
                                                    {"sourceRect", makeRectJson(0.0f, 0.0f, 16.0f, 16.0f)},
                                                    {"type", static_cast<int>(ComponentType::SPRITE)}});

            nlohmann::json scriptedObjectJson = makeGameObjectJson(102, "Scripted Object");
            scriptedObjectJson["position"] = makeVectorJson(12.0f, 34.0f);
            scriptedObjectJson["scale"] = makeVectorJson(1.5f, 2.0f);
            scriptedObjectJson["components"].push_back({{"id", 202},
                                                        {"textureFilePath", textureAssetPath},
                                                        {"flip", static_cast<int>(SDL_FLIP_HORIZONTAL)},
                                                        {"width", 32.0f},
                                                        {"height", 32.0f},
                                                        {"sourceRectEnabled", true},
                                                        {"sourceRect", makeRectJson(0.0f, 0.0f, 16.0f, 16.0f)},
                                                        {"type", static_cast<int>(ComponentType::SPRITE)}});
            scriptedObjectJson["components"].push_back({{"id", 201},
                                                        {"currentFrameIndex", 0},
                                                        {"playbackSpeed", 1.5f},
                                                        {"playing", true},
                                                        {"type", static_cast<int>(ComponentType::ANIMATOR)},
                                                        {"animationClipFilePath", animationClipAssetPath}});
            scriptedObjectJson["components"].push_back({{"id", 203},
                                                        {"type", static_cast<int>(ComponentType::SCRIPT)},
                                                        {"behaviors", nlohmann::json::array({{{"type", "AnnotatedSceneBehavior"},
                                                                                              {"displayName", "Player One"},
                                                                                              {"speed", 123.5f},
                                                                                              {"allowDash", true},
                                                                                              {"precision", 987.654321},
                                                                                              {"offset", makeVectorJson(11.25f, -4.5f)},
                                                                                              {"targetObject", 100},
                                                                                              {"emitter", 300},
                                                                                              {"icon", textureAssetPath},
                                                                                              {"idleClip", animationClipAssetPath},
                                                                                              {"sound", audioAssetPath}}})}});

            const nlohmann::json sceneJson = nlohmann::json::array({targetDummyJson, mainCameraJson, spriteOnlyJson, scriptedObjectJson});
            writeJsonFile(scenePath, sceneJson,
                          "Scene v2 round-trip test failed to create the temporary scene file.");

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
            const std::optional<int> targetObjectId = behavior->targetObject.getReferencedId();
            require(targetObjectId.has_value() && *targetObjectId == 100 && behavior->targetObject.get() == targetDummy,
                    "Scene v2 round-trip test failed to resolve the game object reference field.");
            const std::optional<int> emitterId = behavior->emitter.getReferencedId();
            require(emitterId.has_value() && *emitterId == 300 && behavior->emitter.get() == targetAudio,
                    "Scene v2 round-trip test failed to resolve the component reference field.");
            require(behavior->icon.getFilePath().has_value() && *behavior->icon.getFilePath() == textureAssetPath && behavior->icon.get() != nullptr,
                    "Scene v2 round-trip test failed to resolve the texture asset field.");
            require(behavior->idleClip.getFilePath().has_value() && *behavior->idleClip.getFilePath() == animationClipAssetPath && behavior->idleClip.get() != nullptr,
                    "Scene v2 round-trip test failed to resolve the animation clip asset field.");
            require(behavior->sound.getFilePath().has_value() && *behavior->sound.getFilePath() == audioAssetPath && behavior->sound.get() != nullptr,
                    "Scene v2 round-trip test failed to resolve the audio asset field.");

            gameManager.saveScene(scene);

            std::ifstream savedSceneFile(scenePath);
            require(savedSceneFile.is_open(), "Scene v2 round-trip test failed to reopen the saved scene.");
            const nlohmann::json savedDocument = nlohmann::json::parse(savedSceneFile);
            require(savedDocument.is_array(), "Scene v2 round-trip test failed to save the objects array.");

            const nlohmann::json *savedMainCamera = findSavedObject(savedDocument, "Main Camera");
            require(savedMainCamera != nullptr, "Scene v2 round-trip test failed to save the main camera object.");
            require(savedMainCamera->at("children").is_array() && savedMainCamera->at("children").empty(),
                    "Scene v2 round-trip test failed to preserve the empty child list.");

            const nlohmann::json *savedScriptedObject = findSavedObject(savedDocument, "Scripted Object");
            require(savedScriptedObject != nullptr, "Scene v2 round-trip test failed to save the scripted object.");
            const nlohmann::json *savedScript = findSavedComponent(savedScriptedObject->at("components"), ComponentType::SCRIPT);
            require(savedScript != nullptr, "Scene v2 round-trip test failed to save the explicit script component block.");
            const nlohmann::json &savedBehaviors = savedScript->at("behaviors");
            require(savedBehaviors.is_array() && !savedBehaviors.empty(),
                    "Scene v2 round-trip test failed to save script behaviors.");
            const nlohmann::json &savedBehavior = savedBehaviors.at(0);
            require(savedBehavior.is_object(), "Scene v2 round-trip test failed to save the behavior object.");
            require(savedBehavior.contains("displayName"),
                    "Scene v2 round-trip test failed to preserve annotated field casing when saving behavior data.");
            require(savedBehavior.at("targetObject").get<int>() == 100,
                    "Scene v2 round-trip test failed to serialize the game object reference field using ids.");
            require(savedBehavior.at("emitter").get<int>() == 300,
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
        Runtime &gameManager = Runtime::current();
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
                nlohmann::json invalidScene = nlohmann::json::array();
                invalidScene.push_back(makeGameObjectJson(0, "Zero Id"));
                writeJsonFile(scenePath, invalidScene,
                              "Scene invalid-id test failed to create the temporary scene file.");
            }

            bool threw = false;
            Scene zeroIdScene(scenePath.string());
            gameManager.loadScene(zeroIdScene);
            GameObject *zeroIdObject = gameManager.getGameObject("Zero Id");
            require(zeroIdObject != nullptr && zeroIdObject->getId() == 0,
                    "Scene id test failed to preserve a zero-valued object id.");

            cleanupAllGameObjects(gameManager);

            cleanupFile();

            {
                nlohmann::json firstObject = makeGameObjectJson(400, "First");
                firstObject["components"].push_back({{"id", 900},
                                                     {"filePath", ""},
                                                     {"gain", 0.5f},
                                                     {"loopCount", 0},
                                                     {"type", static_cast<int>(ComponentType::AUDIO)},
                                                     {"autoPlay", false}});

                nlohmann::json secondObject = makeGameObjectJson(401, "Second");
                secondObject["components"].push_back({{"id", 900},
                                                      {"type", static_cast<int>(ComponentType::SCRIPT)},
                                                      {"behaviors", nlohmann::json::array()}});

                writeJsonFile(scenePath, nlohmann::json::array({firstObject, secondObject}),
                              "Scene duplicate-id test failed to create the temporary scene file.");
            }

            threw = false;
            Scene duplicateIdScene(scenePath.string());
            gameManager.loadScene(duplicateIdScene);
            require(gameManager.getGameObject("First") != nullptr && gameManager.getGameObject("Second") != nullptr,
                    "Scene duplicate-id test failed to load objects that share a component id.");

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

    void testUnknownBehaviorTypesAreIgnored()
    {
        Runtime &gameManager = Runtime::current();
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
            nlohmann::json objectJson = makeGameObjectJson(700, "Broken Scripted Object");
            objectJson["components"].push_back({{"id", 701},
                                                {"type", static_cast<int>(ComponentType::SCRIPT)},
                                                {"behaviors", nlohmann::json::array({{{"type", "scene_roundtrip_test_support::NamespacedSceneBehavior"},
                                                                                      {"typeNameNote", "valid"}},
                                                                                     {{"type", "MissingBehaviorType"}}})}});
            writeJsonFile(scenePath, nlohmann::json::array({objectJson}),
                          "Scene unknown-behavior test failed to create the temporary scene file.");

            Scene scene(scenePath.string());
            gameManager.loadScene(scene);

            GameObject *loadedObject = gameManager.getGameObject("Broken Scripted Object");
            require(loadedObject != nullptr, "Scene unknown-behavior test failed to load the scripted object.");
            ScriptComponent *scriptComponent = loadedObject->getComponent<ScriptComponent>();
            require(scriptComponent != nullptr, "Scene unknown-behavior test failed to load the script component.");
            require(scriptComponent->getBehaviors().size() == 1,
                    "Scene unknown-behavior test should ignore missing behavior types without instantiating them.");

            gameManager.simulateFrame(FRAME_TIME);
            require(gameManager.getGameObject("Broken Scripted Object") != nullptr,
                    "Scene unknown-behavior test lost runtime state after loading a scene with an ignored behavior type.");

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
        Runtime &gameManager = Runtime::current();
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
    platformator::Runtime runtime;

    static const TestCase testCases[] = {
        {"scene_version2_round_trip", testSceneVersion2RoundTrip},
        {"scene_version2_rejects_invalid_ids", testSceneVersion2RejectsInvalidIds},
        {"scene_unknown_behaviors_are_ignored", testUnknownBehaviorTypesAreIgnored},
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