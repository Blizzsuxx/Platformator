#include "test_support.h"
#include <iostream>
#include <platformator/objectreference.h>
#include <platformator/scriptcomponent.h>
#include <platformator/scriptregistration.h>
#include <platformator/boxcollider.h>

using namespace test_support;

namespace
{
struct LoadedStart : test_support::Behavior
{
    ObjectReference<GameObject> target;
    inline static int resolutions = 0, starts = 0, updates = 0;
    inline static std::vector<std::string> trace;
    void deserialize(const nlohmann::json &json) override
    {
        json.at("target").get_to(target);
    }
    void resolveReferences(platformator::ReferenceResolver &resolver) override
    {
        ++resolutions;
        trace.push_back("resolve");
        resolver.resolve(target);
        require(target.get() == getGameObject(), "Startup resolution must use the published scene");
    }
    void start() override
    {
        require(target.get() == getGameObject(), "References must resolve before start");
        trace.push_back("start");
        ++starts;
    }
    void update(double) override
    {
        trace.push_back("update");
        ++updates;
    }
};
struct ExitRequest : test_support::Behavior
{
    ExitRequest(std::string path, int &calls) : path(std::move(path)), calls(calls)
    {
    }
    void onCollisionExit(Collider *other, double) override
    {
        require(other->getGameObject()->getName() == "victim", "Exit must retain the retired collider allocation");
        getRuntime().requestLoadScene(path);
        ++calls;
    }
    std::string path;
    int &calls;
};
} // namespace

int main()
{
    headlessEnvironment();
    const auto a = std::filesystem::absolute("transition-a.scene");
    const auto b = std::filesystem::absolute("transition-b.scene");
    try
    {
        platformator::registerScriptFactory("LoadedStart", []() -> ::Behavior * { return new LoadedStart(); });
        writeScene(a, "old");
        writeScene(b, "new");
        platformator::Runtime runtime;
        runtime.loadScene(a.string());
        ObjectReference<GameObject> old(runtime.getGameObject("old"));
        runtime.simulateFrameWithCustomCallback(dt, [&](double) {
            runtime.loadScene("superseded-file-must-not-be-opened.scene");
            runtime.loadScene(b.string());
            require(old && old->getName() == "old", "Scene requests must not retire the running callback's scene");
        });
        require(!old && !runtime.getGameObject("old") && runtime.getGameObject("new"), "Load must replace the scene");
        require(runtime.getObjectById(0) == runtime.getGameObject("new"),
                "Repeated file IDs must resolve only in the new scene");
        bool failed = false;
        try
        {
            runtime.loadScene("missing-idle.scene");
        }
        catch (const std::exception &)
        {
            failed = true;
        }
        require(failed && runtime.getGameObject("new"), "Failed idle preparation must preserve the current scene");
        failed = false;
        bool returned = false;
        try
        {
            runtime.simulateFrameWithCustomCallback(dt, [&](double) {
                runtime.requestLoadScene("missing-deferred.scene");
                returned = true;
            });
        }
        catch (const std::exception &)
        {
            failed = true;
        }
        require(returned && failed && runtime.getGameObject("new"), "Deferred errors belong to the host boundary");
        runtime.requestClearScene();
        runtime.simulateFrame(dt);
        require(runtime.getAllGameObjects().empty() && !runtime.getMainCamera(),
                "Clear must leave an empty scene without a camera");
        failed = false;
        try
        {
            runtime.saveScene();
        }
        catch (const std::logic_error &)
        {
            failed = true;
        }
        require(failed, "Clear must remove the default save path");
        runtime.simulateAndRenderFrame(dt);
        runtime.requestLoadScene(a.string());
        runtime.requestLoadScene(b.string());
        runtime.simulateFrame(dt);
        require(runtime.getGameObject("new") && !runtime.getGameObject("old"),
                "Queued idle requests must also use last-wins replacement");
        nlohmann::json document;
        {
            std::ifstream file(b);
            file >> document;
        }
        document[0]["components"].push_back(
            {{"id", 1}, {"type", SCRIPT}, {"behaviors", {{{"type", "LoadedStart"}, {"target", 0}}}}});
        {
            std::ofstream file(b);
            file << document;
        }
        runtime.simulateFrameWithCustomCallback(dt, [&](double) { runtime.requestLoadScene(b.string()); });
        auto *loaded = runtime.getGameObject("new")->getComponent<ScriptComponent>()->getBehavior<LoadedStart>();
        require(LoadedStart::resolutions == 0 && LoadedStart::starts == 0 && LoadedStart::updates == 0 && !loaded->target,
                "Scene commit must not resolve or start behaviors");
        runtime.simulateFrame(dt);
        require(LoadedStart::starts == 1 && LoadedStart::updates == 1,
                "Published scripts start before dispatch next step");
        require(LoadedStart::trace == std::vector<std::string>{"resolve", "start", "update"},
                "Loaded behaviors must resolve immediately before start");
        runtime.simulateFrame(dt);
        require(LoadedStart::resolutions == 1 && LoadedStart::starts == 1,
                "Ordinary steps must not repeat initialization");
        runtime.getGameObject("new")->setActive(false);
        runtime.getGameObject("new")->setActive(true);
        runtime.simulateFrame(dt);
        require(LoadedStart::resolutions == 1 && LoadedStart::starts == 1,
                "Reactivation must not repeat initialization");

        document[0]["active"] = false;
        {
            std::ofstream file(b);
            file << document;
        }
        runtime.loadScene(b.string());
        runtime.simulateFrame(dt);
        require(LoadedStart::resolutions == 1 && LoadedStart::starts == 1,
                "Inactive loaded behaviors defer resolution as well as start");
        runtime.getGameObject("new")->setActive(true);
        runtime.simulateFrame(dt);
        require(LoadedStart::resolutions == 2 && LoadedStart::starts == 2,
                "First activation resolves and starts a loaded behavior");

        auto *created = runtime.createGameObject()->addComponent<ScriptComponent>();
        auto *createdBehavior = new LoadedStart();
        createdBehavior->target.set(created);
        created->getComponent<ScriptComponent>()->addBehavior(createdBehavior);
        LoadedStart::trace.clear();
        runtime.simulateFrame(dt);
        require(LoadedStart::resolutions == 3 && LoadedStart::starts == 3 &&
                    LoadedStart::trace.size() >= 2 && LoadedStart::trace[0] == "resolve" &&
                    LoadedStart::trace[1] == "start",
                "Programmatic behaviors use the same resolve-then-start path");
        int exits = 0;
        auto *survivor = runtime.createGameObject()
                             ->setName("survivor")
                             ->addComponent<BoxCollider>(20, 20)
                             ->addComponent<ScriptComponent>();
        survivor->getComponent<ScriptComponent>()->addBehavior(new ExitRequest(a.string(), exits));
        auto *victim = runtime.createGameObject()->setName("victim")->addComponent<BoxCollider>(20, 20);
        runtime.simulateFrame(dt);
        failed = false;
        try
        {
            runtime.simulateFrameWithCustomCallback(dt, [&](double) {
                victim->destroy();
                runtime.requestLoadScene("missing-while-retiring.scene");
            });
        }
        catch (const std::exception &)
        {
            failed = true;
        }
        require(failed && exits == 1 && runtime.getGameObject("survivor") == survivor && runtime.getGameObject("new"),
                "Failed preparation must still drain ordinary retirements; exit requests cannot recursively commit");
        runtime.simulateFrame(dt);
        require(runtime.getGameObject("old") && !runtime.getGameObject("survivor"),
                "Retirement exit requests commit on the following step");
        // Pending requests do not outlive their runtime, including an invalid path.
        runtime.requestLoadScene("must-not-load-during-shutdown.scene");
        std::filesystem::remove(a);
        std::filesystem::remove(b);
        std::cout << "[PASS] scene_transition_contract\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
