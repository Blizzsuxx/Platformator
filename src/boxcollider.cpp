#include "boxcollider.h"

#include "constants.h"
#include "helpers.h"
#include "rigidbody.h"

BoxCollider::BoxCollider(GameObject *gameObject, const float width, const float height)
    : Collider(gameObject, ComponentType::COLLIDER, ColliderType::BoxCollider), width(width), height(height), vertices(), normals(2)
{
}

BoxCollider::BoxCollider() : Collider(ComponentType::COLLIDER, ColliderType::BoxCollider), width(0.0f), height(0.0f), vertices(), normals(2)
{
}

BoxCollider::~BoxCollider()
{
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
    return normals;
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
    Eigen::Vector2f center = getWorldPosition();

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

        (vertices)[i].x() = xMinusXOrigin * this->getGameObject()->getCosRotation() - yMinusYOrigin * this->getGameObject()->getSinRotation() + xOrigin;
        (vertices)[i].y() = xMinusXOrigin * this->getGameObject()->getSinRotation() + yMinusYOrigin * this->getGameObject()->getCosRotation() + yOrigin;
    }
}

void BoxCollider::updateCollider()
{
    generateVertices();
    generateNormals();
    generateProjections();
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