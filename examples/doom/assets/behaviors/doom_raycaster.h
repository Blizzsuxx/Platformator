#pragma once

#include <array>
#include <unordered_map>
#include <vector>

#include "doom_constants.h"
#include "platformator/assetreference.h"
#include "platformator/behavior.h"
#include "platformator/camera.h"
#include "platformator/objectreference.h"
#include "platformator/scriptregistration.h"
#include "platformator/sprite.h"

namespace doom
{
    class DoomRaycaster : public Behavior
    {
      public:
        DoomRaycaster();

        void start() override;
        void fixedUpdate(double timeDelta) override;

        int getLastWallHits() const;
        int getLastRenderedBillboardStrips() const;

      private:
        struct TexDims
        {
            float w = 64.0f;
            float h = 64.0f;
        };

        struct PooledSprite
        {
            GameObject *object = nullptr;
            Sprite *sprite = nullptr;
            bool active = true;
        };

        ObjectReference<Camera> camera;
        platformator::TextureAssetRef ceilingTex;
        platformator::TextureAssetRef floorTex;
        platformator::TextureAssetRef hudBarTex;
        platformator::TextureAssetRef digitsTex;
        platformator::TextureAssetRef faceTex;
        platformator::TextureAssetRef crosshairTex;
        platformator::TextureAssetRef keyIconTex;
        platformator::TextureAssetRef whiteTex;
        platformator::TextureAssetRef defaultWallTex;
        platformator::TextureAssetRef pistolTex;
        platformator::TextureAssetRef pistolFireTex;
        platformator::TextureAssetRef shotgunTex;
        platformator::TextureAssetRef shotgunFireTex;
        platformator::TextureAssetRef plasmaTex;
        platformator::TextureAssetRef plasmaFireTex;

        std::array<PooledSprite, RAY_COLUMNS> wallColumns{};
        std::array<PooledSprite, MAX_BILLBOARD_STRIPS> billboardStrips{};
        std::array<float, RAY_COLUMNS> wallDepth{};

        PooledSprite ceilingBackdrop{};
        PooledSprite floorBackdrop{};
        PooledSprite weaponViewmodel{};
        PooledSprite screenFlashOverlay{};
        PooledSprite crosshairSprite{};
        PooledSprite hudBarSprite{};
        PooledSprite faceSprite{};
        PooledSprite keyBadgeSprite{};
        std::array<PooledSprite, 3> weaponBadges{};
        std::array<PooledSprite, 3> ammoDigits{};
        std::array<PooledSprite, 3> hpDigits{};
        std::array<PooledSprite, 3> armorDigits{};
        std::array<PooledSprite, 2> killDigits{};

        std::unordered_map<const TextureWrapper *, TexDims> texDimsCache;
        bool poolCreated = false;
        int lastWallHits = 0;
        int lastRenderedBillboardStrips = 0;

        void ensureStagePool();
        PooledSprite createStageSprite(const std::string &name, TextureWrapper *tex, float w, float h,
                                       std::int32_t zOrder, float x, float y);
        void parkSprite(PooledSprite &slot);
        TexDims getTexDims(TextureWrapper *wrapper);
        void render3DView();
        void updateHud(int hp, int armor, int ammo, int kills, int weapon, bool hasShotgun, bool hasPlasma,
                       bool hasRedKey, float muzzleLight, SDL_Color flashColor);
        void setDigitGroup(std::array<PooledSprite, 3> &digits, int value, float rightEdgeX, float centerY);
        void setDigitPair(std::array<PooledSprite, 2> &digits, int value, float rightEdgeX, float centerY);

        SERIALIZABLE_SCRIPT(
            DoomRaycaster,
            camera,
            ceilingTex,
            floorTex,
            hudBarTex,
            digitsTex,
            faceTex,
            crosshairTex,
            keyIconTex,
            whiteTex,
            defaultWallTex,
            pistolTex,
            pistolFireTex,
            shotgunTex,
            shotgunFireTex,
            plasmaTex,
            plasmaFireTex);
    };
} // namespace doom
