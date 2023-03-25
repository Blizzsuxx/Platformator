#include "gameobject.h"

GameObject::GameObject() : GameObject(0.0f, true, Eigen::Vector2f(0.0f, 0.0f), Eigen::Vector2f(1.0f, 1.0f), "GameObject", NULL)
{
}

GameObject::GameObject(const float angle, const bool active, const Eigen::Vector2f& position, const Eigen::Vector2f& scale, const std::string& name, const std::string& tag)
: angle(angle), active(active), position(position), scale(scale), name(name), tag(tag), children(), components()
{
}
