#pragma once

#include <cstddef>
#include <string>

#include <SDL3/SDL.h>

#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/objectreference.h"
#include "platformator/runtime.h"
#include "platformator/scriptregistration.h"

namespace mario
{
    class MarioPlayer;

    class MarioGame : public Behavior
    {
    public:
        MarioGame();
        ~MarioGame();

        static MarioGame &getInstance();

        void start() override;

        bool isGameWon() const;
        size_t getCoinCount() const;
        size_t getCollectedCoinCount() const;
        MarioPlayer *getPlayer() const;

        void onCoinCollected();
        void winLevel();

    private:
        static MarioGame *instance;

        SDL_Window *window;
        ObjectReference<GameObject> player;
        ObjectReference<Audio> bgm;
        MarioPlayer *playerScript;
        size_t totalCoins;
        size_t collectedCoins;
        bool gameWon;

        void updateWindowTitle();

        SERIALIZABLE_SCRIPT(
            MarioGame,
            totalCoins,
            collectedCoins,
            player,
            bgm);
    };
} // namespace mario