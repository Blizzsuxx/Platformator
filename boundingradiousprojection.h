#pragma once

#include "gameobject.h"

class BoundingRadiusProjection
{
public:
    BoundingRadiusProjection();
    BoundingRadiusProjection(GameObject* gameObject, float min, float max);
    ~BoundingRadiusProjection();

    GameObject* getGameObject();
    float getMin() const;
    float getMax() const;

    void setGameObject(GameObject* gameObject);
    void setMin(float min);
    void setMax(float max);

    bool isOverlapping(BoundingRadiusProjection* other);
    
    bool operator==(const BoundingRadiusProjection& other) const;
    bool operator!=(const BoundingRadiusProjection& other) const;
    bool operator<(const BoundingRadiusProjection& other) const;
    bool operator>(const BoundingRadiusProjection& other) const;
    bool operator<=(const BoundingRadiusProjection& other) const;
    bool operator>=(const BoundingRadiusProjection& other) const;

private:
    GameObject* gameObject;
    float min;
    float max;
};