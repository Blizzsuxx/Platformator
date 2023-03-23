#pragma once

#include "gameobject.h"

class Camera : public Component
{
public:
    Camera(GameObject* gameObject);
    ~Camera();

    // Getters
    float getZoom() const;
    float getRotation() const;
    float getAspectRatio() const;
    float getNearPlane() const;
    float getFarPlane() const;

    // Setters
    void setZoom(const float zoom);
    void setRotation(const float rotation);
    void setAspectRatio(const float aspectRatio);
    void setNearPlane(const float nearPlane);
    void setFarPlane(const float farPlane);

private:
    float zoom;
    float rotation;
    float aspectRatio;
    float nearPlane;
    float farPlane;
};