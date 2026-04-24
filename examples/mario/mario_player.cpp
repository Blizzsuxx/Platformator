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
    MarioPlayer::MarioPlayer(GameObject *gameObject)
        : MarioEntity(gameObject),
          body(gameObject != nullptr ? gameObject->getComponent<Rigidbody>() : nullptr),
          collider(gameObject != nullptr ? static_cast<Collider *>(gameObject->getComponent(ComponentType::COLLIDER)) : nullptr),
          animator(gameObject != nullptr ? gameObject->getComponent<Animator>() : nullptr),
          sprite(gameObject != nullptr ? gameObject->getComponent<Sprite>() : nullptr),
          spawn(gameObject != nullptr ? gameObject->getPosition() : Eigen::Vector2f::Zero()),
          positionBeforePhysics(spawn),
          velocityBeforePhysics(body != nullptr ? body->getVelocity() : Eigen::Vector2f::Zero()),
          jumpWasPressed(false)
    {
    }

    void MarioPlayer::registerCallbacks(MarioGame &game)
    {
        if (collider == nullptr)
        {
            return;
        }

        collider->addCollisionEnterCallback([this, &game](Collider *other, double)
                                            { handleCollisionEnter(game, other); });
    }

    void MarioPlayer::update(MarioGame &game, double timeDelta)
    {
        updateInput(game, timeDelta);
        updateAnimation(game);
    }

    void MarioPlayer::handleKeyDown(SDL_Keycode key)
    {
        if (key == SDLK_R)
        {
            respawn();
        }
    }

    void MarioPlayer::respawn()
    {
        if (gameObject == nullptr || body == nullptr)
        {
            return;
        }

        gameObject->setPosition(spawn);
        gameObject->setRotation(0.0f);
        body->setVelocity(Eigen::Vector2f::Zero());
        body->setAngularVelocity(0.0f);
        body->setForce(Eigen::Vector2f::Zero());
        body->setTorque(0.0f);
        body->wakeUp();
        positionBeforePhysics = spawn;
        velocityBeforePhysics = Eigen::Vector2f::Zero();
        jumpWasPressed = false;
    }

    void MarioPlayer::defeat(MarioGame &game)
    {
        if (game.isGameWon() || !isActive())
        {
            return;
        }

        game.playHurtSound();
        respawn();
    }

    void MarioPlayer::bounceAfterStomp()
    {
        if (body == nullptr)
        {
            return;
        }

        Eigen::Vector2f velocity = body->getVelocity();
        velocity.y() = -PLAYER_JUMP_SPEED * 0.65f;
        body->setVelocity(velocity);
        velocityBeforePhysics = velocity;
    }

    void MarioPlayer::stopForWin()
    {
        if (body == nullptr)
        {
            return;
        }

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

    void MarioPlayer::handleCollisionEnter(MarioGame &game, Collider *other)
    {
        if (other == nullptr || body == nullptr || !isActive())
        {
            return;
        }

        GameObject *otherObject = other->getGameObject();
        if (otherObject == nullptr || !otherObject->getActive())
        {
            return;
        }

        if (MarioCoin *coin = game.findCoin(otherObject))
        {
            coin->collect(game);
            return;
        }

        if (MarioGoalFlag *goal = game.findGoal(otherObject))
        {
            goal->reach(game);
            return;
        }

        if (MarioPatrolEnemy *enemy = game.findEnemy(otherObject))
        {
            enemy->handlePlayerContact(game, *this);
            return;
        }

        if (otherObject->getTag() == "death")
        {
            defeat(game);
        }
    }

    void MarioPlayer::updateInput(MarioGame &game, double timeDelta)
    {
        if (!isActive() || body == nullptr || game.isGameWon())
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
            horizontalVelocity = moveLeft ? -PLAYER_WALK_SPEED : PLAYER_WALK_SPEED;
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
            velocity.y() = std::min(velocity.y() + PLAYER_GRAVITY * static_cast<float>(timeDelta), PLAYER_MAX_FALL_SPEED);
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
            velocity.y() = -PLAYER_JUMP_SPEED;
            game.playJumpSound();
        }

        body->setVelocity(velocity);
        positionBeforePhysics = gameObject->getPosition();
        velocityBeforePhysics = velocity;
        jumpWasPressed = jumpPressed;
    }

    void MarioPlayer::updateAnimation(const MarioGame &game)
    {
        if (!isActive() || body == nullptr || animator == nullptr)
        {
            return;
        }

        if (game.isGameWon())
        {
            playClipIfChanged(animator, "win");
            return;
        }

        const Eigen::Vector2f velocity = body->getVelocity();
        if (!body->hasSupportContact())
        {
            if (velocity.y() < -5.0f)
            {
                playClipIfChanged(animator, "jump");
            }
            else
            {
                playClipIfChanged(animator, "fall");
            }
            return;
        }

        if (std::abs(velocity.x()) > 5.0f)
        {
            playClipIfChanged(animator, "run");
        }
        else
        {
            playClipIfChanged(animator, "idle");
        }
    }
} // namespace mario