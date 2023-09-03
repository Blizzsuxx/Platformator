#include "boxcollider.h"

RectCollider::RectCollider(GameObject *gameObject) : Collider(gameObject, ComponentType::COLLIDER)
{
}

RectCollider::RectCollider(GameObject *gameObject, const float width, const float height) 
    : Collider(gameObject, ComponentType::COLLIDER), width(width), height(height)
{
}

RectCollider::~RectCollider()
{
}

// Inherited via Collider
float RectCollider::getBoundingBoxLengthX() const
{
}

float RectCollider::getBoundingBoxLengthY() const
{
}

ColliderType RectCollider::getColliderType() const
{
    return ColliderType::BoxCollider;
}

std::vector<Eigen::Vector2f*> *RectCollider::getNormals(Collider *other)
{
    std::vector<Eigen::Vector2f*> *normals(new std::vector<Eigen::Vector2f*>(2));
    (*normals)[0] = new Eigen::Vector2f(1, 0);
    (*normals)[1] = new Eigen::Vector2f(0, 1);

    // Rotate the normals
    float angle = getGameObject()->getRotation();
    float xOrigin = getGameObject()->getPosition().x();
    float yOrigin = getGameObject()->getPosition().y();
    float cosAngle = cos(angle);
    float sinAngle = sin(angle);

    for (int i = 0; i < 2; i++)
    {
        (*normals)[i]->x() = (*normals)[i]->x() * cosAngle - (*normals)[i]->y() * sinAngle;
        (*normals)[i]->y() = (*normals)[i]->x() * sinAngle + (*normals)[i]->y() * cosAngle;
    }

    return normals;
}

std::unique_ptr<std::vector<Eigen::Vector2f>> RectCollider::getVertices()
{
    std::unique_ptr<std::vector<Eigen::Vector2f>> extremePoints(new std::vector<Eigen::Vector2f>(4));

    (*extremePoints)[0] = Eigen::Vector2f(getGameObject()->getPosition().x() - width / 2, getGameObject()->getPosition().y() - height / 2);
    (*extremePoints)[1] = Eigen::Vector2f(getGameObject()->getPosition().x() + width / 2, getGameObject()->getPosition().y() - height / 2);
    (*extremePoints)[2] = Eigen::Vector2f(getGameObject()->getPosition().x() - width / 2, getGameObject()->getPosition().y() + height / 2);
    (*extremePoints)[3] = Eigen::Vector2f(getGameObject()->getPosition().x() + width / 2, getGameObject()->getPosition().y() + height / 2);
    
    // Rotate the extreme points
    float angle = getGameObject()->getRotation();
    float xOrigin = getGameObject()->getPosition().x();
    float yOrigin = getGameObject()->getPosition().y();
    float cosAngle = cos(angle);
    float sinAngle = sin(angle);

    for (int i = 0; i < 4; i++)
    {
        float xMinusXOrigin = (*extremePoints)[i].x() - xOrigin;
        float yMinusYOrigin = (*extremePoints)[i].y() - yOrigin;
        
        (*extremePoints)[i].x() = xMinusXOrigin * cosAngle - yMinusYOrigin * sinAngle + xOrigin;
        (*extremePoints)[i].y() = xMinusXOrigin * sinAngle + yMinusYOrigin * cosAngle + yOrigin;
    }

    return extremePoints;
}

std::unique_ptr<Eigen::Vector2f> RectCollider::projectOntoAxis(const Eigen::Vector2f &axis)
{
    std::unique_ptr<std::vector<Eigen::Vector2f>> extremePoints = getVertices();
    float min = axis.dot((*extremePoints)[0]);
    float max = min;

    for (int i = 1; i < 4; i++)
    {
        float projection = axis.dot((*extremePoints)[i]);

        if (projection < min)
        {
            min = projection;
        }
        else if (projection > max)
        {
            max = projection;
        }
    }

    return std::unique_ptr<Eigen::Vector2f>(new Eigen::Vector2f(min, max));
}

std::unique_ptr<Eigen::Vector2f> RectCollider::projectOntoAxis(const Eigen::Vector2f &axis, size_t index)
{
    float pos = axis.dot(getGameObject()->getPosition());

    if (index == 0)
    {
        return std::unique_ptr<Eigen::Vector2f>(new Eigen::Vector2f(pos - width / 2, pos + width / 2));
    }
    else
    {
        return std::unique_ptr<Eigen::Vector2f>(new Eigen::Vector2f(pos - height / 2, pos + height / 2));
    }
}

// Getters
float RectCollider::getWidth() const
{
    return width;
}

float RectCollider::getHeight() const
{
    return height;
}

// Setters
void RectCollider::setWidth(const float width)
{
    this->width = width;
}

void RectCollider::setHeight(const float height)
{
    this->height = height;
}
