#include "mario_helpers.h"

#include "animator.h"
#include "boxcollider.h"
#include "circlecollider.h"
#include "collider.h"

namespace mario
{
    Bounds getBounds(const GameObject *gameObject)
    {
        if (gameObject == nullptr)
        {
            return Bounds{Eigen::Vector2f::Zero(), Eigen::Vector2f::Zero()};
        }

        Collider *collider = static_cast<Collider *>(gameObject->getComponent(ComponentType::COLLIDER));
        if (collider == nullptr)
        {
            return Bounds{gameObject->getPosition(), Eigen::Vector2f::Zero()};
        }

        if (collider->getColliderType() == ColliderType::BoxCollider)
        {
            const BoxCollider *boxCollider = static_cast<const BoxCollider *>(collider);
            return Bounds{gameObject->getPosition(), Eigen::Vector2f(boxCollider->getWidth() * 0.5f, boxCollider->getHeight() * 0.5f)};
        }

        const CircleCollider *circleCollider = static_cast<const CircleCollider *>(collider);
        return Bounds{gameObject->getPosition(), Eigen::Vector2f::Constant(circleCollider->getRadius())};
    }

    void playClipIfChanged(Animator *animator, const std::string &name)
    {
        if (animator == nullptr || animator->getCurrentClipName() == name)
        {
            return;
        }

        animator->play(name);
    }
} // namespace mario