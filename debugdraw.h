#pragma once

#include <SDL2/SDL.h>
#include <list>
#include "collider.h"
#include "boxcollider.h"
#include "circlecollider.h"
#include "collision.h"
#include "camera.h"

class DebugObject
{
public:
    DebugObject(std::vector<Eigen::Vector2f> vertices);
    DebugObject(std::vector<Eigen::Vector2f> vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool closed = true);
    ~DebugObject();

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

    void addDebugObject(const std::vector<Eigen::Vector2f> &vertices);
    void addDebugObject(const std::vector<Eigen::Vector2f> &vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool closed = true);
    void addDebugObject(std::vector<Eigen::Vector2f> &&vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool closed);

    void addBoxColliderDebugObject(const BoxCollider &collider);
    void addCircleColliderDebugObject(const CircleCollider &collider);
    void addCollisionDebugObject(const Collision &collision);

    void clearDebugObjects();

    void toggleShowColliders();
    void toggleShowCollisionPoints();
    void toggleShowCollisionNormals();

private:
    DebugDraw();
    ~DebugDraw();

    void render(SDL_Renderer *renderer, Camera *camera);

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

    friend class SDLWindow;
};
