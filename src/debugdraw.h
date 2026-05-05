#pragma once

#include "buildconfig.h"

#include <list>
#include <string>
#include <vector>

#include "debugsettings.h"
#include "collider.h"
#include "boxcollider.h"
#include "circlecollider.h"
#include "collision.h"
#include "camera.h"

struct GridCellKey;

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS

class DebugObject
{
public:
    DebugObject(std::vector<Eigen::Vector2f> vertices, bool closed = true, std::string name = "");
    DebugObject(std::vector<Eigen::Vector2f> vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool closed = true, std::string name = "");
    ~DebugObject();

    std::vector<Eigen::Vector2f> vertices;
    Uint8 r, g, b, a;
    bool closed;
    std::string name;
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

    void addDebugObject(const std::vector<Eigen::Vector2f> &vertices, std::string name = "");
    void addDebugObject(const std::vector<Eigen::Vector2f> &vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool closed = true, std::string name = "");
    void addDebugObject(std::vector<Eigen::Vector2f> &&vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool closed, std::string name = "");

    void addBoxColliderDebugObject(const BoxCollider &collider);
    void addCircleColliderDebugObject(const CircleCollider &collider);
    void addGridCellDebugObject(const GridCellKey &cellKey);
    void addCollisionDebugObject(const Collision &collision);
    void addPointDebugObject(const Eigen::Vector2f &point, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool showLabel = false, std::string name = "");
    void addEdgeDebugObject(const Eigen::Vector2f &start, const Eigen::Vector2f &end, Uint8 r, Uint8 g, Uint8 b, Uint8 a, std::string name = "");
    void setSettings(const DebugSettings &debugSettings);

    void clearDebugObjects();

    void toggleShowColliders();
    void toggleShowCollisionPoints();
    void toggleShowCollisionNormals();
    void toggleShowGridCells();

private:
    DebugDraw();
    ~DebugDraw();

    void render(SDL_Renderer *renderer, Camera *camera, int outputWidth, int outputHeight);

    void drawBoxCollider(SDL_Renderer *renderer, Camera *camera, const BoxCollider *collider);
    void drawCircleCollider(SDL_Renderer *renderer, Camera *camera, const CircleCollider *collider);
    void drawContactPoint(SDL_Renderer *renderer, Camera *camera, const Collision *collision);
    void drawNormal(SDL_Renderer *renderer, Camera *camera, const Collision *collision);

    void drawCircle(SDL_Renderer *renderer, int centerX, int centerY, int radius);
    void drawCross(SDL_Renderer *renderer, int centerX, int centerY, int size);

    std::vector<DebugObject> debugObjectsBuffer;

    bool showColliders;
    bool showCollisionPoints;
    bool showCollisionNormals;
    bool showGridCells;

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

    void addDebugObject(const std::vector<Eigen::Vector2f> &, std::string = "") {}
    void addDebugObject(const std::vector<Eigen::Vector2f> &, Uint8, Uint8, Uint8, Uint8, bool = true, std::string = "") {}
    void addDebugObject(std::vector<Eigen::Vector2f> &&, Uint8, Uint8, Uint8, Uint8, bool, std::string = "") {}

    void addBoxColliderDebugObject(const BoxCollider &) {}
    void addCircleColliderDebugObject(const CircleCollider &) {}
    void addGridCellDebugObject(const GridCellKey &) {}
    void addCollisionDebugObject(const Collision &) {}
    void addPointDebugObject(const Eigen::Vector2f &, Uint8, Uint8, Uint8, Uint8, bool = false, std::string = "") {}
    void addEdgeDebugObject(const Eigen::Vector2f &, const Eigen::Vector2f &, Uint8, Uint8, Uint8, Uint8, std::string = "") {}
    void setSettings(const DebugSettings &) {}

    void clearDebugObjects() {}

    void toggleShowColliders() {}
    void toggleShowCollisionPoints() {}
    void toggleShowCollisionNormals() {}
    void toggleShowGridCells() {}

private:
    DebugDraw() = default;
    ~DebugDraw() = default;

    void render(SDL_Renderer *, Camera *, int, int) {}

    friend class SDLWindow;
};

#endif
