#pragma once

#include "mario_entity.h"

namespace mario
{
    class MarioGoalFlag : public MarioEntity
    {
    public:
        MarioGoalFlag();

        std::string getTypeName() const override;
        void deserialize(const ScriptDescriptor &descriptor) override;
        void serialize(ScriptDescriptor &descriptor) const override;

        bool reach();
    };
} // namespace mario