#pragma once

#include "gameobject.h"

constexpr auto _START = __LINE__;
enum ComponentType
{
    ANIMATOR = 0,
    AUDIO = 1,
    CAMERA = 2,
    COLLIDER = 3,
    LIGHT = 4,
    RIDIDBODY = 5,
    SPRITE = 6
};
constexpr auto COMPONENT_TYPE_COUNT = __LINE__ - _START - 4;

class Component
{
public:
    Component(GameObject* gameObject, ComponentType type);
    virtual ~Component();

    GameObject* getGameObject();
    ComponentType getType();
private:
    GameObject* gameObject;
    ComponentType type;
};