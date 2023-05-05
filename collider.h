#pragma once

#include "gameobject.h"

class Collider : public Component
{
public:
    Collider(GameObject* gameObject, ComponentType type);
    ~Collider();
    
    virtual float getBoundingRadius() const = 0;

    int getLayer() const;
    void setLayer(const int layer);

    bool isTriggered() const;
    void setTrigger(const bool isTrigger);

    private:
        int layer;
        bool isTrigger;
};