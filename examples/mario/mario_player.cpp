#include "mario_player.h"

#include <algorithm>
#include <cmath>

#include "animator.h"
#include "collider.h"
#include "mario_coin.h"
#include "mario_constants.h"
#include "mario_game.h"
#include "mario_goal_flag.h"
#include "mario_helpers.h"
#include "mario_patrol_enemy.h"
#include "rigidbody.h"
#include "sprite.h"

namespace mario
{
    MarioPlayer::MarioPlayer()
        : body(nullptr),
          animator(nullptr),
          sprite(nullptr),
          walkSpeed(PLAYER_WALK_SPEED),
          jumpSpeed(PLAYER_JUMP_SPEED),
          gravity(PLAYER_GRAVITY),
          maxFallSpeed(PLAYER_MAX_FALL_SPEED),
          stompBounceFactor(0.65f),
          idleAnimset(),
          runAnimset(),
          jumpAnimset(),
          fallAnimset(),
          winAnimset(),
          jumpSound(),
          hurtSound(),
          spawn(Eigen::Vector2f::Zero()),
          positionBeforePhysics(Eigen::Vector2f::Zero()),
          velocityBeforePhysics(Eigen::Vector2f::Zero()),
          jumpWasPressed(false),
          respawnWasPressed(false)
    {
    }

    void MarioPlayer::start()
    {
        body = getGameObject()->getComponent<Rigidbody>();
        animator = getAnimator();
        sprite = getGameObject()->getComponent<Sprite>();
        spawn = getGameObject()->getPosition();
        positionBeforePhysics = spawn;
        velocityBeforePhysics = body->getVelocity();
    }

    void MarioPlayer::fixedUpdate(double timeDelta)
    {
        updateInput(timeDelta);
    }

    void MarioPlayer::update(double)
    {
        updateAnimation();
    }

    void MarioPlayer::respawn()
    {
        getGameObject()->setPosition(spawn);
        getGameObject()->setRotation(0.0f);
        body->setVelocity(Eigen::Vector2f::Zero());
        body->setAngularVelocity(0.0f);
        body->setForce(Eigen::Vector2f::Zero());
        body->setTorque(0.0f);
        body->wakeUp();
        positionBeforePhysics = spawn;
        velocityBeforePhysics = Eigen::Vector2f::Zero();
        jumpWasPressed = false;
    }

    void MarioPlayer::defeat()
    {
        MarioGame &game = MarioGame::getInstance();
        if (game.isGameWon() || !isActive())
        {
            return;
        }

        playSound(hurtSound);
        respawn();
    }

    void MarioPlayer::bounceAfterStomp()
    {
        Eigen::Vector2f velocity = body->getVelocity();
        velocity.y() = -jumpSpeed * stompBounceFactor;
        body->setVelocity(velocity);
        velocityBeforePhysics = velocity;
    }

    void MarioPlayer::stopForWin()
    {
        body->setVelocity(Eigen::Vector2f::Zero());
        velocityBeforePhysics = Eigen::Vector2f::Zero();
    }

    Rigidbody *MarioPlayer::getBody() const
    {
        return body;
    }

    const Eigen::Vector2f &MarioPlayer::getPositionBeforePhysics() const
    {
        return positionBeforePhysics;
    }

    const Eigen::Vector2f &MarioPlayer::getVelocityBeforePhysics() const
    {
        return velocityBeforePhysics;
    }

    void MarioPlayer::onCollisionEnter(const Collision *collision, Collider *other, double timeDelta)
    {
        if (!isActive())
        {
            return;
        }

        GameObject *otherObject = other->getGameObject();
        if (!otherObject->getActive())
        {
            return;
        }

        if (other->getIsTrigger())
        {
            if (MarioCoin *coin = getBehavior<MarioCoin>(otherObject))
            {
                coin->collect();
                return;
            }

            if (MarioGoalFlag *goal = getBehavior<MarioGoalFlag>(otherObject))
            {
                goal->reach();
                return;
            }

            if (otherObject->getTag() == "death")
            {
                defeat();
            }
            return;
        }

        if (MarioPatrolEnemy *enemy = getBehavior<MarioPatrolEnemy>(otherObject))
        {
            enemy->handlePlayerContact(*this);
        }
    }

    void MarioPlayer::updateInput(double timeDelta)
    {
        MarioGame &game = MarioGame::getInstance();
        const bool respawnPressed = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_R];
        if (respawnPressed && !respawnWasPressed)
        {
            respawn();
        }
        respawnWasPressed = respawnPressed;

        if (!isActive() || game.isGameWon())
        {
            return;
        }

        const bool *keyboardState = SDL_GetKeyboardState(nullptr);
        const bool moveLeft = keyboardState[SDL_SCANCODE_LEFT] || keyboardState[SDL_SCANCODE_A];
        const bool moveRight = keyboardState[SDL_SCANCODE_RIGHT] || keyboardState[SDL_SCANCODE_D];
        const bool jumpPressed = keyboardState[SDL_SCANCODE_SPACE] || keyboardState[SDL_SCANCODE_UP] || keyboardState[SDL_SCANCODE_W];

        float horizontalVelocity = 0.0f;
        if (moveLeft != moveRight)
        {
            horizontalVelocity = moveLeft ? -walkSpeed : walkSpeed;
        }

        const bool isGrounded = body->hasSupportContact();
        Eigen::Vector2f velocity = body->getVelocity();
        velocity.x() = horizontalVelocity;

        if (isGrounded)
        {
            velocity.y() = std::min(velocity.y(), 0.0f);
        }
        else
        {
            velocity.y() = std::min(velocity.y() + gravity * static_cast<float>(timeDelta), maxFallSpeed);
        }

        if (sprite != nullptr)
        {
            if (horizontalVelocity < -1.0f)
            {
                sprite->setFlip(SDL_FLIP_HORIZONTAL);
            }
            else if (horizontalVelocity > 1.0f)
            {
                sprite->setFlip(SDL_FLIP_NONE);
            }
        }

        if (jumpPressed && !jumpWasPressed && isGrounded)
        {
            velocity.y() = -jumpSpeed;
            playSound(jumpSound);
        }

        body->setVelocity(velocity);
        positionBeforePhysics = getGameObject()->getPosition();
        velocityBeforePhysics = velocity;
        jumpWasPressed = jumpPressed;
    }

    void MarioPlayer::updateAnimation()
    {
        MarioGame &game = MarioGame::getInstance();
        if (!isActive() || animator == nullptr)
        {
            return;
        }

        if (game.isGameWon())
        {
            animator->play(winAnimset.get());
            return;
        }

        const Eigen::Vector2f velocity = body->getVelocity();
        if (!body->hasSupportContact())
        {
            if (velocity.y() < -5.0f)
            {
                animator->play(jumpAnimset.get());
            }
            else
            {
                animator->play(fallAnimset.get());
            }
            return;
        }

        if (std::abs(velocity.x()) > 5.0f)
        {
            animator->play(runAnimset.get());
        }
        else
        {
            animator->play(idleAnimset.get());
        }
    }
} // namespace mario