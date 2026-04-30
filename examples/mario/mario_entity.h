#pragma once

#include "animator.h"
#include "audio.h"
#include "behavior.h"

namespace mario
{
    class MarioEntity : public Behavior
    {
    public:
        bool isActive() const
        {
            return getGameObject()->getActive();
        }

    protected:
        Animator *getAnimator() const
        {
            GameObject *gameObject = getGameObject();
            if (gameObject == nullptr)
            {
                return nullptr;
            }

            return gameObject->getComponent<Animator>();
        }

        Audio *getAudioEmitter() const
        {
            GameObject *gameObject = getGameObject();
            if (gameObject == nullptr)
            {
                return nullptr;
            }

            return gameObject->getComponent<Audio>();
        }

        void playSound(AudioWrapper *audioAsset) const
        {
            Audio *audioEmitter = getAudioEmitter();
            if (audioEmitter != nullptr)
            {
                audioEmitter->replay(audioAsset);
            }
        }
    };
} // namespace mario