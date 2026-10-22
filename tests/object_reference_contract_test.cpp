#include "test_support.h"
#include <iostream>
#include <platformator/boxcollider.h>
#include <platformator/circlecollider.h>
#include <platformator/objectreference.h>
#include <platformator/scene.h>
#include <platformator/scriptcomponent.h>
#include <platformator/scriptregistration.h>

using namespace test_support;
namespace
{
struct References : ::Behavior
{
    ObjectReference<GameObject> target;
    SERIALIZABLE_SCRIPT(References, target);
};
void requireThrow(const std::function<void()> &action, const std::string &message)
{
    bool threw = false;
    try
    {
        action();
    }
    catch (const std::exception &)
    {
        threw = true;
    }
    require(threw, message);
}
} // namespace
int main()
{
    headlessEnvironment();
    try
    {
        ObjectReference<GameObject> expired;
        require(!expired && !expired.resolve(), "No runtime must be an empty reference");
        {
            platformator::Runtime runtime;
            auto *object = runtime.createGameObject()->setName("target")->addComponent<CircleCollider>(10);
            ObjectReference<GameObject> unbound(object->getId());
            require(!unbound && unbound.getReferencedId() == object->getId(),
                    "File IDs stay unbound until explicit resolution");
            requireThrow([&] { nlohmann::json json = unbound; },
                         "Unbound ID cannot serialize as a published reference");
            require(unbound.resolve(runtime) == object && unbound,
                    "Explicit bind must find the correctly typed object");
            ObjectReference<BoxCollider> wrongShape(object->getComponent<CircleCollider>()->getId());
            ObjectReference<GameObject> wrongObject(object->getComponent<CircleCollider>()->getId());
            require(!wrongShape.resolve(runtime) && !wrongObject.resolve(runtime),
                    "Wrong object/shape kinds cannot bind");
            require(nlohmann::json(unbound) == object->getId(), "Valid live references retain integer JSON");
            expired = unbound;
            const auto oldHandle = object->getHandle();
            runtime.simulateFrameWithCustomCallback(dt, [&](double) {
                object->destroy();
                require(!unbound && nlohmann::json(unbound).is_null(), "Logical retirement expires immediately");
            });
            auto *reused = runtime.createGameObject();
            require(reused->getHandle().slot == oldHandle.slot &&
                        reused->getHandle().generation != oldHandle.generation,
                    "Fixture must actually reuse a slot with a new generation");
            require(!unbound.resolve(runtime), "A bound expired reference cannot rebind");
            unbound.setReferencedId(reused->getId());
            require(unbound.resolve(runtime) == reused, "Explicit file ID reset permits a fresh binding");
            auto *source = runtime.createGameObject()->setName("source")->addComponent<ScriptComponent>();
            auto *references = new References();
            const auto targetId = reused->getId();
            references->target.set(reused);
            source->getComponent<ScriptComponent>()->addBehavior(references);
            const auto path = std::filesystem::absolute("reference-contract.scene");
            runtime.saveScene(path.string());
            Scene subset(path.string());
            requireThrow([&] { subset.saveScene({source}); },
                         "Saving a subset cannot reference a live object outside it");
            const auto id = source->getId();
            ObjectReference<GameObject> previous(source);
            runtime.loadScene(path.string());
            require(!previous && runtime.getObjectById(id) == runtime.getGameObject("source"),
                    "Repeated file IDs cannot revive old handles");
            auto *loaded = runtime.getGameObject("source")->getComponent<ScriptComponent>()->getBehavior<References>();
            require(loaded && loaded->target && loaded->target->getId() == targetId,
                    "Staged references publish with their target");
            std::filesystem::remove(path);
        }
        require(!expired && nlohmann::json(expired).is_null(),
                "References survive runtime destruction as empty values");
        {
            platformator::Runtime runtime;
            runtime.createGameObject();
            require(!expired.resolve(runtime), "A new runtime cannot revive an old identity");
        }
        std::cout << "[PASS] public reference contracts\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
