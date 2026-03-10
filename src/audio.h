#pragma once

#include <SDL3_mixer/SDL_mixer.h>
#include "gameobject.h"

class Audio : public Component
{
public:
    Audio(GameObject *gameObject);
    ~Audio();

    // Getters
    MIX_Audio *getAudio() const;

    // Setters
    void setAudio(MIX_Audio *audio);

private:
    MIX_Audio *audio;
};

template <>
struct ComponentTypeFor<Audio>
{
    static constexpr ComponentType value = ComponentType::AUDIO;
};