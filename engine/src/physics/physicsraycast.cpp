#include "physics/physicsmanager.h"

#include "benchmark/workprobe.h"
#include "platformator/boxcollider.h"
#include "platformator/circlecollider.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
// ============================================================================
// Ray Validation & Slab / Primitive Intersection Helpers (Zero-Allocation)
// ============================================================================

Eigen::Vector2d validateAndNormalizeRay(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction,
                                        float maxDistance)
{
    if (!origin.allFinite() || !direction.allFinite() || !std::isfinite(maxDistance) || maxDistance < 0.0f)
    {
        throw std::invalid_argument("Raycast origin, direction, and non-negative maxDistance must be finite");
    }
    const float maxAbs = direction.cwiseAbs().maxCoeff();
    if (maxAbs == 0.0f || (maxAbs <= 1e-6f && direction.squaredNorm() <= 1e-12f))
    {
        throw std::invalid_argument("Raycast direction must be non-zero");
    }
    const double scale = static_cast<double>(maxAbs);
    const Eigen::Vector2d scaled(static_cast<double>(direction.x()) / scale,
                                 static_cast<double>(direction.y()) / scale);
    return scaled / std::hypot(scaled.x(), scaled.y());
}

bool clipRayToSlab(double origin, double dir, double minBound, double maxBound, double &tMin, double &tMax,
                   int axis = -1, int *outHitAxis = nullptr, double *outHitSign = nullptr) noexcept
{
    if (dir == 0.0)
    {
        return origin >= minBound && origin <= maxBound;
    }
    double t1 = (minBound - origin) / dir;
    double t2 = (maxBound - origin) / dir;
    double sign = -1.0;
    if (t1 > t2)
    {
        std::swap(t1, t2);
        sign = 1.0;
    }
    if (t1 > tMin)
    {
        tMin = t1;
        if (outHitAxis != nullptr)
        {
            *outHitAxis = axis;
        }
        if (outHitSign != nullptr)
        {
            *outHitSign = sign;
        }
    }
    if (t2 < tMax)
    {
        tMax = t2;
    }
    return tMin <= tMax;
}

bool rayIntersectsAABBInterval(const Eigen::Vector2f &origin, const Eigen::Vector2d &dir, double dLimit,
                               double minX, double maxX, double minY, double maxY, double tol,
                               double &outTMin, double &outTMax) noexcept
{
    double tMin = 0.0;
    double tMax = dLimit;
    if (!clipRayToSlab(static_cast<double>(origin.x()), dir.x(), minX - tol, maxX + tol, tMin, tMax) ||
        !clipRayToSlab(static_cast<double>(origin.y()), dir.y(), minY - tol, maxY + tol, tMin, tMax) ||
        tMin > dLimit || tMax < 0.0)
    {
        return false;
    }
    outTMin = tMin;
    outTMax = tMax;
    return true;
}

bool intersectCircleRay(const CircleCollider *circle, const Eigen::Vector2f &center, const Eigen::Vector2f &origin,
                        const Eigen::Vector2d &dir, double dLimit, double limitTol, float &hitDistance,
                        Eigen::Vector2f &hitPoint, Eigen::Vector2f &hitNormal) noexcept
{
    const float radius = circle->getPublishedRadius();
    if (!std::isfinite(radius) || radius <= 0.0f)
    {
        return false;
    }

    const double dirX = dir.x();
    const double dirY = dir.y();
    const double toCenterX = static_cast<double>(center.x()) - static_cast<double>(origin.x());
    const double toCenterY = static_cast<double>(center.y()) - static_cast<double>(origin.y());
    const double originDistSq = toCenterX * toCenterX + toCenterY * toCenterY;
    const double r = static_cast<double>(radius);
    const double rSq = r * r;
    if (originDistSq <= rSq)
    {
        // Origin starts inside or on the boundary of the circle: immediate hit at t = 0.
        return true;
    }

    const double proj = toCenterX * dirX + toCenterY * dirY;
    if (proj < 0.0)
    {
        return false;
    }
    const double perpX = toCenterX - proj * dirX;
    const double perpY = toCenterY - proj * dirY;
    const double perpSq = perpX * perpX + perpY * perpY;
    const double tangencyTol = 1e-11 * std::max({1.0, r, std::abs(toCenterX), std::abs(toCenterY)});
    const double maxPerp = r + tangencyTol;
    if (perpSq > maxPerp * maxPerp)
    {
        return false;
    }
    const double disc = std::max(0.0, rSq - perpSq);
    double tHit = std::max(0.0, proj - std::sqrt(disc));
    if (tHit > dLimit + limitTol)
    {
        return false;
    }
    tHit = std::min(tHit, dLimit);
    const double exactHitX = static_cast<double>(origin.x()) + tHit * dirX;
    const double exactHitY = static_cast<double>(origin.y()) + tHit * dirY;
    hitDistance = static_cast<float>(tHit);
    hitPoint = Eigen::Vector2f(static_cast<float>(exactHitX), static_cast<float>(exactHitY));
    const double nx = exactHitX - static_cast<double>(center.x());
    const double ny = exactHitY - static_cast<double>(center.y());
    const double nLen = std::hypot(nx, ny);
    hitNormal = nLen > 0.0 ? Eigen::Vector2f(static_cast<float>(nx / nLen), static_cast<float>(ny / nLen))
                           : Eigen::Vector2f(static_cast<float>(-dirX), static_cast<float>(-dirY));
    return true;
}

bool intersectBoxRay(const BoxCollider *box, const Eigen::Vector2f &center, const Eigen::Vector2f &origin,
                     const Eigen::Vector2d &dir, double dLimit, double limitTol, float &hitDistance,
                     Eigen::Vector2f &hitPoint, Eigen::Vector2f &hitNormal) noexcept
{
    const float halfW = box->getPublishedHalfWidth();
    const float halfH = box->getPublishedHalfHeight();
    if (!std::isfinite(halfW) || !std::isfinite(halfH) || halfW <= 0.0f || halfH <= 0.0f)
    {
        return false;
    }

    const Eigen::Vector2f &xAxis = box->getPublishedXAxis();
    const double cosR = static_cast<double>(xAxis.x());
    const double sinR = static_cast<double>(xAxis.y());
    const double dirX = dir.x();
    const double dirY = dir.y();
    const double relX = static_cast<double>(origin.x()) - static_cast<double>(center.x());
    const double relY = static_cast<double>(origin.y()) - static_cast<double>(center.y());
    const double ox = relX * cosR + relY * sinR;
    const double oy = -relX * sinR + relY * cosR;
    const double dHalfW = static_cast<double>(halfW);
    const double dHalfH = static_cast<double>(halfH);

    if (std::abs(ox) <= dHalfW && std::abs(oy) <= dHalfH)
    {
        // Origin starts inside or on the boundary of the OBB: immediate hit at t = 0.
        return true;
    }

    const double dx = dirX * cosR + dirY * sinR;
    const double dy = -dirX * sinR + dirY * cosR;
    double tMin = 0.0;
    double tMax = dLimit + limitTol;
    int hitAxis = -1;
    double hitSign = 0.0;

    if (!clipRayToSlab(ox, dx, -dHalfW, dHalfW, tMin, tMax, 0, &hitAxis, &hitSign) ||
        !clipRayToSlab(oy, dy, -dHalfH, dHalfH, tMin, tMax, 1, &hitAxis, &hitSign) ||
        tMin > dLimit + limitTol || hitAxis < 0)
    {
        return false;
    }

    tMin = std::min(tMin, dLimit);
    hitDistance = static_cast<float>(tMin);
    hitPoint = Eigen::Vector2f(static_cast<float>(static_cast<double>(origin.x()) + tMin * dirX),
                               static_cast<float>(static_cast<double>(origin.y()) + tMin * dirY));
    const double lnx = (hitAxis == 0) ? hitSign : 0.0;
    const double lny = (hitAxis == 1) ? hitSign : 0.0;
    const double wnx = lnx * cosR - lny * sinR;
    const double wny = lnx * sinR + lny * cosR;
    const double nLen = std::hypot(wnx, wny);
    hitNormal = nLen > 0.0 ? Eigen::Vector2f(static_cast<float>(wnx / nLen), static_cast<float>(wny / nLen))
                           : Eigen::Vector2f(static_cast<float>(-dirX), static_cast<float>(-dirY));
    return true;
}

bool intersectColliderRay(Collider *collider, const Eigen::Vector2f &origin, const Eigen::Vector2d &dir,
                          float maxDistance, double limit, uint64_t collisionMask, bool includeTriggers, float cMinX,
                          float cMaxX, float cMinY, float cMaxY, platformator::RaycastHit &outHit)
{
    if (collider == nullptr || collider->isRetired())
    {
        return false;
    }
    GameObject *obj = collider->getGameObject();
    if (obj == nullptr || !obj->getActive() || obj->isRetired())
    {
        return false;
    }
    if ((collider->getCollisionGroup() & collisionMask) == 0ULL)
    {
        return false;
    }
    if (collider->getIsTrigger() && !includeTriggers)
    {
        return false;
    }

    const double aabbMinX = static_cast<double>(cMinX);
    const double aabbMaxX = static_cast<double>(cMaxX);
    const double aabbMinY = static_cast<double>(cMinY);
    const double aabbMaxY = static_cast<double>(cMaxY);

    PLATFORMATOR_WORK_ADD(raycastCandidateVisits, 1);

    const double maxCoord =
        std::max({1.0, std::abs(aabbMinX), std::abs(aabbMaxX), std::abs(aabbMinY), std::abs(aabbMaxY)});
    const double aabbTol = std::max(1e-4, 1e-6 * maxCoord);
    const double dLimit = std::min(static_cast<double>(maxDistance), limit);
    double aabbTMin = 0.0;
    double aabbTMax = 0.0;
    if (!rayIntersectsAABBInterval(origin, dir, dLimit, aabbMinX, aabbMaxX, aabbMinY, aabbMaxY, aabbTol, aabbTMin,
                                   aabbTMax))
    {
        return false;
    }

    const Eigen::Vector2f &center = collider->getCachedWorldPosition();
    if (!center.allFinite())
    {
        return false;
    }

    const double limitTol = 1e-8 * std::max(1.0, dLimit);
    float hitDistance = 0.0f;
    Eigen::Vector2f hitPoint = origin;
    Eigen::Vector2f hitNormal(static_cast<float>(-dir.x()), static_cast<float>(-dir.y()));

    const ColliderType shape = collider->getColliderType();
    bool hit = false;
    if (shape == ColliderType::CircleCollider)
    {
        hit = intersectCircleRay(static_cast<const CircleCollider *>(collider), center, origin, dir, dLimit, limitTol,
                                 hitDistance, hitPoint, hitNormal);
    }
    else if (shape == ColliderType::BoxCollider)
    {
        hit = intersectBoxRay(static_cast<const BoxCollider *>(collider), center, origin, dir, dLimit, limitTol,
                              hitDistance, hitPoint, hitNormal);
    }
    if (!hit)
    {
        return false;
    }

    outHit.collider = collider;
    outHit.gameObject = obj;
    outHit.point = hitPoint;
    outHit.normal = hitNormal;
    outHit.distance = hitDistance;
    outHit.fraction = maxDistance > 0.0f ? (hitDistance / maxDistance) : 0.0f;
    return true;
}
} // namespace

// ============================================================================
// Grid DDA Ray Traversal & Public Raycast Queries
// ============================================================================

template <typename HitCallback>
void PhysicsManager::traverseRayCandidates(const Eigen::Vector2f &origin, const Eigen::Vector2d &dir, float maxDistance,
                                           const double &limit, uint64_t collisionMask, bool includeTriggers,
                                           HitCallback &&onHit) const
{
    platformator::RaycastHit candidate;
    const GridCellRange &activeBounds = grid.getActiveCellBounds();
    if (!activeBounds.getIsValid() || grid.getCells().empty())
    {
        return;
    }

    const double cellSize = static_cast<double>(grid.getCellSize());
    const double gridMinX = static_cast<double>(activeBounds.minX) * cellSize;
    const double gridMaxX = static_cast<double>(activeBounds.maxX + 1) * cellSize;
    const double gridMinY = static_cast<double>(activeBounds.minY) * cellSize;
    const double gridMaxY = static_cast<double>(activeBounds.maxY + 1) * cellSize;

    double tGridStart = 0.0;
    double tGridEnd = 0.0;
    if (!rayIntersectsAABBInterval(origin, dir, limit, gridMinX, gridMaxX, gridMinY, gridMaxY, 1.0, tGridStart,
                                   tGridEnd))
    {
        return;
    }

    std::array<const Collider *, 64> seenMultiCell{};
    size_t seenMultiCellCount = 0;

    const double ox = static_cast<double>(origin.x());
    const double oy = static_cast<double>(origin.y());
    const double dx = dir.x();
    const double dy = dir.y();

    const auto visitCellInterval = [&](const GridCell &cell, double tEnter, double tExit) {
        const double tClampedEnter = std::max(0.0, tEnter);
        const double tClampedExit = std::max(tClampedEnter, std::min(limit, tExit));
        const double p0x = ox + tClampedEnter * dx;
        const double p0y = oy + tClampedEnter * dy;
        const double p1x = ox + tClampedExit * dx;
        const double p1y = oy + tClampedExit * dy;
        const float maxAbsCoord = static_cast<float>(
            std::max({1.0, std::abs(p0x), std::abs(p1x), std::abs(p0y), std::abs(p1y)}));
        const float pad = 1e-2f + 1e-5f * maxAbsCoord;
        const float segMinX = static_cast<float>(std::min(p0x, p1x)) - pad;
        const float segMaxX = static_cast<float>(std::max(p0x, p1x)) + pad;
        const float segMinY = static_cast<float>(std::min(p0y, p1y)) - pad;
        const float segMaxY = static_cast<float>(std::max(p0y, p1y)) + pad;

        cell.getAABB()->forEachOverlappingAABB(
            segMinX, segMaxX, segMinY, segMaxY,
            [&](Collider *c, float cMinX, float cMaxX, float cMinY, float cMaxY) {
                const GridCellRange &range = c->getGridCellRange();
                if (range.minX != range.maxX || range.minY != range.maxY)
                {
                    for (size_t i = 0; i < seenMultiCellCount; ++i)
                    {
                        if (seenMultiCell[i] == c)
                        {
                            return;
                        }
                    }
                    if (seenMultiCellCount < seenMultiCell.size())
                    {
                        seenMultiCell[seenMultiCellCount++] = c;
                    }
                }
                if (intersectColliderRay(c, origin, dir, maxDistance, limit, collisionMask, includeTriggers, cMinX,
                                         cMaxX, cMinY, cMaxY, candidate))
                {
                    onHit(candidate);
                }
            });
    };

    const double tStart = std::max(0.0, tGridStart);
    const double tEnd = std::min(limit, tGridEnd);
    if (tStart > tEnd)
    {
        return;
    }

    const double startX = ox + tStart * dx;
    const double startY = oy + tStart * dy;
    const double endX = ox + tEnd * dx;
    const double endY = oy + tEnd * dy;

    const int startCx = static_cast<int>(std::floor(startX / cellSize));
    const int startCy = static_cast<int>(std::floor(startY / cellSize));
    const int endCx = static_cast<int>(std::floor(endX / cellSize));
    const int endCy = static_cast<int>(std::floor(endY / cellSize));
    const std::int64_t maxSteps =
        std::abs(static_cast<std::int64_t>(endCx) - startCx) + std::abs(static_cast<std::int64_t>(endCy) - startCy) + 1;

    constexpr std::int64_t kMaxGridDdaSteps = 65536;
    if (maxSteps <= kMaxGridDdaSteps)
    {
        const int stepX = (dx > 0.0) ? 1 : (dx < 0.0 ? -1 : 0);
        const int stepY = (dy > 0.0) ? 1 : (dy < 0.0 ? -1 : 0);
        const double inf = std::numeric_limits<double>::infinity();
        double tDeltaX = inf;
        double tMaxX = inf;
        if (stepX != 0)
        {
            tDeltaX = cellSize / std::abs(dx);
            const double nextX = static_cast<double>(stepX > 0 ? (startCx + 1) : startCx) * cellSize;
            tMaxX = std::max(tStart, (nextX - ox) / dx);
        }
        double tDeltaY = inf;
        double tMaxY = inf;
        if (stepY != 0)
        {
            tDeltaY = cellSize / std::abs(dy);
            const double nextY = static_cast<double>(stepY > 0 ? (startCy + 1) : startCy) * cellSize;
            tMaxY = std::max(tStart, (nextY - oy) / dy);
        }

        const double maxAbsCoord =
            std::max({1.0, std::abs(startX), std::abs(startY), std::abs(endX), std::abs(endY)});
        const double boundaryCoordEps = std::max(1e-4, 1e-6 * maxAbsCoord);
        const double tCornerEps = 1e-7 * std::max(1.0, tEnd) + 1e-12;

        std::array<GridCellKey, 16> recentCells{
            GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0),
            GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0),
            GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0),
            GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0)};
        size_t recentCount = 0;
        size_t recentWrite = 0;
        const auto visitOnce = [&](int vx, int vy, double t0, double t1) {
            const GridCellKey key(vx, vy);
            for (size_t i = 0; i < recentCount; ++i)
            {
                if (recentCells[i] == key)
                {
                    return;
                }
            }
            recentCells[recentWrite] = key;
            recentWrite = (recentWrite + 1) % recentCells.size();
            if (recentCount < recentCells.size())
            {
                ++recentCount;
            }
            if (const GridCell *cell = grid.findCell(key))
            {
                visitCellInterval(*cell, t0, t1);
            }
        };

        const int sMinCx = static_cast<int>(std::floor((startX - boundaryCoordEps) / cellSize));
        const int sMaxCx = static_cast<int>(std::floor((startX + boundaryCoordEps) / cellSize));
        const int sMinCy = static_cast<int>(std::floor((startY - boundaryCoordEps) / cellSize));
        const int sMaxCy = static_cast<int>(std::floor((startY + boundaryCoordEps) / cellSize));
        for (int bx = sMinCx; bx <= sMaxCx; ++bx)
        {
            for (int by = sMinCy; by <= sMaxCy; ++by)
            {
                const bool behindX = stepX != 0 && (bx - startCx) * stepX < 0;
                const bool behindY = stepY != 0 && (by - startCy) * stepY < 0;
                if (behindX || behindY)
                {
                    visitOnce(bx, by, tStart, tStart);
                }
            }
        }
        const int parallelAltCx = (stepX == 0 && sMinCx != sMaxCx) ? ((sMinCx == startCx) ? sMaxCx : sMinCx) : 0;
        const bool hasParallelAltX = (stepX == 0 && sMinCx != sMaxCx);
        const int parallelAltCy = (stepY == 0 && sMinCy != sMaxCy) ? ((sMinCy == startCy) ? sMaxCy : sMinCy) : 0;
        const bool hasParallelAltY = (stepY == 0 && sMinCy != sMaxCy);

        int cx = startCx;
        int cy = startCy;
        double tCellEnter = tStart;
        const std::int64_t stepBudget = maxSteps + 4;
        for (std::int64_t step = 0; step <= stepBudget; ++step)
        {
            const double activeEnd = std::clamp(std::min(limit, tEnd), tStart, tEnd);
            const double tLimitEnd = activeEnd + tCornerEps;
            if (tCellEnter > tLimitEnd)
            {
                break;
            }

            const double t0 = std::clamp(tCellEnter, tStart, activeEnd);
            const double t1 = std::clamp(std::min({activeEnd, tMaxX, tMaxY}), t0, activeEnd);
            visitOnce(cx, cy, t0, t1);
            if (hasParallelAltX)
            {
                visitOnce(parallelAltCx, cy, t0, t1);
            }
            if (hasParallelAltY)
            {
                visitOnce(cx, parallelAltCy, t0, t1);
            }

            const double postHitEnd = std::clamp(std::min(limit, tEnd), tStart, tEnd);
            const double postHitLimit = postHitEnd + tCornerEps;
            if (cx == endCx && cy == endCy && tMaxX > postHitLimit && tMaxY > postHitLimit)
            {
                break;
            }

            if (std::isfinite(tMaxX) && std::isfinite(tMaxY) && std::abs(tMaxX - tMaxY) <= tCornerEps)
            {
                if (std::min(tMaxX, tMaxY) > postHitLimit)
                {
                    break;
                }
                const double tC0 = std::clamp(std::min(tMaxX, tMaxY), tStart, postHitEnd);
                const double tC1 = std::clamp(std::max(tMaxX, tMaxY), tC0, postHitEnd);
                visitOnce(cx + stepX, cy, tC0, tC1);
                visitOnce(cx, cy + stepY, tC0, tC1);
                tCellEnter = std::max(tMaxX, tMaxY);
                tMaxX += tDeltaX;
                tMaxY += tDeltaY;
                cx += stepX;
                cy += stepY;
            }
            else if (tMaxX < tMaxY)
            {
                if (!std::isfinite(tMaxX) || tMaxX > postHitLimit)
                {
                    break;
                }
                tCellEnter = tMaxX;
                tMaxX += tDeltaX;
                cx += stepX;
            }
            else
            {
                if (!std::isfinite(tMaxY) || tMaxY > postHitLimit)
                {
                    break;
                }
                tCellEnter = tMaxY;
                tMaxY += tDeltaY;
                cy += stepY;
            }
        }

        const double finalEnd = std::clamp(std::min(limit, tEnd), tStart, tEnd);
        const double finalX = ox + finalEnd * dx;
        const double finalY = oy + finalEnd * dy;
        const int eMinCx = static_cast<int>(std::floor((finalX - boundaryCoordEps) / cellSize));
        const int eMaxCx = static_cast<int>(std::floor((finalX + boundaryCoordEps) / cellSize));
        const int eMinCy = static_cast<int>(std::floor((finalY - boundaryCoordEps) / cellSize));
        const int eMaxCy = static_cast<int>(std::floor((finalY + boundaryCoordEps) / cellSize));
        for (int ex = eMinCx; ex <= eMaxCx; ++ex)
        {
            for (int ey = eMinCy; ey <= eMaxCy; ++ey)
            {
                visitOnce(ex, ey, finalEnd, finalEnd);
            }
        }
        return;
    }

    for (const auto &entry : grid.getCells())
    {
        const GridCellKey &key = entry.first;
        const double cMinX = static_cast<double>(key.x) * cellSize;
        const double cMaxX = static_cast<double>(key.x + 1) * cellSize;
        const double cMinY = static_cast<double>(key.y) * cellSize;
        const double cMaxY = static_cast<double>(key.y + 1) * cellSize;
        double cTEnter = 0.0;
        double cTExit = 0.0;
        if (rayIntersectsAABBInterval(origin, dir, limit, cMinX, cMaxX, cMinY, cMaxY, 1e-2, cTEnter, cTExit))
        {
            visitCellInterval(entry.second, cTEnter, cTExit);
        }
    }
}

std::optional<platformator::RaycastHit> PhysicsManager::raycast(const Eigen::Vector2f &origin,
                                                                const Eigen::Vector2f &direction, float maxDistance,
                                                                uint64_t collisionMask, bool includeTriggers) const
{
    platformator::RaycastHit hit;
    if (!raycast(origin, direction, maxDistance, hit, collisionMask, includeTriggers))
    {
        return std::nullopt;
    }
    return hit;
}

bool PhysicsManager::raycast(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction, float maxDistance,
                             platformator::RaycastHit &outHit, uint64_t collisionMask, bool includeTriggers) const
{
    const Eigen::Vector2d dir = validateAndNormalizeRay(origin, direction, maxDistance);

    bool hasHit = false;
    double limit = static_cast<double>(maxDistance);
    platformator::RaycastHit best;

    traverseRayCandidates(origin, dir, maxDistance, limit, collisionMask, includeTriggers,
                          [&](const platformator::RaycastHit &candidate) {
                              if (!hasHit || candidate.distance < best.distance ||
                                  (candidate.distance == best.distance &&
                                   candidate.collider->getId() < best.collider->getId()))
                              {
                                  best = candidate;
                                  hasHit = true;
                                  // Keep the upper bound of best.distance's float rounding interval so
                                  // equal-float-distance candidates with lower IDs are never pruned early.
                                  const double nextAfterMid =
                                      0.5 * (static_cast<double>(best.distance) +
                                             static_cast<double>(std::nextafter(
                                                 best.distance, std::numeric_limits<float>::infinity())));
                                  limit = std::min(static_cast<double>(maxDistance), nextAfterMid);
                              }
                          });

    if (hasHit)
    {
        outHit = best;
    }
    return hasHit;
}

std::vector<platformator::RaycastHit> PhysicsManager::raycastAll(const Eigen::Vector2f &origin,
                                                                 const Eigen::Vector2f &direction, float maxDistance,
                                                                 uint64_t collisionMask, bool includeTriggers) const
{
    const Eigen::Vector2d dir = validateAndNormalizeRay(origin, direction, maxDistance);

    std::vector<platformator::RaycastHit> hits;
    const double limit = static_cast<double>(maxDistance);
    traverseRayCandidates(origin, dir, maxDistance, limit, collisionMask, includeTriggers,
                          [&](const platformator::RaycastHit &candidate) { hits.push_back(candidate); });

    std::sort(hits.begin(), hits.end(), [](const auto &a, const auto &b) {
        if (a.distance != b.distance)
        {
            return a.distance < b.distance;
        }
        return a.collider->getId() < b.collider->getId();
    });
    // Repeated visits intersect the same published geometry and yield the same distance.
    hits.erase(std::unique(hits.begin(), hits.end(), [](const auto &a, const auto &b) {
                   return a.collider == b.collider;
               }),
               hits.end());
    return hits;
}
