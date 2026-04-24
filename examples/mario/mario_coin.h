#pragma once

#include "mario_entity.h"

namespace mario
{
    class MarioGame;

    class MarioCoin : public MarioEntity
    {
    public:
        explicit MarioCoin(GameObject *gameObject);

        bool collect(MarioGame &game);
    };
} // namespace mario