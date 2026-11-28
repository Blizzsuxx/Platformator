#pragma once

#include "buildconfig.h"

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
#include <chrono>
#endif
#include <vector>

#include <Eigen/Dense>
#include <SDL3/SDL.h>

#include "platformator/debugsettings.h"

class BoxCollider;
class CircleCollider;
class Collision;
struct GridCellKey;

namespace platformator_detail
{
struct SpriteRenderView;
}

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS

class DebugObject
{
  public:
    DebugObject(std::vector<Eigen::Vector2f> vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool closed = true);

    std::vector<Eigen::Vector2f> vertices;
    Uint8 r, g, b, a;
    bool closed;
};

class DebugDraw
{
  public:
    static DebugDraw &getInstance()
    {
        static DebugDraw instance;
        return instance;
    }

    DebugDraw(const DebugDraw &) = delete;
    DebugDraw &operator=(const DebugDraw &) = delete;

    void addBoxColliderDebugObject(const BoxCollider &collider);
    void addCircleColliderDebugObject(const CircleCollider &collider);
    void addGridCellDebugObject(const GridCellKey &cellKey);
    void addCollisionDebugObject(const Collision &collision);
    void collectPhysicsDebugObjects();
    void setSettings(const DebugSettings &debugSettings);

    void clearDebugObjects();

    void toggleShowColliders();
    void toggleShowCollisionPoints();
    void toggleShowCollisionNormals();
    void toggleShowGridCells();
    void toggleShowFps();

    bool isShowingColliders() const noexcept
    {
        return showColliders;
    }
    bool isShowingGridCells() const noexcept
    {
        return showGridCells;
    }

  private:
    DebugDraw();
    ~DebugDraw() = default;

    void addDebugObject(std::vector<Eigen::Vector2f> &&vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool closed);
    void resetFpsSampling(bool clearDisplayed) noexcept;
    void render(SDL_Renderer *renderer, const platformator_detail::SpriteRenderView &view);
    void renderFpsOverlay(SDL_Renderer *renderer);

    std::vector<DebugObject> debugObjectsBuffer;

    bool showColliders;
    bool showCollisionPoints;
    bool showCollisionNormals;
    bool showGridCells;
    bool showFps;

    bool hasLastFrameTime;
    std::chrono::steady_clock::time_point lastFrameTime;
    double fpsSampleAccumSeconds;
    int fpsSampleFrames;
    double displayedFps;
    double displayedFrameMs;

    friend class SDLWindow;
};

#else

class DebugDraw
{
  public:
    static DebugDraw &getInstance()
    {
        static DebugDraw instance;
        return instance;
    }

    DebugDraw(const DebugDraw &) = delete;
    DebugDraw &operator=(const DebugDraw &) = delete;

    void addBoxColliderDebugObject(const BoxCollider &)
    {
    }
    void addCircleColliderDebugObject(const CircleCollider &)
    {
    }
    void addGridCellDebugObject(const GridCellKey &)
    {
    }
    void addCollisionDebugObject(const Collision &)
    {
    }
    void collectPhysicsDebugObjects()
    {
    }
    void setSettings(const DebugSettings &)
    {
    }

    void clearDebugObjects()
    {
    }

    void toggleShowColliders()
    {
    }
    void toggleShowCollisionPoints()
    {
    }
    void toggleShowCollisionNormals()
    {
    }
    void toggleShowGridCells()
    {
    }
    void toggleShowFps()
    {
    }
    bool isShowingColliders() const noexcept
    {
        return false;
    }
    bool isShowingGridCells() const noexcept
    {
        return false;
    }

  private:
    DebugDraw() = default;
    ~DebugDraw() = default;

    void render(SDL_Renderer *, const platformator_detail::SpriteRenderView &)
    {
    }

    friend class SDLWindow;
};

#endif
