#include "preview.h"

#include "editorwork.h"
#include "imagepreflight.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <utility>
#include <deque>
#include <fstream>
#include <mutex>
#include <oneapi/tbb/task_group.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <platformator/animationclip.h>
#include <platformator/camera.h>
#include <platformator/gameobject.h>
#include <platformator/runtime.h>
#include <platformator/sprite.h>
#include <platformator/texturewrapper.h>

#include "gamemanager.h"
#include "runtimeaccess.h"
#include "spriterendergeometry.h"

namespace platformator_editor
{
namespace
{
int integer_value(const Json &record, const char *key, int fallback);

const Json *resolve_object(const Json &root, const ObjectLocation &location)
{
    if (!root.is_array() || location.childIndices.empty())
        return nullptr;
    const Json *object = nullptr;
    const Json *siblings = &root;
    for (std::size_t depth = 0; depth < location.childIndices.size(); ++depth)
    {
        if (depth > 0)
        {
            if (object == nullptr)
                return nullptr;
            const auto children = object->find("children");
            if (children == object->end())
                return nullptr;
            siblings = &*children;
        }
        const auto index = location.childIndices[depth];
        if (!siblings->is_array() || index >= siblings->size())
            return nullptr;
        object = &(*siblings)[index];
    }
    return object;
}

const Json *component_of_type(const Json &object, int type)
{
    const auto components = object.find("components");
    if (components == object.end() || !components->is_array())
        return nullptr;
    for (const auto &component : *components)
        if (component.is_object() && component.value("type", -1) == type)
            return &component;
    return nullptr;
}

float finite_number(const Json &record, const char *key, float fallback)
{
    const auto found = record.find(key);
    if (found == record.end() || !found->is_number())
        return fallback;
    try
    {
        const float value = found->get<float>();
        return std::isfinite(value) ? value : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

float nested_number(const Json &record, const char *outer, const char *inner, float fallback)
{
    const auto found = record.find(outer);
    return found != record.end() && found->is_object()
               ? finite_number(*found, inner, fallback)
               : fallback;
}

int integer_value(const Json &record, const char *key, int fallback)
{
    const auto found = record.find(key);
    if (found == record.end() || !found->is_number_integer())
        return fallback;
    try
    {
        const auto value = found->get<std::int64_t>();
        return value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max()
                   ? static_cast<int>(value)
                   : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

bool positive_number(const Json &record, const char *key, float &result)
{
    const auto found = record.find(key);
    if (found == record.end() || !found->is_number())
        return false;
    try
    {
        const float value = found->get<float>();
        if (!std::isfinite(value) || value <= 0.0f)
            return false;
        result = value;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

float placeholder_dimension(float scale)
{
    const double dimension = 12.0 * std::abs(static_cast<double>(scale));
    return std::isfinite(dimension) && dimension >= 12.0 && dimension <= std::numeric_limits<float>::max()
               ? static_cast<float>(dimension)
               : 12.0f;
}

bool boolean_value(const Json &record, const char *key, bool fallback)
{
    const auto found = record.find(key);
    return found != record.end() && found->is_boolean() ? found->get<bool>() : fallback;
}

std::string string_value(const Json &record, const char *key)
{
    const auto found = record.find(key);
    return found != record.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

Eigen::Vector2f accumulated_translation(const DocumentSession &document, int objectId)
{
    const auto location = document.indexes.objectsById.find(objectId);
    if (location == document.indexes.objectsById.end())
        return Eigen::Vector2f::Zero();
    const Json *object = resolve_object(document.root, location->second);
    if (object == nullptr)
        return Eigen::Vector2f::Zero();

    std::vector<const Json *> chain;
    const Json *current = object;
    chain.push_back(current);
    while (current != nullptr)
    {
        const int id = current->value("id", -1);
        const auto parent = document.indexes.parentById.find(id);
        if (parent == document.indexes.parentById.end() || !parent->second)
            break;
        const auto parentLocation = document.indexes.objectsById.find(*parent->second);
        current = parentLocation == document.indexes.objectsById.end()
                      ? nullptr
                      : resolve_object(document.root, parentLocation->second);
        if (current != nullptr)
            chain.push_back(current);
    }
    Eigen::Vector2f result = Eigen::Vector2f::Zero();
    for (const Json *part : chain)
    {
        result.x() += nested_number(*part, "position", "x", 0.0f);
        result.y() += nested_number(*part, "position", "y", 0.0f);
    }
    return result;
}

class RendererState
{
  public:
    explicit RendererState(SDL_Renderer *renderer) : renderer_(renderer)
    {
        if (renderer_ == nullptr)
            return;
        target_ = SDL_GetRenderTarget(renderer_);
        viewportSet_ = SDL_RenderViewportSet(renderer_);
        valid_ = SDL_GetRenderViewport(renderer_, &viewport_);
        valid_ = SDL_GetRenderLogicalPresentation(renderer_, &logicalWidth_, &logicalHeight_, &logicalMode_) && valid_;
        valid_ = SDL_GetRenderScale(renderer_, &scaleX_, &scaleY_) && valid_;
        valid_ = SDL_GetRenderClipRect(renderer_, &clip_) && valid_;
        valid_ = SDL_GetRenderDrawColorFloat(renderer_, &drawR_, &drawG_, &drawB_, &drawA_) && valid_;
        valid_ = SDL_GetRenderDrawBlendMode(renderer_, &drawBlend_) && valid_;
        valid_ = SDL_GetRenderColorScale(renderer_, &colorScale_) && valid_;
        clipEnabled_ = SDL_RenderClipEnabled(renderer_);
    }

    RendererState(const RendererState &) = delete;
    RendererState &operator=(const RendererState &) = delete;

    ~RendererState()
    {
        restore();
    }

    bool valid() const noexcept { return valid_; }

    bool restore() noexcept
    {
        if (restored_ || renderer_ == nullptr)
            return valid_;
        restored_ = true;
        bool ok = SDL_SetRenderTarget(renderer_, target_);
        ok = SDL_SetRenderLogicalPresentation(renderer_, logicalWidth_, logicalHeight_, logicalMode_) && ok;
        ok = SDL_SetRenderScale(renderer_, scaleX_, scaleY_) && ok;
        ok = SDL_SetRenderViewport(renderer_, viewportSet_ ? &viewport_ : nullptr) && ok;
        ok = SDL_SetRenderClipRect(renderer_, clipEnabled_ ? &clip_ : nullptr) && ok;
        ok = SDL_SetRenderColorScale(renderer_, colorScale_) && ok;
        ok = SDL_SetRenderDrawColorFloat(renderer_, drawR_, drawG_, drawB_, drawA_) && ok;
        ok = SDL_SetRenderDrawBlendMode(renderer_, drawBlend_) && ok;
        valid_ = valid_ && ok;
        return valid_;
    }

  private:
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *target_ = nullptr;
    SDL_Rect viewport_{};
    SDL_Rect clip_{};
    int logicalWidth_ = 0;
    int logicalHeight_ = 0;
    SDL_RendererLogicalPresentation logicalMode_ = SDL_LOGICAL_PRESENTATION_DISABLED;
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;
    float drawR_ = 0.0f;
    float drawG_ = 0.0f;
    float drawB_ = 0.0f;
    float drawA_ = 1.0f;
    float colorScale_ = 1.0f;
    SDL_BlendMode drawBlend_ = SDL_BLENDMODE_NONE;
    bool viewportSet_ = false;
    bool clipEnabled_ = false;
    bool valid_ = false;
    bool restored_ = false;
};
} // namespace

struct EditorPreview::AssetReloadState
{
    enum class Kind { Texture, Animation };
    struct Target
    {
        std::string logicalPath;
        ResourceRequestToken token;
        std::vector<ResourceConsumer> consumers;
    };
    struct Request
    {
        Kind kind = Kind::Texture;
        std::string logicalPath;
        std::filesystem::path decodePath;
        ResourceRequestToken token;
        std::vector<ResourceConsumer> consumers;
        std::vector<Target> targets;
        std::uintmax_t inputBytes = 0;
        unsigned retry = 0;
        std::chrono::steady_clock::time_point readyAt{};
    };
    struct Result
    {
        explicit Result(Request value) : request(std::move(value)) {}
        Request request;
        // The SDL surface borrows pixels from the bounded decoder. Keep both
        // the pixel allocation and its budget alive through owner-thread upload.
        std::shared_ptr<void> decoderLifetime;
        ImagePixelBuffer pixels;
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface{nullptr, SDL_DestroySurface};
        Json animation;
        std::string error;
        bool dependencyFailure = false;
    };
    struct Dependency
    {
        std::string logicalPath;
        ResourceRequestToken token;
    };
    struct DeferredAnimation
    {
        Result result;
        std::vector<Dependency> textures;
        std::vector<std::string> pendingTexturePaths;
        std::size_t nextTexturePath = 0;
        std::uintmax_t byteCost = 0;
    };

    std::mutex mutex;
    std::deque<Request> pending;
    std::optional<Result> completed;
    std::shared_ptr<Request> inFlightRequest;
    std::vector<Target> inFlightJoinedTargets;
    std::deque<DeferredAnimation> deferredAnimations;
    std::uintmax_t deferredAnimationBytes = 0;
    oneapi::tbb::task_group tasks;
    std::function<void()> wakeCallback;
    bool running = false;
    bool stopping = false;
    std::size_t pendingLimit = 64;
};

namespace
{
constexpr std::size_t MaxPendingAssetReloads = 64;
constexpr std::size_t MaxDeferredAnimations = 64;
constexpr std::uintmax_t MaxAnimationDescriptionBytes = 1024U * 1024U;
constexpr std::uintmax_t MaxDeferredAnimationBytes = 8U * 1024U * 1024U;
constexpr std::uint64_t MaxSceneTargetBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t MaxDeferredAnimationsVisitedPerFrame = 8;
constexpr std::size_t MaxAnimationDependenciesScheduledPerFrame = 32;

bool same_reload_source(const EditorPreview::AssetReloadState::Request &left,
                       const EditorPreview::AssetReloadState::Request &right)
{
    if (left.kind != right.kind ||
        left.token.projectGeneration != right.token.projectGeneration)
        return false;
    if (left.token.logicalPath == right.token.logicalPath &&
        left.token.requestId != right.token.requestId)
        return false;
    if (!left.token.canonicalPath.empty() && !right.token.canonicalPath.empty())
        return left.token.canonicalPath == right.token.canonicalPath &&
               left.token.canonicalVersion == right.token.canonicalVersion;
    return left.token.logicalPath == right.token.logicalPath &&
           left.token.logicalVersion == right.token.logicalVersion &&
           left.token.uncertaintyEpoch == right.token.uncertaintyEpoch;
}

void merge_reload_targets(EditorPreview::AssetReloadState::Request &destination,
                          const EditorPreview::AssetReloadState::Request &source)
{
    const auto append = [&](const EditorPreview::AssetReloadState::Target &incoming) {
        const auto existing = std::find_if(destination.targets.begin(), destination.targets.end(),
            [&](const auto &target) { return target.logicalPath == incoming.logicalPath; });
        if (existing == destination.targets.end())
        {
            destination.targets.push_back(incoming);
            return;
        }
        if (existing->token.requestId <= incoming.token.requestId)
            existing->token = incoming.token;
        for (const auto &consumer : incoming.consumers)
            if (std::find(existing->consumers.begin(), existing->consumers.end(), consumer) ==
                existing->consumers.end())
                existing->consumers.push_back(consumer);
    };
    if (destination.targets.empty())
        destination.targets.push_back({destination.logicalPath, destination.token, destination.consumers});
    if (source.targets.empty())
        append({source.logicalPath, source.token, source.consumers});
    else
        for (const auto &target : source.targets)
            append(target);
}

std::optional<ResourceRequestToken> start_asset_reload_locked(
    EditorPreview::AssetReloadState &state, std::chrono::steady_clock::time_point now)
{
    if (state.stopping || state.running || state.completed || state.pending.empty())
        return std::nullopt;
    const auto ready = std::find_if(state.pending.begin(), state.pending.end(), [&](const auto &request) {
        return request.readyAt <= now;
    });
    if (ready == state.pending.end())
        return std::nullopt;
    auto request = std::move(*ready);
    state.pending.erase(ready);
    const auto token = request.token;
    state.running = true;
    auto requestState = std::make_shared<EditorPreview::AssetReloadState::Request>(std::move(request));
    state.inFlightRequest = requestState;
    state.inFlightJoinedTargets.clear();
    const auto wakeCallback = state.wakeCallback;
    state.tasks.run([&state, requestState, wakeCallback] {
        EditorPreview::AssetReloadState::Result result(*requestState);
        try
        {
            if (result.request.kind == EditorPreview::AssetReloadState::Kind::Texture)
            {
                work::record(work::Counter::ImageDecodes);
                auto prepared = prepare_image(result.request.decodePath);
                result.surface = std::move(prepared.surface);
                result.pixels = std::move(prepared.pixels);
                result.decoderLifetime = std::move(prepared.decoderLifetime);
                if (!result.surface)
                    result.error = std::move(prepared.preflight.message);
            }
            else
            {
                if (result.request.inputBytes > MaxAnimationDescriptionBytes)
                    throw std::runtime_error("Animation preview description exceeds the 1 MiB limit.");
                std::ifstream file(result.request.decodePath, std::ios::binary);
                if (!file)
                    throw std::runtime_error("Cannot open animation: " + result.request.logicalPath);
                result.animation = Json::parse(file);
            }
        }
        catch (const std::exception &error)
        {
            result.error = error.what();
        }
        catch (...)
        {
            result.error = "Unknown asset preparation failure";
        }
        {
            std::lock_guard lock(state.mutex);
            EditorPreview::AssetReloadState::Request joinedTargets;
            joinedTargets.targets = std::move(state.inFlightJoinedTargets);
            merge_reload_targets(result.request, joinedTargets);
            state.running = false;
            state.inFlightRequest.reset();
            if (!state.stopping)
                state.completed.emplace(std::move(result));
        }
        if (wakeCallback)
        {
            try { wakeCallback(); }
            catch (...) {}
        }
    });
    return token;
}

bool enqueue_asset_reload(EditorPreview::AssetReloadState &state,
                          EditorPreview::AssetReloadState::Request request)
{
    std::lock_guard lock(state.mutex);
    if (state.stopping)
        return false;
    if (state.inFlightRequest && same_reload_source(*state.inFlightRequest, request))
    {
        auto joined = *state.inFlightRequest;
        EditorPreview::AssetReloadState::Request previousJoins;
        previousJoins.targets = state.inFlightJoinedTargets;
        merge_reload_targets(joined, previousJoins);
        merge_reload_targets(joined, request);
        state.inFlightJoinedTargets = std::move(joined.targets);
        return true;
    }
    if (state.completed && same_reload_source(state.completed->request, request))
    {
        merge_reload_targets(state.completed->request, request);
        return true;
    }
    const auto existing = std::find_if(state.pending.begin(), state.pending.end(), [&](const auto &queued) {
        return same_reload_source(queued, request);
    });
    if (existing != state.pending.end())
        merge_reload_targets(*existing, request);
    else
    {
        if (state.pending.size() >= std::min(MaxPendingAssetReloads, state.pendingLimit))
            return false;
        state.pending.push_back(std::move(request));
    }
    (void)start_asset_reload_locked(state, std::chrono::steady_clock::now());
    return true;
}
} // namespace

EditorPreview::EditorPreview(platformator::Runtime &runtime)
    : runtime_(&runtime), renderer_(runtime.getRenderer()),
      assetReloadState_(std::make_unique<AssetReloadState>())
{
}

EditorPreview::~EditorPreview()
{
    if (assetReloadState_)
    {
        {
            std::lock_guard lock(assetReloadState_->mutex);
            assetReloadState_->stopping = true;
            assetReloadState_->pending.clear();
            assetReloadState_->completed.reset();
        }
        assetReloadState_->tasks.wait();
    }
    release();
}

void EditorPreview::set_resource_index(ResourceIndex *resourceIndex) noexcept
{
    if (resourceIndex_ == resourceIndex)
        return;
    if (resourceIndex_ != nullptr && resourceDocument_)
        resourceIndex_->unregister_document(*resourceDocument_);
    resourceIndex_ = resourceIndex;
    resourceDocument_.reset();
    for (auto &[id, proxy] : proxies_)
    {
        (void)id;
        proxy.registeredSpritePath.clear();
        proxy.registeredClipPath.clear();
        proxy.spriteConsumer.reset();
        proxy.clipConsumer.reset();
        proxy.frameConsumers.clear();
    }
}

void EditorPreview::set_wake_callback(std::function<void()> callback)
{
    if (!assetReloadState_)
        return;
    std::lock_guard lock(assetReloadState_->mutex);
    assetReloadState_->wakeCallback = std::move(callback);
}

void EditorPreview::reconcile(const DocumentSession &document, bool structureChanged,
                             const std::vector<int> &changedObjectIds,
                             bool inheritedTranslationChanged)
{
#ifndef NDEBUG
    lastReconcileStats_ = {};
#endif
    const DocumentIdentity identity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                    DocumentGeneration{document.documentGeneration}};
    const bool resourceIdentityChanged = resourceDocument_ != identity;
    if (resourceDocument_ && resourceDocument_ != identity && resourceIndex_ != nullptr)
        resourceIndex_->unregister_document(*resourceDocument_);
    if (resourceIdentityChanged)
    {
        resourceDocument_ = identity;
        for (auto &[id, proxy] : proxies_)
        {
            (void)id;
            proxy.registeredSpritePath.clear();
            proxy.registeredClipPath.clear();
            proxy.spriteConsumer.reset();
            proxy.clipConsumer.reset();
            proxy.frameConsumers.clear();
        }
    }
    const bool rootChanged = assetRoot_ != document.assetRoot;
    const bool generationChanged = documentGeneration_ != document.documentGeneration;
    if (rootChanged || generationChanged || resourceIdentityChanged || structureChanged ||
        structureRevision_ != document.structureRevision)
    {
        invalidate_render_cache();
        if (rootChanged || generationChanged)
        {
            release();
            resourceDocument_ = identity;
        }
        assetRoot_ = document.assetRoot;
        documentGeneration_ = document.documentGeneration;
        structureRevision_ = document.structureRevision;
        rebuild_authoring_info(document);
        if (runtime_ != nullptr)
            rebuild(document);
        return;
    }
    if (changedObjectIds.empty())
        return;

    std::vector<int> changedRoots;
    changedRoots.reserve(changedObjectIds.size());
    std::unordered_set<int> changedIds;
    changedIds.reserve(changedObjectIds.size());
    for (const int objectId : changedObjectIds)
    {
        if (document.indexes.objectsById.contains(objectId) && changedIds.insert(objectId).second)
            changedRoots.push_back(objectId);
    }

    std::vector<int> effectiveRoots;
    effectiveRoots.reserve(changedRoots.size());
    for (const int rootId : changedRoots)
    {
        bool coveredByChangedAncestor = false;
        const auto indexedParent = document.indexes.parentById.find(rootId);
        std::optional<int> parentId = indexedParent == document.indexes.parentById.end()
                                          ? std::nullopt : indexedParent->second;
        while (parentId)
        {
            if (changedIds.contains(*parentId))
            {
                coveredByChangedAncestor = true;
                break;
            }
            const auto nextParent = document.indexes.parentById.find(*parentId);
            parentId = nextParent == document.indexes.parentById.end()
                           ? std::nullopt : nextParent->second;
        }
        if (!coveredByChangedAncestor)
            effectiveRoots.push_back(rootId);
    }
    if (!effectiveRoots.empty())
        invalidate_render_cache();

    authoringUpdateIds_.clear();
    // EFFICIENCY: a changed object refreshes only its descriptor and proxy. Position
    // edits additionally visit descendants because this engine inherits translation;
    // rotation and scale remain local. The retained cost is O(changed subtree) for
    // inherited moves instead of rebuilding the entire authored index after each edit.
    for (const int rootId : effectiveRoots)
        authoringIndex_.update_subtree(document, rootId, authoringUpdateIds_,
                                       inheritedTranslationChanged);
    update_authoring_info(document, authoringUpdateIds_);
#ifndef NDEBUG
    lastReconcileStats_.visitedAuthoringObjects = authoringUpdateIds_.size();
    lastReconcileStats_.visitedObjects += authoringUpdateIds_.size();
#endif

    for (const int rootId : effectiveRoots)
    {
        const auto rootLocation = document.indexes.objectsById.find(rootId);
        if (rootLocation == document.indexes.objectsById.end())
            continue;
        const Json *rootObject = resolve_object(document.root, rootLocation->second);
        if (rootObject == nullptr)
            continue;

        float inheritedX = 0.0f;
        float inheritedY = 0.0f;
        const auto rootParent = document.indexes.parentById.find(rootId);
        if (rootParent != document.indexes.parentById.end() && rootParent->second)
        {
            const Eigen::Vector2f parentWorld = accumulated_translation(document, *rootParent->second);
            inheritedX = parentWorld.x();
            inheritedY = parentWorld.y();
        }
        struct PendingObject
        {
            const Json *record;
            float parentX;
            float parentY;
        };
        std::vector<PendingObject> pending;
        pending.push_back({rootObject, inheritedX, inheritedY});
        while (!pending.empty())
        {
            const PendingObject current = pending.back();
            pending.pop_back();
            if (current.record == nullptr || !current.record->is_object())
                continue;

            const Json &object = *current.record;
            const int objectId = object.value("id", -1);
            const float worldX = current.parentX + nested_number(object, "position", "x", 0.0f);
            const float worldY = current.parentY + nested_number(object, "position", "y", 0.0f);
#ifndef NDEBUG
            ++lastReconcileStats_.visitedObjects;
            ++lastReconcileStats_.visitedProxyObjects;
#endif
            if (proxies_.contains(objectId))
            {
                update_proxy(document, objectId, object, worldX, worldY);
#ifndef NDEBUG
                ++lastReconcileStats_.updatedProxies;
#endif
            }

            const auto children = object.find("children");
            if (!inheritedTranslationChanged || children == object.end() || !children->is_array())
                continue;
            for (std::size_t index = children->size(); index > 0; --index)
                pending.push_back({&(*children)[index - 1], worldX, worldY});
        }
    }
}

void EditorPreview::refresh_asset(const DocumentSession &document,
                                  const std::filesystem::path &changedPath, bool directory,
                                  const std::vector<ResourceConsumer> &consumers)
{
    if (runtime_ == nullptr || resourceIndex_ == nullptr || consumers.empty() || changedPath.empty())
        return;
    const DocumentIdentity identity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                    DocumentGeneration{document.documentGeneration}};
    (void)directory;
    std::map<std::string, std::vector<ResourceConsumer>> textureRequests;
    std::map<std::string, std::vector<ResourceConsumer>> clipRequests;
    for (const auto &consumer : consumers)
    {
        if (consumer.document != identity)
            continue;
        const auto found = proxies_.find(consumer.objectId);
        if (found == proxies_.end())
            continue;
        const Proxy &proxy = found->second;
        if (consumer.role == "sprite.texture" && !proxy.registeredSpritePath.empty())
            textureRequests[proxy.registeredSpritePath].push_back(consumer);
        else if (consumer.role == "animator.clip" && !proxy.registeredClipPath.empty())
            clipRequests[proxy.registeredClipPath].push_back(consumer);
        else if (consumer.role.starts_with("animation.frame."))
        {
            for (const auto &[path, frameConsumer] : proxy.frameConsumers)
            {
                if (frameConsumer != consumer)
                    continue;
                textureRequests[path.generic_string()].push_back(consumer);
                if (!proxy.registeredClipPath.empty())
                    clipRequests[proxy.registeredClipPath].push_back(consumer);
            }
        }
    }
    if (textureRequests.empty() && clipRequests.empty())
        return;

    auto queue_request = [&](AssetReloadState::Kind kind, const std::string &path,
                             const std::vector<ResourceConsumer> &pathConsumers) {
        const ResourceKind resourceKind = kind == AssetReloadState::Kind::Texture
                                              ? ResourceKind::Texture : ResourceKind::Animation;
        auto resolution = resourceIndex_->resolve(path, resourceKind);
        if (resolution.preparation == ResourcePreparationState::Failed)
        {
            resourceIndex_->invalidate(path);
            resolution = resourceIndex_->resolve(path, resourceKind);
        }
        const auto token = resourceIndex_->begin_request(path);
        if (token.requestId == 0)
            return;
        if (resolution.status == ResourceStatus::OutsideRoot ||
            resolution.status == ResourceStatus::SymlinkCycle ||
            resolution.status == ResourceStatus::Unreadable)
        {
            const std::string error = resolution.status == ResourceStatus::OutsideRoot
                                          ? "Asset resolves outside the project asset root."
                                          : resolution.status == ResourceStatus::SymlinkCycle
                                                ? "Asset path contains a symbolic-link cycle."
                                                : "Asset path is not readable.";
            resourceIndex_->set_preparation_state(token, ResourcePreparationState::Failed, error);
            for (const auto &consumer : pathConsumers)
            {
                const auto proxy = proxies_.find(consumer.objectId);
                if (proxy == proxies_.end() || consumer.document != identity)
                    continue;
                proxy->second.info.assetError = error;
                if (auto info = authoringInfos_.find(consumer.objectId); info != authoringInfos_.end())
                    info->second.assetError = error;
                lastError_ = error;
            }
            return;
        }

        AssetReloadState::Request request;
        request.kind = kind;
        request.logicalPath = path;
        request.decodePath = resolution.status == ResourceStatus::Resolved
                                 ? resolution.canonicalPath : resolution.logicalPath;
        request.token = token;
        request.consumers = pathConsumers;
        request.targets.push_back({path, token, pathConsumers});
        request.inputBytes = resolution.size.value_or(0);
        resourceIndex_->set_preparation_state(token, ResourcePreparationState::Preparing);
        if (!enqueue_asset_reload(*assetReloadState_, std::move(request)))
        {
            const std::string error = "Asset preparation queue is full; use Refresh Assets to retry.";
            resourceIndex_->set_preparation_state(token, ResourcePreparationState::Failed, error);
            for (const auto &consumer : pathConsumers)
            {
                const auto proxy = proxies_.find(consumer.objectId);
                if (proxy == proxies_.end() || consumer.document != identity)
                    continue;
                proxy->second.info.assetError = error;
                if (auto info = authoringInfos_.find(consumer.objectId); info != authoringInfos_.end())
                    info->second.assetError = error;
                lastError_ = error;
            }
        }
    };
    for (const auto &[path, pathConsumers] : textureRequests)
    {
        queue_request(AssetReloadState::Kind::Texture, path, pathConsumers);
    }
    for (const auto &[path, pathConsumers] : clipRequests)
    {
        queue_request(AssetReloadState::Kind::Animation, path, pathConsumers);
    }
}

std::size_t EditorPreview::retry_failed_assets(const DocumentSession &document)
{
    if (resourceIndex_ == nullptr || runtime_ == nullptr)
        return 0;

    using RetryKey = std::pair<ResourceKind, std::string>;
    std::set<RetryKey> failed;
    const auto consider = [&](const std::filesystem::path &path, ResourceKind kind) {
        if (path.empty())
            return;
        const auto state = resourceIndex_->resolve(path, kind);
        if (state.preparation == ResourcePreparationState::Failed)
            failed.emplace(kind, path.generic_string());
    };
    for (const auto &[objectId, proxy] : proxies_)
    {
        (void)objectId;
        if (!proxy.registeredSpritePath.empty())
            consider(proxy.registeredSpritePath, ResourceKind::Texture);
        if (!proxy.registeredClipPath.empty())
            consider(proxy.registeredClipPath, ResourceKind::Animation);
        for (const auto &[path, consumer] : proxy.frameConsumers)
        {
            (void)consumer;
            consider(path, ResourceKind::Texture);
        }
    }

    std::size_t scheduled = 0;
    for (const auto &[kind, path] : failed)
    {
        (void)kind;
        const auto consumers = resourceIndex_->affected_consumers(path);
        if (consumers.empty())
            continue;
        refresh_asset(document, path, false, consumers);
        ++scheduled;
    }
    return scheduled;
}

void EditorPreview::poll_asset_reloads(const DocumentSession &document)
{
    if (!assetReloadState_ || resourceIndex_ == nullptr || runtime_ == nullptr)
        return;
    std::optional<AssetReloadState::Result> completed;
    std::optional<ResourceRequestToken> started;
    {
        std::lock_guard lock(assetReloadState_->mutex);
        if (assetReloadState_->completed)
        {
            completed.emplace(std::move(*assetReloadState_->completed));
            assetReloadState_->completed.reset();
        }
        started = start_asset_reload_locked(*assetReloadState_, std::chrono::steady_clock::now());
    }
    if (started)
        resourceIndex_->set_preparation_state(*started, ResourcePreparationState::Preparing);

    std::string stagingError;
    const auto queue_texture_dependency = [&](const std::string &logicalPath)
        -> std::optional<AssetReloadState::Dependency> {
        auto resolution = resourceIndex_->resolve(logicalPath, ResourceKind::Texture);
        if (resolution.preparation == ResourcePreparationState::Failed)
        {
            // Explicit clip retry must revalidate dependencies that previously failed,
            // including a missing frame that has since been restored on disk.
            resourceIndex_->invalidate(logicalPath);
            resolution = resourceIndex_->resolve(logicalPath, ResourceKind::Texture);
        }
        if (resolution.status == ResourceStatus::OutsideRoot ||
            resolution.status == ResourceStatus::SymlinkCycle ||
            resolution.status == ResourceStatus::Unreadable)
        {
            stagingError = resolution.status == ResourceStatus::OutsideRoot
                ? "Animation frame texture resolves outside the project asset root."
                : resolution.status == ResourceStatus::SymlinkCycle
                    ? "Animation frame texture contains a symbolic-link cycle."
                    : "Animation frame texture is not readable.";
            return std::nullopt;
        }
        const auto token = resourceIndex_->join_request(logicalPath);
        if (token.requestId == 0)
        {
            stagingError = "Could not allocate an animation frame texture request.";
            return std::nullopt;
        }
        if (resourceIndex_->is_ready(token))
            return AssetReloadState::Dependency{logicalPath, token};
        AssetReloadState::Request request;
        request.kind = AssetReloadState::Kind::Texture;
        request.logicalPath = logicalPath;
        request.decodePath = resolution.status == ResourceStatus::Resolved
                                 ? resolution.canonicalPath : resolution.logicalPath;
        request.token = token;
        request.targets.push_back({logicalPath, token, {}});
        request.inputBytes = resolution.size.value_or(0);
        resourceIndex_->set_preparation_state(token, ResourcePreparationState::Preparing);
        if (!enqueue_asset_reload(*assetReloadState_, std::move(request)))
        {
            resourceIndex_->set_preparation_state(token, ResourcePreparationState::Failed,
                "Animation frame texture queue is full; the preview will retry when capacity is available.");
        }
        return AssetReloadState::Dependency{logicalPath, token};
    };

    const DocumentIdentity currentIdentity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                           DocumentGeneration{document.documentGeneration}};
    const auto target_has_live_demand = [&](const AssetReloadState::Target &target,
                                            AssetReloadState::Kind kind) {
        if (kind == AssetReloadState::Kind::Texture)
            return true;
        if (target.consumers.empty())
            return false;
        for (const auto &consumer : target.consumers)
        {
            if (consumer.document != currentIdentity)
                continue;
            const auto found = proxies_.find(consumer.objectId);
            if (found == proxies_.end())
                continue;
            const Proxy &proxy = found->second;
            if (kind == AssetReloadState::Kind::Animation)
            {
                if (proxy.clipConsumer && *proxy.clipConsumer == consumer &&
                    proxy.registeredClipPath == target.logicalPath)
                    return true;
            }
        }
        return false;
    };

    if (completed && completed->request.kind == AssetReloadState::Kind::Animation &&
        completed->error.empty() &&
        std::any_of(completed->request.targets.begin(), completed->request.targets.end(),
                    [&](const auto &target) {
                        return resourceIndex_->accepts(target.token) &&
                               target_has_live_demand(target, AssetReloadState::Kind::Animation);
                    }))
    {
        AssetReloadState::DeferredAnimation deferred{std::move(*completed), {}};
        completed.reset();
        std::set<std::string> texturePaths;
        const auto frames = deferred.result.animation.find("frames");
        if (frames != deferred.result.animation.end() && frames->is_array())
        {
            if (frames->size() > 512)
                stagingError = "Animation preview supports at most 512 frames.";
            for (const auto &frame : *frames)
            {
                if (!stagingError.empty())
                    break;
                const auto path = frame.is_object() ? frame.find("textureWrapperFilePath") : Json::const_iterator{};
                if (path == frame.end() || !path->is_string())
                    continue;
                const auto logicalPath = path->get<std::string>();
                if (!logicalPath.empty())
                    texturePaths.insert(logicalPath);
            }
        }
        deferred.byteCost = deferred.result.request.inputBytes;
        if (texturePaths.size() > 512)
            stagingError = "Animation references more than 512 unique frame textures.";
        else
            deferred.pendingTexturePaths.assign(texturePaths.begin(), texturePaths.end());

        if (stagingError.empty() &&
            (assetReloadState_->deferredAnimations.size() >= MaxDeferredAnimations ||
             deferred.byteCost > MaxDeferredAnimationBytes - assetReloadState_->deferredAnimationBytes))
            stagingError = "Deferred animation preparation capacity is full; the last-good clip is retained.";
        if (!stagingError.empty())
        {
            deferred.result.error = stagingError;
            deferred.result.dependencyFailure = true;
            completed.emplace(std::move(deferred.result));
        }
        else
        {
            assetReloadState_->deferredAnimationBytes += deferred.byteCost;
            assetReloadState_->deferredAnimations.push_back(std::move(deferred));
        }
    }

    bool animationPreparationOutstanding = false;
    {
        std::lock_guard lock(assetReloadState_->mutex);
        animationPreparationOutstanding =
            (assetReloadState_->inFlightRequest &&
             assetReloadState_->inFlightRequest->kind == AssetReloadState::Kind::Animation) ||
            std::any_of(assetReloadState_->pending.begin(), assetReloadState_->pending.end(),
                        [](const auto &request) { return request.kind == AssetReloadState::Kind::Animation; });
    }
    if (!completed && !animationPreparationOutstanding)
    {
        std::size_t visited = 0;
        std::size_t dependencyScheduleBudget = MaxAnimationDependenciesScheduledPerFrame;
        for (auto deferred = assetReloadState_->deferredAnimations.begin();
             deferred != assetReloadState_->deferredAnimations.end() &&
             visited < MaxDeferredAnimationsVisitedPerFrame;)
        {
            ++visited;
            const auto releaseDeferred = [&] {
                assetReloadState_->deferredAnimationBytes -= deferred->byteCost;
                deferred = assetReloadState_->deferredAnimations.erase(deferred);
            };
            if (deferred->result.request.token.projectGeneration != resourceIndex_->project_generation())
            {
                releaseDeferred();
                continue;
            }

            const bool hasLiveClipTarget = std::any_of(
                deferred->result.request.targets.begin(), deferred->result.request.targets.end(),
                [&](const auto &target) {
                    return resourceIndex_->accepts(target.token) &&
                           target_has_live_demand(target, AssetReloadState::Kind::Animation);
                });
            if (!hasLiveClipTarget)
            {
                for (const auto &target : deferred->result.request.targets)
                {
                    if (resourceIndex_->accepts(target.token) &&
                        !target_has_live_demand(target, AssetReloadState::Kind::Animation))
                        resourceIndex_->set_preparation_state(target.token, ResourcePreparationState::Failed,
                            "Animation preparation was cancelled because the clip is no longer open.");
                    if (target.token.projectGeneration != resourceIndex_->project_generation() ||
                        !target_has_live_demand(target, AssetReloadState::Kind::Animation) ||
                        resourceIndex_->accepts(target.token))
                        continue;
                    const auto latest = resourceIndex_->resolve(target.logicalPath, ResourceKind::Animation);
                    const auto currentToken = resourceIndex_->current_request(target.logicalPath);
                    const auto revision = resourceIndex_->directory_revision(
                        std::filesystem::path(target.logicalPath).parent_path());
                    const bool sameContentVersion = latest.logicalVersion == target.token.logicalVersion &&
                        latest.canonicalPath == target.token.canonicalPath &&
                        latest.canonicalVersion == target.token.canonicalVersion;
                    const bool currentRevisionScheduled = currentToken &&
                        currentToken->invalidationRevision == revision;
                    if (sameContentVersion && target.token.invalidationRevision != revision &&
                        !currentRevisionScheduled)
                        refresh_asset(document, target.logicalPath, false, target.consumers);
                }
                releaseDeferred();
                continue;
            }

            std::string dependencyError;
            while (deferred->nextTexturePath < deferred->pendingTexturePaths.size() &&
                   dependencyScheduleBudget > 0)
            {
                const auto &path = deferred->pendingTexturePaths[deferred->nextTexturePath];
                const auto dependency = queue_texture_dependency(path);
                if (!dependency)
                {
                    dependencyError = stagingError.empty()
                        ? "Could not schedule an animation frame texture."
                        : stagingError;
                    break;
                }
                deferred->textures.push_back(*dependency);
                ++deferred->nextTexturePath;
                --dependencyScheduleBudget;
            }

            bool activeWork = deferred->nextTexturePath < deferred->pendingTexturePaths.size();
            std::vector<std::string> retryPaths;
            for (auto &dependency : deferred->textures)
            {
                if (!resourceIndex_->accepts(dependency.token))
                {
                    retryPaths.push_back(dependency.logicalPath);
                    continue;
                }
                const auto state = resourceIndex_->resolve(dependency.logicalPath, ResourceKind::Texture);
                if (state.preparation == ResourcePreparationState::Failed)
                {
                    if (state.preparationError.starts_with("Animation frame texture queue is full"))
                        retryPaths.push_back(dependency.logicalPath);
                    else
                        dependencyError = state.preparationError.empty()
                            ? "An animation frame texture could not be prepared."
                            : state.preparationError;
                    if (!dependencyError.empty())
                        break;
                }
                else if (state.preparation != ResourcePreparationState::Ready)
                    activeWork = true;
            }

            if (!dependencyError.empty())
            {
                deferred->result.error = std::move(dependencyError);
                deferred->result.dependencyFailure = true;
                completed.emplace(std::move(deferred->result));
                releaseDeferred();
                break;
            }

            for (const auto &path : retryPaths)
            {
                if (dependencyScheduleBudget == 0)
                {
                    activeWork = true;
                    break;
                }
                const auto replacement = queue_texture_dependency(path);
                if (!replacement)
                {
                    deferred->result.error = stagingError.empty()
                        ? "Could not reschedule an animation frame texture."
                        : stagingError;
                    deferred->result.dependencyFailure = true;
                    completed.emplace(std::move(deferred->result));
                    releaseDeferred();
                    break;
                }
                const auto found = std::find_if(deferred->textures.begin(), deferred->textures.end(),
                    [&](const auto &dependency) { return dependency.logicalPath == path; });
                if (found != deferred->textures.end())
                    *found = *replacement;
                --dependencyScheduleBudget;
                activeWork = true;
            }
            if (completed)
                break;
            if (!activeWork && deferred->nextTexturePath == deferred->pendingTexturePaths.size())
            {
                completed.emplace(std::move(deferred->result));
                releaseDeferred();
                break;
            }
            ++deferred;
        }
    }
    if (!completed)
        return;

    auto &result = *completed;
    const auto &request = result.request;
    std::vector<AssetReloadState::Target> liveTargets;
    for (const auto &target : request.targets)
    {
        if (resourceIndex_->accepts(target.token) && target_has_live_demand(target, request.kind))
            liveTargets.push_back(target);
        else if (target.token.projectGeneration == resourceIndex_->project_generation() &&
                 !target.consumers.empty() && target_has_live_demand(target, request.kind))
        {
            // A sibling change can invalidate a result through its directory revision
            // without changing this resource's content version. Requeue only if no
            // newer request already covers that directory revision.
            const auto latest = resourceIndex_->resolve(target.logicalPath,
                request.kind == AssetReloadState::Kind::Texture ? ResourceKind::Texture
                                                                : ResourceKind::Animation);
            const auto currentToken = resourceIndex_->current_request(target.logicalPath);
            const auto parentPath = std::filesystem::path(target.logicalPath).parent_path();
            const auto currentDirectoryRevision = resourceIndex_->directory_revision(parentPath);
            const bool sameContentVersion = latest.logicalVersion == target.token.logicalVersion &&
                latest.canonicalPath == target.token.canonicalPath &&
                latest.canonicalVersion == target.token.canonicalVersion;
            const bool currentRevisionScheduled = currentToken &&
                currentToken->invalidationRevision == currentDirectoryRevision;
            if (sameContentVersion && target.token.invalidationRevision != currentDirectoryRevision &&
                !currentRevisionScheduled)
                refresh_asset(document, target.logicalPath, false, target.consumers);
        }
    }
    if (liveTargets.empty())
    {
        for (const auto &target : request.targets)
            if (resourceIndex_->accepts(target.token) &&
                !target_has_live_demand(target, request.kind))
                resourceIndex_->set_preparation_state(target.token, ResourcePreparationState::Failed,
                    "Asset preparation was cancelled because it no longer has a live consumer.");
        return;
    }
    for (const auto &target : liveTargets)
        resourceIndex_->set_preparation_state(target.token, ResourcePreparationState::AwaitingOwnerCommit);

    std::string error = result.error;
    try
    {
        if (error.empty())
        {
            std::set<std::filesystem::path> committedTexturePaths;
            for (const auto &target : liveTargets)
            {
                if (request.kind == AssetReloadState::Kind::Texture)
                {
                    if (committedTexturePaths.contains(target.token.canonicalPath))
                    {
                        (void)platformator_detail::RuntimeAccess::findTexture(target.logicalPath);
                        continue;
                    }
                    committedTexturePaths.insert(target.token.canonicalPath);
                    const std::uint64_t candidateBytes =
                        static_cast<std::uint64_t>(result.surface->w) *
                        static_cast<std::uint64_t>(result.surface->h) * 4ULL;
                    const std::uint64_t retainedBytes =
                        platformator_detail::RuntimeAccess::textureCacheBytes();
                    if (candidateBytes > proxyTextureBudgetBytes_ ||
                        retainedBytes > proxyTextureBudgetBytes_ - candidateBytes)
                        throw std::runtime_error("Scene preview proxy textures exceed the 256 MiB retained-texture limit.");
                    platformator_detail::RuntimeAccess::reloadTexture(target.logicalPath, result.surface.get());
                    work::record(work::Counter::TextureUploads);
                }
                else
                    platformator_detail::RuntimeAccess::reloadAnimationClip(target.logicalPath, result.animation);
            }
        }
    }
    catch (const std::exception &exception)
    {
        error = exception.what();
    }

    if (!error.empty())
    {
        const auto retryAt = std::chrono::steady_clock::now() +
                             std::chrono::milliseconds(200U << request.retry);
        if (request.retry < 2 && !result.dependencyFailure)
        {
            auto retry = request;
            retry.targets = liveTargets;
            retry.logicalPath = liveTargets.front().logicalPath;
            retry.token = liveTargets.front().token;
            retry.consumers = liveTargets.front().consumers;
            ++retry.retry;
            retry.readyAt = retryAt;
            for (const auto &target : liveTargets)
                resourceIndex_->set_preparation_state(target.token, ResourcePreparationState::RetryPending, error);
            if (!enqueue_asset_reload(*assetReloadState_, std::move(retry)))
                for (const auto &target : liveTargets)
                    resourceIndex_->set_preparation_state(target.token, ResourcePreparationState::Failed,
                                                           "Asset retry queue is full.");
        }
        else
            for (const auto &target : liveTargets)
                resourceIndex_->set_preparation_state(target.token, ResourcePreparationState::Failed, error);
        const DocumentIdentity identity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                        DocumentGeneration{document.documentGeneration}};
        for (const auto &target : liveTargets)
        for (const auto &consumer : target.consumers)
        {
            if (consumer.document != identity)
                continue;
            const auto proxy = proxies_.find(consumer.objectId);
            if (proxy == proxies_.end())
                continue;
            proxy->second.info.assetError = error;
            if (auto info = authoringInfos_.find(consumer.objectId); info != authoringInfos_.end())
                info->second.assetError = error;
        }
        lastError_ = error;
        return;
    }

    for (const auto &target : liveTargets)
    {
        resourceIndex_->resolve(target.logicalPath,
            request.kind == AssetReloadState::Kind::Texture ? ResourceKind::Texture : ResourceKind::Animation);
        resourceIndex_->set_preparation_state(target.token, ResourcePreparationState::Ready);
    }
    const DocumentIdentity identity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                    DocumentGeneration{document.documentGeneration}};
    std::vector<std::filesystem::path> preparedFrameLogicalPaths;
    if (request.kind == AssetReloadState::Kind::Animation)
    {
        const auto frames = result.animation.find("frames");
        if (frames != result.animation.end() && frames->is_array())
        {
            preparedFrameLogicalPaths.reserve(frames->size());
            for (const auto &frame : *frames)
            {
                std::filesystem::path logicalPath;
                if (frame.is_object())
                {
                    const auto path = frame.find("textureWrapperFilePath");
                    if (path != frame.end() && path->is_string())
                        logicalPath = path->get<std::string>();
                }
                preparedFrameLogicalPaths.push_back(std::move(logicalPath));
            }
        }
    }
    std::set<int> affectedObjects;
    for (const auto &target : liveTargets)
        for (const auto &consumer : target.consumers)
            if (consumer.document == identity)
                affectedObjects.insert(consumer.objectId);

    std::vector<int> commitObjects;
    commitObjects.reserve(affectedObjects.size());
    for (const int objectId : affectedObjects)
    {
        auto found = proxies_.find(objectId);
        if (found == proxies_.end())
            continue;
        Proxy &proxy = found->second;
        if (request.kind == AssetReloadState::Kind::Animation)
            proxy.frameLogicalPaths = preparedFrameLogicalPaths;
        if (proxy.clip != nullptr)
        {
            proxy.clip->removeReferenceAndFreeIfNoReferences();
            proxy.clip = nullptr;
        }
        for (const auto &[path, consumer] : proxy.frameConsumers)
            resourceIndex_->unregister_consumer(path, consumer);
        proxy.frameConsumers.clear();
        proxy.failedAssetIdentity.clear();
        proxy.failedAssetMessage.clear();
        proxy.info.assetError.clear();
        if (auto info = authoringInfos_.find(objectId); info != authoringInfos_.end())
            info->second.assetError.clear();
        proxy.hasCachedDocumentState = false;
        commitObjects.push_back(objectId);
    }
    if (!commitObjects.empty())
    {
        lastError_.clear();
        reconcile(document, false, commitObjects, false);
    }
}

bool EditorPreview::asset_preparation_pending() const noexcept
{
    if (!assetReloadState_)
        return false;
    std::lock_guard lock(assetReloadState_->mutex);
    return assetReloadState_->running || assetReloadState_->completed.has_value() ||
           !assetReloadState_->pending.empty() || !assetReloadState_->deferredAnimations.empty();
}

void EditorPreview::rebuild(const DocumentSession &document)
{
    if (runtime_ == nullptr)
        return;
    std::unordered_set<int> desired(document.indexes.orderedSpriteIds.begin(),
                                    document.indexes.orderedSpriteIds.end());
    for (auto it = proxies_.begin(); it != proxies_.end();)
    {
        if (!desired.contains(it->first))
        {
            destroy_proxy(it->second);
            it = proxies_.erase(it);
        }
        else
            ++it;
    }
    orderedDocumentIds_ = document.indexes.orderedSpriteIds;
    for (int documentId : orderedDocumentIds_)
    {
        const auto location = document.indexes.objectsById.find(documentId);
        if (location == document.indexes.objectsById.end())
            continue;
        const Json *object = resolve_object(document.root, location->second);
        if (object != nullptr)
            update_proxy(document, documentId, *object);
    }
}

void EditorPreview::rebuild_authoring_info(const DocumentSession &document)
{
    authoringIndex_.rebuild(document);
    for (auto iterator = authoringInfos_.begin(); iterator != authoringInfos_.end();)
    {
        if (authoringIndex_.find(iterator->first) == nullptr)
            iterator = authoringInfos_.erase(iterator);
        else
            ++iterator;
    }
    update_authoring_info(document, authoringIndex_.ordered_ids());
}

void EditorPreview::update_authoring_info(const DocumentSession &document,
                                          const std::vector<int> &updatedObjectIds)
{
    for (const int id : updatedObjectIds)
    {
        const auto *source = authoringIndex_.find(id);
        if (source == nullptr)
            continue;
        PreviewObjectInfo info;
        info.documentId = id;
        info.componentIds = source->componentIds;
        info.worldX = source->worldX;
        info.worldY = source->worldY;
        info.localScaleX = source->scaleX;
        info.localScaleY = source->scaleY;
        info.rotationDegrees = source->rotationRadians * 180.0f / std::numbers::pi_v<float>;
        info.width = source->spriteWidth * source->scaleX;
        info.height = source->spriteHeight * source->scaleY;
        info.bounds = {source->bounds.x, source->bounds.y, source->bounds.width, source->bounds.height};
        info.nonSpriteBounds = {source->nonSpriteBounds.x, source->nonSpriteBounds.y,
                                source->nonSpriteBounds.width, source->nonSpriteBounds.height};
        info.active = source->active;
        info.hasSprite = source->hasSprite;
        info.hasCamera = source->hasCamera;
        info.emptyMarker = source->emptyMarker;
        info.colliderPlaceholder = source->colliderPlaceholder;
        info.cameraWidth = source->cameraWidth;
        info.cameraHeight = source->cameraHeight;
        if (source->hasCollider)
        {
            info.colliderShape = source->circleCollider ? PreviewColliderShape::Circle : PreviewColliderShape::Box;
            info.colliderWidth = source->colliderWidth;
            info.colliderHeight = source->colliderHeight;
            info.colliderRadius = source->colliderRadius;
            info.colliderOffsetX = source->colliderOffsetX;
            info.colliderOffsetY = source->colliderOffsetY;
        }
        const auto previous = authoringInfos_.find(id);
        if (previous != authoringInfos_.end() && previous->second.hasSprite && info.hasSprite)
        {
            info.textureAvailable = previous->second.textureAvailable;
            info.assetError = previous->second.assetError;
            if (info.textureAvailable)
            {
                info.width = previous->second.width;
                info.height = previous->second.height;
            }
        }
        authoringInfos_.insert_or_assign(id, std::move(info));
    }
}

void EditorPreview::update_proxy(const DocumentSession &document, int documentId, const Json &objectData)
{
    const Eigen::Vector2f world = accumulated_translation(document, documentId);
    update_proxy(document, documentId, objectData, world.x(), world.y());
}

void EditorPreview::update_proxy(const DocumentSession &document, int documentId, const Json &objectData,
                                 float worldX, float worldY)
{
    const Json *spriteData = component_of_type(objectData, 6);
    if (spriteData == nullptr)
    {
        if (auto found = proxies_.find(documentId); found != proxies_.end())
        {
            destroy_proxy(found->second);
            proxies_.erase(found);
            orderedDocumentIds_.erase(std::remove(orderedDocumentIds_.begin(), orderedDocumentIds_.end(), documentId),
                                      orderedDocumentIds_.end());
        }
        return;
    }

    Proxy &proxy = proxies_[documentId];
    const Json emptyAnimatorData = Json::object();
    const Json *animatorDataPointer = component_of_type(objectData, 0);
    const Json &animatorData = animatorDataPointer == nullptr ? emptyAnimatorData : *animatorDataPointer;
    Proxy::SpriteInputs spriteInputs;
    spriteInputs.texturePath = string_value(*spriteData, "textureFilePath");
    spriteInputs.width = finite_number(*spriteData, "width", 0.0f);
    spriteInputs.height = finite_number(*spriteData, "height", 0.0f);
    spriteInputs.flip = integer_value(*spriteData, "flip", static_cast<int>(SDL_FLIP_NONE));
    spriteInputs.sourceRectEnabled = boolean_value(*spriteData, "sourceRectEnabled", false);
    if (const auto sourceRect = spriteData->find("sourceRect");
        sourceRect != spriteData->end() && sourceRect->is_object())
    {
        spriteInputs.sourceRectX = finite_number(*sourceRect, "x", 0.0f);
        spriteInputs.sourceRectY = finite_number(*sourceRect, "y", 0.0f);
        spriteInputs.sourceRectWidth = finite_number(*sourceRect, "w", 0.0f);
        spriteInputs.sourceRectHeight = finite_number(*sourceRect, "h", 0.0f);
    }
    Proxy::AnimatorInputs animatorInputs;
    animatorInputs.clipPath = string_value(animatorData, "animationClipFilePath");
    animatorInputs.frameIndex = integer_value(animatorData, "currentFrameIndex", 0);
    const Json *colliderData = component_of_type(objectData, 3);
    Proxy::ColliderInputs colliderInputs;
    if (colliderData != nullptr)
    {
        colliderInputs.present = true;
        colliderInputs.type = integer_value(*colliderData, "colliderType", -1);
        colliderInputs.supportedType = colliderInputs.type == 0 || colliderInputs.type == 1;
        colliderInputs.validDimensions = colliderInputs.type == 0
            ? positive_number(*colliderData, "radius", colliderInputs.radius)
            : colliderInputs.type == 1 &&
                  positive_number(*colliderData, "width", colliderInputs.width) &&
                  positive_number(*colliderData, "height", colliderInputs.height);
        colliderInputs.offsetX = nested_number(*colliderData, "offset", "x", 0.0f);
        colliderInputs.offsetY = nested_number(*colliderData, "offset", "y", 0.0f);
    }
    sync_asset_consumers(document, documentId, proxy, *spriteData, animatorData);
    const float positionX = nested_number(objectData, "position", "x", 0.0f);
    const float positionY = nested_number(objectData, "position", "y", 0.0f);
    const float scaleX = nested_number(objectData, "scale", "x", 1.0f);
    const float scaleY = nested_number(objectData, "scale", "y", 1.0f);
    const float rotation = finite_number(objectData, "rotation", 0.0f);
    const bool active = boolean_value(objectData, "active", true);
    const bool documentStateUnchanged = proxy.hasCachedDocumentState &&
        proxy.cachedPositionX == positionX && proxy.cachedPositionY == positionY &&
        proxy.cachedScaleX == scaleX && proxy.cachedScaleY == scaleY &&
        proxy.cachedRotation == rotation && proxy.cachedActive == active &&
        proxy.cachedSpriteInputs == spriteInputs && proxy.cachedAnimatorInputs == animatorInputs &&
        proxy.cachedColliderInputs == colliderInputs;
    const auto cacheDocumentState = [&]() {
        proxy.cachedPositionX = positionX;
        proxy.cachedPositionY = positionY;
        proxy.cachedScaleX = scaleX;
        proxy.cachedScaleY = scaleY;
        proxy.cachedRotation = rotation;
        proxy.cachedActive = active;
        proxy.cachedSpriteInputs = spriteInputs;
        proxy.cachedAnimatorInputs = animatorInputs;
        proxy.cachedColliderInputs = colliderInputs;
        proxy.hasCachedDocumentState = true;
    };

    proxy.info.documentId = documentId;
    proxy.info.worldX = worldX;
    proxy.info.worldY = worldY;
    proxy.info.localScaleX = scaleX;
    proxy.info.localScaleY = scaleY;
    proxy.info.rotationDegrees = rotation * 180.0f /
                                 std::numbers::pi_v<float>;
    proxy.info.active = active;
    proxy.info.hasSprite = true;
    if (!documentStateUnchanged)
    {
        proxy.info.width = finite_number(*spriteData, "width", 0.0f) * proxy.info.localScaleX;
        proxy.info.height = finite_number(*spriteData, "height", 0.0f) * proxy.info.localScaleY;
    }

    const auto syncAuthoringAsset = [&]() {
        const auto found = authoringInfos_.find(documentId);
        if (found == authoringInfos_.end())
            return;
        found->second.textureAvailable = proxy.info.textureAvailable;
        found->second.assetError = proxy.info.assetError;
        if (proxy.info.width > 0.0f)
            found->second.width = proxy.info.width;
        if (proxy.info.height > 0.0f)
            found->second.height = proxy.info.height;
        PreviewBounds bounds = found->second.nonSpriteBounds;
        if (found->second.width > 0.0f && found->second.height > 0.0f)
        {
            const float radians = found->second.rotationDegrees * std::numbers::pi_v<float> / 180.0f;
            const float halfWidth = std::abs(found->second.width) * 0.5f;
            const float halfHeight = std::abs(found->second.height) * 0.5f;
            const float extentX = std::abs(std::cos(radians)) * halfWidth +
                                  std::abs(std::sin(radians)) * halfHeight;
            const float extentY = std::abs(std::sin(radians)) * halfWidth +
                                  std::abs(std::cos(radians)) * halfHeight;
            const PreviewBounds spriteBounds{found->second.worldX - extentX,
                                             found->second.worldY - extentY,
                                             extentX * 2.0f, extentY * 2.0f};
            if (bounds.width <= 0.0f || bounds.height <= 0.0f)
                bounds = spriteBounds;
            else
            {
                const float minX = std::min(bounds.x, spriteBounds.x);
                const float minY = std::min(bounds.y, spriteBounds.y);
                const float maxX = std::max(bounds.x + bounds.width,
                                            spriteBounds.x + spriteBounds.width);
                const float maxY = std::max(bounds.y + bounds.height,
                                            spriteBounds.y + spriteBounds.height);
                bounds = {minX, minY, maxX - minX, maxY - minY};
            }
        }
        found->second.bounds = bounds;
    };

    if (proxy.object == nullptr)
        proxy.object = runtime_->createGameObject();
    if (proxy.sprite == nullptr)
    {
        proxy.object->addComponent<Sprite>(static_cast<TextureWrapper *>(nullptr), SDL_FLIP_NONE, 1.0f, 1.0f);
        proxy.sprite = proxy.object->getComponent<Sprite>();
    }
    if (proxy.object == nullptr || proxy.sprite == nullptr)
    {
        proxy.info.assetError = "Preview proxy creation failed.";
        lastError_ = proxy.info.assetError;
        return;
    }

    proxy.object->setName("Preview object " + std::to_string(documentId));
    proxy.object->setActive(proxy.info.active);
    proxy.object->setScale(Eigen::Vector2f(proxy.info.localScaleX, proxy.info.localScaleY));
    proxy.object->setRotation(rotation);
    platformator_editor_detail::EditorPreviewAccess::publish_proxy_position(*proxy.object, worldX, worldY);

    if (documentStateUnchanged)
    {
        syncAuthoringAsset();
        return;
    }

    proxy.info.assetError.clear();
    proxy.info.textureAvailable = false;

    const std::string &spritePath = spriteInputs.texturePath;
    const std::string &animationPath = animatorInputs.clipPath;
    const int frameIndex = animatorInputs.frameIndex;
    const std::string assetIdentity = animationPath + "#" + std::to_string(frameIndex) + "|" + spritePath;
    if (!proxy.failedAssetIdentity.empty() && proxy.failedAssetIdentity == assetIdentity)
    {
        proxy.info.assetError = proxy.failedAssetMessage;
        cacheDocumentState();
        syncAuthoringAsset();
        return;
    }
    if (proxy.failedAssetIdentity != assetIdentity)
    {
        proxy.failedAssetIdentity.clear();
        proxy.failedAssetMessage.clear();
    }

    TextureWrapper *texture = nullptr;
    SDL_FRect sourceRectangle{};
    bool hasSourceRectangle = false;
    float width = spriteInputs.width;
    float height = spriteInputs.height;
    int flip = spriteInputs.flip;
    try
    {
        if (!animationPath.empty())
        {
            AnimationClip *cachedClip = platformator_detail::RuntimeAccess::findAnimationClip(animationPath);
            if (proxy.clip != cachedClip)
            {
                if (proxy.clip != nullptr)
                {
                    proxy.clip->removeReferenceAndFreeIfNoReferences();
                    proxy.clip = nullptr;
                }
                proxy.clip = cachedClip;
                if (proxy.clip != nullptr)
                {
                    proxy.clip->addReference();
                    register_frame_consumers(document, documentId, proxy);
                }
            }
            if (proxy.clip != nullptr && frameIndex >= 0 &&
                static_cast<std::size_t>(frameIndex) < proxy.clip->getFrames().size())
            {
                const auto &frame = proxy.clip->getFrames()[static_cast<std::size_t>(frameIndex)];
                texture = frame.getTextureWrapper();
                if (frame.hasSourceRect)
                {
                    sourceRectangle = frame.sourceRect;
                    hasSourceRectangle = true;
                }
                width = proxy.clip->getWidth() > 0.0f ? proxy.clip->getWidth()
                       : frame.hasSourceRect ? frame.sourceRect.w : width;
                height = proxy.clip->getHeight() > 0.0f ? proxy.clip->getHeight()
                        : frame.hasSourceRect ? frame.sourceRect.h : height;
            }
        }
        if (texture == nullptr && !spritePath.empty())
            texture = platformator_detail::RuntimeAccess::findTexture(spritePath);
    }
    catch (const std::exception &error)
    {
        proxy.info.assetError = error.what();
        proxy.failedAssetIdentity = assetIdentity;
        proxy.failedAssetMessage = proxy.info.assetError;
        lastError_ = proxy.info.assetError;
    }

    if (texture != proxy.sprite->getTextureWrapper())
        proxy.sprite->setTextureWrapper(texture);
    proxy.sprite->setFlip(static_cast<SDL_FlipMode>(flip));
    if (hasSourceRectangle)
        proxy.sprite->setSourceRect(sourceRectangle);
    else if (spriteInputs.sourceRectEnabled)
    {
        SDL_FRect rect{spriteInputs.sourceRectX, spriteInputs.sourceRectY,
                       spriteInputs.sourceRectWidth, spriteInputs.sourceRectHeight};
        proxy.sprite->setSourceRect(rect);
    }
    else
        proxy.sprite->clearSourceRect();
    proxy.sprite->setWidth(width);
    proxy.sprite->setHeight(height);
    proxy.info.width = width * proxy.info.localScaleX;
    proxy.info.height = height * proxy.info.localScaleY;
    proxy.info.textureAvailable = proxy.sprite->getTexture() != nullptr;

    if (colliderInputs.present)
    {
        const bool supportedKind = colliderInputs.supportedType;
        const bool circle = supportedKind && colliderInputs.type == 0;
        proxy.info.colliderShape = circle ? PreviewColliderShape::Circle : PreviewColliderShape::Box;
        const double scaledWidth = static_cast<double>(colliderInputs.width) * std::abs(proxy.info.localScaleX);
        const double scaledHeight = static_cast<double>(colliderInputs.height) * std::abs(proxy.info.localScaleY);
        const double scaledRadius = static_cast<double>(colliderInputs.radius) *
                                    std::max(std::abs(proxy.info.localScaleX),
                                             std::abs(proxy.info.localScaleY));
        proxy.info.colliderPlaceholder = !supportedKind || !colliderInputs.validDimensions;
        if (circle && (!std::isfinite(scaledRadius) || scaledRadius <= 0.0 ||
                       scaledRadius > std::numeric_limits<float>::max()))
            proxy.info.colliderPlaceholder = true;
        if (!circle && (!std::isfinite(scaledWidth) || scaledWidth <= 0.0 ||
                        scaledWidth > std::numeric_limits<float>::max() ||
                        !std::isfinite(scaledHeight) || scaledHeight <= 0.0 ||
                        scaledHeight > std::numeric_limits<float>::max()))
            proxy.info.colliderPlaceholder = true;
        if (proxy.info.colliderPlaceholder)
        {
            proxy.info.colliderWidth = placeholder_dimension(proxy.info.localScaleX);
            proxy.info.colliderHeight = placeholder_dimension(proxy.info.localScaleY);
            proxy.info.colliderRadius = 6.0f;
        }
        else if (circle)
            proxy.info.colliderRadius = static_cast<float>(scaledRadius);
        else
        {
            proxy.info.colliderWidth = static_cast<float>(scaledWidth);
            proxy.info.colliderHeight = static_cast<float>(scaledHeight);
        }
        proxy.info.colliderOffsetX = colliderInputs.offsetX;
        proxy.info.colliderOffsetY = colliderInputs.offsetY;
    }
    else
        proxy.info.colliderShape = PreviewColliderShape::None;

    cacheDocumentState();
    syncAuthoringAsset();
}

void EditorPreview::destroy_proxy(Proxy &proxy) noexcept
{
    clear_asset_consumers(proxy);
    if (runtime_ != nullptr && proxy.object != nullptr)
    {
        try
        {
            runtime_->destroyGameObject(proxy.object);
        }
        catch (...)
        {
        }
    }
    proxy.object = nullptr;
    proxy.sprite = nullptr;
    if (proxy.clip != nullptr)
    {
        try
        {
            proxy.clip->removeReferenceAndFreeIfNoReferences();
        }
        catch (...)
        {
        }
        proxy.clip = nullptr;
    }
}

void EditorPreview::clear_asset_consumers(Proxy &proxy) noexcept
{
    if (resourceIndex_ != nullptr)
    {
        if (proxy.spriteConsumer)
            resourceIndex_->unregister_consumer(proxy.registeredSpritePath, *proxy.spriteConsumer);
        if (proxy.clipConsumer)
            resourceIndex_->unregister_consumer(proxy.registeredClipPath, *proxy.clipConsumer);
        for (const auto &[path, consumer] : proxy.frameConsumers)
            resourceIndex_->unregister_consumer(path, consumer);
    }
    proxy.registeredSpritePath.clear();
    proxy.registeredClipPath.clear();
    proxy.spriteConsumer.reset();
    proxy.clipConsumer.reset();
    proxy.frameConsumers.clear();
    proxy.frameLogicalPaths.clear();
}

void EditorPreview::sync_asset_consumers(const DocumentSession &document, int documentId, Proxy &proxy,
                                         const Json &spriteData, const Json &animatorData)
{
    if (resourceIndex_ == nullptr || !resourceDocument_)
        return;
    const auto spritePath = string_value(spriteData, "textureFilePath");
    const auto clipPath = string_value(animatorData, "animationClipFilePath");
    const auto spriteId = integer_value(spriteData, "id", 0);
    const auto animatorId = integer_value(animatorData, "id", 0);
    const ResourceConsumer nextSprite{*resourceDocument_, documentId,
        spriteId == 0 ? std::optional<int>{} : std::optional<int>{spriteId}, "sprite.texture"};
    const ResourceConsumer nextClip{*resourceDocument_, documentId,
        animatorId == 0 ? std::optional<int>{} : std::optional<int>{animatorId}, "animator.clip"};
    if (proxy.spriteConsumer &&
        (proxy.registeredSpritePath != spritePath || *proxy.spriteConsumer != nextSprite))
    {
        resourceIndex_->unregister_consumer(proxy.registeredSpritePath, *proxy.spriteConsumer);
        proxy.spriteConsumer.reset();
        proxy.registeredSpritePath.clear();
    }
    if (proxy.clipConsumer &&
        (proxy.registeredClipPath != clipPath || *proxy.clipConsumer != nextClip))
    {
        resourceIndex_->unregister_consumer(proxy.registeredClipPath, *proxy.clipConsumer);
        proxy.clipConsumer.reset();
        proxy.registeredClipPath.clear();
        for (const auto &[path, consumer] : proxy.frameConsumers)
            resourceIndex_->unregister_consumer(path, consumer);
        proxy.frameConsumers.clear();
        proxy.frameLogicalPaths.clear();
    }
    if (!spritePath.empty() && !proxy.spriteConsumer)
    {
        resourceIndex_->register_consumer(spritePath, ResourceKind::Texture, nextSprite);
        proxy.registeredSpritePath = spritePath;
        proxy.spriteConsumer = nextSprite;
    }
    if (!clipPath.empty() && !proxy.clipConsumer)
    {
        resourceIndex_->register_consumer(clipPath, ResourceKind::Animation, nextClip);
        proxy.registeredClipPath = clipPath;
        proxy.clipConsumer = nextClip;
    }

    const auto request_if_missing = [&](const std::string &path, ResourceKind kind, bool cached) {
        if (path.empty() || cached)
            return;
        const auto state = resourceIndex_->resolve(path, kind).preparation;
        if (state == ResourcePreparationState::Preparing ||
            state == ResourcePreparationState::AwaitingOwnerCommit ||
            state == ResourcePreparationState::RetryPending ||
            state == ResourcePreparationState::Failed)
            return;
        const auto consumers = resourceIndex_->affected_consumers(path);
        if (!consumers.empty())
            refresh_asset(document, path, false, consumers);
    };
    request_if_missing(spritePath, ResourceKind::Texture,
        platformator_detail::RuntimeAccess::findTexture(spritePath) != nullptr);
    request_if_missing(clipPath, ResourceKind::Animation,
        platformator_detail::RuntimeAccess::findAnimationClip(clipPath) != nullptr);
}

void EditorPreview::register_frame_consumers(const DocumentSession &document, int documentId, Proxy &proxy)
{
    if (resourceIndex_ == nullptr || !resourceDocument_ || proxy.clip == nullptr)
        return;
    const auto location = document.indexes.objectsById.find(documentId);
    const Json *animator = nullptr;
    if (location != document.indexes.objectsById.end())
    {
        const Json *object = resolve_object(document.root, location->second);
        if (object != nullptr)
            animator = component_of_type(*object, 0);
    }
    const int componentId = animator == nullptr ? 0 : integer_value(*animator, "id", 0);
    for (std::size_t index = 0; index < proxy.clip->getFrames().size(); ++index)
    {
        const auto *texture = proxy.clip->getFrames()[index].getTextureWrapper();
        if (texture == nullptr || texture->getFilePath().empty())
            continue;
        ResourceConsumer consumer{*resourceDocument_, documentId,
            componentId == 0 ? std::optional<int>{} : std::optional<int>{componentId},
            "animation.frame." + std::to_string(index)};
        const std::filesystem::path path = index < proxy.frameLogicalPaths.size() &&
                !proxy.frameLogicalPaths[index].empty()
            ? proxy.frameLogicalPaths[index]
            : std::filesystem::path(texture->getFilePath());
        resourceIndex_->register_consumer(path, ResourceKind::Texture, consumer);
        proxy.frameConsumers.emplace_back(path, std::move(consumer));
    }
}

bool EditorPreview::ensure_target(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;
    const std::uint64_t targetBytes = static_cast<std::uint64_t>(width) *
                                      static_cast<std::uint64_t>(height) * 4ULL;
    if (targetBytes > MaxSceneTargetBytes)
    {
        lastError_ = "Scene target exceeds the 64 MiB allocation limit.";
        return false;
    }
    if (target_ != nullptr && targetWidth_ == width && targetHeight_ == height)
        return true;
    if (renderer_ == nullptr)
        return false;
    if (SDL_GetRenderTarget(renderer_) == target_ && target_ != nullptr)
    {
        lastError_ = "Cannot resize the preview target while it is active on the renderer.";
        return false;
    }
    SDL_Texture *replacement = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                                 SDL_TEXTUREACCESS_TARGET, width, height);
    if (replacement == nullptr)
    {
        lastError_ = std::string("Preview target allocation failed: ") + SDL_GetError();
        return false;
    }
    if (!SDL_SetTextureBlendMode(replacement, SDL_BLENDMODE_BLEND))
    {
        lastError_ = std::string("Preview target blend setup failed: ") + SDL_GetError();
        SDL_DestroyTexture(replacement);
        return false;
    }
    SDL_Texture *previous = target_;
    target_ = replacement;
    targetWidth_ = width;
    targetHeight_ = height;
    invalidate_render_cache();
    if (previous != nullptr)
        SDL_DestroyTexture(previous);
    return true;
}

void EditorPreview::invalidate_render_cache() noexcept
{
    sceneImageDirty_ = true;
}

bool EditorPreview::render(float cameraX, float cameraY, float worldWidth, float worldHeight,
                           int pixelWidth, int pixelHeight,
                           const std::unordered_set<int> *hiddenObjectIds)
{
    if (pixelWidth <= 0 || pixelHeight <= 0 || !std::isfinite(worldWidth) || !std::isfinite(worldHeight) ||
        worldWidth <= 0.0f || worldHeight <= 0.0f)
        return false;
    if (!ensure_target(pixelWidth, pixelHeight))
        return false;
    static const std::unordered_set<int> noHiddenObjects;
    const auto &hidden = hiddenObjectIds == nullptr ? noHiddenObjects : *hiddenObjectIds;
    if (!sceneImageDirty_ && hasRenderedView_ && renderedRenderer_ == renderer_ &&
        renderedTarget_ == target_ && renderedCameraX_ == cameraX && renderedCameraY_ == cameraY &&
        renderedWorldWidth_ == worldWidth && renderedWorldHeight_ == worldHeight &&
        renderedPixelWidth_ == pixelWidth && renderedPixelHeight_ == pixelHeight &&
        renderedHiddenObjectIds_ == hidden)
    {
        work::record(work::Counter::SceneCacheHits);
        return true;
    }
    RendererState state(renderer_);
    if (!state.valid())
    {
        lastError_ = std::string("Could not capture SDL renderer state: ") + SDL_GetError();
        return false;
    }
    bool ok = SDL_SetRenderTarget(renderer_, target_);
    ok = SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED) && ok;
    ok = SDL_SetRenderViewport(renderer_, nullptr) && ok;
    ok = SDL_SetRenderClipRect(renderer_, nullptr) && ok;
    ok = SDL_SetRenderScale(renderer_, 1.0f, 1.0f) && ok;
    ok = SDL_SetRenderColorScale(renderer_, 1.0f) && ok;
    ok = SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE) && ok;
    ok = SDL_SetRenderDrawColor(renderer_, 30, 32, 36, 255) && ok;
    if (!ok || !SDL_RenderClear(renderer_))
    {
        lastError_ = std::string("Could not prepare the preview target: ") + SDL_GetError();
        state.restore();
        return false;
    }
    work::record(work::Counter::SceneCaptures);

    Camera fallbackCamera;
    Camera *camera = runtime_ != nullptr ? runtime_->getMainCamera() : nullptr;
    if (camera == nullptr)
        camera = &fallbackCamera;
    // EFFICIENCY: submitting O(renderable proxies) every Scene frame preserves
    // document draw order. A dirty view currently checks every proxy in O(N) and
    // rejects only sprites whose conservative rotated bounds are wholly outside it;
    // retain this ordered pass until matched mostly-offscreen measurements justify
    // a spatial index with overlap, parenting, resize, and asset-change regressions.
    const float viewRight = cameraX + worldWidth;
    const float viewBottom = cameraY + worldHeight;
    for (const int documentId : orderedDocumentIds_)
    {
        if (hidden.contains(documentId))
            continue;
        const auto proxy = proxies_.find(documentId);
        if (proxy == proxies_.end() || proxy->second.object == nullptr || proxy->second.sprite == nullptr ||
            !proxy->second.info.active || !proxy->second.info.textureAvailable)
            continue;
        work::record(work::Counter::SceneCullTests);
        const auto &info = proxy->second.info;
        const float halfWidth = std::abs(info.width) * 0.5f;
        const float halfHeight = std::abs(info.height) * 0.5f;
        const float radians = info.rotationDegrees * std::numbers::pi_v<float> / 180.0f;
        const float extentX = std::abs(std::cos(radians)) * halfWidth +
                              std::abs(std::sin(radians)) * halfHeight;
        const float extentY = std::abs(std::sin(radians)) * halfWidth +
                              std::abs(std::cos(radians)) * halfHeight;
        if (std::isfinite(viewRight) && std::isfinite(viewBottom) &&
            std::isfinite(info.worldX) && std::isfinite(info.worldY) &&
            std::isfinite(extentX) && std::isfinite(extentY) &&
            (info.worldX + extentX < cameraX || info.worldX - extentX > viewRight ||
             info.worldY + extentY < cameraY || info.worldY - extentY > viewBottom))
        {
            work::record(work::Counter::SceneCulledSprites);
            continue;
        }
        work::record(work::Counter::SceneSubmissions);
        platformator_editor_detail::EditorPreviewAccess::render_sprite(
            *camera, *proxy->second.sprite, renderer_, cameraX, cameraY,
            worldWidth, worldHeight, pixelWidth, pixelHeight);
    }
    const bool restored = state.restore();
    if (!restored)
    {
        lastError_ = std::string("Could not restore SDL renderer state: ") + SDL_GetError();
        return false;
    }
    sceneImageDirty_ = false;
    hasRenderedView_ = true;
    renderedRenderer_ = renderer_;
    renderedTarget_ = target_;
    renderedCameraX_ = cameraX;
    renderedCameraY_ = cameraY;
    renderedWorldWidth_ = worldWidth;
    renderedWorldHeight_ = worldHeight;
    renderedPixelWidth_ = pixelWidth;
    renderedPixelHeight_ = pixelHeight;
    renderedHiddenObjectIds_ = hidden;
    if (renderRevision_ == std::numeric_limits<std::uint64_t>::max())
        renderRevision_ = 1;
    else
        ++renderRevision_;
    lastError_.clear();
    return true;
}

SDL_Texture *EditorPreview::target() const noexcept { return target_; }
std::uint64_t EditorPreview::render_revision() const noexcept { return renderRevision_; }

const std::vector<int> &EditorPreview::ordered_document_ids() const noexcept
{
    return orderedDocumentIds_;
}

const std::vector<int> &EditorPreview::ordered_authoring_ids() const noexcept
{
    return authoringIndex_.ordered_ids();
}

const PreviewObjectInfo *EditorPreview::object_info(int documentId) const noexcept
{
    const auto found = authoringInfos_.find(documentId);
    return found == authoringInfos_.end() ? nullptr : &found->second;
}

const std::string &EditorPreview::last_error() const noexcept { return lastError_; }

PreviewResourceCounts EditorPreview::resource_counts() const
{
    PreviewResourceCounts counts;
    counts.proxies = proxies_.size();
    counts.renderTargetAllocated = target_ != nullptr;
    std::unordered_set<TextureWrapper *> textures;
    for (const auto &[id, proxy] : proxies_)
    {
        (void)id;
        if (proxy.sprite == nullptr || !proxy.info.textureAvailable)
            continue;
        ++counts.texturedProxies;
        if (auto *texture = proxy.sprite->getTextureWrapper())
            textures.insert(texture);
    }
    counts.uniqueTextureWrappers = textures.size();
    for (const auto *texture : textures)
        counts.textureWrapperReferences += texture->getReferenceCount();
    return counts;
}

#ifndef NDEBUG
PreviewReconcileStats EditorPreview::last_reconcile_stats() const noexcept
{
    return lastReconcileStats_;
}
#endif

void EditorPreview::release() noexcept
{
    sceneImageDirty_ = true;
    hasRenderedView_ = false;
    renderedRenderer_ = nullptr;
    renderedTarget_ = nullptr;
    renderedHiddenObjectIds_.clear();
    for (auto &[id, proxy] : proxies_)
    {
        (void)id;
        destroy_proxy(proxy);
    }
    proxies_.clear();
    if (resourceIndex_ != nullptr && resourceDocument_)
        resourceIndex_->unregister_document(*resourceDocument_);
    resourceDocument_.reset();
    orderedDocumentIds_.clear();
    authoringIndex_.clear();
    authoringInfos_.clear();
    authoringUpdateIds_.clear();
    if (renderer_ != nullptr && target_ != nullptr && SDL_GetRenderTarget(renderer_) == target_)
        SDL_SetRenderTarget(renderer_, nullptr);
    if (target_ != nullptr)
    {
        SDL_DestroyTexture(target_);
        target_ = nullptr;
    }
    targetWidth_ = 0;
    targetHeight_ = 0;
    documentGeneration_ = 0;
    structureRevision_ = 0;
}
} // namespace platformator_editor

namespace platformator_editor_detail
{
void EditorPreviewAccess::set_asset_reload_queue_limit_for_testing(
    platformator_editor::EditorPreview &preview, std::size_t limit)
{
    if (!preview.assetReloadState_)
        return;
    std::lock_guard lock(preview.assetReloadState_->mutex);
    preview.assetReloadState_->pendingLimit = limit;
}

void EditorPreviewAccess::set_proxy_texture_budget_for_testing(
    platformator_editor::EditorPreview &preview, std::uint64_t limit)
{
    preview.proxyTextureBudgetBytes_ = limit;
}

void EditorPreviewAccess::publish_proxy_position(GameObject &object, float x, float y)
{
    auto &manager = platformator_detail::RuntimeAccess::gameManager();
    if (manager.mainThread != std::this_thread::get_id() || manager.phase != GameManager::Phase::Idle)
        throw std::logic_error("preview proxy transforms must be published on the idle Runtime owner thread");
    object.setPosition(Eigen::Vector2f(x, y));
    manager.synchronizeTransforms();
}

void EditorPreviewAccess::render_sprite(Camera &camera, Sprite &sprite, SDL_Renderer *renderer,
                                        float cameraX, float cameraY, float worldWidth, float worldHeight,
                                        int outputWidth, int outputHeight)
{
    const platformator_detail::SpriteRenderView view{
        cameraX, cameraY, static_cast<float>(outputWidth) / worldWidth,
        static_cast<float>(outputHeight) / worldHeight, outputWidth, outputHeight, false};
    camera.renderWithView(&sprite, renderer, view, false);
}
} // namespace platformator_editor_detail
