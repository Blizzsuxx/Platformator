#pragma once

#include "behaviorfactoryregistry.h"
#include "mario_entity.h"

namespace mario
{
    class MarioCoin : public MarioEntity
    {
    public:
        MarioCoin();

        std::string getTypeName() const override;

        bool collect();
    };

    REGISTER_BEHAVIOR(MarioCoin);
} // namespace mario