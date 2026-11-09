#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <SDL3/SDL_render.h>

#include "authoringindex.h"
#include "document.h"
#include "resourceindex.h"

class GameObject;
class Sprite;
class AnimationClip;
class Camera;

namespace platformator
{
class Runtime;
}

namespace platformator_editor_detail
{
class EditorPreviewAccess;
}

namespace platformator_editor
{
enum class PreviewColliderShape
{
    None,
    Box,
    Circle
};

struct PreviewBounds
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct PreviewObjectInfo
{
    int documentId = 0;
    std::array<int, COMPONENT_TYPE_COUNT> componentIds{};
    float worldX = 0.0f;
    float worldY = 0.0f;
    float localScaleX = 1.0f;
    float localScaleY = 1.0f;
    float rotationDegrees = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    PreviewBounds bounds{};
    PreviewBounds nonSpriteBounds{};
    bool active = true;
    bool hasSprite = false;
    bool hasCamera = false;
    bool emptyMarker = false;
    bool colliderPlaceholder = false;
    float cameraWidth = 0.0f;
    float cameraHeight = 0.0f;
    bool textureAvailable = false;
    PreviewColliderShape colliderShape = PreviewColliderShape::None;
    float colliderWidth = 0.0f;
    float colliderHeight = 0.0f;
    float colliderRadius = 0.0f;
    float colliderOffsetX = 0.0f;
    float colliderOffsetY = 0.0f;
    std::string assetError;
};

struct PreviewReconcileStats
{
    // Total descriptor/proxy object visits performed for the latest reconciliation.
    std::size_t visitedObjects = 0;
    std::size_t visitedAuthoringObjects = 0;
    std::size_t visitedProxyObjects = 0;
    std::size_t updatedProxies = 0;
};

struct PreviewResourceCounts
{
    std::size_t proxies = 0;
    std::size_t texturedProxies = 0;
    std::size_t uniqueTextureWrappers = 0;
    std::size_t textureWrapperReferences = 0;
    bool renderTargetAllocated = false;
};

inline constexpr std::uint64_t ScenePreviewMaxProxyTextureBytes = 256ULL * 1024ULL * 1024ULL;

class EditorPreview
{
  public:
    struct AssetReloadState;

    explicit EditorPreview(platformator::Runtime &runtime);
    ~EditorPreview();
    EditorPreview(const EditorPreview &) = delete;
    EditorPreview &operator=(const EditorPreview &) = delete;

    void reconcile(const DocumentSession &document, bool structureChanged,
                   const std::vector<int> &changedObjectIds = {},
                   bool inheritedTranslationChanged = true);
    void set_resource_index(ResourceIndex *resourceIndex) noexcept;
    void set_wake_callback(std::function<void()> callback);
    void refresh_asset(const DocumentSession &document, const std::filesystem::path &changedPath,
                       bool directory, const std::vector<ResourceConsumer> &consumers);
    std::size_t retry_failed_assets(const DocumentSession &document);
    void poll_asset_reloads(const DocumentSession &document);
    bool asset_preparation_pending() const noexcept;
    void invalidate_render_cache() noexcept;
    bool render(float cameraX, float cameraY, float worldWidth, float worldHeight,
                int pixelWidth, int pixelHeight,
                const std::unordered_set<int> *hiddenObjectIds = nullptr);
    SDL_Texture *target() const noexcept;
    std::uint64_t render_revision() const noexcept;
    const std::vector<int> &ordered_document_ids() const noexcept;
    const std::vector<int> &ordered_authoring_ids() const noexcept;
    const PreviewObjectInfo *object_info(int documentId) const noexcept;
    const std::string &last_error() const noexcept;
    PreviewResourceCounts resource_counts() const;
#ifndef NDEBUG
    PreviewReconcileStats last_reconcile_stats() const noexcept;
#endif
    void release() noexcept;

  private:
    friend class ::platformator_editor_detail::EditorPreviewAccess;
    struct Proxy
    {
        struct SpriteInputs
        {
            std::string texturePath;
            float width = 0.0f;
            float height = 0.0f;
            int flip = 0;
            bool sourceRectEnabled = false;
            float sourceRectX = 0.0f;
            float sourceRectY = 0.0f;
            float sourceRectWidth = 0.0f;
            float sourceRectHeight = 0.0f;
            bool operator==(const SpriteInputs &) const = default;
        };
        struct AnimatorInputs
        {
            std::string clipPath;
            int frameIndex = 0;
            bool operator==(const AnimatorInputs &) const = default;
        };
        struct ColliderInputs
        {
            bool present = false;
            int type = -1;
            bool supportedType = false;
            bool validDimensions = false;
            float width = 0.0f;
            float height = 0.0f;
            float radius = 0.0f;
            float offsetX = 0.0f;
            float offsetY = 0.0f;
            bool operator==(const ColliderInputs &) const = default;
        };

        GameObject *object = nullptr;
        Sprite *sprite = nullptr;
        AnimationClip *clip = nullptr;
        PreviewObjectInfo info;
        SpriteInputs cachedSpriteInputs;
        AnimatorInputs cachedAnimatorInputs;
        ColliderInputs cachedColliderInputs;
        float cachedPositionX = 0.0f;
        float cachedPositionY = 0.0f;
        float cachedScaleX = 1.0f;
        float cachedScaleY = 1.0f;
        float cachedRotation = 0.0f;
        bool cachedActive = true;
        bool hasCachedDocumentState = false;
        std::string failedAssetIdentity;
        std::string failedAssetMessage;
        std::string registeredSpritePath;
        std::string registeredClipPath;
        std::optional<ResourceConsumer> spriteConsumer;
        std::optional<ResourceConsumer> clipConsumer;
        std::vector<std::filesystem::path> frameLogicalPaths;
        std::vector<std::pair<std::filesystem::path, ResourceConsumer>> frameConsumers;
    };
    void rebuild(const DocumentSession &document);
    void rebuild_authoring_info(const DocumentSession &document);
    void update_authoring_info(const DocumentSession &document,
                               const std::vector<int> &updatedObjectIds);
    void update_proxy(const DocumentSession &document, int documentId, const Json &object);
    void update_proxy(const DocumentSession &document, int documentId, const Json &object,
                      float worldX, float worldY);
    void destroy_proxy(Proxy &proxy) noexcept;
    void clear_asset_consumers(Proxy &proxy) noexcept;
    void sync_asset_consumers(const DocumentSession &document, int documentId, Proxy &proxy,
                              const Json &spriteData, const Json &animatorData);
    void register_frame_consumers(const DocumentSession &document, int documentId, Proxy &proxy);
    bool ensure_target(int width, int height);

    platformator::Runtime *runtime_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *target_ = nullptr;
    int targetWidth_ = 0;
    int targetHeight_ = 0;
    std::uint64_t documentGeneration_ = 0;
    std::uint64_t structureRevision_ = 0;
    std::filesystem::path assetRoot_;
    ResourceIndex *resourceIndex_ = nullptr;
    std::unique_ptr<AssetReloadState> assetReloadState_;
    std::optional<DocumentIdentity> resourceDocument_;
    std::unordered_map<int, Proxy> proxies_;
    AuthoringIndex authoringIndex_;
    std::unordered_map<int, PreviewObjectInfo> authoringInfos_;
    std::vector<int> authoringUpdateIds_;
    std::vector<int> orderedDocumentIds_;
    std::string lastError_;
    std::uint64_t proxyTextureBudgetBytes_ = ScenePreviewMaxProxyTextureBytes;
    bool sceneImageDirty_ = true;
    bool hasRenderedView_ = false;
    std::uint64_t renderRevision_ = 0;
    SDL_Renderer *renderedRenderer_ = nullptr;
    SDL_Texture *renderedTarget_ = nullptr;
    float renderedCameraX_ = 0.0f;
    float renderedCameraY_ = 0.0f;
    float renderedWorldWidth_ = 0.0f;
    float renderedWorldHeight_ = 0.0f;
    int renderedPixelWidth_ = 0;
    int renderedPixelHeight_ = 0;
    std::unordered_set<int> renderedHiddenObjectIds_;
#ifndef NDEBUG
    PreviewReconcileStats lastReconcileStats_;
#endif
};
} // namespace platformator_editor

namespace platformator_editor_detail
{
class EditorPreviewAccess
{
  public:
    static void set_asset_reload_queue_limit_for_testing(platformator_editor::EditorPreview &preview,
                                                         std::size_t limit);
    static void set_proxy_texture_budget_for_testing(platformator_editor::EditorPreview &preview,
                                                     std::uint64_t limit);
    static void publish_proxy_position(GameObject &object, float x, float y);
    static void render_sprite(Camera &camera, Sprite &sprite, SDL_Renderer *renderer,
                              float cameraX, float cameraY, float worldWidth, float worldHeight,
                              int outputWidth, int outputHeight);
};
} // namespace platformator_editor_detail
