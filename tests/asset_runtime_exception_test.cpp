#include "scenebuilder.h"
#include "assetrequestaliases.h"
#include "runtimeaccess.h"
#include "test_support.h"
#include <platformator/assetreference.h>
#include <platformator/scene.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <vector>

namespace
{
thread_local long failAfter = -1;
thread_local bool injected = false;
thread_local bool countEnabled = false;
thread_local size_t allocations = 0;
thread_local size_t targetSize = 0;
thread_local bool failAllSizes = false;
}

void *operator new(std::size_t size)
{
    const bool eligible = failAllSizes || targetSize == 0 || size == targetSize;
    if (countEnabled && eligible) ++allocations;
    if (eligible && failAfter >= 0 && failAfter-- == 0)
    {
        failAfter = -1;
        injected = true;
        throw std::bad_alloc();
    }
    if (void *memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }

namespace
{
using test_support::require;
namespace fs = std::filesystem;

void exerciseDeepJsonCleanup()
{
    nlohmann::json nested = 1;
    for (int depth = 0; depth < 512; ++depth)
    {
        nlohmann::json parent = nlohmann::json::array();
        parent.push_back(std::move(nested));
        nested = std::move(parent);
    }
    injected = false;
    failAfter = 0;
    nested = nullptr;
    failAfter = -1;
    require(!injected, "Deep JSON cleanup must not allocate");
}

template <typename T, typename Load>
void exerciseReferenceFailures(const std::string &name, const std::string &path,
                               const fs::path &file, Load load, platformator::Runtime &runtime)
{
    AssetReference<T> owner(load(path));
    auto *original = owner.get();
    targetSize = path.size() + 1;
    auto failCopy = [&](auto &&action, long budget = 0) {
        injected = false;
        failAfter = budget;
        try { action(); }
        catch (const std::bad_alloc &) { failAfter = -1; require(injected, name + " unexpected failure"); return; }
        failAfter = -1;
        require(false, name + " must exercise a path allocation failure");
    };
    AssetReference<T> destination;
    failCopy([&] { AssetReference<T> adopted(original); });
    require(owner.get() == original, name + " failed construction must preserve ownership");
    failCopy([&] { destination.set(original); });
    require(!destination && owner.get() == original, name + " failed set must preserve ownership");
    destination.set(original);
    destination.clear();
    failCopy([&] { destination = owner; });
    require(!destination && owner.get() == original, name + " failed assignment must preserve ownership");
    destination = owner;
    failCopy([&] { destination.setFilePath(path); });
    require(destination.get() == original, name + " failed path replacement must preserve ownership");
    destination.clear();
    failCopy([&] { AssetReference<T> copy(owner); });
    require(owner.get() == original, name + " failed copy construction must preserve ownership");
    AssetReference<T> pending(path);
    failCopy([&] { pending.resolve(runtime); }, 1);
    require(!pending && owner.get() == original, name + " failed runtime resolution must preserve ownership");
    require(pending.resolve(runtime) == original, name + " runtime resolution retry must succeed");
    pending.clear();
    {
        SceneBuildContext context(platformator_detail::RuntimeAccess::gameManager());
        AssetReference<T> staged(path);
        failCopy([&] { staged.resolve(context); }, 1);
        require(!staged && owner.get() == original, name + " failed staged resolution must preserve ownership");
        require(staged.resolve(context) == original, name + " staged resolution retry must succeed");
    }
    owner.clear();
    auto *fresh = load(path);
    failCopy([&] { destination.set(fresh); });
    require(!destination, name + " failed adoption must not retain a pointer");
    const auto backup = file.string() + ".backup";
    fs::rename(file, backup);
    bool rejected = false;
    try { load(path); } catch (const std::exception &) { rejected = true; }
    fs::rename(backup, file);
    require(rejected, name + " failed adoption must evict the unowned resource");
    { AssetReference<T> retry(load(path)); }
    std::cout << name << " reference allocation failures and retries verified\n";
}

template <typename T, typename Load>
void exerciseLoad(const std::string &name, const std::string &path, Load load)
{
    targetSize = path.size() + 1;
    allocations = 0;
    countEnabled = true;
    auto *sample = load(path);
    countEnabled = false;
    const size_t total = allocations;
    { AssetReference<T> held(sample); }
    require(total > 0, name + " fixture did not allocate");

    std::vector<size_t> budgets;
    if (failAllSizes)
        for (size_t i = 0; i < total; ++i) budgets.push_back(i);
    else
    {
        for (size_t i = 0; i < total && i < 8; ++i) budgets.push_back(i);
        for (size_t i = total > 18 ? total - 18 : 8; i < total; ++i) budgets.push_back(i);
    }
    size_t failures = 0;
    for (const auto budget : budgets)
    {
        injected = false;
        failAfter = static_cast<long>(budget);
        T *result = nullptr;
        try { result = load(path); }
        catch (const std::exception &)
        {
            failAfter = -1;
            require(injected, name + " failed without injected allocation failure");
            ++failures;
        }
        failAfter = -1;
        if (result) { AssetReference<T> held(result); }
        auto *retry = load(path);
        require(retry != nullptr, name + " retry must load a usable resource");
        { AssetReference<T> held(retry); }
    }
    require(failures > 0, name + " did not exercise an injected failure");
    std::cout << name << " allocations=" << total << " injected_failures=" << failures << '\n';
}

template <typename T, typename Load>
void exerciseExisting(const std::string &name, const std::string &basePath, const std::string &suffix, Load load)
{
    AssetReference<T> held(load(basePath));
    auto *original = held.get();
    size_t failures = 0;
    for (int i = 0; i < 3; ++i)
    {
        const auto alias = "assets/" + std::string(150 + i, '/') + suffix;
        targetSize = alias.size() + 1;
        injected = false;
        failAfter = 0;
        T *result = nullptr;
        try { result = load(alias); }
        catch (const std::exception &)
        {
            failAfter = -1;
            require(injected, name + " alias failed without injected allocation failure");
            ++failures;
        }
        failAfter = -1;
        require(held.get() == original && load(basePath) == original,
                name + " failure must preserve the already retained resource");
        if (result) require(result == original, name + " alias must share the existing resource");
        require(load(alias) == original, name + " alias retry must share the existing resource");
    }
    require(failures > 0, name + " existing-resource failure was not exercised");
    std::cout << name << " existing-resource failures=" << failures << '\n';
}

void exerciseNestedPublication(platformator::Runtime &runtime, const fs::path &root, const fs::path &source)
{
    const std::string path = "assets/textures/nested-publication.png";
    const std::string request(80, 'r'), canonical(80, 'c');
    bool sawFailure = false, sawSuccess = false;
    for (long budget = 0; budget < 32; ++budget)
    {
        fs::copy_file(source, root / "textures/nested-publication.png", fs::copy_options::overwrite_existing);
        AssetReference<TextureWrapper> held(runtime.loadTexture(path));
        std::vector<AnimationFrame> frames;
        frames.emplace_back(held.get(), 0.1f);
        auto clip = std::make_unique<AnimationClip>(std::move(frames), 1.0, true, 1.0f, 1.0f, "nested");
        std::unordered_map<std::string, std::unique_ptr<AnimationClip>> cache;
        platformator_detail::AssetRequestAliases aliases;
        targetSize = 0;
        injected = false;
        failAfter = budget;
        try
        {
            platformator_detail::publishAsset(cache, aliases, request, canonical, std::move(clip));
            failAfter = -1;
            sawSuccess = true;
        }
        catch (const std::bad_alloc &)
        {
            failAfter = -1;
            sawFailure = true;
            require(cache.empty() && aliases.paths.empty() && aliases.reverse.empty(),
                    "Failed nested publication must remove its clip and aliases");
        }
        aliases.clear();
        cache.clear();
        held.clear();
        fs::remove(root / "textures/nested-publication.png");
        bool rejected = false;
        try { runtime.loadTexture(path); }
        catch (const std::exception &) { rejected = true; }
        require(rejected, "Failed or discarded clip publication must release its nested texture");
        if (sawSuccess) break;
    }
    require(sawFailure && sawSuccess, "Nested publication failure sweep was incomplete");
    std::cout << "nested publication failures and release verified\n";
}

void exerciseScenePreparation(platformator::Runtime &runtime, const fs::path &scenePath, size_t requestSize)
{
    Scene scene(scenePath.string());
    auto &manager = platformator_detail::RuntimeAccess::gameManager();
    auto *existing = runtime.createGameObject()->setName("still-live");
    targetSize = requestSize + 1;
    allocations = 0;
    {
        SceneBuildContext context(manager);
        countEnabled = true;
        context.prepare(scene);
        countEnabled = false;
    }
    const size_t total = allocations;
    require(total > 0, "Scene preparation fixture did not allocate");
    size_t failures = 0;
    for (size_t budget = failAllSizes ? 0 : (total > 24 ? total - 24 : 0); budget < total; ++budget)
    {
        {
            SceneBuildContext context(manager);
            injected = false;
            failAfter = static_cast<long>(budget);
            try { context.prepare(scene); }
            catch (const std::exception &)
            {
                failAfter = -1;
                require(injected, "Scene preparation failed without injected allocation failure");
                ++failures;
            }
            failAfter = -1;
        }
        require(runtime.getGameObject("still-live") == existing,
                "Failed or discarded preparation must preserve the existing scene");
        const auto textureFile = scenePath.parent_path() / "textures/missing.png";
        fs::remove(textureFile);
        bool rejected = false;
        try { runtime.loadTexture("assets/textures/missing.png"); }
        catch (const std::exception &) { rejected = true; }
        require(rejected, "Discarded scene preparation must release its nested texture");
        const auto source = fs::path(__FILE__).parent_path().parent_path() / "assets/textures/missing.png";
        fs::copy_file(source, textureFile);
        { SceneBuildContext retry(manager); retry.prepare(scene); }
    }
    require(failures > 0, "Scene preparation did not exercise allocation failures");
    std::cout << "scene allocations=" << total << " injected_failures=" << failures << '\n';
}
}

int main()
{
    failAllSizes = std::getenv("PLATFORMATOR_TEST_ALL_ALLOCATIONS") != nullptr;
    test_support::headlessEnvironment();
    const auto root = fs::absolute("asset-allocation-failure-fixtures");
    try
    {
        fs::create_directories(root / "textures");
        fs::create_directories(root / "audio");
        fs::create_directories(root / "animations");
        const auto source = fs::path(__FILE__).parent_path().parent_path() / "assets";
        fs::copy_file(source / "textures/missing.png", root / "textures/missing.png", fs::copy_options::overwrite_existing);
        fs::copy_file(source / "audio/missing.wav", root / "audio/missing.wav", fs::copy_options::overwrite_existing);
        fs::copy_file(source / "animations/missing.animset", root / "animations/missing.animset", fs::copy_options::overwrite_existing);
        const auto scenePath = root / "fixture.scene";
        test_support::writeScene(scenePath, "staged");
        nlohmann::json document;
        { std::ifstream file(scenePath); file >> document; }
        const std::string prefix = "assets/" + std::string(128, '/');
        const std::string texturePath = prefix + "textures/missing.png";
        const std::string audioPath = prefix + "audio/missing.wav";
        const std::string animationPath = prefix + "animations/missing.animset";
        document[0]["components"] = {{{"id", 1}, {"type", SPRITE}, {"textureFilePath", texturePath},
                                       {"flip", 0}, {"width", 20}, {"height", 20}, {"sourceRectEnabled", false}},
                                      {{"id", 2}, {"type", ANIMATOR}, {"animationClipFilePath", animationPath},
                                       {"currentFrameIndex", 0}, {"playbackSpeed", 1}, {"playing", false}}};
        { std::ofstream file(scenePath); file << document; }
        {
            RuntimeOptions options; options.assetRoot = root.string();
            platformator::Runtime runtime(options);
            exerciseDeepJsonCleanup();
            exerciseLoad<TextureWrapper>("texture", texturePath, [&](const auto &path) { return runtime.loadTexture(path); });
            exerciseLoad<AudioWrapper>("audio", audioPath, [&](const auto &path) { return runtime.loadAudio(path); });
            exerciseLoad<AnimationClip>("animation", animationPath, [&](const auto &path) { return runtime.loadAnimationClip(path); });
            exerciseExisting<TextureWrapper>("texture", texturePath, "textures/missing.png", [&](const auto &path) { return runtime.loadTexture(path); });
            exerciseExisting<AudioWrapper>("audio", audioPath, "audio/missing.wav", [&](const auto &path) { return runtime.loadAudio(path); });
            exerciseExisting<AnimationClip>("animation", animationPath, "animations/missing.animset", [&](const auto &path) { return runtime.loadAnimationClip(path); });
            exerciseNestedPublication(runtime, root, source / "textures/missing.png");
            exerciseScenePreparation(runtime, scenePath, texturePath.size());
            const std::string longName(80, 'x');
            const auto texture = root / "textures" / (longName + ".png");
            const auto audio = root / "audio" / (longName + ".wav");
            const auto animation = root / "animations" / (longName + ".animset");
            fs::copy_file(source / "textures/missing.png", texture, fs::copy_options::overwrite_existing);
            fs::copy_file(source / "audio/missing.wav", audio, fs::copy_options::overwrite_existing);
            fs::copy_file(source / "animations/missing.animset", animation, fs::copy_options::overwrite_existing);
            exerciseReferenceFailures<TextureWrapper>("texture", "assets/textures/" + longName + ".png", texture,
                [&](const auto &path) { return runtime.loadTexture(path); }, runtime);
            exerciseReferenceFailures<AudioWrapper>("audio", "assets/audio/" + longName + ".wav", audio,
                [&](const auto &path) { return runtime.loadAudio(path); }, runtime);
            exerciseReferenceFailures<AnimationClip>("animation", "assets/animations/" + longName + ".animset", animation,
                [&](const auto &path) { return runtime.loadAnimationClip(path); }, runtime);
        }
        fs::remove_all(root);
        std::cout << "[PASS] runtime asset allocation failures and scene preparation\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        failAfter = -1;
        countEnabled = false;
        fs::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
