#include "boxcollider.h"

BoxCollider::BoxCollider(GameObject *gameObject) : Collider(gameObject, ComponentType::COLLIDER), width(0.0f), height(0.0f), vertices(), normals(2)
{
}

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
    Eigen::Vector2f yAxis = projectOntoAxis(Y_AXIS);

    xProjections.getMin()->setProjectedPosition(xAxis.x());
    xProjections.getMax()->setProjectedPosition(xAxis.y());

    yProjections.getMin()->setProjectedPosition(yAxis.x());
    yProjections.getMax()->setProjectedPosition(yAxis.y());
}

void BoxCollider::generateVertices()
{
    const std::array<Eigen::Vector2f, 4> &extremePoints = getVertices();

    vertices[0] = Eigen::Vector2f(getGameObject()->getPosition().x() - width / 2, getGameObject()->getPosition().y() - height / 2);
    vertices[1] = Eigen::Vector2f(getGameObject()->getPosition().x() + width / 2, getGameObject()->getPosition().y() - height / 2);
    vertices[2] = Eigen::Vector2f(getGameObject()->getPosition().x() - width / 2, getGameObject()->getPosition().y() + height / 2);
    vertices[3] = Eigen::Vector2f(getGameObject()->getPosition().x() + width / 2, getGameObject()->getPosition().y() + height / 2);

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
    return width;
}

float BoxCollider::getHeight() const
{
    return height;
}

// Setters
void BoxCollider::setWidth(const float width)
{
    this->width = width;
}

void BoxCollider::setHeight(const float height)
{
    this->height = height;
}

void BoxCollider::updateCollider()
{
    generateVertices();
    generateNormals();
    generateProjections();
}

std::vector<Eigen::Vector2f> BoxCollider::getNormals(const Collider *other) const
{
    return normals;
}