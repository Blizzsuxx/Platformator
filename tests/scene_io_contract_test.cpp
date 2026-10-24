#include "test_support.h"
#include <functional>
#include <iostream>
#include <limits>
#include <platformator/assetreference.h>
#include <platformator/objectreference.h>
#include <platformator/scriptcomponent.h>
#include <platformator/scriptregistration.h>

using namespace test_support;
namespace
{
using Json = nlohmann::json;
struct RequiredTarget : ::Behavior
{
    ObjectReference<GameObject> target;
    SERIALIZABLE_SCRIPT(RequiredTarget, target);
};
struct RequiredTexture : ::Behavior
{
    AssetReference<TextureWrapper> texture;
    SERIALIZABLE_SCRIPT(RequiredTexture, texture);
};
struct ThrowingResolver : test_support::Behavior
{
    AssetReference<TextureWrapper> texture;
    inline static int destroyed = 0, callbacks = 0, attempts = 0, starts = 0;
    inline static bool published = false;
    inline static TextureWrapper *shared = nullptr;
    ~ThrowingResolver() override
    {
        ++destroyed;
    }
    void onDestroy() override
    {
        ++callbacks;
    }
    void start() override
    {
        ++starts;
    }
    void resolveReferences(platformator::ReferenceResolver &resolver) override
    {
        ++attempts;
        auto *object = resolver.findObject(0, platformator_detail::ObjectKind::GameObject);
        published = getRuntime().resolveHandle(object->getHandle(),
                                               platformator_detail::ObjectKind::GameObject) != nullptr;
        require(resolver.loadTexture("assets/textures/ball.png") == shared,
                "Startup must reuse a retained resource");
        texture.set(resolver.loadTexture("assets/textures/black_circle.png"));
        throw std::runtime_error("intentional resolver failure");
    }
};
void write(const std::filesystem::path &path, const Json &document)
{
    std::ofstream file(path);
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file << document;
}
std::string read(const std::filesystem::path &path)
{
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
std::string failure(const std::function<void()> &action)
{
    try
    {
        action();
    }
    catch (const std::exception &error)
    {
        return error.what();
    }
    throw std::runtime_error("Expected operation to fail");
}
} // namespace
int main()
{
    headlessEnvironment();
    const auto directory = std::filesystem::absolute("scene-io-fixtures");
    std::filesystem::create_directories(directory);
    try
    {
        platformator::registerScriptFactory("ThrowingResolver",
                                            []() -> ::Behavior * { return new ThrowingResolver(); });
        platformator::Runtime runtime;
        const auto original = directory / "original.scene", candidate = directory / "candidate.scene";
        writeScene(original, "committed");
        runtime.loadScene(original.string());
        auto *committed = runtime.getGameObject("committed");
        Json document = Json::parse(read(original));
        const auto base = document[0];
        const auto rejects = [&](const Json &bad) {
            write(candidate, bad);
            const auto message = failure([&] { runtime.loadScene(candidate.string()); });
            require(message.find(candidate.string()) != std::string::npos,
                    "Load diagnostics must identify the scene path");
            require(runtime.getGameObject("committed") == committed && runtime.getObjectById(0) == committed,
                    "Failed preparation cannot alter committed ownership or lookup");
        };
        const auto rejectsAtStartup = [&](const Json &bad) {
            ObjectReference<GameObject> previous(committed);
            write(candidate, bad);
            runtime.loadScene(candidate.string());
            auto *published = runtime.getObjectById(0);
            require(!previous && published, "Behavior reference errors must not prevent scene replacement");
            failure([&] { runtime.simulateFrame(dt); });
            require(runtime.getObjectById(0) == published,
                    "Startup reference failure leaves the replacement scene committed");
            runtime.loadScene(original.string());
            committed = runtime.getGameObject("committed");
        };
        auto bad = base;
        bad["id"] = -1;
        rejects(Json::array({bad}));
        rejects(Json::array({base, base}));
        bad = base;
        bad["components"] = {{{"id", 0}, {"type", CAMERA}, {"width", 640}, {"height", 480}}};
        rejects(Json::array({bad}));
        bad = base;
        bad["components"] = {{{"id", 2}, {"type", SCRIPT}, {"behaviors", {{{"type", "UnknownType"}}}}}};
        rejects(Json::array({bad}));
        bad = base;
        bad["components"] = {
            {{"id", 2}, {"type", SCRIPT}, {"behaviors", {{{"type", "RequiredTarget"}, {"target", 999}}}}}};
        auto parent = base;
        parent["id"] = 3;
        parent["children"] = Json::array({bad});
        rejectsAtStartup(Json::array({parent}));
        bad["components"][0]["behaviors"][0]["target"] = 2;
        rejectsAtStartup(Json::array({bad})); // Script component is not a GameObject.
        bad["components"][0]["behaviors"] = {{{"type", "RequiredTexture"},
                                               {"texture", "assets/textures/missing-startup-resource.png"}}};
        rejectsAtStartup(Json::array({bad}));
        bad = base;
        bad["scale"]["x"] = 0;
        rejects(Json::array({bad}));
        AssetReference<TextureWrapper> retained(runtime.loadTexture("assets/textures/ball.png"));
        require(static_cast<bool>(retained), "Shared texture fixture must load");
        ThrowingResolver::shared = retained.get();
        bad = base;
        bad["components"] = {{{"id", 2}, {"type", SCRIPT}, {"behaviors", {{{"type", "ThrowingResolver"}}}}}};
        for (int i = 0; i < 20; ++i)
        {
            write(candidate, Json::array({bad}));
            runtime.loadScene(candidate.string());
            require(ThrowingResolver::attempts == i, "Loading must not invoke the behavior resolver");
            const auto message = failure([&] { runtime.simulateFrame(dt); });
            require(message.find("intentional resolver failure") != std::string::npos &&
                        ThrowingResolver::attempts == i + 1 && ThrowingResolver::starts == 0 &&
                        ThrowingResolver::published,
                    "Resolver errors propagate before start, against the published scene");
            runtime.clearScene();
        }
        require(ThrowingResolver::destroyed == 20 && ThrowingResolver::callbacks == 20 && retained->getTexture(),
                "Clearing a failed startup must reclaim behaviors/resources through normal retirement");
        runtime.loadScene(original.string());
        committed = runtime.getGameObject("committed");
        // Fail after one fully constructed behavior, during the next deserialize.
        bad["components"][0]["behaviors"].push_back({{"type", "RequiredTarget"}, {"target", "invalid integer"}});
        rejects(Json::array({bad}));
        runtime.createGameObject()->setName("unsaved");
        const auto oldContents = read(original);
        failure([&] { runtime.saveScene((directory / "absent" / "failed.scene").string()); });
        require(read(original) == oldContents, "Failed save cannot alter the prior destination");
        runtime.saveScene();
        require(read(original).find("unsaved") != std::string::npos,
                "Failed save cannot replace the current successful path");
        const auto blocked = directory / "blocked.scene";
        std::filesystem::create_directory(blocked);
        write(blocked / "marker", Json("keep"));
        failure([&] { runtime.saveScene(blocked.string()); });
        require(read(blocked / "marker") == "\"keep\"", "Replace failure must preserve its prior destination");
        const auto longPath = directory / (std::string(249, 'a') + ".scene");
        write(longPath, Json("original"));
        failure([&] { runtime.saveScene(longPath.string()); });
        require(read(longPath) == "\"original\"", "Temporary file creation failure must preserve the destination");
        bool callbackRejected = false;
        runtime.simulateFrameWithCustomCallback(dt, [&](double) {
            try
            {
                runtime.saveScene(original.string());
            }
            catch (const std::logic_error &)
            {
                callbackRejected = true;
            }
        });
        require(callbackRejected, "Save is an idle-only API");
        // A valid graph can reserve INT_MAX; automatic creation must fail before ownership changes.
        bad = base;
        bad["id"] = std::numeric_limits<int>::max();
        bad["components"] = {{{"id", 0}, {"type", CAMERA}, {"width", 640}, {"height", 480}}};
        write(candidate, Json::array({bad}));
        runtime.loadScene(candidate.string());
        const auto count = runtime.getAllGameObjects().size();
        failure([&] { runtime.createGameObject(); });
        require(runtime.getAllGameObjects().size() == count, "ID exhaustion cannot publish an allocation");
        auto *last = runtime.getObjectById(std::numeric_limits<int>::max());
        failure([&] { last->setId(5); });
        runtime.clearScene();
        require(runtime.createGameObject()->getId() == 0, "A new empty scene resets its file-ID allocator");
        for (const auto &entry : std::filesystem::directory_iterator(directory))
            require(entry.path().filename().string().find(".tmp.") == std::string::npos,
                    "Failed saves must clean temporary files");
        std::filesystem::remove_all(directory);
        std::cout << "[PASS] scene I/O contracts\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
