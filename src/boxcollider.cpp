#include "boxcollider.h"
#include "helpers.h"
#include "rigidbody.h"

BoxCollider::BoxCollider(GameObject *gameObject, const float width, const float height)
    : Collider(gameObject, ComponentType::COLLIDER), width(width), height(height), vertices(), normals(2)
{
}

BoxCollider::~BoxCollider()
{
}

ColliderType BoxCollider::getColliderType() const
{
    return ColliderType::BoxCollider;
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

float BoxCollider::getHeight() const
{
    return height * getGameObject()->getScale().y();
}

// Setters
void BoxCollider::setWidth(const float width)
{
    this->width = width;

    Rigidbody *rigidbodyComponent = getGameObject()->getComponent<Rigidbody>();
    if (rigidbodyComponent != nullptr)
    {
        rigidbodyComponent->refreshMomentOfInertiaCache();
    }

    scheduleSync();
}

void BoxCollider::setHeight(const float height)
{
    this->height = height;

    Rigidbody *rigidbodyComponent = getGameObject()->getComponent<Rigidbody>();
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
    repairMinProjectionProxiesForProjection(xProjections.getMin(), &xProjections);

    xProjections.getMax()->setProjectedPosition(xAxis.y());
    repairMaxProjectionProxiesForProjection(xProjections.getMax(), &xProjections);

    Eigen::Vector2f yAxis = projectOntoAxis(Y_AXIS);

    yProjections.getMin()->setProjectedPosition(yAxis.x());
    repairMinProjectionProxiesForProjection(yProjections.getMin(), &yProjections);

    yProjections.getMax()->setProjectedPosition(yAxis.y());
    repairMaxProjectionProxiesForProjection(yProjections.getMax(), &yProjections);
}

void BoxCollider::generateVertices()
{
    float scaledWidth = getWidth();
    float scaledHeight = getHeight();

    // top left, top right, bottom right, bottom left
    vertices[0] = Eigen::Vector2f(getGameObject()->getPosition().x() - scaledWidth / 2, getGameObject()->getPosition().y() - scaledHeight / 2);
    vertices[1] = Eigen::Vector2f(getGameObject()->getPosition().x() + scaledWidth / 2, getGameObject()->getPosition().y() - scaledHeight / 2);
    vertices[2] = Eigen::Vector2f(getGameObject()->getPosition().x() + scaledWidth / 2, getGameObject()->getPosition().y() + scaledHeight / 2);
    vertices[3] = Eigen::Vector2f(getGameObject()->getPosition().x() - scaledWidth / 2, getGameObject()->getPosition().y() + scaledHeight / 2);

    // Rotate the extreme points
    float xOrigin = getGameObject()->getPosition().x();
    float yOrigin = getGameObject()->getPosition().y();

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
    float maxDot = -std::numeric_limits<float>::infinity();
    size_t bestEdgeIndex = 0;

    for (size_t i = 0; i < vertices.size(); i++)
    {
        float dot = vertices[i].dot(normal);
        if (dot > maxDot)
        {
            maxDot = dot;
            bestEdgeIndex = i;
        }
    }

    size_t nextVertexIndex = (bestEdgeIndex + 1) % vertices.size();
    size_t previousVertexIndex = (bestEdgeIndex + vertices.size() - 1) % vertices.size();

    Eigen::Vector2f left = vertices[bestEdgeIndex] - vertices[nextVertexIndex];
    Eigen::Vector2f right = vertices[bestEdgeIndex] - vertices[previousVertexIndex];

    left.normalize();
    right.normalize();

    if (right.dot(normal) <= left.dot(normal))
    {
        return Edge(vertices[bestEdgeIndex], vertices[previousVertexIndex], vertices[bestEdgeIndex]);
    }
    else
    {
        return Edge(vertices[bestEdgeIndex], vertices[bestEdgeIndex], vertices[nextVertexIndex]);
    }
}