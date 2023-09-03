#pragma once

#include "gameobject.h"

class Collision
{
public:
    Collision();
    Collision(GameObject *gameObjectA, GameObject *gameObjectB);
    Collision(Eigen::Vector2f *normal, float penetration, Eigen::Vector2f *contactPoint, GameObject *gameObjectA, GameObject *gameObjectB);
    ~Collision();

    // Getters
    const Eigen::Vector2f *getNormal() const;
    float getPenetration() const;
    const Eigen::Vector2f *getContactPoint() const;
    GameObject *getReferenceObject();
    GameObject *getIncidentObject();

    // Setters
    void setNormal(Eigen::Vector2f *normal);
    void setPenetration(const float penetration);
    void setContactPoint(Eigen::Vector2f *contactPoint);
    void setReferenceObject(GameObject *gameObjectA);
    void setIncidentObject(GameObject *gameObjectB);

private:
    Eigen::Vector2f *normal;
    float penetration;
    Eigen::Vector2f *contactPoint;
    GameObject *referenceObject;
    GameObject *incidentObject;
};