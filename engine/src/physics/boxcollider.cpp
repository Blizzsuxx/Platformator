#include "platformator/boxcollider.h"

#include "core/inputvalidation.h"
#include "core/objectpool.h"

PLATFORMATOR_DEFINE_POOLED_ALLOCATORS(BoxCollider, BoxCollider)

BoxCollider::BoxCollider(GameObject *gameObject, const float width, const float height)
    : Collider(gameObject, ComponentType::COLLIDER, ColliderType::BoxCollider), width(width), height(height),
      publishedHalfWidth(width * 0.5f), publishedHalfHeight(height * 0.5f), vertices(),
      normals{Eigen::Vector2f::UnitX(), Eigen::Vector2f::UnitY()}
{
    platformator_detail::positive(width, "box.width");
    platformator_detail::positive(height, "box.height");
}

BoxCollider::BoxCollider()
    : Collider(ComponentType::COLLIDER, ColliderType::BoxCollider), width(1.0f), height(1.0f),
      publishedHalfWidth(0.5f), publishedHalfHeight(0.5f), vertices(),
      normals{Eigen::Vector2f::UnitX(), Eigen::Vector2f::UnitY()}
{
}

BoxCollider::~BoxCollider() = default;

Component *BoxCollider::clone() const
{
    auto cloned = std::make_unique<BoxCollider>();
    cloned->setWidth(width);
    cloned->setHeight(height);
    cloned->setOffset(offset);
    cloned->setIsTrigger(getIsTrigger());
    cloned->setCollisionGroup(collisionGroup);
    cloned->setCollisionMask(collisionMask);
    return cloned.release();
}

const std::array<Eigen::Vector2f, 4> &BoxCollider::getVertices() const
{
    return vertices;
}

float BoxCollider::getWidth() const
{
    return width * getGameObject()->getScale().x();
}

float BoxCollider::getWidthWithoutScale() const
{
    return width;
}

float BoxCollider::getHeight() const
{
    return height * getGameObject()->getScale().y();
}

float BoxCollider::getHeightWithoutScale() const
{
    return height;
}

void BoxCollider::setWidth(const float width)
{
    platformator_detail::positive(width, "box.width");
    if (this->width == width)
        return;
    this->width = width;
    notifyShapeChanged();
}

void BoxCollider::setHeight(const float height)
{
    platformator_detail::positive(height, "box.height");
    if (this->height == height)
        return;
    this->height = height;
    notifyShapeChanged();
}

void BoxCollider::updateCollider()
{
    const GameObject *object = getGameObject();
    const Eigen::Vector2f &scale = object->getScale();
    publishedHalfWidth = width * scale.x() * 0.5f;
    publishedHalfHeight = height * scale.y() * 0.5f;

    const float c = object->getCosRotation();
    const float s = object->getSinRotation();
    normals[0] = Eigen::Vector2f(c, s);
    normals[1] = Eigen::Vector2f(-s, c);

    const Eigen::Vector2f &center = getCachedWorldPosition();
    const float hxX = c * publishedHalfWidth;
    const float hxY = s * publishedHalfWidth;
    const float hyX = -s * publishedHalfHeight;
    const float hyY = c * publishedHalfHeight;

    // top left, top right, bottom right, bottom left
    vertices[0] = Eigen::Vector2f(center.x() - hxX - hyX, center.y() - hxY - hyY);
    vertices[1] = Eigen::Vector2f(center.x() + hxX - hyX, center.y() + hxY - hyY);
    vertices[2] = Eigen::Vector2f(center.x() + hxX + hyX, center.y() + hxY + hyY);
    vertices[3] = Eigen::Vector2f(center.x() - hxX + hyX, center.y() - hxY + hyY);

    const float extentX = std::abs(hxX) + std::abs(hyX);
    const float extentY = std::abs(hxY) + std::abs(hyY);
    setTightBounds(center.x() - extentX, center.x() + extentX, center.y() - extentY, center.y() + extentY);
}
