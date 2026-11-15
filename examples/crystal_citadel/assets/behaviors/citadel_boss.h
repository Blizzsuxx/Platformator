#pragma once

#include "platformator/assetreference.h"
#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/objectreference.h"
#include "platformator/rigidbody.h"
#include "platformator/scriptregistration.h"

namespace citadel
{
    class CitadelArchonBoss : public Behavior
    {
      public:
        CitadelArchonBoss() = default;

        void start() override;
        void fixedUpdate(double timeDelta) override;
        void update(double timeDelta) override;

        void takeDamage(int amount);
        bool fireVolleyNow();
        bool summonMinionNow();
        bool hasResolvedChildren() const;
        int getHp() const;
        int getMaxHp() const;
        bool isDefeated() const;
        GameObject *getCrownChild() const;
        GameObject *getLeftOrbChild() const;
        GameObject *getRightOrbChild() const;

      private:
        int maxHp = 12;
        int hp = 12;
        float arenaMinX = 160.0f;
        float arenaMaxX = 780.0f;
        float hoverY = 230.0f;
        float moveSpeed = 75.0f;
        float attackInterval = 2.2f;
        float summonInterval = 6.5f;
        ObjectReference<GameObject> crownChild;
        ObjectReference<GameObject> leftOrbChild;
        ObjectReference<GameObject> rightOrbChild;
        platformator::TemplateAssetRef bossBoltTemplate;
        platformator::TemplateAssetRef minionTemplate;
        platformator::TemplateAssetRef rewardGemTemplate;
        platformator::TemplateAssetRef defeatPortalTemplate;
        platformator::TemplateAssetRef explosionFxTemplate;

        Rigidbody *body = nullptr;
        Audio *audio = nullptr;
        float direction = 1.0f;
        double phaseTime = 0.0;
        double attackTimer = 1.4;
        double summonTimer = 3.5;
        bool defeated = false;

        void defeatBoss();

        SERIALIZABLE_SCRIPT(
            CitadelArchonBoss,
            maxHp,
            hp,
            arenaMinX,
            arenaMaxX,
            hoverY,
            moveSpeed,
            attackInterval,
            summonInterval,
            crownChild,
            leftOrbChild,
            rightOrbChild,
            bossBoltTemplate,
            minionTemplate,
            rewardGemTemplate,
            defeatPortalTemplate,
            explosionFxTemplate);
    };
} // namespace citadel
