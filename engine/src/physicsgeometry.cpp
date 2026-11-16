#include "physicsgeometry.h"
#include "colliderpair.h"
#include "constants.h"
#include "debugdraw.h"
#include "helpers.h"
#include "platformator/boxcollider.h"
#include "platformator/circlecollider.h"
#include <cmath>
#include <limits>
#include <span>

namespace physics_geometry
{
namespace
{
// Circle dispatch builds its own manifold; clipping below is exclusively box/box.
void calculateBoxContactPoints(Collision *collision);

void generateFallbackContactPoint(Collision *collision)
{
    const auto *referenceObject = collision->getReferenceObject();
    const auto *incidentObject = collision->getIncidentObject();
    // As a fallback, we can use the midpoint between the two objects as the contact point, with a penetration of 0.
    Eigen::Vector2f positionA = referenceObject->getGameObject()->getPosition();
    Eigen::Vector2f positionB = incidentObject->getGameObject()->getPosition();
    Eigen::Vector2f midpoint = (positionA + positionB) * 0.5f;

    ClipVertex fallbackContactPoint(midpoint, ContactFeature());
    ClipPoints fallbackClipPoints;
    fallbackClipPoints.add(fallbackContactPoint);
    collision->setContactPoints(fallbackClipPoints);
}

void circleCircleCollision(const ColliderPair &pair, const Eigen::Vector2f &gravityDirection)
{
    const auto *a = static_cast<const CircleCollider *>(pair.getObjectA());
    const auto *b = static_cast<const CircleCollider *>(pair.getObjectB());
    const Eigen::Vector2f delta = b->getCachedWorldPosition() - a->getCachedWorldPosition();
    const float radius = a->getRadius() + b->getRadius();
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
    ClipPoints points;
    points.add((a->getCachedWorldPosition() + normal * a->getRadius() + b->getCachedWorldPosition() -
                normal * b->getRadius()) *
                   0.5f,
               ContactFeature(0x100));
    collision->setContactPoints(points);
    collision->getContactPoints().points[0].separation = distance - radius;
    collision->updateSupportState(gravityDirection);
}

void circleBoxCollision(const ColliderPair &pair, const Eigen::Vector2f &gravityDirection)
{
    const bool boxIsA = pair.getObjectA()->getColliderType() == ColliderType::BoxCollider;
    const auto *box = static_cast<const BoxCollider *>(boxIsA ? pair.getObjectA() : pair.getObjectB());
    const auto *circle = static_cast<const CircleCollider *>(boxIsA ? pair.getObjectB() : pair.getObjectA());
    const auto &axes = box->getAxes();
    const Eigen::Vector2f delta = circle->getCachedWorldPosition() - box->getCachedWorldPosition();
    const Eigen::Vector2f local(delta.dot(axes[0]), delta.dot(axes[1]));
    const Eigen::Vector2f half(box->getWidth() * 0.5f, box->getHeight() * 0.5f);
    const Eigen::Vector2f closest = local.cwiseMax(-half).cwiseMin(half);
    const Eigen::Vector2f gap = local - closest;
    const float radius = circle->getRadius(), squared = gap.squaredNorm();
    if (squared > radius * radius)
    {
        pair.clearCollisionNoEvent();
        return;
    }
    Eigen::Vector2f localNormal;
    float separation;
    uint32_t feature = 0x200;
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
    ClipPoints points;
    points.add(circle->getCachedWorldPosition() - normal * radius, ContactFeature(feature));
    collision->setContactPoints(points);
    collision->getContactPoints().points[0].separation = separation;
    collision->updateSupportState(gravityDirection);
}

bool checkProjections(std::span<const Eigen::Vector2f> normals, const Collider *referenceCollider,
                      const Collider *incidentCollider, float &minOverlap, Eigen::Vector2f &minNormal,
                      const Collider *&realIncidentCollider)
{
    for (long unsigned int i = 0; i < normals.size(); i++)
    {
        const Eigen::Vector2f &normal = normals[i];
        Eigen::Vector2f projections1 = referenceCollider->projectOntoAxis(
            normal); // we are using the index on the owner of the normal to further optimize projection
        Eigen::Vector2f projections2 = incidentCollider->projectOntoAxis(
            normal); // TODO: see if it's better to approximate from the perspective of the incident or reference object

        if (projections1.y() < projections2.x() || projections2.y() < projections1.x())
        {
            return false;
        }
        else
        {
            float overlap = std::min(projections1.y() - projections2.x(), projections2.y() - projections1.x());
            if (overlap < minOverlap &&
                (realIncidentCollider == incidentCollider || overlap < minOverlap * 0.98f - 0.001f))
            {
                minOverlap = overlap;
                minNormal = normal;
                realIncidentCollider = incidentCollider;
            }
        }
    }

    return true;
}
} // namespace

void updateContactGeometry(const ColliderPair &pair, const Eigen::Vector2f &gravityDirection)
{
    const Collider *referenceCollider = pair.getObjectA();
    const Collider *incidentCollider = pair.getObjectB();
    if (referenceCollider->getColliderType() == ColliderType::CircleCollider &&
        incidentCollider->getColliderType() == ColliderType::CircleCollider)
    {
        circleCircleCollision(pair, gravityDirection);
        return;
    }
    if (referenceCollider->getColliderType() == ColliderType::CircleCollider ||
        incidentCollider->getColliderType() == ColliderType::CircleCollider)
    {
        circleBoxCollision(pair, gravityDirection);
        return;
    }

    float minOverlap = std::numeric_limits<float>::max();
    Eigen::Vector2f minNormal;
    const Collider *realIncidentCollider = incidentCollider;

    std::span<const Eigen::Vector2f> normals = static_cast<const BoxCollider *>(referenceCollider)->getAxes();
    if (checkProjections(normals, referenceCollider, incidentCollider, minOverlap, minNormal, realIncidentCollider) ==
        false)
    {
        pair.clearCollisionNoEvent();
        return;
    }

    normals = static_cast<const BoxCollider *>(incidentCollider)->getAxes();

    if (checkProjections(normals, incidentCollider, referenceCollider, minOverlap, minNormal, realIncidentCollider) ==
        false)
    {
        pair.clearCollisionNoEvent();
        return;
    }

    Collision *collision = pair.getOrCreateCollisionNoEvent();

    collision->clearSupportState();

    collision->setIncidentObject(realIncidentCollider);
    collision->setReferenceObject(realIncidentCollider == incidentCollider ? referenceCollider : incidentCollider);

    // Ensure the normal points from reference toward incident
    Eigen::Vector2f directionVector = collision->getIncidentObject()->getCachedWorldPosition() -
                                      collision->getReferenceObject()->getCachedWorldPosition();
    if (minNormal.dot(directionVector) < 0)
    {
        minNormal = -minNormal;
    }

    // Store the corrected normal
    collision->setNormal(minNormal);

    calculateBoxContactPoints(collision);
    // Float SAT can round a sub-ulp separating gap to overlap. Clipping must
    // produce an actual manifold before this pair contributes events or support.
    if (!collision->getContactPoints().count)
    {
        pair.clearCollisionNoEvent();
        return;
    }
    collision->updateSupportState(gravityDirection);
}

namespace
{
void calculateBoxContactPoints(Collision *collision)
{
    const Collider *referenceCollider = collision->getReferenceObject();
    const Collider *incidentCollider = collision->getIncidentObject();
    const Eigen::Vector2f &normal = collision->getNormal();

    const Edge referenceEdge = referenceCollider->getEdgeWithNormal(normal);
    const Edge incidentEdge = incidentCollider->getEdgeWithNormal(-normal);

    // the edge vector
    Eigen::Vector2f referenceEdgeVector = referenceEdge.getEdgeVector();
    referenceEdgeVector.normalize();

    ClipVertex incidentVertices[2];

    incidentVertices[0] = ClipVertex(
        incidentEdge.v1, makeContactFeature(NO_EDGE, NO_EDGE, getStartVertexAdjacentEdge(incidentEdge.edgeNumber),
                                            incidentEdge.edgeNumber));
    incidentVertices[1] = ClipVertex(
        incidentEdge.v2, makeContactFeature(NO_EDGE, NO_EDGE, getEndVertexAdjacentEdge(incidentEdge.edgeNumber),
                                            incidentEdge.edgeNumber));

    ClipVertex clippedVertices1[2];
    ClipVertex clippedVertices2[2];

    double o1 = referenceEdgeVector.dot(referenceEdge.v1);
    int clippedCount = clipSegmentToLine(clippedVertices1, incidentVertices, referenceEdgeVector, o1,
                                         getStartVertexAdjacentEdge(referenceEdge.edgeNumber));
    if (clippedCount < 2)
    {
        PLATFORMATOR_LOG("Clipping failed at first reference vertex\n");
        generateFallbackContactPoint(collision);
        return;
    }

    // clip whats left of the incident edge by the
    // second vertex of the reference edge
    // but we need to clip in the opposite direction
    // so we flip the direction and offset
    double o2 = referenceEdgeVector.dot(referenceEdge.v2);
    clippedCount = clipSegmentToLine(clippedVertices2, clippedVertices1, -referenceEdgeVector, -o2,
                                     getEndVertexAdjacentEdge(referenceEdge.edgeNumber));
    if (clippedCount < 2)
    {
        PLATFORMATOR_LOG("Clipping failed at second reference vertex\n");
        generateFallbackContactPoint(collision);
        return;
    }

    double max = normal.dot(referenceEdge.max);

    ClipPoints cp;
    for (int i = 0; i < clippedCount; ++i)
    {
        float separation = normal.dot(clippedVertices2[i].point) - max;
        if (separation <= 0.0f)
        {
            cp.add(clippedVertices2[i]);
        }
    }

    collision->setContactPoints(cp);

    ClipPointsWithData &contactPoints = collision->getContactPoints();
    for (size_t i = 0; i < contactPoints.count; i++)
    {
        float separation = normal.dot(cp.points[i].point) - max;
        contactPoints.points[i].separation = separation;
    }
}
} // namespace
} // namespace physics_geometry
