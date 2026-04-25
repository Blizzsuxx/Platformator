#include <cstdlib>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

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

    class TestSerializedBehavior : public Behavior
    {
    public:
        TestSerializedBehavior() : displayName(""), speed(0.0f), allowDash(false)
        {
        }

        std::string getTypeName() const override
        {
            return "TestSerializedBehavior";
        }

        void deserialize(const ScriptDescriptor &descriptor) override
        {
            displayName = descriptor.getString("displayname", displayName);
            speed = descriptor.getFloat("speed", speed);
            allowDash = descriptor.getBool("allowdash", allowDash);
        }

        void serialize(ScriptDescriptor &descriptor) const override
        {
            descriptor.setStringProperty("displayname", displayName);
            descriptor.setFloatProperty("speed", speed);
            descriptor.setBoolProperty("allowdash", allowDash);
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

    private:
        std::string displayName;
        float speed;
        bool allowDash;
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

        auto cleanup = [&]()
        {
            destroyNamedObject(gameManager, "Scripted Object");

            std::error_code errorCode;
            std::filesystem::remove(scenePath, errorCode);
        };

        cleanup();

        try
        {
            std::ofstream sceneFile(scenePath);
            require(sceneFile.is_open(), "Scene script round-trip test failed to create the temporary scene file.");

            sceneFile << "object {\n";
            sceneFile << "    name \"Scripted Object\"\n";
            sceneFile << "    script {\n";
            sceneFile << "        type \"TestSerializedBehavior\"\n";
            sceneFile << "        displayName \"Player One\"\n";
            sceneFile << "        speed 123.5\n";
            sceneFile << "        allowDash true\n";
            sceneFile << "    }\n";
            sceneFile << "}\n";
            sceneFile.close();

            Scene scene(scenePath.string());
            gameManager.loadScene(scene);

            GameObject *scriptedObject = gameManager.getGameObject("Scripted Object");
            require(scriptedObject != nullptr, "Scene script round-trip test expected one loaded object.");

            ScriptComponent *scriptComponent = scriptedObject->getComponent<ScriptComponent>();
            require(scriptComponent != nullptr, "Scene script round-trip test failed to load the script component.");

            TestSerializedBehavior *behavior = scriptComponent->getBehavior<TestSerializedBehavior>();
            require(behavior != nullptr, "Scene script round-trip test failed to load the test behavior.");
            require(behavior->getDisplayName() == "Player One",
                    "Scene script round-trip test failed to deserialize the string property.");
            require(std::abs(behavior->getSpeed() - 123.5f) <= 1e-5f,
                    "Scene script round-trip test failed to deserialize the float property.");
            require(behavior->getAllowDash(),
                    "Scene script round-trip test failed to deserialize the bool property.");

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
            require(saved.find("speed \"123.5\"") == std::string::npos,
                    "Scene script round-trip test incorrectly quoted a numeric script property.");
            require(saved.find("allowdash \"true\"") == std::string::npos,
                    "Scene script round-trip test incorrectly quoted a boolean script property.");

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
        std::cout << "[PASS] scene_script_roundtrip_test\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "[FAIL] scene_script_roundtrip_test: " << exception.what() << '\n';
        return 1;
    }
}