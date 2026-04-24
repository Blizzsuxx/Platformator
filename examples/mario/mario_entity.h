#pragma once

#include "gameobject.h"

namespace mario
{
    class MarioEntity
    {
    public:
        explicit MarioEntity(GameObject *gameObject) : gameObject(gameObject)
        {
        }

        virtual ~MarioEntity() = default;

        virtual void update(double)
        {
        }

        GameObject *getGameObject() const
        {
            return gameObject;
        }

        bool isActive() const
        {
            return gameObject != nullptr && gameObject->getActive();
        }

    protected:
        GameObject *gameObject;
    };
} // namespace mario