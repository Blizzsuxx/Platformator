#pragma once

#include "behaviorfactoryregistry.h"
#include "mario_entity.h"

namespace mario
{
    class MarioGoalFlag : public MarioEntity
    {
    public:
        MarioGoalFlag();

        std::string getTypeName() const override;

        bool reach();
    };

    REGISTER_BEHAVIOR(MarioGoalFlag);
} // namespace mario