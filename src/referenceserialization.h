#pragma once

#include <unordered_set>
#include <vector>
#include "platformator/baseobject.h"

class GameObject;
namespace platformator_detail
{
class SceneReferenceScope
{
  public:
    explicit SceneReferenceScope(const std::vector<GameObject *> &roots);
    ~SceneReferenceScope();
    SceneReferenceScope(const SceneReferenceScope &) = delete;
    SceneReferenceScope &operator=(const SceneReferenceScope &) = delete;

  private:
    void add(const GameObject &object);
    std::unordered_set<const BaseObject *> objects;
    const std::unordered_set<const BaseObject *> *previous;
};
} // namespace platformator_detail
