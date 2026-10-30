#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL_render.h>

#include "document.h"

class GameObject;
class Sprite;
class AnimationClip;
class Camera;

namespace platformator
{
class Runtime;
}

namespace platformator_editor
{
enum class PreviewColliderShape
{
    None,
    Box,
    Circle
};

struct PreviewObjectInfo
{
    int documentId = 0;
    float worldX = 0.0f;
    float worldY = 0.0f;
    float localScaleX = 1.0f;
    float localScaleY = 1.0f;
    float rotationDegrees = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool active = true;
    bool hasSprite = false;
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
    std::size_t visitedObjects = 0;
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


class EditorPreview
{
  public:
    explicit EditorPreview(platformator::Runtime &runtime);
    ~EditorPreview();
    EditorPreview(const EditorPreview &) = delete;
    EditorPreview &operator=(const EditorPreview &) = delete;

    void reconcile(const DocumentSession &document, bool structureChanged,
                   const std::vector<int> &changedObjectIds = {});
    void refresh_assets(const DocumentSession &document);
    bool render(float cameraX, float cameraY, float worldWidth, float worldHeight,
                int pixelWidth, int pixelHeight);
    SDL_Texture *target() const noexcept;
    const std::vector<int> &ordered_document_ids() const noexcept;
    const PreviewObjectInfo *object_info(int documentId) const noexcept;
    const std::string &last_error() const noexcept;
    PreviewResourceCounts resource_counts() const;
#ifndef NDEBUG
    PreviewReconcileStats last_reconcile_stats() const noexcept;
#endif
    void release() noexcept;

  private:
    struct Proxy
    {
        GameObject *object = nullptr;
        Sprite *sprite = nullptr;
        AnimationClip *clip = nullptr;
        PreviewObjectInfo info;
        Json cachedObject;
        Json cachedSprite;
        Json cachedAnimator;
        bool hasCachedDocumentState = false;
        std::string failedAssetIdentity;
        std::string failedAssetMessage;
    };
    void rebuild(const DocumentSession &document);
    void update_proxy(const DocumentSession &document, int documentId, const Json &object);
    void update_proxy(const DocumentSession &document, int documentId, const Json &object,
                      float worldX, float worldY);
    void destroy_proxy(Proxy &proxy) noexcept;
    bool ensure_target(int width, int height);

    platformator::Runtime *runtime_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *target_ = nullptr;
    int targetWidth_ = 0;
    int targetHeight_ = 0;
    std::uint64_t documentGeneration_ = 0;
    std::uint64_t structureRevision_ = 0;
    std::filesystem::path assetRoot_;
    std::unordered_map<int, Proxy> proxies_;
    std::vector<int> orderedDocumentIds_;
    std::string lastError_;
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
    static void publish_proxy_position(GameObject &object, float x, float y);
    static void render_sprite(Camera &camera, Sprite &sprite, SDL_Renderer *renderer,
                              float cameraX, float cameraY, float worldWidth, float worldHeight,
                              int outputWidth, int outputHeight);
};
} // namespace platformator_editor_detail
