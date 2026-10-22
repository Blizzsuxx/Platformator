#include "physicsmanager.h"
#include "platformator/boxcollider.h"
#include "platformator/circlecollider.h"
#include <cmath>

void PhysicsManager::circleCircleCollision(const ColliderPair &pair)
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
    collision->updateSupportState(gravityVectorNormalized);
}

void PhysicsManager::circleBoxCollision(const ColliderPair &pair)
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
    collision->updateSupportState(gravityVectorNormalized);
}
