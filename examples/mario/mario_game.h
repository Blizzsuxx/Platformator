#pragma once

#include <cstddef>
#include <string>

#include <SDL3/SDL.h>

#include "audio.h"
#include "gamemanager.h"
#include "scene.h"
#include "sdlwindow.h"

namespace mario
{
    class MarioPlayer;

    class MarioGame
    {
    public:
        MarioGame(GameManager &gameManager, SDLWindow &window, Scene &scene);
        ~MarioGame();

        static MarioGame &getInstance();

        void initializeScene();
        bool isGameWon() const;
        size_t getCoinCount() const;
        size_t getCollectedCoinCount() const;
        MarioPlayer *getPlayer() const;

        void onCoinCollected();
        void winLevel();

        void playJumpSound();
        void playCoinSound();
        void playStompSound();
        void playHurtSound();
        void playWinSound();

    private:
        static MarioGame *instance;

        GameManager &gameManager;
        SDLWindow &window;
        Scene &scene;
        MarioPlayer *player;
        Audio *jumpAudio;
        Audio *coinAudio;
        Audio *stompAudio;
        Audio *hurtAudio;
        Audio *winAudio;
        size_t totalCoins;
        size_t collectedCoins;
        bool gameWon;

        void registerEventHooks();
        void handleSdlKeyDown(SDL_Keycode key);
        void saveScene() const;
        void updateWindowTitle();
        Audio *getAudioEmitter(const std::string &name) const;
        void replayAudio(Audio *audio) const;
    };
} // namespace mario