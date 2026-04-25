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
        TestSerializedBehavior()
            : displayName(""), speed(0.0f), allowDash(false), precision(0.0), offset(Eigen::Vector2f::Zero()), target()
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
            BEHAVIOR_FIELD(target));

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

    private:
        std::string displayName;
        float speed;
        bool allowDash;
        double precision;
        Eigen::Vector2f offset;
        platformator_behavior_detail::NamedGameObjectReference target;
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
            sceneFile << "        precision 987.654321\n";
            sceneFile << "        offset 11.25,-4.5\n";
            sceneFile << "        target \"Target Dummy\"\n";
            sceneFile << "    }\n";
            sceneFile << "}\n";
            sceneFile.close();

            Scene scene(scenePath.string());
            gameManager.loadScene(scene);

            GameObject *scriptedObject = gameManager.getGameObject("Scripted Object");
            require(scriptedObject != nullptr, "Scene script round-trip test expected one loaded object.");

            ScriptComponent *scriptComponent = scriptedObject->getComponent<ScriptComponent>();
            require(scriptComponent != nullptr, "Scene script round-trip test failed to load the script component.");

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