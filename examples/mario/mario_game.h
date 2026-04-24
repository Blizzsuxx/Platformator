#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "audio.h"
#include "gamemanager.h"
#include "mario_camera_rig.h"
#include "mario_coin.h"
#include "mario_goal_flag.h"
#include "mario_patrol_enemy.h"
#include "mario_player.h"
#include "scene.h"
#include "sdlwindow.h"

namespace mario
{
    class MarioGame
    {
    public:
        MarioGame(GameManager &gameManager, SDLWindow &window, Scene &scene);

        void update(double timeDelta);
        bool isGameWon() const;
        size_t getCoinCount() const;
        size_t getCollectedCoinCount() const;

        MarioPatrolEnemy *findEnemy(GameObject *gameObject) const;
        MarioCoin *findCoin(GameObject *gameObject) const;
        MarioGoalFlag *findGoal(GameObject *gameObject) const;

        void onCoinCollected();
        void winLevel();

        void playJumpSound();
        void playCoinSound();
        void playStompSound();
        void playHurtSound();
        void playWinSound();

    private:
        GameManager &gameManager;
        SDLWindow &window;
        Scene &scene;
        std::unique_ptr<MarioPlayer> player;
        std::unique_ptr<MarioCameraRig> cameraRig;
        std::vector<std::unique_ptr<MarioPatrolEnemy>> enemies;
        std::vector<std::unique_ptr<MarioCoin>> coins;
        std::unique_ptr<MarioGoalFlag> goal;
        std::unordered_map<GameObject *, MarioPatrolEnemy *> enemiesByObject;
        std::unordered_map<GameObject *, MarioCoin *> coinsByObject;
        Audio *jumpAudio;
        Audio *coinAudio;
        Audio *stompAudio;
        Audio *hurtAudio;
        Audio *winAudio;
        size_t collectedCoins;
        bool gameWon;
        bool titleDirty;

        void cacheSceneObjects();
        void registerEventHooks();
        void handleSdlKeyDown(SDL_Keycode key);
        void saveScene() const;
        void updateWindowTitle();
        Audio *getAudioEmitter(const std::string &name) const;
        void replayAudio(Audio *audio) const;
    };
} // namespace mario