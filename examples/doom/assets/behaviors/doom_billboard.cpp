#include "doom_billboard.h"

#include "doom_game.h"

namespace doom
{
    DoomBillboard::DoomBillboard() = default;

    DoomBillboard::~DoomBillboard()
    {
        if (registered)
        {
            if (auto *game = DoomGameManager::tryGetInstance())
            {
                game->unregisterBillboard(this);
            }
            registered = false;
        }
    }

    void DoomBillboard::start()
    {
        ensureRegistered();
    }

    void DoomBillboard::update(double)
    {
        if (!registered)
        {
            ensureRegistered();
        }
    }

    void DoomBillboard::onDestroy()
    {
        if (registered)
        {
            if (auto *game = DoomGameManager::tryGetInstance())
            {
                game->unregisterBillboard(this);
            }
            registered = false;
        }
    }

    void DoomBillboard::ensureRegistered()
    {
        if (registered)
        {
            return;
        }
        if (auto *game = DoomGameManager::tryGetInstance())
        {
            game->registerBillboard(this);
            registered = true;
        }
    }

    float DoomBillboard::getWorldHeight() const
    {
        return worldHeight;
    }

    float DoomBillboard::getVerticalOffset() const
    {
        return verticalOffset;
    }

    bool DoomBillboard::isFullBright() const
    {
        return fullBright;
    }

    TextureRegion DoomBillboard::getCurrentRegion() const
    {
        GameObject *obj = getGameObject();
        if (obj == nullptr)
        {
            return {};
        }
        Sprite *sprite = obj->getComponent<Sprite>();
        if (sprite == nullptr)
        {
            return {};
        }
        return sprite->getTextureRegion();
    }

    SDL_Color DoomBillboard::getTint() const
    {
        GameObject *obj = getGameObject();
        if (obj == nullptr)
        {
            return SDL_Color{255, 255, 255, 255};
        }
        Sprite *sprite = obj->getComponent<Sprite>();
        return sprite != nullptr ? sprite->getColor() : SDL_Color{255, 255, 255, 255};
    }
} // namespace doom
