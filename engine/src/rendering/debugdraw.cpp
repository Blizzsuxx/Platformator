#include "rendering/debugdraw.h"
#include "core/constants.h"
#include "core/runtimeaccess.h"
#include "physics/broadphase/gridcell.h"
#include "physics/physicsmanager.h"
#include "platformator/boxcollider.h"
#include "platformator/circlecollider.h"
#include "platformator/collision.h"
#include "rendering/spriterendergeometry.h"
#include <cmath>
#include <cstdio>
#include <numbers>

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS

DebugDraw::DebugDraw()
    : debugObjectsBuffer(), showColliders(false), showCollisionPoints(false), showCollisionNormals(false),
      showGridCells(false), showFps(false), hasLastFrameTime(false), lastFrameTime(), fpsSampleAccumSeconds(0.0),
      fpsSampleFrames(0), displayedFps(0.0), displayedFrameMs(0.0)
{
}

void DebugDraw::resetFpsSampling(bool clearDisplayed) noexcept
{
    hasLastFrameTime = false;
    fpsSampleAccumSeconds = 0.0;
    fpsSampleFrames = 0;
    if (clearDisplayed)
    {
        displayedFps = 0.0;
        displayedFrameMs = 0.0;
    }
}

void DebugDraw::setSettings(const DebugSettings &debugSettings)
{
    showColliders = debugSettings.showColliders;
    showCollisionPoints = debugSettings.showCollisionPoints;
    showCollisionNormals = debugSettings.showCollisionNormals;
    showGridCells = debugSettings.showGridCells;
    showFps = debugSettings.showFps;
    resetFpsSampling(true);
}

void DebugDraw::render(SDL_Renderer *renderer, const platformator_detail::SpriteRenderView &view)
{
    SDL_BlendMode prevBlend;
    SDL_GetRenderDrawBlendMode(renderer, &prevBlend);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    const auto drawSegment = [&](const Eigen::Vector2f &v1, const Eigen::Vector2f &v2) {
        const float x1 = (v1.x() - view.cameraX) * view.scaleX;
        const float y1 = (v1.y() - view.cameraY) * view.scaleY;
        const float x2 = (v2.x() - view.cameraX) * view.scaleX;
        const float y2 = (v2.y() - view.cameraY) * view.scaleY;
        SDL_RenderLine(renderer, x1, y1, x2, y2);
    };

    for (const DebugObject &debugObject : debugObjectsBuffer)
    {
        const std::vector<Eigen::Vector2f> &vertices = debugObject.vertices;

        SDL_SetRenderDrawColor(renderer, debugObject.r, debugObject.g, debugObject.b, debugObject.a);

        if (debugObject.closed)
        {
            for (size_t i = 0; i < vertices.size(); i++)
            {
                drawSegment(vertices[i], vertices[(i + 1) % vertices.size()]);
            }
        }
        else
        {
            for (size_t i = 0; i + 1 < vertices.size(); i += 2)
            {
                drawSegment(vertices[i], vertices[i + 1]);
            }
        }
    }

    if (showFps)
    {
        renderFpsOverlay(renderer);
    }

    SDL_SetRenderDrawBlendMode(renderer, prevBlend);
}

void DebugDraw::renderFpsOverlay(SDL_Renderer *renderer)
{
    const auto now = std::chrono::steady_clock::now();
    if (hasLastFrameTime)
    {
        const double dtSeconds = std::chrono::duration<double>(now - lastFrameTime).count();
        if (dtSeconds > 0.0 && dtSeconds < 5.0)
        {
            fpsSampleAccumSeconds += dtSeconds;
            ++fpsSampleFrames;
            if (fpsSampleAccumSeconds >= 0.25)
            {
                displayedFrameMs = (fpsSampleAccumSeconds / static_cast<double>(fpsSampleFrames)) * 1000.0;
                displayedFps = static_cast<double>(fpsSampleFrames) / fpsSampleAccumSeconds;
                fpsSampleAccumSeconds = 0.0;
                fpsSampleFrames = 0;
            }
            else if (displayedFps == 0.0)
            {
                displayedFrameMs = dtSeconds * 1000.0;
                displayedFps = 1.0 / dtSeconds;
            }
        }
    }
    lastFrameTime = now;
    hasLastFrameTime = true;

    char label[64];
    if (displayedFps > 0.0)
    {
        std::snprintf(label, sizeof(label), "FPS: %.1f (%.2f ms)", displayedFps, displayedFrameMs);
    }
    else
    {
        std::snprintf(label, sizeof(label), "FPS: ...");
    }

    float prevScaleX = 1.0f;
    float prevScaleY = 1.0f;
    SDL_GetRenderScale(renderer, &prevScaleX, &prevScaleY);
    Uint8 prevR = 0, prevG = 0, prevB = 0, prevA = 255;
    SDL_GetRenderDrawColor(renderer, &prevR, &prevG, &prevB, &prevA);

    constexpr float textScale = 2.0f;
    constexpr float padX = 6.0f;
    constexpr float padY = 4.0f;
    constexpr float boxX = 6.0f;
    constexpr float boxY = 6.0f;
    const float textW = static_cast<float>(std::char_traits<char>::length(label)) * 8.0f;
    const SDL_FRect bgRect{boxX, boxY, textW + padX * 2.0f, 8.0f + padY * 2.0f};

    SDL_SetRenderScale(renderer, textScale, textScale);
    SDL_SetRenderDrawColor(renderer, 0x10, 0x10, 0x14, 0xC8);
    SDL_RenderFillRect(renderer, &bgRect);
    SDL_SetRenderDrawColor(renderer, 0x60, 0x68, 0x78, 0xE0);
    SDL_RenderRect(renderer, &bgRect);

    if (displayedFps == 0.0 || displayedFps >= 55.0)
    {
        SDL_SetRenderDrawColor(renderer, 0x50, 0xFF, 0x78, 0xFF);
    }
    else if (displayedFps >= 30.0)
    {
        SDL_SetRenderDrawColor(renderer, 0xFF, 0xD4, 0x40, 0xFF);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer, 0xFF, 0x55, 0x55, 0xFF);
    }
    SDL_RenderDebugText(renderer, boxX + padX, boxY + padY, label);

    SDL_SetRenderScale(renderer, prevScaleX, prevScaleY);
    SDL_SetRenderDrawColor(renderer, prevR, prevG, prevB, prevA);
}

void DebugDraw::addDebugObject(std::vector<Eigen::Vector2f> &&vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool closed)
{
    debugObjectsBuffer.emplace_back(std::move(vertices), r, g, b, a, closed);
}

void DebugDraw::addBoxColliderDebugObject(const BoxCollider &collider)
{
    if (!showColliders)
    {
        return;
    }

    const auto &v = collider.getVertices();
    std::vector<Eigen::Vector2f> ordered = {v[0], v[1], v[2], v[3]};
    addDebugObject(std::move(ordered), 0x00, 0xFF, 0x00, 0xCC, true);
}

void DebugDraw::addCircleColliderDebugObject(const CircleCollider &collider)
{
    if (!showColliders)
    {
        return;
    }

    Eigen::Vector2f center = collider.getWorldPosition();
    float radius = collider.getRadius();

    std::vector<Eigen::Vector2f> vertices;
    const int numSegments = 36;
    for (int i = 0; i < numSegments; i++)
    {
        float angle = static_cast<float>(i) / numSegments * 2.0f * std::numbers::pi_v<float>;
        vertices.emplace_back(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
    }
    addDebugObject(std::move(vertices), 0x00, 0xFF, 0x00, 0xCC, true);

    // Rotation indicator line from center to edge
    float rot = collider.getGameObject()->getRotation();
    Eigen::Vector2f edge(center.x() + radius * std::cos(rot), center.y() + radius * std::sin(rot));
    addDebugObject({center, edge}, 0x00, 0xCC, 0x00, 0xCC, false);
}

void DebugDraw::addGridCellDebugObject(const GridCellKey &cellKey)
{
    if (!showGridCells)
    {
        return;
    }

    const auto *physics = platformator_detail::RuntimeAccess::physicsManager();
    const float cellSize = physics ? physics->getGridCellSize() : GRID_CELL_SIZE;
    float minX = static_cast<float>(cellKey.x) * cellSize;
    float minY = static_cast<float>(cellKey.y) * cellSize;
    float maxX = minX + cellSize;
    float maxY = minY + cellSize;

    std::vector<Eigen::Vector2f> vertices = {
        Eigen::Vector2f(minX, minY),
        Eigen::Vector2f(maxX, minY),
        Eigen::Vector2f(maxX, maxY),
        Eigen::Vector2f(minX, maxY)};

    addDebugObject(std::move(vertices), 0x30, 0x90, 0xFF, 0x55, true);
}

void DebugDraw::addCollisionDebugObject(const Collision &collision)
{
    if (!showCollisionNormals && !showCollisionPoints)
    {
        return;
    }

    const ClipPointsWithData &contactPoints = collision.getContactPoints();
    if (contactPoints.count == 0)
    {
        return;
    }

    auto normal = collision.getNormal();

    if (showCollisionPoints)
    {
        float size = 6.0f;
        for (size_t i = 0; i < contactPoints.count; ++i)
        {
            const Eigen::Vector2f &contactPoint = contactPoints.points[i].point.point;
            addDebugObject(
                std::vector<Eigen::Vector2f>{contactPoint + Eigen::Vector2f(-size, -size), contactPoint + Eigen::Vector2f(size, size),
                                             contactPoint + Eigen::Vector2f(-size, size), contactPoint + Eigen::Vector2f(size, -size)},
                0xFF, 0x20, 0x20, 0xFF, false);

            addDebugObject(
                std::vector<Eigen::Vector2f>{contactPoint + Eigen::Vector2f(0, -size), contactPoint + Eigen::Vector2f(size, 0),
                                             contactPoint + Eigen::Vector2f(0, size), contactPoint + Eigen::Vector2f(-size, 0)},
                0xFF, 0x60, 0x60, 0xFF, true);
        }
    }

    if (showCollisionNormals)
    {
        Eigen::Vector2f contactPoint = Eigen::Vector2f::Zero();
        for (size_t i = 0; i < contactPoints.count; ++i)
        {
            contactPoint += contactPoints.points[i].point.point;
        }
        contactPoint /= static_cast<float>(contactPoints.count);

        float normalLength = 20.0f; // Fixed length for visibility
        Eigen::Vector2f tip = contactPoint + normal * normalLength;

        addDebugObject(std::vector<Eigen::Vector2f>{contactPoint, tip}, 0xFF, 0xFF, 0x00, 0xFF, false);

        Eigen::Vector2f perp(-normal.y(), normal.x());
        float arrowSize = 6.0f;
        Eigen::Vector2f arrowBase = tip - normal * arrowSize;
        addDebugObject(
            std::vector<Eigen::Vector2f>{tip, arrowBase + perp * arrowSize * 0.5f,
                                         tip, arrowBase - perp * arrowSize * 0.5f},
            0xFF, 0xFF, 0x00, 0xFF, false);

        Eigen::Vector2f barLeft = tip + perp * 4.0f;
        Eigen::Vector2f barRight = tip - perp * 4.0f;
        addDebugObject(std::vector<Eigen::Vector2f>{barLeft, barRight}, 0xFF, 0xAA, 0x00, 0xFF, false);
    }
}

void DebugDraw::collectPhysicsDebugObjects()
{
    if (!showGridCells && !showColliders)
    {
        return;
    }

    PhysicsManager *physicsManager = platformator_detail::RuntimeAccess::physicsManager();
    if (physicsManager == nullptr)
    {
        return;
    }

    if (showGridCells)
    {
        for (const auto &cellEntry : physicsManager->getGrid().getCells())
        {
            addGridCellDebugObject(cellEntry.first);
        }
    }
    if (showColliders)
    {
        for (Collider *collider : physicsManager->getColliders())
        {
            if (collider == nullptr)
            {
                continue;
            }
            if (collider->getColliderType() == ColliderType::BoxCollider)
            {
                addBoxColliderDebugObject(*static_cast<const BoxCollider *>(collider));
            }
            else if (collider->getColliderType() == ColliderType::CircleCollider)
            {
                addCircleColliderDebugObject(*static_cast<const CircleCollider *>(collider));
            }
        }
    }
}

void DebugDraw::clearDebugObjects()
{
    debugObjectsBuffer.clear();
}

void DebugDraw::toggleShowColliders()
{
    showColliders = !showColliders;
}

void DebugDraw::toggleShowCollisionPoints()
{
    showCollisionPoints = !showCollisionPoints;
}

void DebugDraw::toggleShowCollisionNormals()
{
    showCollisionNormals = !showCollisionNormals;
}

void DebugDraw::toggleShowGridCells()
{
    showGridCells = !showGridCells;
}

void DebugDraw::toggleShowFps()
{
    showFps = !showFps;
    if (showFps)
    {
        resetFpsSampling(false);
    }
}

DebugObject::DebugObject(std::vector<Eigen::Vector2f> vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool closed)
    : vertices(std::move(vertices)), r(r), g(g), b(b), a(a), closed(closed)
{
}

#endif
