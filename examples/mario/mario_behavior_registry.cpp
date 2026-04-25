#include "mario_behavior_registry.h"

#include "behaviorfactoryregistry.h"
#include "mario_camera_rig.h"
#include "mario_coin.h"
#include "mario_goal_flag.h"
#include "mario_patrol_enemy.h"
#include "mario_player.h"

namespace mario
{
    void registerMarioBehaviorFactories()
    {
        static bool registered = false;
        if (registered)
        {
            return;
        }

        BehaviorFactoryRegistry &registry = BehaviorFactoryRegistry::getInstance();
        registry.registerBehavior<MarioPlayer>("MarioPlayer");
        registry.registerBehavior<MarioPatrolEnemy>("MarioPatrolEnemy");
        registry.registerBehavior<MarioCoin>("MarioCoin");
        registry.registerBehavior<MarioGoalFlag>("MarioGoalFlag");
        registry.registerBehavior<MarioCameraRig>("MarioCameraRig");

        registered = true;
    }
} // namespace mario