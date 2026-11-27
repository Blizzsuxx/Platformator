#pragma once

#include "buildconfig.h"

#include <chrono>
#include <vector>

#include "platformator/debugsettings.h"
#include "platformator/collider.h"
#include "platformator/boxcollider.h"
#include "platformator/circlecollider.h"
#include "platformator/collision.h"
#include "platformator/camera.h"

struct GridCellKey;

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

    DebugDraw(DebugDraw &) = delete;
    DebugDraw &operator=(const DebugDraw &) = delete;

    void addDebugObject(const std::vector<Eigen::Vector2f> &vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a,
                        bool closed = true);
    void addDebugObject(std::vector<Eigen::Vector2f> &&vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool closed);

    void addBoxColliderDebugObject(const BoxCollider &collider);
    void addCircleColliderDebugObject(const CircleCollider &collider);
    void addGridCellDebugObject(const GridCellKey &cellKey);
    void addCollisionDebugObject(const Collision &collision);
    void setSettings(const DebugSettings &debugSettings);

    void clearDebugObjects();

    void toggleShowColliders();
    void toggleShowCollisionPoints();
    void toggleShowCollisionNormals();
    void toggleShowGridCells();
    void toggleShowFps();

  private:
    DebugDraw();
    ~DebugDraw();

    void render(SDL_Renderer *renderer, Camera *camera, int outputWidth, int outputHeight);
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

    DebugDraw(DebugDraw &) = delete;
    DebugDraw &operator=(const DebugDraw &) = delete;

    void addDebugObject(const std::vector<Eigen::Vector2f> &, Uint8, Uint8, Uint8, Uint8, bool = true)
    {
    }
    void addDebugObject(std::vector<Eigen::Vector2f> &&, Uint8, Uint8, Uint8, Uint8, bool)
    {
    }

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

  private:
    DebugDraw() = default;
    ~DebugDraw() = default;

    void render(SDL_Renderer *, Camera *, int, int)
    {
    }

    friend class SDLWindow;
};

#endif
