#include "mario_game.h"

#include <iostream>

#include "mario_coin.h"
#include "mario_helpers.h"
#include "mario_player.h"

namespace mario
{
    MarioGame *MarioGame::instance = nullptr;

    MarioGame::MarioGame(GameManager &gameManager, SDLWindow &window, Scene &scene)
        : gameManager(gameManager),
          window(window),
          scene(scene),
          player(nullptr),
          totalCoins(0),
          collectedCoins(0),
          gameWon(false)
    {
        instance = this;
        registerEventHooks();
    }

    MarioGame::~MarioGame()
    {
        if (instance == this)
        {
            instance = nullptr;
        }
    }

    MarioGame &MarioGame::getInstance()
    {
        return *instance;
    }

    void MarioGame::initializeScene()
    {
        player = nullptr;
        totalCoins = 0;
        for (GameObject *gameObject : gameManager.getGameObjects())
        {
            if (player == nullptr)
            {
                player = getBehavior<MarioPlayer>(gameObject);
            }

            if (getBehavior<MarioCoin>(gameObject) != nullptr)
            {
                totalCoins++;
            }
        }

        updateWindowTitle();
    }

    bool MarioGame::isGameWon() const
    {
        return gameWon;
    }

    size_t MarioGame::getCoinCount() const
    {
        return totalCoins;
    }

    size_t MarioGame::getCollectedCoinCount() const
    {
        return collectedCoins;
    }

    MarioPlayer *MarioGame::getPlayer() const
    {
        return player;
    }

    void MarioGame::onCoinCollected()
    {
        collectedCoins++;
        updateWindowTitle();
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

        std::cout << "Level clear. Coins collected: " << collectedCoins << '/' << totalCoins << '\n';
        updateWindowTitle();
    }

    void MarioGame::registerEventHooks()
    {
        window.addSdlListener([this](SDL_Event event, double)
                              {
            if (event.type != SDL_EVENT_KEY_DOWN)
            {
                return;
            }

            handleSdlKeyDown(event.key.key); });
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
        }
    }

    void MarioGame::saveScene() const
    {
        gameManager.saveScene(scene);
        std::cout << "Saved Mario example scene to " << scene.filePath << '\n';
    }

    void MarioGame::updateWindowTitle()
    {
        std::string title = "Platformator Mario Example - Coins " + std::to_string(collectedCoins) + "/" + std::to_string(totalCoins);
        if (gameWon)
        {
            title += " - Level Clear";
        }

        SDL_SetWindowTitle(window.getWindow(), title.c_str());
    }
} // namespace mario