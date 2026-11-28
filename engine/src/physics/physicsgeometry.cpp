#include "physics/physicsgeometry.h"

#include <cmath>

#include "buildconfig.h"
#include "physics/colliderpair.h"
#include "platformator/boxcollider.h"
#include "platformator/circlecollider.h"

namespace physics_geometry
{
namespace
{
// Circle dispatch builds its own manifold; clipping below is exclusively box/box.
constexpr uint32_t FEATURE_CIRCLE_CIRCLE = 0x100u;
constexpr uint32_t FEATURE_CIRCLE_BOX_BASE = 0x200u;
// Hysteresis favoring box A axes over box B axes to prevent face-flipping jitter on near-ties.
constexpr float SAT_RELATIVE_AXIS_BIAS = 0.98f;
constexpr float SAT_ABSOLUTE_AXIS_SLOP = 0.001f;

void calculateBoxContactPoints(Collision *collision, const BoxCollider *referenceBox, const BoxCollider *incidentBox,
                               int refAxis, bool refPositive, int incAxis, bool incPositive);

void generateFallbackContactPoint(Collision *collision)
{
    const auto *referenceObject = collision->getReferenceObject();
    const auto *incidentObject = collision->getIncidentObject();
    const Eigen::Vector2f midpoint =
        (referenceObject->getCachedWorldPosition() + incidentObject->getCachedWorldPosition()) * 0.5f;
    collision->getContactPoints().setSinglePoint(midpoint, ContactFeature(), 0.0f);
}

void circleCircleCollision(ColliderPair &pair, const Eigen::Vector2f &gravityDirection)
{
    const auto *a = static_cast<const CircleCollider *>(pair.getObjectA());
    const auto *b = static_cast<const CircleCollider *>(pair.getObjectB());
    const float radiusA = a->getPublishedRadius();
    const float radiusB = b->getPublishedRadius();
    const Eigen::Vector2f delta = b->getCachedWorldPosition() - a->getCachedWorldPosition();
    const float radius = radiusA + radiusB;
    const float squared = delta.squaredNorm();
    if (squared > radius * radius)
    {
        pair.clearCollisionNoEvent();
        return;
    }
    const float distance = std::sqrt(squared);
    const Eigen::Vector2f normal = distance > 0 ? (delta / distance).eval() : Eigen::Vector2f(1, 0);
    auto *collision = pair.getOrCreateCollisionNoEvent();
    collision->setReferenceObject(a);
    collision->setIncidentObject(b);
    collision->setNormal(normal);
    collision->getContactPoints().setSinglePoint(
        (a->getCachedWorldPosition() + normal * radiusA + b->getCachedWorldPosition() - normal * radiusB) * 0.5f,
        ContactFeature(FEATURE_CIRCLE_CIRCLE), distance - radius);
    collision->updateSupportState(gravityDirection);
}

void circleBoxCollision(ColliderPair &pair, const Eigen::Vector2f &gravityDirection)
{
    const bool boxIsA = pair.getObjectA()->getColliderType() == ColliderType::BoxCollider;
    const auto *box = static_cast<const BoxCollider *>(boxIsA ? pair.getObjectA() : pair.getObjectB());
    const auto *circle = static_cast<const CircleCollider *>(boxIsA ? pair.getObjectB() : pair.getObjectA());
    const auto &axes = box->getAxes();
    const Eigen::Vector2f delta = circle->getCachedWorldPosition() - box->getCachedWorldPosition();
    const Eigen::Vector2f local(delta.dot(axes[0]), delta.dot(axes[1]));
    const Eigen::Vector2f half(box->getPublishedHalfWidth(), box->getPublishedHalfHeight());
    const Eigen::Vector2f closest = local.cwiseMax(-half).cwiseMin(half);
    const Eigen::Vector2f gap = local - closest;
    const float radius = circle->getPublishedRadius();
    const float squared = gap.squaredNorm();
    if (squared > radius * radius)
    {
        pair.clearCollisionNoEvent();
        return;
    }
    Eigen::Vector2f localNormal;
    float separation;
    uint32_t feature = FEATURE_CIRCLE_BOX_BASE;
    if (squared > 0)
    {
        const float distance = std::sqrt(squared);
        localNormal = gap / distance;
        separation = distance - radius;
        if (gap.x() != 0)
            feature |= gap.x() > 0 ? 1 : 2;
        if (gap.y() != 0)
            feature |= gap.y() > 0 ? 4 : 8;
    }
    else
    {
        const Eigen::Vector2f distances = half - local.cwiseAbs();
        const int axis = distances.x() <= distances.y() ? 0 : 1;
        localNormal = Eigen::Vector2f::Zero();
        localNormal[axis] = local[axis] >= 0 ? 1.f : -1.f;
        separation = -(distances[axis] + radius);
        feature |= axis == 0 ? (localNormal.x() > 0 ? 1 : 2) : (localNormal.y() > 0 ? 4 : 8);
    }
    const Eigen::Vector2f normal = localNormal.x() * axes[0] + localNormal.y() * axes[1];
    auto *collision = pair.getOrCreateCollisionNoEvent();
    collision->setReferenceObject(box);
    collision->setIncidentObject(circle);
    collision->setNormal(normal);
    collision->getContactPoints().setSinglePoint(circle->getCachedWorldPosition() - normal * radius,
                                                 ContactFeature(feature), separation);
    collision->updateSupportState(gravityDirection);
}
void boxBoxCollision(ColliderPair &pair, const BoxCollider *boxA, const BoxCollider *boxB,
                     const Eigen::Vector2f &gravityDirection)
{
    // Analytical 2D OBB SAT: avoids virtual calls and 4-vertex projection loops.
    const auto &axesA = boxA->getAxes();
    const auto &axesB = boxB->getAxes();
    const float hAx = boxA->getPublishedHalfWidth();
    const float hAy = boxA->getPublishedHalfHeight();
    const float hBx = boxB->getPublishedHalfWidth();
    const float hBy = boxB->getPublishedHalfHeight();

    const Eigen::Vector2f delta = boxB->getCachedWorldPosition() - boxA->getCachedWorldPosition();

    const BoxCollider *referenceBox;
    const BoxCollider *incidentBox;
    Eigen::Vector2f minNormal;
    int refAxis;
    bool refPositive;
    int incAxis;
    bool incPositive;

    if (axesA[0].y() == 0.0f && axesB[0].y() == 0.0f && axesA[0].x() > 0.0f && axesB[0].x() > 0.0f)
    {
        // Fast path for unrotated axis-aligned boxes: exact match with general SAT.
        const float overlapX = (hAx + hBx) - std::abs(delta.x());
        if (overlapX < 0.0f)
        {
            pair.clearCollisionNoEvent();
            return;
        }
        const float overlapY = (hAy + hBy) - std::abs(delta.y());
        if (overlapY < 0.0f)
        {
            pair.clearCollisionNoEvent();
            return;
        }

        referenceBox = boxA;
        incidentBox = boxB;
        if (overlapY < overlapX)
        {
            refAxis = 1;
            refPositive = delta.y() >= 0.0f;
            minNormal = Eigen::Vector2f(0.0f, refPositive ? 1.0f : -1.0f);
        }
        else
        {
            refAxis = 0;
            refPositive = delta.x() >= 0.0f;
            minNormal = Eigen::Vector2f(refPositive ? 1.0f : -1.0f, 0.0f);
        }
        incAxis = refAxis;
        incPositive = !refPositive;
    }
    else
    {
        const float r00 = axesA[0].dot(axesB[0]);
        const float r01 = axesA[0].dot(axesB[1]);
        const float absR00 = std::abs(r00);
        const float absR01 = std::abs(r01);
        const float distA0 = delta.dot(axesA[0]);
        const float overlapA0 = (hAx + hBx * absR00 + hBy * absR01) - std::abs(distA0);
        if (overlapA0 < 0.0f)
        {
            pair.clearCollisionNoEvent();
            return;
        }

        // Both box frames are 2D orthonormal: A1·B0 == -A0·B1 and A1·B1 == A0·B0.
        const float distA1 = delta.dot(axesA[1]);
        const float overlapA1 = (hAy + hBx * absR01 + hBy * absR00) - std::abs(distA1);
        if (overlapA1 < 0.0f)
        {
            pair.clearCollisionNoEvent();
            return;
        }

        const float distB0 = delta.dot(axesB[0]);
        const float overlapB0 = (hBx + hAx * absR00 + hAy * absR01) - std::abs(distB0);
        if (overlapB0 < 0.0f)
        {
            pair.clearCollisionNoEvent();
            return;
        }

        const float distB1 = delta.dot(axesB[1]);
        const float overlapB1 = (hBy + hAx * absR01 + hAy * absR00) - std::abs(distB1);
        if (overlapB1 < 0.0f)
        {
            pair.clearCollisionNoEvent();
            return;
        }

        float minOverlap = overlapA0;
        minNormal = distA0 < 0.0f ? (-axesA[0]).eval() : axesA[0];
        bool swapped = false;
        refAxis = 0;
        refPositive = distA0 >= 0.0f;

        if (overlapA1 < minOverlap)
        {
            minOverlap = overlapA1;
            minNormal = distA1 < 0.0f ? (-axesA[1]).eval() : axesA[1];
            refAxis = 1;
            refPositive = distA1 >= 0.0f;
        }

        if (overlapB0 < minOverlap * SAT_RELATIVE_AXIS_BIAS - SAT_ABSOLUTE_AXIS_SLOP)
        {
            minOverlap = overlapB0;
            minNormal = distB0 > 0.0f ? (-axesB[0]).eval() : axesB[0];
            swapped = true;
            refAxis = 0;
            refPositive = distB0 <= 0.0f;
        }

        if (overlapB1 < minOverlap && (swapped || overlapB1 < minOverlap * SAT_RELATIVE_AXIS_BIAS - SAT_ABSOLUTE_AXIS_SLOP))
        {
            minNormal = distB1 > 0.0f ? (-axesB[1]).eval() : axesB[1];
            swapped = true;
            refAxis = 1;
            refPositive = distB1 <= 0.0f;
        }

        referenceBox = swapped ? boxB : boxA;
        incidentBox = swapped ? boxA : boxB;

        // Derive (-minNormal)·incidentAxes directly from the already-computed orthonormal dots (r00, r01).
        const float sign = refPositive ? -1.0f : 1.0f;
        float incProjX;
        float incProjY;
        if (!swapped)
        {
            incProjX = sign * (refAxis == 0 ? r00 : -r01);
            incProjY = sign * (refAxis == 0 ? r01 : r00);
        }
        else
        {
            incProjX = sign * (refAxis == 0 ? r00 : r01);
            incProjY = sign * (refAxis == 0 ? -r01 : r00);
        }
        if (std::abs(incProjX) > std::abs(incProjY))
        {
            incAxis = 0;
            incPositive = incProjX > 0.0f;
        }
        else
        {
            incAxis = 1;
            incPositive = incProjY > 0.0f;
        }
    }

    Collision *collision = pair.getOrCreateCollisionNoEvent();
    collision->setIncidentObject(incidentBox);
    collision->setReferenceObject(referenceBox);
    collision->setNormal(minNormal);

    calculateBoxContactPoints(collision, referenceBox, incidentBox, refAxis, refPositive, incAxis, incPositive);
    // Float SAT can round a sub-ulp separating gap to overlap. Clipping must
    // produce an actual manifold before this pair contributes events or support.
    if (!collision->getContactPoints().count)
    {
        pair.clearCollisionNoEvent();
        return;
    }
    collision->updateSupportState(gravityDirection);
}
} // namespace

void updateContactGeometry(ColliderPair &pair, const Eigen::Vector2f &gravityDirection)
{
    const Collider *colliderA = pair.getObjectA();
    const Collider *colliderB = pair.getObjectB();

    if (!colliderA->overlapsTightBounds(*colliderB))
    {
        pair.clearCollisionNoEvent();
        return;
    }

    const ColliderType typeA = colliderA->getColliderType();
    const ColliderType typeB = colliderB->getColliderType();
    if (typeA == ColliderType::BoxCollider && typeB == ColliderType::BoxCollider)
    {
        boxBoxCollision(pair, static_cast<const BoxCollider *>(colliderA), static_cast<const BoxCollider *>(colliderB),
                        gravityDirection);
    }
    else if (typeA == ColliderType::CircleCollider && typeB == ColliderType::CircleCollider)
    {
        circleCircleCollision(pair, gravityDirection);
    }
    else
    {
        circleBoxCollision(pair, gravityDirection);
    }
}

namespace
{
void calculateBoxContactPoints(Collision *collision, const BoxCollider *referenceBox, const BoxCollider *incidentBox,
                               int refAxis, bool refPositive, int incAxis, bool incPositive)
{
    const Eigen::Vector2f &normal = collision->getNormal();

    Eigen::Vector2f referenceEdgeVector;
    const Edge referenceEdge = referenceBox->getFaceEdge(refAxis, refPositive, &referenceEdgeVector);
    const Edge incidentEdge = incidentBox->getFaceEdge(incAxis, incPositive);

    ClipVertex incidentVertices[2];
    incidentVertices[0] = ClipVertex(
        incidentEdge.v1, makeContactFeature(NO_EDGE, NO_EDGE, getStartVertexAdjacentEdge(incidentEdge.edgeNumber),
                                            incidentEdge.edgeNumber));
    incidentVertices[1] = ClipVertex(
        incidentEdge.v2, makeContactFeature(NO_EDGE, NO_EDGE, getEndVertexAdjacentEdge(incidentEdge.edgeNumber),
                                            incidentEdge.edgeNumber));

    ClipVertex clippedVertices1[2];
    ClipVertex clippedVertices2[2];

    const float o1 = referenceEdgeVector.dot(referenceEdge.v1);
    int clippedCount = clipSegmentToLine(clippedVertices1, incidentVertices, referenceEdgeVector, o1,
                                         getStartVertexAdjacentEdge(referenceEdge.edgeNumber));
    if (clippedCount < 2)
    {
        PLATFORMATOR_LOG("Clipping failed at first reference vertex\n");
        generateFallbackContactPoint(collision);
        return;
    }

    // Clip what's left of the incident edge by the second vertex of the reference edge.
    const float o2 = referenceEdgeVector.dot(referenceEdge.v2);
    clippedCount = clipSegmentToLine(clippedVertices2, clippedVertices1, -referenceEdgeVector, -o2,
                                     getEndVertexAdjacentEdge(referenceEdge.edgeNumber));
    if (clippedCount < 2)
    {
        PLATFORMATOR_LOG("Clipping failed at second reference vertex\n");
        generateFallbackContactPoint(collision);
        return;
    }

    const float max = normal.dot(referenceEdge.v1);

    ClipPointsWithData &dst = collision->getContactPoints();
    const auto warm = dst.snapshotWarmStart();

    uint32_t outCount = 0;
    for (int i = 0; i < clippedCount; ++i)
    {
        const float separation = normal.dot(clippedVertices2[i].point) - max;
        if (separation <= 0.0f)
        {
            ClipPointWithData &out = dst.points[outCount++];
            out.point = clippedVertices2[i];
            out.separation = separation;
            warm.restore(out, clippedVertices2[i].feature.key);
        }
    }
    dst.count = outCount;
}
} // namespace
} // namespace physics_geometry
