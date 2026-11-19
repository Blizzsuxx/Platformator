#include "doom_raycaster.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "doom_billboard.h"
#include "doom_game.h"
#include "doom_player.h"
#include "platformator/boxcollider.h"
#include "platformator/circlecollider.h"
#include "platformator/collider.h"
#include "platformator/runtime.h"

namespace doom
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        std::int32_t depthToZOrder(float perpDist)
        {
            const float clamped = std::clamp(perpDist * 10.0f, 1.0f, 1500000.0f);
            return -static_cast<std::int32_t>(clamped);
        }

        std::uint8_t mulChannel(std::uint8_t base, float factor)
        {
            return static_cast<std::uint8_t>(std::clamp(static_cast<float>(base) * factor, 0.0f, 255.0f));
        }

        std::uint8_t tintChannel(std::uint8_t entityMod, std::uint8_t fogShade)
        {
            return static_cast<std::uint8_t>((static_cast<int>(entityMod) * static_cast<int>(fogShade)) / 255);
        }
    } // namespace

    DoomRaycaster::DoomRaycaster() = default;

    void DoomRaycaster::start()
    {
        ensureStagePool();
        render3DView();
    }

    void DoomRaycaster::fixedUpdate(double)
    {
        ensureStagePool();
        auto *game = DoomGameManager::tryGetInstance();
        auto *player = game ? game->getPlayer() : nullptr;
        Camera *cam = camera.get() ? camera.get() : (game ? game->getCamera() : getRuntime().getMainCamera());
        if (cam == nullptr || cam->getGameObject() == nullptr)
        {
            return;
        }

        if (game && game->isMapViewMode() && player != nullptr && player->getGameObject() != nullptr)
        {
            const Eigen::Vector2f p = player->getGameObject()->getPosition();
            cam->setWidth(960.0f);
            cam->setHeight(540.0f);
            cam->getGameObject()->setPosition(Eigen::Vector2f(p.x() - 480.0f, p.y() - 270.0f));
            return;
        }

        cam->setWidth(VIEW_WIDTH);
        cam->setHeight(VIEW_HEIGHT);
        cam->getGameObject()->setPosition(Eigen::Vector2f(STAGE_ORIGIN_X, STAGE_ORIGIN_Y));
        render3DView();
    }

    int DoomRaycaster::getLastWallHits() const
    {
        return lastWallHits;
    }

    int DoomRaycaster::getLastRenderedBillboardStrips() const
    {
        return lastRenderedBillboardStrips;
    }

    DoomRaycaster::TexDims DoomRaycaster::getTexDims(TextureWrapper *wrapper)
    {
        if (wrapper == nullptr)
        {
            return TexDims{};
        }
        if (const auto found = texDimsCache.find(wrapper); found != texDimsCache.end())
        {
            return found->second;
        }
        TexDims dims{};
        if (SDL_Texture *tex = wrapper->getTexture())
        {
            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
            float w = 0.0f;
            float h = 0.0f;
            if (SDL_GetTextureSize(tex, &w, &h) && w > 0.0f && h > 0.0f)
            {
                dims.w = w;
                dims.h = h;
            }
        }
        texDimsCache.emplace(wrapper, dims);
        return dims;
    }

    DoomRaycaster::PooledSprite DoomRaycaster::createStageSprite(const std::string &name, TextureWrapper *tex, float w,
                                                                   float h, std::int32_t zOrder, float x, float y)
    {
        GameObject *obj = getRuntime().createGameObject();
        obj->setName(name);
        obj->setTag("stage3d");
        obj->setPosition(Eigen::Vector2f(x, y));
        obj->addComponent<Sprite>(tex, w, h);
        Sprite *spr = obj->getComponent<Sprite>();
        spr->setZOrder(zOrder);
        getTexDims(tex);
        return PooledSprite{obj, spr, true};
    }

    void DoomRaycaster::parkSprite(PooledSprite &slot)
    {
        if (!slot.active || slot.object == nullptr || slot.sprite == nullptr)
        {
            return;
        }
        slot.object->setPosition(Eigen::Vector2f(PARK_X, PARK_Y));
        slot.sprite->setWidth(1.0f);
        slot.sprite->setHeight(1.0f);
        slot.active = false;
    }

    void DoomRaycaster::ensureStagePool()
    {
        if (poolCreated)
        {
            return;
        }
        poolCreated = true;

        // Prime nearest-neighbor texture filtering for HUD & weapon sheets
        getTexDims(ceilingTex.get());
        getTexDims(floorTex.get());
        getTexDims(hudBarTex.get());
        getTexDims(digitsTex.get());
        getTexDims(faceTex.get());
        getTexDims(crosshairTex.get());
        getTexDims(keyIconTex.get());
        getTexDims(whiteTex.get());
        getTexDims(defaultWallTex.get());
        getTexDims(pistolTex.get());
        getTexDims(pistolFireTex.get());
        getTexDims(shotgunTex.get());
        getTexDims(shotgunFireTex.get());
        getTexDims(plasmaTex.get());
        getTexDims(plasmaFireTex.get());

        const float centerX = STAGE_ORIGIN_X + VIEW_WIDTH * 0.5f;
        ceilingBackdrop = createStageSprite("Stage_Ceiling", ceilingTex.get(), VIEW_WIDTH, VIEW3D_HEIGHT * 0.5f,
                                            Z_CEILING, centerX, STAGE_ORIGIN_Y + VIEW3D_HEIGHT * 0.25f);
        floorBackdrop = createStageSprite("Stage_Floor", floorTex.get(), VIEW_WIDTH, VIEW3D_HEIGHT * 0.5f,
                                          Z_FLOOR, centerX, STAGE_ORIGIN_Y + VIEW3D_HEIGHT * 0.75f);

        for (int c = 0; c < RAY_COLUMNS; ++c)
        {
            wallColumns[c] = createStageSprite("Stage_Col_" + std::to_string(c), defaultWallTex.get(), 1.0f, 1.0f,
                                               Z_FLOOR + 1, PARK_X, PARK_Y);
            wallColumns[c].active = false;
        }
        for (int b = 0; b < MAX_BILLBOARD_STRIPS; ++b)
        {
            billboardStrips[b] = createStageSprite("Stage_BB_" + std::to_string(b), defaultWallTex.get(), 1.0f, 1.0f,
                                                   Z_FLOOR + 2, PARK_X, PARK_Y);
            billboardStrips[b].active = false;
        }

        weaponViewmodel = createStageSprite("Stage_Weapon", pistolTex.get(), 150.0f, 150.0f, Z_WEAPON, centerX,
                                            STAGE_ORIGIN_Y + VIEW3D_HEIGHT - 75.0f);
        screenFlashOverlay = createStageSprite("Stage_Flash", whiteTex.get(), VIEW_WIDTH, VIEW3D_HEIGHT, Z_FLASH,
                                               PARK_X, PARK_Y);
        screenFlashOverlay.active = false;
        crosshairSprite = createStageSprite("Stage_Crosshair", crosshairTex.get(), 12.0f, 12.0f, Z_CROSSHAIR,
                                            centerX, STAGE_ORIGIN_Y + VIEW3D_HEIGHT * 0.5f);

        const float hudCenterY = STAGE_ORIGIN_Y + VIEW3D_HEIGHT + HUD_HEIGHT * 0.5f;
        hudBarSprite = createStageSprite("Stage_HudBar", hudBarTex.get(), VIEW_WIDTH, HUD_HEIGHT, Z_HUD_BG,
                                         centerX, hudCenterY);
        faceSprite = createStageSprite("Stage_Face", faceTex.get(), 40.0f, 40.0f, Z_HUD_FG, STAGE_ORIGIN_X + 320.0f,
                                       hudCenterY);
        keyBadgeSprite = createStageSprite("Stage_KeyBadge", keyIconTex.get(), 28.0f, 20.0f, Z_HUD_FG, PARK_X, PARK_Y);
        keyBadgeSprite.active = false;

        for (int i = 0; i < 3; ++i)
        {
            weaponBadges[i] = createStageSprite("Stage_WpnBadge_" + std::to_string(i), whiteTex.get(), 14.0f, 4.0f,
                                                Z_HUD_FG, STAGE_ORIGIN_X + 216.0f + i * 24.0f, hudCenterY + 14.0f);
        }
        for (int i = 0; i < 3; ++i)
        {
            ammoDigits[i] = createStageSprite("Stage_AmmoDig_" + std::to_string(i), digitsTex.get(), 12.0f, 20.0f,
                                              Z_HUD_FG, PARK_X, PARK_Y);
            ammoDigits[i].active = false;
            hpDigits[i] = createStageSprite("Stage_HpDig_" + std::to_string(i), digitsTex.get(), 12.0f, 20.0f,
                                            Z_HUD_FG, PARK_X, PARK_Y);
            hpDigits[i].active = false;
            armorDigits[i] = createStageSprite("Stage_ArmDig_" + std::to_string(i), digitsTex.get(), 12.0f, 20.0f,
                                               Z_HUD_FG, PARK_X, PARK_Y);
            armorDigits[i].active = false;
        }
        for (int i = 0; i < 2; ++i)
        {
            killDigits[i] = createStageSprite("Stage_KillDig_" + std::to_string(i), digitsTex.get(), 12.0f, 20.0f,
                                              Z_HUD_FG, PARK_X, PARK_Y);
            killDigits[i].active = false;
        }
    }

    void DoomRaycaster::render3DView()
    {
        auto *game = DoomGameManager::tryGetInstance();
        auto *player = game ? game->getPlayer() : nullptr;
        if (player == nullptr || player->getGameObject() == nullptr)
        {
            return;
        }

        auto &runtime = getRuntime();
        const Eigen::Vector2f playerPos = player->getGameObject()->getPosition();
        const Eigen::Vector2f forward = player->getForward();
        const Eigen::Vector2f right = player->getRight();

        const float halfFovTan = std::tan(FOV_RADIANS * 0.5f);
        const float focalFactor = (VIEW_WIDTH * 0.5f) / halfFovTan;
        constexpr float kColumnWidth = VIEW_WIDTH / static_cast<float>(RAY_COLUMNS); // 2.0f
        constexpr float kViewTop = STAGE_ORIGIN_Y;
        constexpr float kViewBot = STAGE_ORIGIN_Y + VIEW3D_HEIGHT;
        const float horizonY = STAGE_ORIGIN_Y + VIEW3D_HEIGHT * 0.5f + player->getBobOffset() * 0.35f;
        const float muzzleLight = player->getMuzzleLight();

        // Keep backdrop horizon aligned gently with head bob
        ceilingBackdrop.object->setPosition(Eigen::Vector2f(STAGE_ORIGIN_X + VIEW_WIDTH * 0.5f,
                                                            (kViewTop + horizonY) * 0.5f));
        ceilingBackdrop.sprite->setHeight(std::max(1.0f, horizonY - kViewTop));
        floorBackdrop.object->setPosition(Eigen::Vector2f(STAGE_ORIGIN_X + VIEW_WIDTH * 0.5f,
                                                          (horizonY + kViewBot) * 0.5f));
        floorBackdrop.sprite->setHeight(std::max(1.0f, kViewBot - horizonY));

        // 1. Raycast 320 vertical wall & door columns
        int wallHits = 0;
        for (int c = 0; c < RAY_COLUMNS; ++c)
        {
            const float cameraX = 2.0f * (static_cast<float>(c) + 0.5f) / static_cast<float>(RAY_COLUMNS) - 1.0f;
            const Eigen::Vector2f rayVec = forward + right * (cameraX * halfFovTan);
            const float rayLen = rayVec.norm();
            const Eigen::Vector2f rayDir = rayLen > 1e-6f ? (rayVec / rayLen).eval() : forward;

            const auto hit = runtime.raycast(playerPos, rayDir, MAX_VIEW_DIST, WALL_RAY_MASK, false);
            PooledSprite &col = wallColumns[c];
            if (!hit.has_value())
            {
                wallDepth[c] = MAX_VIEW_DIST;
                parkSprite(col);
                continue;
            }

            ++wallHits;
            const float cosAngle = std::max(0.15f, rayDir.dot(forward));
            const float perpDist = std::max(4.0f, hit->distance * cosAngle);
            wallDepth[c] = perpDist;

            const float projHeight = std::min(2800.0f, (44.0f * focalFactor) / perpDist);
            const float topUnclipped = horizonY - projHeight * 0.5f;
            const float botUnclipped = horizonY + projHeight * 0.5f;
            const float topClipped = std::max(kViewTop, topUnclipped);
            const float botClipped = std::min(kViewBot, botUnclipped);
            if (botClipped <= topClipped)
            {
                parkSprite(col);
                continue;
            }

            // Compute tiled texture coordinate u in [0, 1)
            float u = 0.0f;
            if (hit->collider != nullptr && hit->collider->getColliderType() == ColliderType::BoxCollider)
            {
                const auto *box = static_cast<const BoxCollider *>(hit->collider);
                const Eigen::Vector2f xAxis = box->getPublishedXAxis();
                const Eigen::Vector2f yAxis(-xAxis.y(), xAxis.x());
                const Eigen::Vector2f localDelta = hit->point - box->getCachedWorldPosition();
                const float nLocalX = std::abs(hit->normal.dot(xAxis));
                const float nLocalY = std::abs(hit->normal.dot(yAxis));
                if (nLocalX > nLocalY)
                {
                    const float along = localDelta.dot(yAxis) + box->getHeight() * 0.5f;
                    u = along / WALL_TILE_WORLD;
                }
                else
                {
                    const float along = localDelta.dot(xAxis) + box->getWidth() * 0.5f;
                    u = along / WALL_TILE_WORLD;
                }
            }
            else
            {
                const float angle = std::atan2(hit->normal.y(), hit->normal.x());
                u = (angle / (2.0f * kPi) + 0.5f) * 2.0f;
            }
            u -= std::floor(u);

            TextureWrapper *wallTex = defaultWallTex.get();
            SDL_Color wallTint{255, 255, 255, 255};
            if (hit->gameObject != nullptr)
            {
                if (Sprite *srcSprite = hit->gameObject->getComponent<Sprite>())
                {
                    if (srcSprite->getTextureWrapper() != nullptr)
                    {
                        wallTex = srcSprite->getTextureWrapper();
                    }
                    wallTint = srcSprite->getColor();
                }
            }
            const TexDims dims = getTexDims(wallTex);
            const float srcX = std::clamp(std::floor(u * dims.w), 0.0f, std::max(0.0f, dims.w - 1.0f));
            const float v0 = (topClipped - topUnclipped) / projHeight;
            const float v1 = (botClipped - topUnclipped) / projHeight;
            const float srcY = std::clamp(v0 * dims.h, 0.0f, std::max(0.0f, dims.h - 0.1f));
            const float srcH = std::clamp((v1 - v0) * dims.h, 0.1f, dims.h - srcY);

            // Distance fog + normal directional shading + muzzle light flash
            const float dirLight = 0.72f + 0.28f * std::abs(hit->normal.x());
            const float fog = std::clamp(1.12f - (perpDist / (MAX_VIEW_DIST * 0.92f)), 0.12f, 1.0f);
            const float flashBoost = muzzleLight * 0.35f * std::clamp(1.0f - perpDist / 380.0f, 0.0f, 1.0f);
            const float light = std::clamp(dirLight * fog + flashBoost, 0.10f, 1.0f);

            col.active = true;
            col.sprite->setTextureWrapper(wallTex);
            col.sprite->setSourceRect(SDL_FRect{srcX, srcY, 1.0f, srcH});
            col.sprite->setWidth(kColumnWidth);
            col.sprite->setHeight(botClipped - topClipped);
            col.sprite->setColor(mulChannel(wallTint.r, light), mulChannel(wallTint.g, light),
                                 mulChannel(wallTint.b, light), 255);
            col.sprite->setZOrder(depthToZOrder(perpDist));
            col.object->setPosition(Eigen::Vector2f(STAGE_ORIGIN_X + (static_cast<float>(c) + 0.5f) * kColumnWidth,
                                                    (topClipped + botClipped) * 0.5f));
        }
        lastWallHits = wallHits;

        // 2. Render depth-sliced animated billboards against the 1D wall depth buffer
        int usedStrips = 0;
        if (game != nullptr)
        {
            for (DoomBillboard *billboard : game->getBillboards())
            {
                if (usedStrips >= MAX_BILLBOARD_STRIPS)
                {
                    break;
                }
                if (billboard == nullptr || billboard->getGameObject() == nullptr)
                {
                    continue;
                }
                const TextureRegion region = billboard->getCurrentRegion();
                TextureWrapper *tex = region.getTextureWrapper();
                if (tex == nullptr)
                {
                    continue;
                }

                const Eigen::Vector2f toEntity = billboard->getGameObject()->getPosition() - playerPos;
                const float depth = toEntity.dot(forward);
                if (depth <= 10.0f || depth >= MAX_VIEW_DIST)
                {
                    continue;
                }
                const float side = toEntity.dot(right);
                const float screenCenterX = VIEW_WIDTH * 0.5f + (side / depth) * focalFactor;

                const TexDims texDims = getTexDims(tex);
                const float regX = region.hasSourceRect ? region.sourceRect.x : 0.0f;
                const float regY = region.hasSourceRect ? region.sourceRect.y : 0.0f;
                const float regW = region.hasSourceRect && region.sourceRect.w > 0.0f ? region.sourceRect.w : texDims.w;
                const float regH = region.hasSourceRect && region.sourceRect.h > 0.0f ? region.sourceRect.h : texDims.h;

                const float projH = std::min(1800.0f, (billboard->getWorldHeight() * focalFactor) / depth);
                const float projW = projH * (regW / std::max(1.0f, regH));
                if (projW < 1.0f || projH < 1.0f)
                {
                    continue;
                }

                float topUnclipped = 0.0f;
                float botUnclipped = 0.0f;
                if (billboard->getVerticalOffset() > 0.25f)
                {
                    // Centered in vertical air column (fireballs, plasma bolts, sparks)
                    topUnclipped = horizonY - projH * 0.5f;
                    botUnclipped = horizonY + projH * 0.5f;
                }
                else
                {
                    // Grounded to floor line
                    const float floorLine = horizonY + ((44.0f * focalFactor) / depth) * 0.5f;
                    botUnclipped = floorLine;
                    topUnclipped = floorLine - projH;
                }

                const float topClipped = std::max(kViewTop, topUnclipped);
                const float botClipped = std::min(kViewBot, botUnclipped);
                if (botClipped <= topClipped)
                {
                    continue;
                }

                const float bbLeft = screenCenterX - projW * 0.5f;
                const float bbRight = screenCenterX + projW * 0.5f;
                const int colStart = std::max(0, static_cast<int>(std::floor(bbLeft / kColumnWidth)));
                const int colEnd = std::min(RAY_COLUMNS - 1, static_cast<int>(std::floor(bbRight / kColumnWidth)));
                if (colStart > colEnd)
                {
                    continue;
                }

                const float v0 = (topClipped - topUnclipped) / projH;
                const float v1 = (botClipped - topUnclipped) / projH;
                const float srcY = std::clamp(regY + v0 * regH, 0.0f, std::max(0.0f, texDims.h - 0.1f));
                const float srcH = std::clamp((v1 - v0) * regH, 0.1f, texDims.h - srcY);

                const SDL_Color baseTint = billboard->getTint();
                float light = 1.0f;
                if (!billboard->isFullBright())
                {
                    const float fog = std::clamp(1.12f - (depth / (MAX_VIEW_DIST * 0.92f)), 0.14f, 1.0f);
                    const float flashBoost = muzzleLight * 0.35f * std::clamp(1.0f - depth / 380.0f, 0.0f, 1.0f);
                    light = std::clamp(fog + flashBoost, 0.14f, 1.0f);
                }
                const std::uint8_t shade = static_cast<std::uint8_t>(std::clamp(light * 255.0f, 0.0f, 255.0f));
                const std::int32_t bbZ = depthToZOrder(depth) + 1;

                int c = colStart;
                while (c <= colEnd && usedStrips < MAX_BILLBOARD_STRIPS)
                {
                    while (c <= colEnd && depth >= wallDepth[c])
                    {
                        ++c;
                    }
                    if (c > colEnd)
                    {
                        break;
                    }
                    const int spanStart = c;
                    while (c <= colEnd && depth < wallDepth[c])
                    {
                        ++c;
                    }
                    const int spanEnd = c - 1;

                    const float spanLeft = std::max(bbLeft, static_cast<float>(spanStart) * kColumnWidth);
                    const float spanRight = std::min(bbRight, static_cast<float>(spanEnd + 1) * kColumnWidth);
                    const float spanW = spanRight - spanLeft;
                    if (spanW < 0.25f)
                    {
                        continue;
                    }

                    const float u0 = std::clamp((spanLeft - bbLeft) / projW, 0.0f, 0.999f);
                    const float u1 = std::clamp((spanRight - bbLeft) / projW, u0 + 0.001f, 1.0f);
                    const float srcX = std::clamp(regX + u0 * regW, 0.0f, std::max(0.0f, texDims.w - 0.1f));
                    const float srcW = std::clamp((u1 - u0) * regW, 0.1f, texDims.w - srcX);

                    PooledSprite &strip = billboardStrips[usedStrips++];
                    strip.active = true;
                    strip.sprite->setTextureWrapper(tex);
                    strip.sprite->setSourceRect(SDL_FRect{srcX, srcY, srcW, srcH});
                    strip.sprite->setWidth(spanW);
                    strip.sprite->setHeight(botClipped - topClipped);
                    strip.sprite->setColor(tintChannel(baseTint.r, shade), tintChannel(baseTint.g, shade),
                                           tintChannel(baseTint.b, shade), baseTint.a);
                    strip.sprite->setZOrder(bbZ);
                    strip.object->setPosition(Eigen::Vector2f(STAGE_ORIGIN_X + (spanLeft + spanRight) * 0.5f,
                                                              (topClipped + botClipped) * 0.5f));
                }
            }
        }
        lastRenderedBillboardStrips = usedStrips;

        for (int b = usedStrips; b < MAX_BILLBOARD_STRIPS; ++b)
        {
            parkSprite(billboardStrips[b]);
        }

        // 3. Update first-person weapon viewmodel, screen flash & bitmap HUD
        const bool firing = player->isWeaponFiring();
        TextureWrapper *wpnTex = nullptr;
        switch (player->getCurrentWeapon())
        {
        case WEAPON_SHOTGUN:
            wpnTex = firing && shotgunFireTex ? shotgunFireTex.get() : shotgunTex.get();
            break;
        case WEAPON_PLASMA:
            wpnTex = firing && plasmaFireTex ? plasmaFireTex.get() : plasmaTex.get();
            break;
        default:
            wpnTex = firing && pistolFireTex ? pistolFireTex.get() : pistolTex.get();
            break;
        }
        if (wpnTex != nullptr)
        {
            getTexDims(wpnTex);
            weaponViewmodel.sprite->setTextureWrapper(wpnTex);
            weaponViewmodel.sprite->setColor(player->isDead() ? SDL_Color{180, 60, 60, 200}
                                                              : SDL_Color{255, 255, 255, 255});
            const float recoilOffset = player->getRecoil() * 14.0f;
            const float bobX = player->getBobOffset() * 1.4f;
            const float bobY = std::abs(player->getBobOffset()) * 1.2f;
            weaponViewmodel.object->setPosition(Eigen::Vector2f(
                STAGE_ORIGIN_X + VIEW_WIDTH * 0.5f + bobX,
                STAGE_ORIGIN_Y + VIEW3D_HEIGHT - 68.0f + bobY + recoilOffset));
        }

        const SDL_Color flash = game ? game->getActiveScreenFlash() : SDL_Color{0, 0, 0, 0};
        updateHud(player->getHp(), player->getArmor(), player->getAmmo(), game ? game->getKills() : 0,
                  player->getCurrentWeapon(), player->hasWeapon(WEAPON_SHOTGUN),
                  player->hasWeapon(WEAPON_PLASMA), game ? game->hasRedKey() : false, muzzleLight, flash);
    }

    void DoomRaycaster::setDigitGroup(std::array<PooledSprite, 3> &digits, int value, float rightEdgeX, float centerY)
    {
        const int v = std::clamp(value, 0, 999);
        const int d[3] = {v / 100, (v / 10) % 10, v % 10};
        const bool show[3] = {v >= 100, v >= 10, true};
        const TexDims dims = getTexDims(digitsTex.get());
        const float cellW = dims.w / 10.0f;

        for (int i = 0; i < 3; ++i)
        {
            if (!show[i])
            {
                parkSprite(digits[i]);
                continue;
            }
            digits[i].active = true;
            digits[i].sprite->setWidth(12.0f);
            digits[i].sprite->setHeight(20.0f);
            digits[i].sprite->setSourceRect(SDL_FRect{static_cast<float>(d[i]) * cellW, 0.0f, cellW, dims.h});
            const float x = rightEdgeX - static_cast<float>(2 - i) * 14.0f - 6.0f;
            digits[i].object->setPosition(Eigen::Vector2f(x, centerY));
        }
    }

    void DoomRaycaster::setDigitPair(std::array<PooledSprite, 2> &digits, int value, float rightEdgeX, float centerY)
    {
        const int v = std::clamp(value, 0, 99);
        const int d[2] = {v / 10, v % 10};
        const bool show[2] = {v >= 10, true};
        const TexDims dims = getTexDims(digitsTex.get());
        const float cellW = dims.w / 10.0f;

        for (int i = 0; i < 2; ++i)
        {
            if (!show[i])
            {
                parkSprite(digits[i]);
                continue;
            }
            digits[i].active = true;
            digits[i].sprite->setWidth(12.0f);
            digits[i].sprite->setHeight(20.0f);
            digits[i].sprite->setSourceRect(SDL_FRect{static_cast<float>(d[i]) * cellW, 0.0f, cellW, dims.h});
            const float x = rightEdgeX - static_cast<float>(1 - i) * 14.0f - 6.0f;
            digits[i].object->setPosition(Eigen::Vector2f(x, centerY));
        }
    }

    void DoomRaycaster::updateHud(int hp, int armor, int ammo, int kills, int weapon, bool hasShotgun, bool hasPlasma,
                                  bool hasRedKey, float, SDL_Color flashColor)
    {
        const float hudNumY = STAGE_ORIGIN_Y + VIEW3D_HEIGHT + 34.0f;

        if (flashColor.a > 0)
        {
            screenFlashOverlay.active = true;
            screenFlashOverlay.sprite->setWidth(VIEW_WIDTH);
            screenFlashOverlay.sprite->setHeight(VIEW3D_HEIGHT);
            screenFlashOverlay.sprite->setColor(flashColor);
            screenFlashOverlay.object->setPosition(
                Eigen::Vector2f(STAGE_ORIGIN_X + VIEW_WIDTH * 0.5f, STAGE_ORIGIN_Y + VIEW3D_HEIGHT * 0.5f));
        }
        else
        {
            parkSprite(screenFlashOverlay);
        }

        setDigitGroup(ammoDigits, ammo, STAGE_ORIGIN_X + 74.0f, hudNumY);
        setDigitGroup(hpDigits, hp, STAGE_ORIGIN_X + 168.0f, hudNumY);
        setDigitGroup(armorDigits, armor, STAGE_ORIGIN_X + 426.0f, hudNumY);
        setDigitPair(killDigits, kills, STAGE_ORIGIN_X + 514.0f, hudNumY);

        // Doomguy portrait selection
        int faceFrame = 0;
        if (hp <= 0)
        {
            faceFrame = 3;
        }
        else if (hp < 35)
        {
            faceFrame = 2;
        }
        else if (hp < 70 || (flashColor.r > 180 && flashColor.a > 20))
        {
            faceFrame = 1;
        }
        const TexDims faceDims = getTexDims(faceTex.get());
        const float faceCellW = faceDims.w / 4.0f;
        faceSprite.sprite->setSourceRect(
            SDL_FRect{static_cast<float>(faceFrame) * faceCellW, 0.0f, faceCellW, faceDims.h});

        const bool owned[3] = {true, hasShotgun, hasPlasma};
        for (int i = 0; i < 3; ++i)
        {
            if (weapon == i)
            {
                weaponBadges[i].sprite->setColor(255, 220, 60, 255);
            }
            else if (owned[i])
            {
                weaponBadges[i].sprite->setColor(170, 175, 185, 255);
            }
            else
            {
                weaponBadges[i].sprite->setColor(70, 72, 78, 255);
            }
        }

        if (hasRedKey)
        {
            keyBadgeSprite.active = true;
            keyBadgeSprite.sprite->setWidth(28.0f);
            keyBadgeSprite.sprite->setHeight(20.0f);
            keyBadgeSprite.object->setPosition(Eigen::Vector2f(STAGE_ORIGIN_X + 592.0f, hudNumY));
        }
        else
        {
            parkSprite(keyBadgeSprite);
        }
    }
} // namespace doom
