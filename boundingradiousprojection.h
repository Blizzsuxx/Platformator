#pragma once

#include "collider.h"

class BoundingRadiusProjection
{
public:
    BoundingRadiusProjection();
    BoundingRadiusProjection(Collider*, float projectedPosition);
    ~BoundingRadiusProjection();

    Collider* getCollider();
    float getProjectedPosition() const;

    void setCollider(Collider* collider);
    void setProjectedPosition(float projectedPosition);

    bool operator==(const BoundingRadiusProjection& other) const;
    bool operator!=(const BoundingRadiusProjection& other) const;
    bool operator<(const BoundingRadiusProjection& other) const;
    bool operator>(const BoundingRadiusProjection& other) const;
    bool operator<=(const BoundingRadiusProjection& other) const;
    bool operator>=(const BoundingRadiusProjection& other) const;

private:
    Collider* collider;
    float projectedPosition;
};