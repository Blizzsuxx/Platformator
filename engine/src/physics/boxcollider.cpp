#include "platformator/boxcollider.h"

#include "core/constants.h"
#include "core/helpers.h"
#include "core/objectpool.h"
#include "platformator/rigidbody.h"
#include "core/inputvalidation.h"

void *BoxCollider::operator new(size_t size)
{
    return platformator_detail::allocatePooledObject(platformator_detail::PooledObjectKind::BoxCollider, size);
}

void BoxCollider::operator delete(void *ptr, size_t size) noexcept
{
    platformator_detail::deallocatePooledObject(platformator_detail::PooledObjectKind::BoxCollider, ptr, size);
}

void *BoxCollider::operator new(size_t size, const std::nothrow_t &) noexcept
{
    return platformator_detail::allocatePooledObjectNothrow(platformator_detail::PooledObjectKind::BoxCollider, size);
}

void BoxCollider::operator delete(void *ptr, const std::nothrow_t &) noexcept
{
    platformator_detail::deallocatePooledObject(platformator_detail::PooledObjectKind::BoxCollider, ptr);
}

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

BoxCollider::~BoxCollider()
{
}

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

Eigen::Vector2f BoxCollider::projectOntoAxis(const Eigen::Vector2f &axis) const
{
    const std::array<Eigen::Vector2f, 4> &extremePoints = getVertices();
    float min = axis.dot(extremePoints[0]);
    float max = min;

    for (size_t i = 1; i < extremePoints.size(); i++)
    {
        float projection = axis.dot(extremePoints[i]);

        if (projection < min)
        {
            min = projection;
        }
        else if (projection > max)
        {
            max = projection;
        }
    }

    return Eigen::Vector2f(min, max);
}

// Getters
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

// Setters
void BoxCollider::setWidth(const float width)
{
    platformator_detail::positive(width, "box.width");
    if (this->width == width)
        return;
    this->width = width;

    GameObject *gameObject = getGameObject();
    if (gameObject == nullptr)
    {
        return;
    }

    Rigidbody *rigidbodyComponent = gameObject->getComponent<Rigidbody>();
    if (rigidbodyComponent != nullptr)
    {
        rigidbodyComponent->refreshMomentOfInertiaCache();
    }

    scheduleSync();
}

void BoxCollider::setHeight(const float height)
{
    platformator_detail::positive(height, "box.height");
    if (this->height == height)
        return;
    this->height = height;

    GameObject *gameObject = getGameObject();
    if (gameObject == nullptr)
    {
        return;
    }

    Rigidbody *rigidbodyComponent = gameObject->getComponent<Rigidbody>();
    if (rigidbodyComponent != nullptr)
    {
        rigidbodyComponent->refreshMomentOfInertiaCache();
    }

    scheduleSync();
}

std::vector<Eigen::Vector2f> BoxCollider::getNormals(const Collider *other) const
{
    return {normals.begin(), normals.end()};
}

void BoxCollider::generateNormals()
{
    float c = getGameObject()->getCosRotation();
    float s = getGameObject()->getSinRotation();

    normals[0] = Eigen::Vector2f(c, s);
    normals[1] = Eigen::Vector2f(-s, c);
}

void BoxCollider::generateProjections()
{
    Eigen::Vector2f xAxis = projectOntoAxis(X_AXIS);

    xProjections.getMin()->setProjectedPosition(xAxis.x());
    // repairMinProjectionProxiesForProjection(xProjections.getMin(), &xProjections);

    xProjections.getMax()->setProjectedPosition(xAxis.y());
    // repairMaxProjectionProxiesForProjection(xProjections.getMax(), &xProjections);

    Eigen::Vector2f yAxis = projectOntoAxis(Y_AXIS);

    yProjections.getMin()->setProjectedPosition(yAxis.x());
    // repairMinProjectionProxiesForProjection(yProjections.getMin(), &yProjections);

    yProjections.getMax()->setProjectedPosition(yAxis.y());
    // repairMaxProjectionProxiesForProjection(yProjections.getMax(), &yProjections);
}

void BoxCollider::generateVertices()
{
    float scaledWidth = getWidth();
    float scaledHeight = getHeight();
    Eigen::Vector2f center = getCachedWorldPosition();

    // top left, top right, bottom right, bottom left
    vertices[0] = Eigen::Vector2f(center.x() - scaledWidth / 2, center.y() - scaledHeight / 2);
    vertices[1] = Eigen::Vector2f(center.x() + scaledWidth / 2, center.y() - scaledHeight / 2);
    vertices[2] = Eigen::Vector2f(center.x() + scaledWidth / 2, center.y() + scaledHeight / 2);
    vertices[3] = Eigen::Vector2f(center.x() - scaledWidth / 2, center.y() + scaledHeight / 2);

    // Rotate the extreme points
    float xOrigin = center.x();
    float yOrigin = center.y();

    for (size_t i = 0; i < vertices.size(); i++)
    {
        float xMinusXOrigin = (vertices)[i].x() - xOrigin;
        float yMinusYOrigin = (vertices)[i].y() - yOrigin;

        (vertices)[i].x() = xMinusXOrigin * this->getGameObject()->getCosRotation() -
                            yMinusYOrigin * this->getGameObject()->getSinRotation() + xOrigin;
        (vertices)[i].y() = xMinusXOrigin * this->getGameObject()->getSinRotation() +
                            yMinusYOrigin * this->getGameObject()->getCosRotation() + yOrigin;
    }
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
    xProjections.getMin()->setProjectedPosition(center.x() - extentX);
    xProjections.getMax()->setProjectedPosition(center.x() + extentX);
    yProjections.getMin()->setProjectedPosition(center.y() - extentY);
    yProjections.getMax()->setProjectedPosition(center.y() + extentY);
}

Edge BoxCollider::getEdgeWithNormal(const Eigen::Vector2f &normal) const
{
    float projectionOnLocalX = normal.dot(normals[0]);
    float projectionOnLocalY = normal.dot(normals[1]);

    if (std::abs(projectionOnLocalX) > std::abs(projectionOnLocalY))
    {
        if (projectionOnLocalX > 0.0f)
        {
            return Edge(vertices[1], vertices[1], vertices[2], EDGE4);
        }

        return Edge(vertices[3], vertices[3], vertices[0], EDGE2);
    }

    if (projectionOnLocalY > 0.0f)
    {
        return Edge(vertices[2], vertices[2], vertices[3], EDGE3);
    }

    return Edge(vertices[0], vertices[0], vertices[1], EDGE1);
}
