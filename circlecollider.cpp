#include "circlecollider.h"

CircleCollider::CircleCollider(GameObject *gameObject) : Collider(gameObject, ComponentType::COLLIDER)
{
}

CircleCollider::~CircleCollider()
{
}

// Inherited via Collider
float CircleCollider::getBoundingBoxLengthX() const
{
    return radius;
}

float CircleCollider::getBoundingBoxLengthY() const
{
    return radius;
}

ColliderType CircleCollider::getColliderType() const
{
    return ColliderType::CircleCollider;
}

// Getters
float CircleCollider::getRadius() const
{
    return radius;
}

// Setters
void CircleCollider::setRadius(const float radius)
{
    this->radius = radius;
}

std::auto_ptr<std::vector<Eigen::Vector2f>> CircleCollider::getNormals(Collider *other)
{
    std::auto_ptr<std::vector<Eigen::Vector2f>> normals(new std::vector<Eigen::Vector2f>(1));
    (*normals)[0] = Eigen::Vector2f(other->getGameObject()->getPosition() - getGameObject()->getPosition());
    (*normals)[0].normalize();
    
    return normals;
}

std::auto_ptr<std::vector<Eigen::Vector2f>> CircleCollider::getVertices()
{
    std::auto_ptr<std::vector<Eigen::Vector2f>> extremePoints(new std::vector<Eigen::Vector2f>(1));
    (*extremePoints)[0] = getGameObject()->getPosition();
    
    return extremePoints;
}

std::auto_ptr<Eigen::Vector2f> CircleCollider::projectOntoAxis(const Eigen::Vector2f &axis)
{
    float value = axis.dot(getGameObject()->getPosition());

    return std::auto_ptr<Eigen::Vector2f>(new Eigen::Vector2f(value - radius, value + radius));
}

std::auto_ptr<Eigen::Vector2f> CircleCollider::projectOntoAxis(const Eigen::Vector2f &axis, size_t index)
{
    return projectOntoAxis(axis);
}
