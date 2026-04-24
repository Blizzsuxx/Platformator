#include "mario_game.h"

#include <iostream>

namespace mario
{
    MarioGame::MarioGame(GameManager &gameManager, SDLWindow &window, Scene &scene)
        : gameManager(gameManager),
          window(window),
          scene(scene),
          player(),
          cameraRig(),
          enemies(),
          coins(),
          goal(),
          enemiesByObject(),
          coinsByObject(),
          jumpAudio(nullptr),
          coinAudio(nullptr),
          stompAudio(nullptr),
          hurtAudio(nullptr),
          winAudio(nullptr),
          collectedCoins(0),
          gameWon(false),
          titleDirty(true)
    {
        cacheSceneObjects();
        registerEventHooks();
        updateWindowTitle();
    }

    void MarioGame::update(double timeDelta)
    {
        if (player != nullptr)
        {
            player->update(*this, timeDelta);
        }

        for (const std::unique_ptr<MarioPatrolEnemy> &enemy : enemies)
        {
            enemy->update(timeDelta);
        }

        if (cameraRig != nullptr)
        {
            cameraRig->update();
        }

        if (titleDirty)
        {
            updateWindowTitle();
        }
    }

    bool MarioGame::isGameWon() const
    {
        return gameWon;
    }

    size_t MarioGame::getCoinCount() const
    {
        return coins.size();
    }

    size_t MarioGame::getCollectedCoinCount() const
    {
        return collectedCoins;
    }

    MarioPatrolEnemy *MarioGame::findEnemy(GameObject *gameObject) const
    {
        auto it = enemiesByObject.find(gameObject);
        return it != enemiesByObject.end() ? it->second : nullptr;
    }

    MarioCoin *MarioGame::findCoin(GameObject *gameObject) const
    {
        auto it = coinsByObject.find(gameObject);
        return it != coinsByObject.end() ? it->second : nullptr;
    }

    MarioGoalFlag *MarioGame::findGoal(GameObject *gameObject) const
    {
        if (goal == nullptr || goal->getGameObject() != gameObject)
        {
            return nullptr;
        }

        return goal.get();
    }

    void MarioGame::onCoinCollected()
    {
        collectedCoins++;
        titleDirty = true;
        playCoinSound();
    }

    void MarioGame::winLevel()
    {
        if (gameWon)
        {
            return;
        }

        gameWon = true;
        if (player != nullptr)
        {
            player->stopForWin();
        }

        titleDirty = true;
        playWinSound();
        std::cout << "Level clear. Coins collected: " << collectedCoins << '/' << coins.size() << '\n';
    }

    void MarioGame::playJumpSound()
    {
        replayAudio(jumpAudio);
    }

    void MarioGame::playCoinSound()
    {
        replayAudio(coinAudio);
    }

    void MarioGame::playStompSound()
    {
        replayAudio(stompAudio);
    }

    void MarioGame::playHurtSound()
    {
        replayAudio(hurtAudio);
    }

    void MarioGame::playWinSound()
    {
        replayAudio(winAudio);
    }

    void MarioGame::cacheSceneObjects()
    {
        jumpAudio = getAudioEmitter("SFX Jump");
        coinAudio = getAudioEmitter("SFX Coin");
        stompAudio = getAudioEmitter("SFX Stomp");
        hurtAudio = getAudioEmitter("SFX Hurt");
        winAudio = getAudioEmitter("SFX Win");

        if (GameObject *playerObject = gameManager.getGameObject("Player"))
        {
            player = std::make_unique<MarioPlayer>(playerObject);
        }

        size_t enemyIndex = 0;
        for (GameObject *gameObject : gameManager.getGameObjects())
        {
            if (gameObject == nullptr)
            {
                continue;
            }

            const std::string &tag = gameObject->getTag();
            if (tag == "enemy")
            {
                const float spawnX = gameObject->getX();
                const float patrolHalfRange = enemyIndex == 0 ? 90.0f : 110.0f;
                auto enemy = std::make_unique<MarioPatrolEnemy>(gameObject, spawnX - patrolHalfRange, spawnX + patrolHalfRange, enemyIndex % 2 == 0 ? -1.0f : 1.0f);
                enemiesByObject[gameObject] = enemy.get();
                enemies.push_back(std::move(enemy));
                enemyIndex++;
                continue;
            }

            if (tag == "coin")
            {
                auto coin = std::make_unique<MarioCoin>(gameObject);
                coinsByObject[gameObject] = coin.get();
                coins.push_back(std::move(coin));
                continue;
            }

            if (tag == "goal")
            {
                goal = std::make_unique<MarioGoalFlag>(gameObject);
            }
        }

        cameraRig = std::make_unique<MarioCameraRig>(window.getMainCamera(), player.get());
    }

    void MarioGame::registerEventHooks()
    {
        if (player != nullptr)
        {
            player->registerCallbacks(*this);
        }

        window.addSdlListener([this](SDL_Event event, double)
                              {
        if (event.type != SDL_EVENT_KEY_DOWN)
        {
            return;
        }

        handleSdlKeyDown(event.key.key); });

        gameManager.addUserScriptListeners([this](double timeDelta)
                                           { update(timeDelta); });
    }

    void MarioGame::handleSdlKeyDown(SDL_Keycode key)
    {
        if (key == SDLK_F7)
        {
            try
            {
                saveScene();
            }
            catch (const std::exception &exception)
            {
                std::cout << "Failed to save Mario example scene: " << exception.what() << '\n';
            }
            return;
        }

        if (player != nullptr)
        {
            player->handleKeyDown(key);
            titleDirty = true;
        }
    }

    void MarioGame::saveScene() const
    {
        gameManager.saveScene(scene);
        std::cout << "Saved Mario example scene to " << scene.filePath << '\n';
    }

    void MarioGame::updateWindowTitle()
    {
        if (window.getWindow() == nullptr)
        {
            return;
        }

        std::string title = "Platformator Mario Example - Coins " + std::to_string(collectedCoins) + "/" + std::to_string(coins.size());
        if (gameWon)
        {
            title += " - Level Clear";
        }

        SDL_SetWindowTitle(window.getWindow(), title.c_str());
        titleDirty = false;
    }

    Audio *MarioGame::getAudioEmitter(const std::string &name) const
    {
        GameObject *audioObject = gameManager.getGameObject(name);
        return audioObject != nullptr ? audioObject->getComponent<Audio>() : nullptr;
    }

    void MarioGame::replayAudio(Audio *audio) const
    {
        if (audio == nullptr)
        {
            return;
        }

        audio->stop();
        audio->play();
    }
} // namespace mario