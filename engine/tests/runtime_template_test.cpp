#include "test_support.h"
#include "runtime_work_probe.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

#include <platformator/animator.h>
#include <platformator/assetreference.h>
#include <platformator/audio.h>
#include <platformator/behaviorquery.h>
#include <platformator/boxcollider.h>
#include <platformator/camera.h>
#include <platformator/circlecollider.h>
#include <platformator/objectreference.h>
#include <platformator/rigidbody.h>
#include <platformator/runtime.h>
#include <platformator/scene.h>
#include <platformator/scriptcomponent.h>
#include <platformator/scriptregistration.h>
#include <platformator/sprite.h>
#include <platformator/template.h>
#include <platformator/texturewrapper.h>

using namespace test_support;
namespace fs = std::filesystem;

namespace
{
std::filesystem::path findWorkspaceRelativePath(const std::filesystem::path &relativePath)
{
    std::filesystem::path search = std::filesystem::current_path();
    while (true)
    {
        auto candidate = search / relativePath;
        if (std::filesystem::exists(candidate))
            return candidate;
        if (!search.has_parent_path() || search.parent_path() == search)
            break;
        search = search.parent_path();
    }
    throw std::runtime_error("Could not locate workspace path: " + relativePath.string());
}

void writeJsonFile(const fs::path &path, const nlohmann::json &j)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out << j.dump(2);
}

struct ScopedTempDir
{
    fs::path dir;
    explicit ScopedTempDir(const std::string &name) : dir(fs::temp_directory_path() / name)
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "assets" / "templates");
        fs::create_directories(dir / "assets" / "scenes");
        fs::copy_file(findWorkspaceRelativePath("assets/wall.png"), dir / "assets" / "wall.png",
                      fs::copy_options::overwrite_existing);
    }
    ~ScopedTempDir()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

struct RuntimeTemplateTestState
{
    int compositeStartsVerified = 0;
    int cycleSlimeSpawns = 0;
    int midUpdateStartsVerified = 0;
    int collisionSpawns = 0;
    int updateSpawns = 0;
    int wrongKindDestroys = 0;
    void reset()
    {
        compositeStartsVerified = 0;
        cycleSlimeSpawns = 0;
        midUpdateStartsVerified = 0;
        collisionSpawns = 0;
        updateSpawns = 0;
        wrongKindDestroys = 0;
    }
} gState;

class SpawnerBehavior : public ::Behavior
{
  public:
    platformator::TemplateAssetRef enemyTemplate;
    platformator::TemplateAssetRef unsetTemplate;
    SERIALIZABLE_SCRIPT(SpawnerBehavior, enemyTemplate, unsetTemplate)
};

class EnemyTagBehavior : public ::Behavior
{
  public:
    int hp = 10;
    platformator::TextureAssetRef badgeTexture;
    SERIALIZABLE_SCRIPT(EnemyTagBehavior, hp, badgeTexture)
};

class LargeSerializedBehavior : public ::Behavior
{
  public:
    int f1 = 1, f2 = 2, f3 = 3, f4 = 4, f5 = 5, f6 = 6, f7 = 7, f8 = 8;
    int f9 = 9, f10 = 10, f11 = 11, f12 = 12, f13 = 13, f14 = 14, f15 = 15, f16 = 16;
    int f17 = 17, f18 = 18, f19 = 19, f20 = 20, f21 = 21, f22 = 22, f23 = 23, f24 = 24;
    SERIALIZABLE_SCRIPT(LargeSerializedBehavior, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16,
                        f17, f18, f19, f20, f21, f22, f23, f24)
};

class CompositeRefsBehavior : public ::Behavior
{
  public:
    ObjectReference<GameObject> childRef;
    ObjectReference<Component> componentRef;
    ObjectReference<Animator> animatorRef;
    ObjectReference<Audio> audioRef;
    ObjectReference<Camera> cameraRef;
    ObjectReference<Collider> colliderRef;
    ObjectReference<Rigidbody> rigidbodyRef;
    ObjectReference<Sprite> spriteRef;
    ObjectReference<ScriptComponent> scriptRef;
    ObjectReference<GameObject> externalRef;
    bool started = false;

    void start() override
    {
        require(childRef && childRef.getReferencedId() == childRef->getId(),
                "Composite childRef must resolve with new scene ID before start()");
        require(componentRef && componentRef.getReferencedId() == componentRef->getId(),
                "Composite componentRef must resolve with new scene ID before start()");
        require(animatorRef && animatorRef->getGameObject() == childRef.get(),
                "animatorRef must bind to cloned child Animator");
        require(audioRef && audioRef.getReferencedId() == audioRef->getId(), "audioRef must bind");
        require(cameraRef && cameraRef.getReferencedId() == cameraRef->getId(), "cameraRef must bind");
        require(colliderRef && colliderRef.getReferencedId() == colliderRef->getId(), "colliderRef must bind");
        require(rigidbodyRef && rigidbodyRef.getReferencedId() == rigidbodyRef->getId(), "rigidbodyRef must bind");
        require(spriteRef && spriteRef.getReferencedId() == spriteRef->getId(), "spriteRef must bind");
        require(scriptRef && scriptRef.get() == getGameObject()->getComponent<ScriptComponent>(),
                "scriptRef must bind to own ScriptComponent");
        require(!externalRef && !externalRef.getReferencedId().has_value(),
                "External reference IDs must be cleared rather than aliasing destination scene IDs");
        started = true;
        ++gState.compositeStartsVerified;
    }

    SERIALIZABLE_SCRIPT(CompositeRefsBehavior, childRef, componentRef, animatorRef, audioRef, cameraRef, colliderRef,
                        rigidbodyRef, spriteRef, scriptRef, externalRef)
};

class WrongKindBehavior : public ::Behavior
{
  public:
    ObjectReference<Rigidbody> brokenBodyRef;
    void onDestroy() override
    {
        ++gState.wrongKindDestroys;
    }
    SERIALIZABLE_SCRIPT(WrongKindBehavior, brokenBodyRef)
};

class CloneResolutionFailure : public ::Behavior
{
  public:
    inline static platformator::ObjectHandle attemptedClone;
    std::string getTypeName() const override { return "CloneResolutionFailure"; }
    Behavior *clone() const override { return new CloneResolutionFailure(*this); }
    void serialize(nlohmann::json &json) const override { json = nlohmann::json::object(); }
    void deserialize(const nlohmann::json &) override {}
    void resolveReferences(platformator::ReferenceResolver &) override
    {
        if (getGameObject()->getLifecycle() == ObjectLifecycle::Live)
        {
            attemptedClone = getGameObject()->getHandle();
            throw std::runtime_error("Injected published clone resolution failure");
        }
    }
};

class SlimeCycleBehavior : public ::Behavior
{
  public:
    int generation = 1;
    platformator::TemplateAssetRef selfTemplate;
    GameObject *spawnSplit(const Eigen::Vector2f &pos)
    {
        return platformator::instantiate(selfTemplate, pos);
    }
    void start() override
    {
        require(bool(selfTemplate), "Cloned cyclic template behavior must hold bound selfTemplate before start()");
    }
    SERIALIZABLE_SCRIPT(SlimeCycleBehavior, generation, selfTemplate)
};

class CycleNodeBehavior : public ::Behavior
{
  public:
    std::string nodeName;
    platformator::TemplateAssetRef otherTemplate;
    void start() override
    {
        require(bool(otherTemplate), "Cloned mutual-cycle template must resolve peer template before start()");
    }
    SERIALIZABLE_SCRIPT(CycleNodeBehavior, nodeName, otherTemplate)
};

class StepAndCollisionSpawner : public ::Behavior
{
  public:
    platformator::TemplateAssetRef unitTemplate;
    int pendingUpdateSpawns = 0;
    int pendingCollisionSpawns = 0;

    void update(double) override
    {
        while (pendingUpdateSpawns > 0)
        {
            --pendingUpdateSpawns;
            platformator::instantiate(unitTemplate, Eigen::Vector2f(10.0f + gState.updateSpawns, 0.0f));
            ++gState.updateSpawns;
        }
    }

    void onCollisionEnter(const Collision *, Collider *, double) override
    {
        while (pendingCollisionSpawns > 0)
        {
            --pendingCollisionSpawns;
            platformator::instantiate(unitTemplate, Eigen::Vector2f(100.0f + gState.collisionSpawns, 0.0f));
            ++gState.collisionSpawns;
        }
    }

    SERIALIZABLE_SCRIPT(StepAndCollisionSpawner, unitTemplate)
};

class SpawnedUnitChildBehavior : public ::Behavior
{
  public:
    ObjectReference<GameObject> childLink;
    void start() override
    {
        require(childLink && childLink.getReferencedId() == childLink->getId() &&
                    childLink->getParent() == getGameObject(),
                "Mid-step spawned unit must remap internal childLink before start()");
        ++gState.midUpdateStartsVerified;
    }
    SERIALIZABLE_SCRIPT(SpawnedUnitChildBehavior, childLink)
};

class ResolveCountingTemplateBehavior : public ::Behavior
{
  public:
    int resolveCount = 0;
    int startCount = 0;
    ObjectReference<GameObject> childLink;

    ::Behavior *clone() const override
    {
        auto *copy = new ResolveCountingTemplateBehavior();
        copy->childLink = childLink;
        return copy;
    }

    std::string getTypeName() const override
    {
        return "ResolveCountingTemplateBehavior";
    }

    void serialize(nlohmann::json &j) const override
    {
        j["type"] = getTypeName();
        j["childLink"] = childLink;
    }

    void deserialize(const nlohmann::json &j) override
    {
        if (j.contains("childLink"))
            childLink = j.at("childLink").get<ObjectReference<GameObject>>();
    }

    void resolveReferences(platformator::ReferenceResolver &resolver) override
    {
        ++resolveCount;
        childLink.resolve(resolver);
    }

    void start() override
    {
        ++startCount;
    }
};

struct ResolveCountingTemplateBehaviorRegistrar
{
    ResolveCountingTemplateBehaviorRegistrar()
    {
        platformator::registerScriptFactory("ResolveCountingTemplateBehavior",
                                            []() -> ::Behavior * { return new ResolveCountingTemplateBehavior(); });
    }
} gResolveCountingTemplateBehaviorRegistrar;

class AppendedResolveBehavior : public ::Behavior
{
  public:
    int resolveCount = 0;
    int startCount = 0;
    ObjectReference<GameObject> targetRef;

    std::string getTypeName() const override
    {
        return "AppendedResolveBehavior";
    }

    void serialize(nlohmann::json &j) const override
    {
        j["type"] = getTypeName();
    }

    void deserialize(const nlohmann::json &) override
    {
    }

    void resolveReferences(platformator::ReferenceResolver &resolver) override
    {
        ++resolveCount;
        targetRef.resolve(resolver);
    }

    void start() override
    {
        ++startCount;
    }
};

nlohmann::json makeAnimsetJson()
{
    return {{"filePath", "assets/anim.animset"},
            {"frames",
             nlohmann::json::array({{{"duration", 0.1},
                                     {"sourceRect", {{"x", 0.0}, {"y", 0.0}, {"w", 4.0}, {"h", 4.0}}},
                                     {"hasSourceRect", false},
                                     {"textureWrapperFilePath", "assets/wall.png"}}})},
            {"loop", true}};
}

nlohmann::json makeObjectJson(int id, const std::string &name, const Eigen::Vector2f &pos, float rot,
                              const nlohmann::json &components, const nlohmann::json &children = nlohmann::json::array())
{
    return {{"id", id},
            {"name", name},
            {"tag", "unit"},
            {"position", {{"x", pos.x()}, {"y", pos.y()}}},
            {"scale", {{"x", 1.0f}, {"y", 1.0f}}},
            {"rotation", rot},
            {"active", true},
            {"components", components},
            {"children", children}};
}

nlohmann::json makeTemplateDoc(const std::string &name, const nlohmann::json &root)
{
    return {{"format", "platformator.object-template"},
            {"version", 1},
            {"name", name},
            {"root", root},
            {"reference_bindings", nlohmann::json::array()}};
}

void requireThrow(const std::function<void()> &fn, const std::string &message)
{
    bool threw = false;
    try
    {
        fn();
    }
    catch (const std::exception &)
    {
        threw = true;
    }
    require(threw, message);
}

void testUs1PrototypeIsolationTransformsAssetsAndErrors()
{
    ScopedTempDir tmp("platformator_tpl_us1");
    writeJsonFile(tmp.dir / "assets" / "anim.animset", makeAnimsetJson());

    const auto childObj = makeObjectJson(
        10, "Weapon", {2.0f, 3.0f}, 0.5f,
        nlohmann::json::array({{{"id", 11},
                                {"type", ComponentType::SPRITE},
                                {"textureFilePath", "assets/wall.png"},
                                {"flip", 0},
                                {"width", 8.0f},
                                {"height", 8.0f},
                                {"sourceRectEnabled", false}}}));

    const auto enemyRoot = makeObjectJson(
        0, "EnemyProto", {5.0f, 7.0f}, 0.25f,
        nlohmann::json::array(
            {{{"id", 1},
              {"type", ComponentType::SPRITE},
              {"textureFilePath", "assets/wall.png"},
              {"flip", 0},
              {"width", 16.0f},
              {"height", 16.0f},
              {"sourceRectEnabled", false}},
             {{"id", 2},
              {"type", ComponentType::ANIMATOR},
              {"currentFrameIndex", 0},
              {"playbackSpeed", 1.0f},
              {"playing", true},
              {"animationClipFilePath", "assets/anim.animset"}},
             {{"id", 3},
              {"type", ComponentType::COLLIDER},
              {"colliderType", static_cast<int>(ColliderType::BoxCollider)},
              {"width", 16.0f},
              {"height", 16.0f},
              {"offset", {{"x", 0.0f}, {"y", 0.0f}}},
              {"trigger", false},
              {"collisionGroup", 1},
              {"collisionMask", 1}},
             {{"id", 4},
              {"type", ComponentType::RIGID_BODY},
              {"velocity", {{"x", 0.0f}, {"y", 0.0f}}},
              {"force", {{"x", 0.0f}, {"y", 0.0f}}},
              {"mass", 2.0f},
              {"angularVelocity", 0.0f},
              {"torque", 0.0f},
              {"friction", 0.5f},
              {"restitution", 0.1f},
              {"bodyType", static_cast<int>(BodyType::KINEMATIC)},
              {"gravity", false}},
             {{"id", 5},
              {"type", ComponentType::SCRIPT},
              {"behaviors",
               nlohmann::json::array({{{"type", "EnemyTagBehavior"},
                                       {"hp", 42},
                                       {"badgeTexture", "assets/wall.png"}}})}}}),
        nlohmann::json::array({childObj}));

    writeJsonFile(tmp.dir / "assets" / "templates" / "enemy.pttemplate", makeTemplateDoc("Enemy", enemyRoot));

    const auto spawnerSceneObj = makeObjectJson(
        0, "Spawner", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array({{{"id", 1},
                                {"type", ComponentType::SCRIPT},
                                {"behaviors",
                                 nlohmann::json::array({{{"type", "SpawnerBehavior"},
                                                         {"enemyTemplate", "assets/templates/enemy.pttemplate"},
                                                         {"unsetTemplate", nullptr}}})}}}));
    writeJsonFile(tmp.dir / "assets" / "scenes" / "us1.scene", nlohmann::json::array({spawnerSceneObj}));

    RuntimeOptions opts;
    opts.assetRoot = (tmp.dir / "assets").string();
    platformator::Runtime runtime(opts);
    runtime.loadScene((tmp.dir / "assets" / "scenes" / "us1.scene").string());

    // Prototype isolation before any spawn: zero prototype objects exist in scene/runtime.
    require(runtime.getGameObject("EnemyProto") == nullptr, "Prototype must not register in active GameManager");
    require(runtime.getGameObject("Weapon") == nullptr, "Prototype children must not register before spawning");
    const size_t initialCount = runtime.getAllGameObjects().size();
    require(initialCount == 2, "Only Spawner and fallback Main Camera must be registered");

    const auto savedBeforePath = tmp.dir / "saved_before.scene";
    runtime.saveScene(savedBeforePath.string());
    {
        std::ifstream in(savedBeforePath);
        nlohmann::json saved = nlohmann::json::parse(in);
        require(saved.size() == initialCount, "Saved scene before spawn must omit inert template prototype");
    }

    auto *spawnerGo = runtime.getGameObject("Spawner");
    auto *spawner = spawnerGo->getComponent<ScriptComponent>()->getBehavior<SpawnerBehavior>();
    require(spawner && bool(spawner->enemyTemplate), "TemplateAssetRef must resolve on scene load");
    require(!bool(spawner->unsetTemplate), "Null TemplateAssetRef must remain unset");

    // Preloaded asset check: wall.png is already retained >= 2 (EnemyProto Sprite + Animator + badgeTexture).
    TextureWrapper *wallTex = runtime.loadTexture("assets/wall.png");
    require(wallTex->getReferenceCount() >= 2, "Template dependencies must be preloaded & retained before spawn");

    // Boundary error: instantiating unset reference must throw clearly.
    requireThrow([&] { platformator::instantiate(spawner->unsetTemplate); },
                 "Instantiating unset TemplateAssetRef must throw");

    // 1. Default instantiation (authored local pos/rot preserved).
    GameObject *instA = platformator::instantiate(spawner->enemyTemplate);
    require(instA != nullptr && bool(instA->getHandle()) && !instA->isRetired(),
            "instA must be live");
    require(instA->getLocalPosition().isApprox(Eigen::Vector2f(5.0f, 7.0f), 1e-4f),
            "Default spawn must preserve prototype local position");
    require(std::abs(instA->getRotation() - 0.25f) < 1e-4f, "Default spawn must preserve prototype rotation");
    require(instA->getChildren().size() == 1 &&
                instA->getChildren()[0]->getPosition().isApprox(Eigen::Vector2f(7.0f, 10.0f), 1e-4f),
            "Spawned child world position must equal root + child local position");
    require(instA->getComponent<ScriptComponent>()->getBehavior<EnemyTagBehavior>()->hp == 42,
            "Cloned behavior fields must preserve prototype values");

    // 2. Parented default instantiation (keeps authored local position relative to parent).
    GameObject *parentGo = runtime.createGameObject()->setName("Parent")->setPosition({10.0f, 20.0f});
    GameObject *instB = spawner->enemyTemplate->instantiate(parentGo);
    require(instB->getParent() == parentGo, "Parented spawn must attach to parent");
    require(instB->getLocalPosition().isApprox(Eigen::Vector2f(5.0f, 7.0f), 1e-4f),
            "Parented default spawn must keep prototype local offset");
    require(instB->getPosition().isApprox(Eigen::Vector2f(15.0f, 27.0f), 1e-4f),
            "Parented default spawn world position must add parent position");
    require(std::abs(instB->getRotation() - 0.25f) < 1e-4f,
            "Parented default spawn must preserve prototype rotation");

    // 3. Explicit world position + rotation even when parented, preserving child local offset.
    GameObject *instC = platformator::instantiate(spawner->enemyTemplate, Eigen::Vector2f(30.0f, 40.0f), 1.25f, parentGo);
    require(instC->getParent() == parentGo, "Parented world spawn must attach to parent");
    require(instC->getPosition().isApprox(Eigen::Vector2f(30.0f, 40.0f), 1e-4f),
            "Explicit world placement must set root world position even when parented");
    require(instC->getLocalPosition().isApprox(Eigen::Vector2f(20.0f, 20.0f), 1e-4f),
            "Parented explicit world placement must compute local position relative to parent");
    require(std::abs(instC->getRotation() - 1.25f) < 1e-4f, "Explicit world rotation must be applied");
    require(instC->getChildren()[0]->getPosition().isApprox(Eigen::Vector2f(32.0f, 43.0f), 1e-4f),
            "Spawned child must preserve parent-relative local position under explicit root world placement");

    // Independent IDs, mutation, and destruction.
    require(instA->getId() != instB->getId() && instB->getId() != instC->getId(),
            "Each spawned instance must have distinct scene IDs");
    instA->setPosition({-50.0f, -60.0f});
    instA->getComponent<ScriptComponent>()->getBehavior<EnemyTagBehavior>()->hp = 999;
    require(instB->getComponent<ScriptComponent>()->getBehavior<EnemyTagBehavior>()->hp == 42,
            "Mutating instA must not affect instB");
    instA->destroy();
    runtime.simulateFrame(dt);
    require(bool(instB->getHandle()) && !instB->isRetired(),
            "Destroying instA must leave sibling instB unaffected");

    // Missing and malformed template files must fail fast cleanly.
    requireThrow([&] { runtime.loadTemplate("assets/templates/does_not_exist.pttemplate"); },
                 "Missing template must throw");
    {
        std::ofstream bad(tmp.dir / "assets" / "templates" / "corrupt.pttemplate");
        bad << "{not valid json";
    }
    requireThrow([&] { runtime.loadTemplate("assets/templates/corrupt.pttemplate"); },
                 "Corrupt template JSON must throw");
    const auto brokenSceneObj = makeObjectJson(
        0, "BrokenSpawner", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array({{{"id", 1},
                                {"type", ComponentType::SCRIPT},
                                {"behaviors",
                                 nlohmann::json::array({{{"type", "SpawnerBehavior"},
                                                         {"enemyTemplate", "assets/templates/does_not_exist.pttemplate"},
                                                         {"unsetTemplate", nullptr}}})}}}));
    writeJsonFile(tmp.dir / "assets" / "scenes" / "broken.scene", nlohmann::json::array({brokenSceneObj}));
    requireThrow([&] { runtime.loadScene((tmp.dir / "assets" / "scenes" / "broken.scene").string()); },
                 "Scene load with missing template path must fail fast");
}

void testUs2AndUs4CompositeAndRollbackAndExternalClearing()
{
    gState.reset();
    ScopedTempDir tmp("platformator_tpl_us2");
    writeJsonFile(tmp.dir / "assets" / "anim.animset", makeAnimsetJson());

    // Prototype internal IDs: root=0, script=1, rigidbody=2, box=3, camera=4, audio=5,
    // child=10, childSprite=11, childAnimator=12; external=999 (absent from {0..12}).
    const auto childJson = makeObjectJson(
        10, "CompChild", {1.0f, 2.0f}, 0.0f,
        nlohmann::json::array(
            {{{"id", 11},
              {"type", ComponentType::SPRITE},
              {"textureFilePath", "assets/wall.png"},
              {"flip", 0},
              {"width", 4.0f},
              {"height", 4.0f},
              {"sourceRectEnabled", false}},
             {{"id", 12},
              {"type", ComponentType::ANIMATOR},
              {"currentFrameIndex", 0},
              {"playbackSpeed", 1.0f},
              {"playing", false},
              {"animationClipFilePath", "assets/anim.animset"}}}));

    const auto rootJson = makeObjectJson(
        0, "CompositeRoot", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array(
            {{{"id", 1},
              {"type", ComponentType::SCRIPT},
              {"behaviors",
               nlohmann::json::array(
                   {{{"type", "CompositeRefsBehavior"},
                     {"childRef", 10},
                     {"componentRef", 11},
                     {"animatorRef", 12},
                     {"audioRef", 5},
                     {"cameraRef", 4},
                     {"colliderRef", 3},
                     {"rigidbodyRef", 2},
                     {"spriteRef", 11},
                     {"scriptRef", 1},
                     {"externalRef", 999}}})}},
             {{"id", 2},
              {"type", ComponentType::RIGID_BODY},
              {"velocity", {{"x", 0.0f}, {"y", 0.0f}}},
              {"force", {{"x", 0.0f}, {"y", 0.0f}}},
              {"mass", 1.0f},
              {"angularVelocity", 0.0f},
              {"torque", 0.0f},
              {"friction", 0.5f},
              {"restitution", 0.0f},
              {"bodyType", static_cast<int>(BodyType::STATIC)},
              {"gravity", false}},
             {{"id", 3},
              {"type", ComponentType::COLLIDER},
              {"colliderType", static_cast<int>(ColliderType::BoxCollider)},
              {"width", 4.0f},
              {"height", 4.0f},
              {"offset", {{"x", 0.0f}, {"y", 0.0f}}},
              {"trigger", false},
              {"collisionGroup", 1},
              {"collisionMask", 1}},
             {{"id", 4}, {"type", ComponentType::CAMERA}, {"width", 640.0f}, {"height", 480.0f}},
             {{"id", 5},
              {"type", ComponentType::AUDIO},
              {"filePath", ""},
              {"gain", 1.0f},
              {"loopCount", 0},
              {"autoPlay", false}}}),
        nlohmann::json::array({childJson}));

    writeJsonFile(tmp.dir / "assets" / "templates" / "composite.pttemplate", makeTemplateDoc("Composite", rootJson));

    // Wrong-kind internal reference template: WrongKindBehavior expects Rigidbody at id 1 (which is a Sprite).
    const auto wrongKindRoot = makeObjectJson(
        0, "WrongKindRoot", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array(
            {{{"id", 1},
              {"type", ComponentType::SPRITE},
              {"textureFilePath", "assets/wall.png"},
              {"flip", 0},
              {"width", 4.0f},
              {"height", 4.0f},
              {"sourceRectEnabled", false}},
             {{"id", 2},
              {"type", ComponentType::SCRIPT},
              {"behaviors",
               nlohmann::json::array({{{"type", "WrongKindBehavior"}, {"brokenBodyRef", 1}}})}}}));
    writeJsonFile(tmp.dir / "assets" / "templates" / "wrongkind.pttemplate",
                  makeTemplateDoc("WrongKind", wrongKindRoot));

    // Destination scene has an existing object with id 999 to prove externalRef clears instead of aliasing id 999!
    writeJsonFile(tmp.dir / "assets" / "scenes" / "dest.scene",
                  nlohmann::json::array({makeObjectJson(999, "DestColliderDummy", {0.0f, 0.0f}, 0.0f,
                                                        nlohmann::json::array())}));

    RuntimeOptions opts;
    opts.assetRoot = (tmp.dir / "assets").string();
    platformator::Runtime runtime(opts);
    runtime.loadScene((tmp.dir / "assets" / "scenes" / "dest.scene").string());
    require(runtime.getObjectById(999) != nullptr, "Destination scene must contain object ID 999");

    Template *compTpl = runtime.loadTemplate("assets/templates/composite.pttemplate");
    GameObject *instA = compTpl->instantiate();
    GameObject *instB = compTpl->instantiate();
    runtime.simulateFrame(dt);

    auto *behA = instA->getComponent<ScriptComponent>()->getBehavior<CompositeRefsBehavior>();
    auto *behB = instB->getComponent<ScriptComponent>()->getBehavior<CompositeRefsBehavior>();
    require(gState.compositeStartsVerified == 2 && behA->started && behB->started,
            "Both spawned composite instances must remap internal refs before start() and run start()");
    require(behA->childRef.get() == instA->getChildren()[0] && behB->childRef.get() == instB->getChildren()[0],
            "Each instance's childRef must point only to its own cloned child");
    require(behA->childRef.get() != behB->childRef.get() && behA->rigidbodyRef.get() != behB->rigidbodyRef.get() &&
                behA->animatorRef.get() != behB->animatorRef.get(),
            "Instance A and Instance B component references must be isolated");
    require(!behA->externalRef && !behA->externalRef.getReferencedId().has_value(),
            "External reference 999 must be cleared even when ID 999 exists in destination scene");

    // Wrong internal target kind must throw & roll back atomically without leaving partial objects.
    const size_t countBeforeFailure = runtime.getAllGameObjects().size();
    Template *wrongTpl = runtime.loadTemplate("assets/templates/wrongkind.pttemplate");
    requireThrow([&] { wrongTpl->instantiate(); }, "Wrong internal target kind must throw");
    require(runtime.getAllGameObjects().size() == countBeforeFailure && gState.wrongKindDestroys == 0,
            "Failed idle instantiation must roll back before registration and without invoking onDestroy");

    bool midStepThrew = false;
    runtime.simulateFrameWithCustomCallback(dt, [&](double) {
        try
        {
            wrongTpl->instantiate();
        }
        catch (const std::exception &)
        {
            midStepThrew = true;
        }
        require(runtime.getAllGameObjects().size() == countBeforeFailure,
                "Failed mid-step instantiation must leave zero partial objects during the step");
    });
    require(midStepThrew && runtime.getAllGameObjects().size() == countBeforeFailure && gState.wrongKindDestroys == 0,
            "Failed mid-step instantiation must throw and avoid invoking onDestroy on partial clones");

    const auto originalHandle = instA->getHandle();
    size_t growths = 0;
    runtime.simulateFrameWithCustomCallback(dt, [&](double) {
        for (int index = 0; index < 128; ++index)
        {
            const auto capacity = runtime.getAllGameObjects().capacity();
            compTpl->instantiate();
            growths += runtime.getAllGameObjects().capacity() != capacity;
        }
    });
    require(growths <= 8 && runtime.resolveHandle(originalHandle, platformator_detail::ObjectKind::GameObject) == instA,
            "Repeated template registration must grow geometrically and preserve existing handles");

    platformator::registerScriptFactory("CloneResolutionFailure", []() -> ::Behavior * {
        return new CloneResolutionFailure();
    });
    auto failingRoot = rootJson;
    for (auto &component : failingRoot["components"])
        if (component["type"] == ComponentType::SCRIPT)
            component["behaviors"].push_back({{"type", "CloneResolutionFailure"}});
    writeJsonFile(tmp.dir / "assets/templates/failing-registration.pttemplate",
                  makeTemplateDoc("FailingRegistration", failingRoot));
    auto *failingTemplate = runtime.loadTemplate("assets/templates/failing-registration.pttemplate");
    const auto countBeforeRollback = runtime.getAllGameObjects().size();
    requireThrow([&] { failingTemplate->instantiate(); }, "Published clone resolution must exercise rollback");
    require(CloneResolutionFailure::attemptedClone &&
                !runtime.resolveHandle(CloneResolutionFailure::attemptedClone, platformator_detail::ObjectKind::GameObject) &&
                runtime.getAllGameObjects().size() == countBeforeRollback,
            "Failed published clone resolution must remove the entire newly registered subtree");
}

void testUs4CyclesMidStepCollisionAndCleanup()
{
    gState.reset();
    ScopedTempDir tmp("platformator_tpl_us4");

    // Self-referential Slime -> Slime
    const auto slimeRoot = makeObjectJson(
        0, "Slime", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array({{{"id", 1},
                                {"type", ComponentType::SCRIPT},
                                {"behaviors",
                                 nlohmann::json::array({{{"type", "SlimeCycleBehavior"},
                                                         {"generation", 1},
                                                         {"selfTemplate", "assets/templates/slime.pttemplate"}}})}}}));
    writeJsonFile(tmp.dir / "assets" / "templates" / "slime.pttemplate", makeTemplateDoc("Slime", slimeRoot));

    // Mutual cycle A -> B -> A
    const auto cycleARoot = makeObjectJson(
        0, "CycleA", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array({{{"id", 1},
                                {"type", ComponentType::SCRIPT},
                                {"behaviors",
                                 nlohmann::json::array({{{"type", "CycleNodeBehavior"},
                                                         {"nodeName", "A"},
                                                         {"otherTemplate", "assets/templates/cycle_b.pttemplate"}}})}}}));
    const auto cycleBRoot = makeObjectJson(
        0, "CycleB", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array({{{"id", 1},
                                {"type", ComponentType::SCRIPT},
                                {"behaviors",
                                 nlohmann::json::array({{{"type", "CycleNodeBehavior"},
                                                         {"nodeName", "B"},
                                                         {"otherTemplate", "assets/templates/cycle_a.pttemplate"}}})}}}));
    writeJsonFile(tmp.dir / "assets" / "templates" / "cycle_a.pttemplate", makeTemplateDoc("CycleA", cycleARoot));
    writeJsonFile(tmp.dir / "assets" / "templates" / "cycle_b.pttemplate", makeTemplateDoc("CycleB", cycleBRoot));

    // Unit template with child + ObjectReference for 100 mid-update & collision spawns.
    const auto unitChild = makeObjectJson(10, "UnitChild", {1.0f, 0.0f}, 0.0f, nlohmann::json::array());
    const auto unitRoot = makeObjectJson(
        0, "SpawnedUnit", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array({{{"id", 1},
                                {"type", ComponentType::SCRIPT},
                                {"behaviors",
                                 nlohmann::json::array({{{"type", "SpawnedUnitChildBehavior"},
                                                         {"childLink", 10}}})}}}),
        nlohmann::json::array({unitChild}));
    writeJsonFile(tmp.dir / "assets" / "templates" / "unit.pttemplate", makeTemplateDoc("Unit", unitRoot));

    RuntimeOptions opts;
    opts.assetRoot = (tmp.dir / "assets").string();

    {
        platformator::Runtime runtime(opts);
        runtime.createMainCameraIfNoMainCameraExists();

        // Direct self-cycle load + live recursive spawn.
        Template *slimeTpl = runtime.loadTemplate("assets/templates/slime.pttemplate");
        GameObject *slime1 = slimeTpl->instantiate();
        auto *slime1Beh = slime1->getComponent<ScriptComponent>()->getBehavior<SlimeCycleBehavior>();
        require(bool(slime1Beh->selfTemplate), "Spawned cyclic Slime must bind selfTemplate");
        GameObject *slime2 = slime1Beh->spawnSplit({4.0f, 0.0f});
        require(slime2 && slime2 != slime1 &&
                    bool(slime2->getComponent<ScriptComponent>()->getBehavior<SlimeCycleBehavior>()->selfTemplate),
                "Second-generation split from cyclic Slime must also spawn and bind");

        // Mutual cycle load + alternating spawn.
        Template *cycleATpl = runtime.loadTemplate("assets/templates/cycle_a.pttemplate");
        GameObject *instA = cycleATpl->instantiate();
        auto *behA = instA->getComponent<ScriptComponent>()->getBehavior<CycleNodeBehavior>();
        require(behA->nodeName == "A" && bool(behA->otherTemplate), "CycleA must bind CycleB");
        GameObject *instB = platformator::instantiate(behA->otherTemplate);
        auto *behB = instB->getComponent<ScriptComponent>()->getBehavior<CycleNodeBehavior>();
        require(behB->nodeName == "B" && bool(behB->otherTemplate), "CycleB must bind CycleA");

        // Mid-update (50) + collision-callback (50) spawns = 100 instances with internal remapping before start().
        Template *unitTpl = runtime.loadTemplate("assets/templates/unit.pttemplate");
        auto *spawnerGo = runtime.createGameObject()->setPosition({0.0f, 0.0f});
        spawnerGo->addComponent<BoxCollider>(10.0f, 10.0f);
        spawnerGo->addComponent<Rigidbody>(BodyType::DYNAMIC, false);
        spawnerGo->addComponent<ScriptComponent>();
        auto *stepSpawner = new StepAndCollisionSpawner();
        stepSpawner->unitTemplate.set(unitTpl);
        stepSpawner->pendingUpdateSpawns = 50;
        stepSpawner->pendingCollisionSpawns = 50;
        spawnerGo->getComponent<ScriptComponent>()->addBehavior(stepSpawner);

        auto *blocker = runtime.createGameObject()->setPosition({2.0f, 0.0f});
        blocker->addComponent<BoxCollider>(10.0f, 10.0f);
        blocker->addComponent<Rigidbody>(BodyType::STATIC, false);

        runtime.simulateFrame(dt);
        require(gState.updateSpawns == 50 && gState.collisionSpawns == 50,
                "All 50 mid-update and 50 collision-callback spawns must occur in frame 1");
        runtime.simulateFrame(dt);
        require(gState.midUpdateStartsVerified == 100,
                "All 100 mid-update and collision-callback spawns must run start() with remapped refs on next frame");
    }

    // Repeated scene load/unload cleanup check (SC-005).
    const auto cycleScene = nlohmann::json::array({slimeRoot});
    writeJsonFile(tmp.dir / "assets" / "scenes" / "cyclic.scene", cycleScene);
    writeJsonFile(tmp.dir / "assets" / "scenes" / "empty.scene", nlohmann::json::array());
    {
        platformator::Runtime runtime(opts);
        for (int i = 0; i < 5; ++i)
        {
            runtime.loadScene((tmp.dir / "assets" / "scenes" / "cyclic.scene").string());
            require(runtime.getGameObject("Slime") != nullptr, "Cyclic scene must load on iteration");
            runtime.loadScene((tmp.dir / "assets" / "scenes" / "empty.scene").string());
            require(runtime.getGameObject("Slime") == nullptr && runtime.getAllGameObjects().size() == 1,
                    "Unloading scene must clear all spawned/loaded objects cleanly");
        }
    }
}

void testEngineCleanupContracts()
{
    ScopedTempDir tmp("platformator_cleanup_contracts");
    const auto unitJson = makeObjectJson(
        1, "Unit", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array({{{"id", 2},
                                {"type", ComponentType::SCRIPT},
                                {"behaviors",
                                 nlohmann::json::array({{{"type", "LargeSerializedBehavior"},
                                                         {"f1", 101},
                                                         {"f17", 217},
                                                         {"f24", 324}}})}}}));
    writeJsonFile(tmp.dir / "assets" / "templates" / "unit.pttemplate", makeTemplateDoc("Unit", unitJson));

    const auto spawnerScene = nlohmann::json::array({makeObjectJson(
        10, "Spawner", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array({{{"id", 11},
                                {"type", ComponentType::SCRIPT},
                                {"behaviors",
                                 nlohmann::json::array({{{"type", "SpawnerBehavior"},
                                                         {"enemyTemplate", "templates/unit.pttemplate"},
                                                         {"unsetTemplate", ""}}})}}}))});
    writeJsonFile(tmp.dir / "assets" / "scenes" / "cleanup.scene", spawnerScene);

    RuntimeOptions opts;
    opts.windowSettings.headless = true;
    opts.assetRoot = (tmp.dir / "assets").string();
    opts.sceneFilePath = (tmp.dir / "assets" / "scenes" / "cleanup.scene").string();

    platformator::Runtime runtime(opts);
    runtime.simulateFrame(dt);

    // Optional "" AssetReference slots remain unset cleanly while "templates/..." resolves identically to "assets/templates/...".
    auto *spawnerGo = runtime.getGameObject("Spawner");
    require(spawnerGo != nullptr, "Startup scene must load via Runtime(opts)");
    auto *spawner = platformator::getBehavior<SpawnerBehavior>(spawnerGo);
    require(spawner != nullptr && ::getBehavior<SpawnerBehavior>(spawnerGo) == spawner,
            "getBehavior free helpers must resolve script behaviors");
    require(!spawner->unsetTemplate.hasFilePath() && spawner->unsetTemplate.resolve(runtime) == nullptr,
            "Empty string optional AssetReference must remain unset cleanly");
    require(bool(spawner->enemyTemplate) &&
                spawner->enemyTemplate.get() == runtime.loadTemplate("assets/templates/unit.pttemplate") &&
                runtime.loadTemplate("templates/unit.pttemplate") == spawner->enemyTemplate.get(),
            "templates/... and assets/templates/... must resolve to the same canonical Template");

    // Empty ctor/setFilePath clearing
    AssetReference<TextureWrapper> texRef("");
    require(!texRef.hasFilePath(), "Constructing AssetReference with empty string must stay unset");
    texRef.setFilePath("assets/wall.png");
    require(texRef.hasFilePath(), "setFilePath must store non-empty path");
    texRef.setFilePath("");
    require(!texRef.hasFilePath(), "setFilePath(\"\") must clear AssetReference");

    // 24-field SERIALIZABLE_SCRIPT round-trip & inspection
    GameObject *spawned = platformator::instantiate(spawner->enemyTemplate);
    require(spawned != nullptr, "Instantiating 24-field template must succeed");
    auto *large = platformator::getBehavior<LargeSerializedBehavior>(spawned);
    require(large != nullptr && large->f1 == 101 && large->f17 == 217 && large->f24 == 324,
            "SERIALIZABLE_SCRIPT with 24 fields must deserialize values beyond field 16");
    const auto inspected = platformator::inspectRegisteredScriptFields("LargeSerializedBehavior", *large);
    require(inspected.providerAvailable && inspected.fields.size() == 24,
            "Script inspection must expose all 24 serialized fields");
    nlohmann::json roundtrip;
    large->serialize(roundtrip);
    require(roundtrip.at("f17") == 217 && roundtrip.at("f24") == 324,
            "SERIALIZABLE_SCRIPT with 24 fields must serialize values beyond field 16");
}
void testSpriteZOrderTemplateInstantiationAndClone()
{
    ScopedTempDir tmp("platformator_tpl_zorder");
    const auto rootJson = makeObjectJson(
        0, "ZOrderSprite", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array({{{"id", 1},
                                {"type", ComponentType::SPRITE},
                                {"textureFilePath", ""},
                                {"flip", 0},
                                {"width", 12.0f},
                                {"height", 12.0f},
                                {"zOrder", -77},
                                {"sourceRectEnabled", false}}}));
    writeJsonFile(tmp.dir / "assets" / "templates" / "zorder.pttemplate", makeTemplateDoc("ZOrderSprite", rootJson));

    {
        RuntimeOptions opts;
        opts.windowSettings.headless = true;
        opts.assetRoot = (tmp.dir / "assets").string();
        platformator::Runtime runtime(opts);

        Template *tpl = runtime.loadTemplate("assets/templates/zorder.pttemplate");
        GameObject *inst = tpl->instantiate();
        Sprite *sprite = inst->getComponent<Sprite>();
        require(sprite != nullptr && sprite->getZOrder() == -77,
                "Instantiated template Sprite must retain authored zOrder -77");

        std::unique_ptr<Component> cloned(sprite->clone());
        auto *clonedSprite = dynamic_cast<Sprite *>(cloned.get());
        require(clonedSprite != nullptr && clonedSprite->getZOrder() == -77,
                "Sprite::clone must copy authored zOrder");
    }

    // Repeated template instantiation with serialized children sharing equal Z and failed scene load preservation.
    const auto childA = makeObjectJson(
        10, "ChildA", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array({{{"id", 11}, {"type", ComponentType::SPRITE},
                                {"textureFilePath", "assets/wall.png"}, {"flip", 0},
                                {"width", 16.0f}, {"height", 16.0f}, {"zOrder", 0},
                                {"sourceRectEnabled", false}}}));
    const auto childB = makeObjectJson(
        20, "ChildB", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array({{{"id", 21}, {"type", ComponentType::SPRITE},
                                {"textureFilePath", "assets/wall.png"}, {"flip", 0},
                                {"width", 16.0f}, {"height", 16.0f}, {"zOrder", 0},
                                {"sourceRectEnabled", false}}}));
    const auto childHigh = makeObjectJson(
        30, "ChildHigh", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array({{{"id", 31}, {"type", ComponentType::SPRITE},
                                {"textureFilePath", "assets/wall.png"}, {"flip", 0},
                                {"width", 16.0f}, {"height", 16.0f}, {"zOrder", 5},
                                {"sourceRectEnabled", false}}}));
    const auto hierarchyRoot = makeObjectJson(
        0, "HierarchyRoot", {160.0f, 120.0f}, 0.0f,
        nlohmann::json::array({{{"id", 1}, {"type", ComponentType::SPRITE},
                                {"textureFilePath", "assets/wall.png"}, {"flip", 0},
                                {"width", 16.0f}, {"height", 16.0f}, {"zOrder", 0},
                                {"sourceRectEnabled", false}}}),
        nlohmann::json::array({childA, childB, childHigh}));
    writeJsonFile(tmp.dir / "assets" / "templates" / "hierarchy_ties.pttemplate",
                  makeTemplateDoc("HierarchyTies", hierarchyRoot));

    RuntimeOptions renderOpts;
    renderOpts.assetRoot = (tmp.dir / "assets").string();
    renderOpts.windowSettings.width = 320;
    renderOpts.windowSettings.height = 240;
    platformator::Runtime renderRuntime(renderOpts);
    GameObject *camObj = renderRuntime.createGameObject();
    camObj->addComponent<Camera>(320.0f, 240.0f);
    Template *hierarchyTpl = renderRuntime.loadTemplate("assets/templates/hierarchy_ties.pttemplate");

    GameObject *inst1 = hierarchyTpl->instantiate();
    GameObject *inst2 = hierarchyTpl->instantiate();
    require(inst1 != inst2 &&
                inst1->getChildren()[0]->getComponent<Sprite>() != inst2->getChildren()[0]->getComponent<Sprite>() &&
                inst1->getChildren()[2]->getComponent<Sprite>()->getZOrder() == 5 &&
                inst2->getChildren()[2]->getComponent<Sprite>()->getZOrder() == 5,
            "Repeated template instantiation must produce distinct live sprites with authored Z values");

#if PLATFORMATOR_ENABLE_WORK_PROBES
    std::vector<const Sprite *> expectedOrder{
        inst1->getComponent<Sprite>(),
        inst1->getChildren()[0]->getComponent<Sprite>(),
        inst1->getChildren()[1]->getComponent<Sprite>(),
        inst2->getComponent<Sprite>(),
        inst2->getChildren()[0]->getComponent<Sprite>(),
        inst2->getChildren()[1]->getComponent<Sprite>(),
        inst1->getChildren()[2]->getComponent<Sprite>(),
        inst2->getChildren()[2]->getComponent<Sprite>()};
    std::unordered_map<const Sprite *, platformator_test::SpriteIdentity> idMap;
    for (size_t i = 0; i < expectedOrder.size(); ++i)
        idMap.emplace(expectedOrder[i], platformator_test::SpriteIdentity{i, 0});

    const auto captureOrder = [&]() {
        std::vector<platformator_test::SpriteIdentity> submissions;
        submissions.reserve(expectedOrder.size());
        platformator_test::RenderObservation obs;
        obs.identityContext = &idMap;
        obs.resolveIdentity = [](const Sprite *s, platformator_test::SpriteIdentity &out, void *ctx) -> bool {
            const auto *map = static_cast<const std::unordered_map<const Sprite *, platformator_test::SpriteIdentity> *>(ctx);
            const auto it = map->find(s);
            if (it == map->end())
                return false;
            out = it->second;
            return true;
        };
        obs.submissions = &submissions;
        platformator_test::ScopedRenderObservation scope(obs);
        require(renderRuntime.simulateAndRenderFrame(dt), "Template render frame failed");
        require(obs.unresolvedSprites == 0 && obs.submissionOverflows == 0,
                "Template render observation had unresolved or overflowed sprites");
        return submissions;
    };

    const std::vector<platformator_test::SpriteIdentity> expectedIds{
        {0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}};
    require(captureOrder() == expectedIds,
            "Repeated template instantiation must draw serialized equal-Z ties deterministically");

    // Failed scene preparation must keep live template instances and their tie order intact.
    writeJsonFile(tmp.dir / "assets" / "scenes" / "invalid_z.scene",
                  nlohmann::json::array({makeObjectJson(
                      99, "BadZ", {0.0f, 0.0f}, 0.0f,
                      nlohmann::json::array({{{"id", 100}, {"type", ComponentType::SPRITE},
                                              {"textureFilePath", ""}, {"flip", 0},
                                              {"width", 10.0f}, {"height", 10.0f},
                                              {"zOrder", true}, {"sourceRectEnabled", false}}}))}));
    requireThrow([&] { renderRuntime.loadScene((tmp.dir / "assets" / "scenes" / "invalid_z.scene").string()); },
                 "Invalid scene preparation must throw");
    require(captureOrder() == expectedIds,
            "Failed scene preparation must preserve live template instances and their tie order");
#endif
}

void testSpriteColorTemplateInstantiationAndClone()
{
    ScopedTempDir tmp("platformator_sprite_color_tpl");
    const auto tintedRoot = makeObjectJson(
        1, "TintedRoot", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array({{{"id", 2},
                                {"type", ComponentType::SPRITE},
                                {"textureFilePath", "assets/wall.png"},
                                {"flip", 0},
                                {"width", 16.0f},
                                {"height", 16.0f},
                                {"color", {{"r", 120}, {"g", 200}, {"b", 45}, {"a", 160}}},
                                {"sourceRectEnabled", false}}}));
    const auto legacyRoot = makeObjectJson(
        3, "LegacyRoot", {24.0f, 0.0f}, 0.0f,
        nlohmann::json::array({{{"id", 4},
                                {"type", ComponentType::SPRITE},
                                {"textureFilePath", "assets/wall.png"},
                                {"flip", 0},
                                {"width", 16.0f},
                                {"height", 16.0f},
                                {"sourceRectEnabled", false}}}));
    writeJsonFile(tmp.dir / "assets" / "templates" / "tinted.pttemplate",
                  makeTemplateDoc("TintedTemplate", tintedRoot));
    writeJsonFile(tmp.dir / "assets" / "templates" / "legacy.pttemplate",
                  makeTemplateDoc("LegacyTemplate", legacyRoot));

    RuntimeOptions options;
    options.windowSettings.headless = false;
    options.assetRoot = (tmp.dir / "assets").string();
    platformator::Runtime runtime(options);

    const auto tintedTpl = runtime.loadTemplate("assets/templates/tinted.pttemplate");
    const auto legacyTpl = runtime.loadTemplate("assets/templates/legacy.pttemplate");
    GameObject *tintedObj = tintedTpl->instantiate();
    GameObject *legacyObj = legacyTpl->instantiate();
    require(tintedObj != nullptr && legacyObj != nullptr, "Templates must instantiate cleanly");

    Sprite *tintedSprite = tintedObj->getComponent<Sprite>();
    Sprite *legacySprite = legacyObj->getComponent<Sprite>();
    require(tintedSprite != nullptr && legacySprite != nullptr, "Template instances must carry Sprite components");

    const SDL_Color tintedColor = tintedSprite->getColor();
    require(tintedColor.r == 120 && tintedColor.g == 200 && tintedColor.b == 45 && tintedColor.a == 160,
            "Template instantiation must preserve serialized Sprite color");
    const SDL_Color legacyColor = legacySprite->getColor();
    require(legacyColor.r == 255 && legacyColor.g == 255 && legacyColor.b == 255 && legacyColor.a == 255,
            "Legacy template without Sprite color must default to opaque white");

    std::unique_ptr<Component> clonedComp(tintedSprite->clone());
    auto *clonedSprite = dynamic_cast<Sprite *>(clonedComp.get());
    require(clonedSprite != nullptr && clonedSprite->getColor().r == 120 && clonedSprite->getColor().g == 200 &&
                clonedSprite->getColor().b == 45 && clonedSprite->getColor().a == 160,
            "Sprite::clone must preserve RGBA tint");

    // Failed scene preparation with invalid Sprite "color" must leave live template instances intact.
    writeJsonFile(tmp.dir / "assets" / "scenes" / "invalid_color.scene",
                  nlohmann::json::array({makeObjectJson(
                      99, "BadColor", {0.0f, 0.0f}, 0.0f,
                      nlohmann::json::array({{{"id", 100},
                                              {"type", ComponentType::SPRITE},
                                              {"textureFilePath", ""},
                                              {"flip", 0},
                                              {"width", 10.0f},
                                              {"height", 10.0f},
                                              {"color", {{"r", 256}, {"g", 0}, {"b", 0}, {"a", 255}}},
                                              {"sourceRectEnabled", false}}}))}));
    requireThrow([&] { runtime.loadScene((tmp.dir / "assets" / "scenes" / "invalid_color.scene").string()); },
                 "Invalid scene Sprite color must throw");
    require(tintedSprite->getColor().r == 120 && tintedSprite->getColor().a == 160,
            "Failed scene preparation must preserve live template Sprite color");
}

void testAppendingBehaviorToTemplateInstanceDoesNotReresolveTemplateBehaviors()
{
    ScopedTempDir tmp("platformator_b04_append_behavior");
    const auto childObj = makeObjectJson(10, "ChildPart", {1.0f, 0.0f}, 0.0f, nlohmann::json::array());
    const auto rootObj = makeObjectJson(
        1, "UnitRoot", {0.0f, 0.0f}, 0.0f,
        nlohmann::json::array({{{"id", 2},
                                {"type", ComponentType::SCRIPT},
                                {"behaviors",
                                 nlohmann::json::array({{{"type", "ResolveCountingTemplateBehavior"},
                                                         {"childLink", 10}}})}}}),
        nlohmann::json::array({childObj}));
    writeJsonFile(tmp.dir / "assets" / "templates" / "resolve_unit.pttemplate",
                  makeTemplateDoc("ResolveUnit", rootObj));

    RuntimeOptions options;
    options.assetRoot = (tmp.dir / "assets").string();
    platformator::Runtime runtime(options);

    GameObject *sceneTarget = runtime.createGameObject()->setName("SceneTarget");
    auto *tpl = runtime.loadTemplate("assets/templates/resolve_unit.pttemplate");
    GameObject *instance = tpl->instantiate();
    require(instance != nullptr && instance->getChildren().size() == 1,
            "Template instance must spawn with child");

    auto *script = instance->getComponent<ScriptComponent>();
    auto *tplBehavior = script->getBehavior<ResolveCountingTemplateBehavior>();
    require(tplBehavior != nullptr, "Template behavior must exist on instantiated script");
    require(tplBehavior->resolveCount == 1,
            "Template instantiation must resolve template behavior references exactly once");
    require(tplBehavior->childLink.get() == instance->getChildren()[0],
            "Template behavior childLink must point to instantiated child");

    auto *appended = new AppendedResolveBehavior();
    appended->targetRef.setReferencedId(sceneTarget->getId());
    script->addBehavior(appended);

    runtime.simulateFrame(dt);

    require(tplBehavior->resolveCount == 1,
            "Appending a new behavior to an instantiated template script must not re-resolve already-resolved template behaviors");
    require(tplBehavior->childLink.get() == instance->getChildren()[0],
            "Template behavior reference must remain bound to its instantiated child after start");
    require(tplBehavior->startCount == 1, "Template behavior must start once");
    require(appended->resolveCount == 1, "Appended behavior must resolve its references once before start");
    require(appended->targetRef.get() == sceneTarget,
            "Appended behavior must resolve its active-scene target reference");
    require(appended->startCount == 1, "Appended behavior must start once");
}
} // namespace

int main()
{
    headlessEnvironment();
    try
    {
        testUs1PrototypeIsolationTransformsAssetsAndErrors();
        testUs2AndUs4CompositeAndRollbackAndExternalClearing();
        testUs4CyclesMidStepCollisionAndCleanup();
        testEngineCleanupContracts();
        testSpriteZOrderTemplateInstantiationAndClone();
        testSpriteColorTemplateInstantiationAndClone();
        testAppendingBehaviorToTemplateInstanceDoesNotReresolveTemplateBehaviors();
        std::cout << "Runtime template tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Runtime template tests failed: " << error.what() << '\n';
        return 1;
    }
}
