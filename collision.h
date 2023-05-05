#pragma once

#include "gameobject.h"

class Collision
{
public:
    Collision();
    Collision(Eigen::Vector2f* normal, float penetration, Eigen::Vector2f* contactPoint, GameObject* gameObjectA, GameObject* gameObjectB);
    ~Collision();

    // Getters
    const Eigen::Vector2f* getNormal() const;
    float getPenetration() const;
    const Eigen::Vector2f* getContactPoint() const;
    const GameObject* getGameObjectA() const;
    const GameObject* getGameObjectB() const;

    // Setters
    void setNormal(Eigen::Vector2f* normal);
    void setPenetration(const float penetration);
    void setContactPoint(Eigen::Vector2f* contactPoint);
    void setGameObjectA(GameObject* gameObjectA);
    void setGameObjectB(GameObject* gameObjectB);

private:
    Eigen::Vector2f* normal;
    float penetration;
    Eigen::Vector2f* contactPoint;
    GameObject* gameObjectA;
    GameObject* gameObjectB;
};