#include "mario_player.h"

#include <algorithm>
#include <cmath>

#include "platformator/behaviorquery.h"

#include "mario_coin.h"
#include "mario_constants.h"
#include "mario_game.h"
#include "mario_goal_flag.h"
#include "mario_patrol_enemy.h"

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
          jumpWasPressed(false),
          respawnWasPressed(false)
    {
    }

    void MarioPlayer::start()
    {
        body = getGameObject()->getComponent<Rigidbody>();
        animator = getGameObject()->getComponent<Animator>();
        sprite = getGameObject()->getComponent<Sprite>();
        spawn = getGameObject()->getPosition();
        audio = getGameObject()->getComponent<Audio>();
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
        body->setVelocity(Eigen::Vector2f::Zero());
        jumpWasPressed = false;
    }

    void MarioPlayer::defeat()
    {
        MarioGame &game = MarioGame::getInstance();

        audio->replay(hurtSound.get());
        respawn();
    }

    void MarioPlayer::bounceAfterStomp()
    {
        Eigen::Vector2f velocity = body->getVelocity();
        velocity.y() = -jumpSpeed * stompBounceFactor;
        body->setVelocity(velocity);
    }

    void MarioPlayer::stopForWin()
    {
        if (body != nullptr)
        {
            body->setVelocity(Eigen::Vector2f::Zero());
        }

        jumpWasPressed = false;
        respawnWasPressed = false;
    }

    Rigidbody *MarioPlayer::getBody() const
    {
        return body;
    }

    void MarioPlayer::onCollisionEnter(const Collision *collision, Collider *other, double timeDelta)
    {
        GameObject *otherObject = other->getGameObject();

        if (other->getIsTrigger())
        {
            MarioCoin *coin = getBehavior<MarioCoin>(otherObject);
            if (coin != nullptr)
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
            enemy->handlePlayerContact(this, collision);
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

        if (game.isGameWon())
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
        velocity.y() = std::min(velocity.y() + gravity * static_cast<float>(timeDelta), maxFallSpeed);

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
            audio->replay(jumpSound.get());
        }

        body->setVelocity(velocity);
        jumpWasPressed = jumpPressed;
    }

    void MarioPlayer::updateAnimation()
    {
        MarioGame &game = MarioGame::getInstance();
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