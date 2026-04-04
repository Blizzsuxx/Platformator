#include "debugdraw.h"
#include "constants.h"
#include "gridcell.h"
#include <cmath>

DebugDraw::DebugDraw()
    : debugObjectsBuffer(), showColliders(true), showCollisionPoints(true), showCollisionNormals(true), showGridCells(true)
{
}

DebugDraw::~DebugDraw()
{
}

void DebugDraw::render(SDL_Renderer *renderer, Camera *camera)
{
    SDL_BlendMode prevBlend;
    SDL_GetRenderDrawBlendMode(renderer, &prevBlend);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    for (const DebugObject &debugObject : debugObjectsBuffer)
    {
        const std::vector<Eigen::Vector2f> &vertices = debugObject.vertices;
        int camX = camera->getCamera().x;
        int camY = camera->getCamera().y;

        SDL_SetRenderDrawColor(renderer, debugObject.r, debugObject.g, debugObject.b, debugObject.a);

        if (debugObject.closed)
        {
            // Draw as a closed polygon (colliders)
            for (size_t i = 0; i < vertices.size(); i++)
            {
                const Eigen::Vector2f &v1 = vertices[i];
                const Eigen::Vector2f &v2 = vertices[(i + 1) % vertices.size()];

                int x1 = static_cast<int>(v1.x()) - camX;
                int y1 = static_cast<int>(v1.y()) - camY;
                int x2 = static_cast<int>(v2.x()) - camX;
                int y2 = static_cast<int>(v2.y()) - camY;

                // Draw 3px thick by offsetting
                SDL_RenderLine(renderer, x1, y1, x2, y2);
            }
        }
        else
        {
            // Draw as individual line segments (pairs of points)
            for (size_t i = 0; i + 1 < vertices.size(); i += 2)
            {
                const Eigen::Vector2f &v1 = vertices[i];
                const Eigen::Vector2f &v2 = vertices[i + 1];

                int x1 = static_cast<int>(v1.x()) - camX;
                int y1 = static_cast<int>(v1.y()) - camY;
                int x2 = static_cast<int>(v2.x()) - camX;
                int y2 = static_cast<int>(v2.y()) - camY;

                SDL_RenderLine(renderer, x1, y1, x2, y2);
            }
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, prevBlend);
    clearDebugObjects();
}

void DebugDraw::drawBoxCollider(SDL_Renderer *renderer, Camera *camera, const BoxCollider *collider)
{
    const std::array<Eigen::Vector2f, 4> &vertices = collider->getVertices();
    int camX = camera->getCamera().x;
    int camY = camera->getCamera().y;

    // Collider in active collision: red, otherwise green
    SDL_SetRenderDrawColor(renderer, 0x00, 0xFF, 0x00, 0xFF);

    // Draw lines: 0→1, 1→3, 3→2, 2→0 (top-left, top-right, bottom-right, bottom-left)
    // Vertex order: 0=TL, 1=TR, 2=BL, 3=BR
    int x0 = static_cast<int>(vertices[0].x()) - camX;
    int y0 = static_cast<int>(vertices[0].y()) - camY;
    int x1 = static_cast<int>(vertices[1].x()) - camX;
    int y1 = static_cast<int>(vertices[1].y()) - camY;
    int x2 = static_cast<int>(vertices[2].x()) - camX;
    int y2 = static_cast<int>(vertices[2].y()) - camY;
    int x3 = static_cast<int>(vertices[3].x()) - camX;
    int y3 = static_cast<int>(vertices[3].y()) - camY;

    SDL_RenderLine(renderer, x0, y0, x1, y1); // top
    SDL_RenderLine(renderer, x1, y1, x3, y3); // right
    SDL_RenderLine(renderer, x3, y3, x2, y2); // bottom
    SDL_RenderLine(renderer, x2, y2, x0, y0); // left
}

void DebugDraw::drawCircleCollider(SDL_Renderer *renderer, Camera *camera, const CircleCollider *collider)
{
    SDL_SetRenderDrawColor(renderer, 0x00, 0xFF, 0x00, 0xFF);

    int cx = static_cast<int>(collider->getGameObject()->getPosition().x()) - camera->getCamera().x;
    int cy = static_cast<int>(collider->getGameObject()->getPosition().y()) - camera->getCamera().y;
    int r = static_cast<int>(collider->getRadius());

    drawCircle(renderer, cx, cy, r);

    // Draw a line from center to edge to show rotation
    float angle = collider->getGameObject()->getRotation();
    int edgeX = cx + static_cast<int>(r * std::cos(angle));
    int edgeY = cy + static_cast<int>(r * std::sin(angle));
    SDL_RenderLine(renderer, cx, cy, edgeX, edgeY);
}

void DebugDraw::drawContactPoint(SDL_Renderer *renderer, Camera *camera, const Collision *collision)
{
    // Red cross at contact point
    SDL_SetRenderDrawColor(renderer, 0xFF, 0x00, 0x00, 0xFF);

    int cx = static_cast<int>(collision->getContactPoint().x()) - camera->getCamera().x;
    int cy = static_cast<int>(collision->getContactPoint().y()) - camera->getCamera().y;

    drawCross(renderer, cx, cy, 5);
}

void DebugDraw::drawNormal(SDL_Renderer *renderer, Camera *camera, const Collision *collision)
{
    // Yellow line from contact point along normal, length = penetration * 5 (scaled for visibility)
    SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0x00, 0xFF);

    int cx = static_cast<int>(collision->getContactPoint().x()) - camera->getCamera().x;
    int cy = static_cast<int>(collision->getContactPoint().y()) - camera->getCamera().y;

    float normalLength = std::max(collision->getPenetration() * 5.0f, 15.0f);
    int nx = cx + static_cast<int>(collision->getNormal().x() * normalLength);
    int ny = cy + static_cast<int>(collision->getNormal().y() * normalLength);

    SDL_RenderLine(renderer, cx, cy, nx, ny);

    // Small arrowhead
    Eigen::Vector2f normal = collision->getNormal();
    Eigen::Vector2f perp(-normal.y(), normal.x());
    int arrowSize = 4;
    int ax1 = nx - static_cast<int>((normal.x() * arrowSize + perp.x() * arrowSize));
    int ay1 = ny - static_cast<int>((normal.y() * arrowSize + perp.y() * arrowSize));
    int ax2 = nx - static_cast<int>((normal.x() * arrowSize - perp.x() * arrowSize));
    int ay2 = ny - static_cast<int>((normal.y() * arrowSize - perp.y() * arrowSize));
    SDL_RenderLine(renderer, nx, ny, ax1, ay1);
    SDL_RenderLine(renderer, nx, ny, ax2, ay2);
}

void DebugDraw::drawCircle(SDL_Renderer *renderer, int centerX, int centerY, int radius)
{
    // Midpoint circle algorithm
    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y)
    {
        SDL_RenderPoint(renderer, centerX + x, centerY + y);
        SDL_RenderPoint(renderer, centerX + y, centerY + x);
        SDL_RenderPoint(renderer, centerX - y, centerY + x);
        SDL_RenderPoint(renderer, centerX - x, centerY + y);
        SDL_RenderPoint(renderer, centerX - x, centerY - y);
        SDL_RenderPoint(renderer, centerX - y, centerY - x);
        SDL_RenderPoint(renderer, centerX + y, centerY - x);
        SDL_RenderPoint(renderer, centerX + x, centerY - y);

        if (err <= 0)
        {
            y++;
            err += 2 * y + 1;
        }
        if (err > 0)
        {
            x--;
            err -= 2 * x + 1;
        }
    }
}

void DebugDraw::drawCross(SDL_Renderer *renderer, int centerX, int centerY, int size)
{
    SDL_RenderLine(renderer, centerX - size, centerY - size, centerX + size, centerY + size);
    SDL_RenderLine(renderer, centerX - size, centerY + size, centerX + size, centerY - size);
}

void DebugDraw::addDebugObject(const std::vector<Eigen::Vector2f> &vertices, std::string name)
{
    debugObjectsBuffer.push_back(DebugObject(vertices));
}

void DebugDraw::addDebugObject(const std::vector<Eigen::Vector2f> &vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool closed, std::string name)
{
    debugObjectsBuffer.push_back(DebugObject(vertices, r, g, b, a, closed, name));
}

void DebugDraw::addDebugObject(std::vector<Eigen::Vector2f> &&vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool closed, std::string name)
{
    debugObjectsBuffer.emplace_back(std::move(vertices), r, g, b, a, closed, name);
}

void DebugDraw::addBoxColliderDebugObject(const BoxCollider &collider)
{
    if (!showColliders)
    {
        return;
    }

    const auto &v = collider.getVertices();
    // Vertex order: 0=TL, 1=TR, 2=BL, 3=BR → draw as TL→TR→BR→BL (clockwise)
    std::vector<Eigen::Vector2f> ordered = {v[0], v[1], v[3], v[2]};
    addDebugObject(std::move(ordered), 0x00, 0xFF, 0x00, 0xCC, true, collider.getGameObject()->getName());
}

void DebugDraw::addCircleColliderDebugObject(const CircleCollider &collider)
{
    if (!showColliders)
    {
        return;
    }

    const Eigen::Vector2f &center = collider.getGameObject()->getPosition();
    float radius = collider.getRadius();

    std::vector<Eigen::Vector2f> vertices;
    const int numSegments = 36;
    for (int i = 0; i < numSegments; i++)
    {
        float angle = static_cast<float>(i) / numSegments * 2.0f * static_cast<float>(M_PI);
        vertices.emplace_back(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
    }
    addDebugObject(std::move(vertices), 0x00, 0xFF, 0x00, 0xCC, true, collider.getGameObject()->getName());

    // Rotation indicator line from center to edge
    float rot = collider.getGameObject()->getRotation();
    Eigen::Vector2f edge(center.x() + radius * std::cos(rot), center.y() + radius * std::sin(rot));
    addDebugObject({center, edge}, 0x00, 0xCC, 0x00, 0xCC, false, collider.getGameObject()->getName() + " rotation");
}

void DebugDraw::addGridCellDebugObject(const GridCellKey &cellKey)
{
    if (!showGridCells)
    {
        return;
    }

    float minX = static_cast<float>(cellKey.x) * GRID_CELL_SIZE;
    float minY = static_cast<float>(cellKey.y) * GRID_CELL_SIZE;
    float maxX = minX + GRID_CELL_SIZE;
    float maxY = minY + GRID_CELL_SIZE;

    std::vector<Eigen::Vector2f> vertices = {
        Eigen::Vector2f(minX, minY),
        Eigen::Vector2f(maxX, minY),
        Eigen::Vector2f(maxX, maxY),
        Eigen::Vector2f(minX, maxY)};

    addDebugObject(std::move(vertices), 0x30, 0x90, 0xFF, 0x55, true, "Grid Cell");
}

void DebugDraw::addCollisionDebugObject(const Collision &collision)
{
    if (!showCollisionNormals && !showCollisionPoints)
    {
        return;
    }

    auto contactPoint = collision.getContactPoint();
    auto normal = collision.getNormal();
    float penetration = collision.getPenetration();

    if (showCollisionPoints)
    {
        // Filled-looking cross at contact point (two separate line segments)
        float size = 6.0f;
        addDebugObject(
            {contactPoint + Eigen::Vector2f(-size, -size), contactPoint + Eigen::Vector2f(size, size),
             contactPoint + Eigen::Vector2f(-size, size), contactPoint + Eigen::Vector2f(size, -size)},
            0xFF, 0x20, 0x20, 0xFF, false);

        // Small diamond around the point for extra visibility
        addDebugObject(
            {contactPoint + Eigen::Vector2f(0, -size), contactPoint + Eigen::Vector2f(size, 0),
             contactPoint + Eigen::Vector2f(0, size), contactPoint + Eigen::Vector2f(-size, 0)},
            0xFF, 0x60, 0x60, 0xFF, true);
    }

    if (showCollisionNormals)
    {
        // Normal arrow with minimum visible length
        float normalLength = std::max(penetration * 5.0f, 20.0f);
        Eigen::Vector2f tip = contactPoint + normal * normalLength;

        // Main shaft
        addDebugObject({contactPoint, tip}, 0xFF, 0xFF, 0x00, 0xFF, false);

        // Arrowhead
        Eigen::Vector2f perp(-normal.y(), normal.x());
        float arrowSize = 6.0f;
        Eigen::Vector2f arrowBase = tip - normal * arrowSize;
        addDebugObject(
            {tip, arrowBase + perp * arrowSize * 0.5f,
             tip, arrowBase - perp * arrowSize * 0.5f},
            0xFF, 0xFF, 0x00, 0xFF, false);

        // Penetration depth text-free indicator: small bar at penetration distance
        if (penetration > 2.0f)
        {
            Eigen::Vector2f penTip = contactPoint + normal * penetration;
            Eigen::Vector2f barLeft = penTip + perp * 4.0f;
            Eigen::Vector2f barRight = penTip - perp * 4.0f;
            addDebugObject({barLeft, barRight}, 0xFF, 0xAA, 0x00, 0xFF, false);
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

////////////

DebugObject::DebugObject(std::vector<Eigen::Vector2f> vertices, bool closed, std::string name)
    : vertices(std::move(vertices)), r(0x00), g(0xFF), b(0x00), a(0xFF), closed(closed), name(std::move(name))
{
}
DebugObject::DebugObject(std::vector<Eigen::Vector2f> vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool closed, std::string name)
    : vertices(std::move(vertices)), r(r), g(g), b(b), a(a), closed(closed), name(std::move(name))
{
}

DebugObject::~DebugObject()
{
}
