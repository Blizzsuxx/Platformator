#include "preview.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <utility>

#include <SDL3/SDL.h>

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

EditorPreview::EditorPreview(platformator::Runtime &runtime)
    : runtime_(&runtime), renderer_(runtime.getRenderer())
{
}

EditorPreview::~EditorPreview()
{
    release();
}

void EditorPreview::reconcile(const DocumentSession &document, bool structureChanged,
                             const std::vector<int> &changedObjectIds)
{
#ifndef NDEBUG
    lastReconcileStats_ = {};
#endif
    if (runtime_ == nullptr)
        return;
    const bool rootChanged = assetRoot_ != document.assetRoot;
    const bool generationChanged = documentGeneration_ != document.documentGeneration;
    if (rootChanged || generationChanged || structureChanged || structureRevision_ != document.structureRevision)
    {
        if (rootChanged || generationChanged)
            release();
        assetRoot_ = document.assetRoot;
        documentGeneration_ = document.documentGeneration;
        structureRevision_ = document.structureRevision;
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
        if (coveredByChangedAncestor)
            continue;

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
#endif
            if (proxies_.contains(objectId))
            {
                update_proxy(document, objectId, object, worldX, worldY);
#ifndef NDEBUG
                ++lastReconcileStats_.updatedProxies;
#endif
            }

            const auto children = object.find("children");
            if (children == object.end() || !children->is_array())
                continue;
            for (std::size_t index = children->size(); index > 0; --index)
                pending.push_back({&(*children)[index - 1], worldX, worldY});
        }
    }
}

void EditorPreview::refresh_assets(const DocumentSession &document)
{
    for (int id : orderedDocumentIds_)
    {
        auto found = proxies_.find(id);
        if (found == proxies_.end())
            continue;
        found->second.failedAssetIdentity.clear();
        found->second.failedAssetMessage.clear();
        found->second.hasCachedDocumentState = false;
        const auto objectLocation = document.indexes.objectsById.find(id);
        if (objectLocation == document.indexes.objectsById.end())
            continue;
        const Json *object = resolve_object(document.root, objectLocation->second);
        if (object != nullptr)
            update_proxy(document, id, *object);
    }
}

void EditorPreview::rebuild(const DocumentSession &document)
{
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
    const Json animatorData = component_of_type(objectData, 0) == nullptr
                                  ? Json::object()
                                  : *component_of_type(objectData, 0);
    const bool documentStateUnchanged = proxy.hasCachedDocumentState && proxy.cachedObject == objectData &&
                                        proxy.cachedSprite == *spriteData &&
                                        proxy.cachedAnimator == animatorData;

    proxy.info.documentId = documentId;
    proxy.info.worldX = worldX;
    proxy.info.worldY = worldY;
    proxy.info.localScaleX = nested_number(objectData, "scale", "x", 1.0f);
    proxy.info.localScaleY = nested_number(objectData, "scale", "y", 1.0f);
    proxy.info.rotationDegrees = finite_number(objectData, "rotation", 0.0f) * 180.0f /
                                 std::numbers::pi_v<float>;
    proxy.info.active = boolean_value(objectData, "active", true);
    proxy.info.hasSprite = true;

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
    proxy.object->setRotation(finite_number(objectData, "rotation", 0.0f));
    platformator_editor_detail::EditorPreviewAccess::publish_proxy_position(*proxy.object, worldX, worldY);

    if (documentStateUnchanged)
        return;

    proxy.info.assetError.clear();
    proxy.info.textureAvailable = false;

    const std::string spritePath = string_value(*spriteData, "textureFilePath");
    const std::string animationPath = string_value(animatorData, "animationClipFilePath");
    const int frameIndex = integer_value(animatorData, "currentFrameIndex", 0);
    const std::string assetIdentity = animationPath + "#" + std::to_string(frameIndex) + "|" + spritePath;
    if (!proxy.failedAssetIdentity.empty() && proxy.failedAssetIdentity == assetIdentity)
    {
        proxy.info.assetError = proxy.failedAssetMessage;
        proxy.cachedObject = objectData;
        proxy.cachedSprite = *spriteData;
        proxy.cachedAnimator = animatorData;
        proxy.hasCachedDocumentState = true;
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
    float width = finite_number(*spriteData, "width", 0.0f);
    float height = finite_number(*spriteData, "height", 0.0f);
    int flip = integer_value(*spriteData, "flip", static_cast<int>(SDL_FLIP_NONE));
    try
    {
        if (!animationPath.empty())
        {
            if (proxy.clip == nullptr || proxy.clip->getFilePath() != animationPath)
            {
                if (proxy.clip != nullptr)
                {
                    proxy.clip->removeReferenceAndFreeIfNoReferences();
                    proxy.clip = nullptr;
                }
                proxy.clip = runtime_->loadAnimationClip(animationPath);
                if (proxy.clip != nullptr)
                    proxy.clip->addReference();
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
            texture = runtime_->loadTexture(spritePath);
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
    else if (boolean_value(*spriteData, "sourceRectEnabled", false))
    {
        const Json rectangle = spriteData->value("sourceRect", Json::object());
        SDL_FRect rect{finite_number(rectangle, "x", 0.0f), finite_number(rectangle, "y", 0.0f),
                       finite_number(rectangle, "w", 0.0f), finite_number(rectangle, "h", 0.0f)};
        proxy.sprite->setSourceRect(rect);
    }
    else
        proxy.sprite->clearSourceRect();
    proxy.sprite->setWidth(width);
    proxy.sprite->setHeight(height);
    proxy.info.width = width * proxy.info.localScaleX;
    proxy.info.height = height * proxy.info.localScaleY;
    proxy.info.textureAvailable = proxy.sprite->getTexture() != nullptr;

    if (const Json *collider = component_of_type(objectData, 3))
    {
        proxy.info.colliderShape = integer_value(*collider, "colliderType", 1) == 0
                                       ? PreviewColliderShape::Circle
                                       : PreviewColliderShape::Box;
        proxy.info.colliderWidth = finite_number(*collider, "width", 0.0f) * proxy.info.localScaleX;
        proxy.info.colliderHeight = finite_number(*collider, "height", 0.0f) * proxy.info.localScaleY;
        proxy.info.colliderRadius = finite_number(*collider, "radius", 0.0f) *
                                    std::max(std::abs(proxy.info.localScaleX), std::abs(proxy.info.localScaleY));
        proxy.info.colliderOffsetX = nested_number(*collider, "offset", "x", 0.0f);
        proxy.info.colliderOffsetY = nested_number(*collider, "offset", "y", 0.0f);
    }
    else
        proxy.info.colliderShape = PreviewColliderShape::None;

    proxy.cachedObject = objectData;
    proxy.cachedSprite = *spriteData;
    proxy.cachedAnimator = animatorData;
    proxy.hasCachedDocumentState = true;
}

void EditorPreview::destroy_proxy(Proxy &proxy) noexcept
{
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

bool EditorPreview::ensure_target(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;
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
    if (previous != nullptr)
        SDL_DestroyTexture(previous);
    return true;
}

bool EditorPreview::render(float cameraX, float cameraY, float worldWidth, float worldHeight,
                           int pixelWidth, int pixelHeight)
{
    if (pixelWidth <= 0 || pixelHeight <= 0 || !std::isfinite(worldWidth) || !std::isfinite(worldHeight) ||
        worldWidth <= 0.0f || worldHeight <= 0.0f)
        return false;
    if (!ensure_target(pixelWidth, pixelHeight))
        return false;
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

    Camera fallbackCamera;
    Camera *camera = runtime_ != nullptr ? runtime_->getMainCamera() : nullptr;
    if (camera == nullptr)
        camera = &fallbackCamera;
    // EFFICIENCY: submitting O(renderable proxies) every Scene frame preserves
    // document draw order and avoids coupling this nondefault target to the
    // main-camera culling cache. Keep the linear pass until matched mostly-offscreen
    // measurements show meaningful CPU cost; then evaluate a view-aware candidate
    // index with correctness checks for overlap, parenting, resize, and asset changes.
    for (const int documentId : orderedDocumentIds_)
    {
        const auto proxy = proxies_.find(documentId);
        if (proxy == proxies_.end() || proxy->second.object == nullptr || proxy->second.sprite == nullptr ||
            !proxy->second.info.active || !proxy->second.info.textureAvailable)
            continue;
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
    lastError_.clear();
    return true;
}

SDL_Texture *EditorPreview::target() const noexcept { return target_; }

const std::vector<int> &EditorPreview::ordered_document_ids() const noexcept
{
    return orderedDocumentIds_;
}

const PreviewObjectInfo *EditorPreview::object_info(int documentId) const noexcept
{
    const auto found = proxies_.find(documentId);
    return found == proxies_.end() ? nullptr : &found->second.info;
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
    for (auto &[id, proxy] : proxies_)
    {
        (void)id;
        destroy_proxy(proxy);
    }
    proxies_.clear();
    orderedDocumentIds_.clear();
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
