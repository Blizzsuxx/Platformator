#include "mario_game.h"

#include <iostream>

#include "mario_coin.h"
#include "mario_player.h"

namespace mario
{
    MarioGame *MarioGame::instance = nullptr;

    MarioGame::MarioGame()
        : gameManager(GameManager::getInstance()),
          window(*gameManager.getWindow()),
          player(),
          playerScript(nullptr),
          totalCoins(0),
          collectedCoins(0),
          gameWon(false)
    {
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

    void MarioGame::start()
    {
        instance = this;
        playerScript = getBehavior<MarioPlayer>(player.get());
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
        return playerScript;
    }

    void MarioGame::onCoinCollected()
    {
        collectedCoins++;
        updateWindowTitle();
    }

    void MarioGame::winLevel()
    {
        gameWon = true;
        playerScript->stopForWin();
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