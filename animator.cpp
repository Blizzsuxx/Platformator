#include "animator.h"

Animator::Animator(GameObject *gameObject) : Component(gameObject, ComponentType::ANIMATOR)
{
}

Animator::~Animator()
{
}