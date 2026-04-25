#pragma once

#include "mario_entity.h"

namespace mario
{
    class MarioCoin : public MarioEntity
    {
    public:
        MarioCoin();

        std::string getTypeName() const override;
        void deserialize(const ScriptDescriptor &descriptor) override;
        void serialize(ScriptDescriptor &descriptor) const override;

        bool collect();
    };
} // namespace mario