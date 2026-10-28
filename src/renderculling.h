#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "platformator/spatialsettings.h"

class Camera;
class SDL_Renderer;
class Sprite;
class SDLWindow;

namespace platformator_detail
{
struct RenderBounds
{
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;

    bool isFiniteOrdered() const;
    bool overlapsInclusive(const RenderBounds &other) const;
};

struct VisibilityCandidate
{
    size_t recordIndex = 0;
    size_t windowIndex = 0;
    bool operator==(const VisibilityCandidate &) const = default;
};

struct RenderCullingFrameStats
{
    size_t candidateCount = 0;
    size_t visibleCount = 0;
    size_t submittedCount = 0;
    size_t indexProxyCount = 0;
    size_t visibilityQueries = 0;
    size_t maintenanceOperations = 0;
    size_t boundsPrepared = 0;
    size_t rawDiscoveries = 0;
    size_t exactChecks = 0;
    size_t uncullableCount = 0;
    size_t visibilityCells = 0;
    size_t proxyInserts = 0;
    size_t proxyRemoves = 0;
    size_t proxyMoves = 0;
    size_t rebuildCalls = 0;
    size_t rebuildItems = 0;
    size_t treeHeight = 0;
    size_t treeNodes = 0;
    uint64_t boundsPrepareNanoseconds = 0;
    uint64_t indexUpdateNanoseconds = 0;
    uint64_t indexMaintenanceNanoseconds = 0;
    uint64_t indexQueryNanoseconds = 0;
    uint64_t resultReduceNanoseconds = 0;
    uint64_t geometryRevision = 0;
    uint64_t viewRevision = 0;
    uint64_t drawOrderRevision = 0;
    uint64_t outputOrderHash = 0;
    bool resultCacheHit = false;
    bool visibilityKnown = false;
    std::string fallbackReason;
};

struct RenderCullingSnapshotSprite
{
    Sprite *sprite = nullptr;
    double centerX = 0.0;
    double centerY = 0.0;
    double width = 0.0;
    double height = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double angleDegrees = 0.0;
};

struct RenderCullingSnapshotView
{
    double positionX = 0.0;
    double positionY = 0.0;
    double width = 0.0;
    double height = 0.0;
    int outputWidth = 0;
    int outputHeight = 0;
};

class RenderCulling
{
  public:
    RenderCulling(SDLWindow *window, SpatialSettings settings, uint64_t runtimeSerial);
    ~RenderCulling();
    RenderCulling(const RenderCulling &) = delete;
    RenderCulling &operator=(const RenderCulling &) = delete;

    void addSprite(Sprite *sprite);
    void removeSprite(Sprite *sprite);
    void markSpriteDirty(Sprite *sprite);
    void setCamera(Camera *camera);
    const std::vector<Sprite *> &prepare(Camera *camera, int outputWidth, int outputHeight,
                                         SDL_Renderer *renderer, const std::vector<Sprite *> &drawOrder,
                                         uint64_t geometryRevision, uint64_t viewRevision,
                                         uint64_t drawOrderRevision);
    // Numeric headless entry used by replay. Inputs are already allocated inert Sprite records;
    // it applies the same geometry, proxy, draw-order, query and result-cache machinery as prepare().
    const std::vector<Sprite *> &prepareSnapshot(Camera *cameraIdentity, const RenderCullingSnapshotView &view,
                                                 const std::vector<Sprite *> &drawOrder,
                                                 uint64_t geometryRevision, uint64_t viewRevision,
                                                 uint64_t drawOrderRevision);
    void updateSnapshotSprite(const RenderCullingSnapshotSprite &input);
    bool isSnapshotSpriteUncullable(Sprite *sprite) const;
    const RenderCullingFrameStats &getFrameStats() const;
    size_t getOwnedCapacityBytes() const;

    static bool computeSpriteWorldBounds(const Sprite &sprite, const Camera &camera, int outputWidth, int outputHeight,
                                         RenderBounds &bounds);
    static bool computeSpriteWorldBounds(float spriteCenterX, float spriteCenterY, float renderedWidth,
                                         float renderedHeight, float angleDegrees, float cameraX, float cameraY,
                                         float cameraWidth, float cameraHeight, int outputWidth, int outputHeight,
                                         RenderBounds &bounds);
    static bool computeCameraWorldBounds(const Camera &camera, int outputWidth, int outputHeight,
                                         RenderBounds &bounds);
    static bool computeCameraWorldBounds(float cameraX, float cameraY, float cameraWidth, float cameraHeight,
                                         int outputWidth, int outputHeight, RenderBounds &bounds);
    static void sortByDrawOrder(std::vector<VisibilityCandidate> &candidates);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace platformator_detail
