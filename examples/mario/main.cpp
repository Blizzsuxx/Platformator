#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "boxcollider.h"
#include "camera.h"
#include "circlecollider.h"
#include "constants.h"
#include "gamemanager.h"
#include "rigidbody.h"
#include "scene.h"
#include "sprite.h"

namespace
{
    constexpr float PLAYER_WALK_SPEED = 180.0f;
    constexpr float PLAYER_JUMP_SPEED = 250.0f;
    constexpr float PLAYER_GRAVITY = GRAVITY_VECTOR_Y * 3.65f;
    constexpr float PLAYER_MAX_FALL_SPEED = 460.0f;
    constexpr float ENEMY_WALK_SPEED = 55.0f;
    constexpr float PLAYER_RESPAWN_Y = 680.0f;
    constexpr float STOMP_TOLERANCE = 14.0f;
    constexpr float CAMERA_LEAD = 90.0f;
    constexpr float LEVEL_WIDTH = 2240.0f;

    struct PatrolEnemy
    {
        GameObject *gameObject;
        float minX;
        float maxX;
        float direction;
    };

    struct Bounds
    {
        Eigen::Vector2f center;
        Eigen::Vector2f halfExtents;
    };

    Bounds getBounds(const GameObject *gameObject)
    {
        Collider *collider = static_cast<Collider *>(gameObject->getComponent(ComponentType::COLLIDER));
        if (collider == nullptr)
        {
            return Bounds{gameObject->getPosition(), Eigen::Vector2f::Zero()};
        }

        if (collider->getColliderType() == ColliderType::BoxCollider)
        {
            const BoxCollider *boxCollider = static_cast<const BoxCollider *>(collider);
            return Bounds{gameObject->getPosition(), Eigen::Vector2f(boxCollider->getWidth() * 0.5f, boxCollider->getHeight() * 0.5f)};
        }

        const CircleCollider *circleCollider = static_cast<const CircleCollider *>(collider);
        return Bounds{gameObject->getPosition(), Eigen::Vector2f::Constant(circleCollider->getRadius())};
    }

    bool overlaps(const GameObject *objectA, const GameObject *objectB)
    {
        if (objectA == nullptr || objectB == nullptr || !objectA->getActive() || !objectB->getActive())
        {
            return false;
        }

        const Bounds boundsA = getBounds(objectA);
        const Bounds boundsB = getBounds(objectB);

        const float deltaX = std::abs(boundsA.center.x() - boundsB.center.x());
        const float deltaY = std::abs(boundsA.center.y() - boundsB.center.y());

        return deltaX <= (boundsA.halfExtents.x() + boundsB.halfExtents.x()) &&
               deltaY <= (boundsA.halfExtents.y() + boundsB.halfExtents.y());
    }

    class MarioExample
    {
    public:
        MarioExample(GameManager &gameManager, SDLWindow *window, Scene &scene)
            : gameManager(gameManager), window(window), scene(scene), player(nullptr), playerBody(nullptr), camera(nullptr), playerSpawn(Eigen::Vector2f::Zero()), enemies(), coins(), goal(nullptr), collectedCoins(0), jumpWasPressed(false), gameWon(false), titleDirty(true)
        {
            cacheSceneObjects();
            registerEventHooks();
            updateWindowTitle();
        }

    private:
        GameManager &gameManager;
        SDLWindow *window;
        Scene &scene;

        GameObject *player;
        Rigidbody *playerBody;
        Camera *camera;
        Eigen::Vector2f playerSpawn;
        std::vector<PatrolEnemy> enemies;
        std::vector<GameObject *> coins;
        GameObject *goal;
        size_t collectedCoins;
        bool jumpWasPressed;
        bool gameWon;
        bool titleDirty;

        void cacheSceneObjects()
        {
            player = gameManager.getGameObject("Player");
            playerBody = player != nullptr ? player->getComponent<Rigidbody>() : nullptr;
            camera = window->getMainCamera();
            playerSpawn = player != nullptr ? player->getPosition() : Eigen::Vector2f::Zero();

            enemies.clear();
            coins.clear();
            goal = nullptr;

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
                    enemies.push_back(PatrolEnemy{gameObject, spawnX - patrolHalfRange, spawnX + patrolHalfRange, enemyIndex % 2 == 0 ? -1.0f : 1.0f});
                    enemyIndex++;
                    continue;
                }

                if (tag == "coin")
                {
                    coins.push_back(gameObject);
                    continue;
                }

                if (tag == "goal")
                {
                    goal = gameObject;
                }
            }
        }

        void registerEventHooks()
        {
            window->addSdlListener([this](SDL_Event event)
                                   {
            if (event.type != SDL_EVENT_KEY_DOWN)
            {
                return;
            }

            if (event.key.key == SDLK_F7)
            {
                try
                {
                    gameManager.saveScene(scene);
                    std::cout << "Saved Mario example scene to " << scene.filePath << '\n';
                }
                catch (const std::exception &exception)
                {
                    std::cout << "Failed to save Mario example scene: " << exception.what() << '\n';
                }
                return;
            }

            if (event.key.key == SDLK_R)
            {
                respawnPlayer();
            } });

            window->addFrameListener([this](double timeDelta)
                                     {
            updatePlayerInput(timeDelta);
            updateEnemies();
            collectCoins();
            handleEnemyInteractions();
            handleGoal();
            handleFallOut();
            updateCamera();

            if (titleDirty)
            {
                updateWindowTitle();
            } });
        }

        void updatePlayerInput(double timeDelta)
        {
            if (player == nullptr || playerBody == nullptr || !player->getActive() || gameWon)
            {
                return;
            }

            const bool *keyboardState = SDL_GetKeyboardState(nullptr);
            const bool moveLeft = keyboardState[SDL_SCANCODE_LEFT] || keyboardState[SDL_SCANCODE_A];
            const bool moveRight = keyboardState[SDL_SCANCODE_RIGHT] || keyboardState[SDL_SCANCODE_D];
            const bool jumpPressed = keyboardState[SDL_SCANCODE_SPACE] || keyboardState[SDL_SCANCODE_UP] || keyboardState[SDL_SCANCODE_W];

            float horizontalVelocity = 0.0f;
            if (moveLeft == moveRight)
            {
                horizontalVelocity = 0.0f;
            }
            else
            {
                horizontalVelocity = moveLeft ? -PLAYER_WALK_SPEED : PLAYER_WALK_SPEED;
            }

            const bool isGrounded = playerBody->hasSupportContact();
            Eigen::Vector2f velocity = playerBody->getVelocity();
            velocity.x() = horizontalVelocity;

            if (isGrounded)
            {
                velocity.y() = std::min(velocity.y(), 0.0f);
            }
            else
            {
                velocity.y() = std::min(velocity.y() + PLAYER_GRAVITY * static_cast<float>(timeDelta), PLAYER_MAX_FALL_SPEED);
            }

            Sprite *sprite = player->getComponent<Sprite>();
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
            }

            playerBody->setVelocity(velocity);
            jumpWasPressed = jumpPressed;
        }

        void updateEnemies()
        {
            for (PatrolEnemy &enemy : enemies)
            {
                if (enemy.gameObject == nullptr || !enemy.gameObject->getActive())
                {
                    continue;
                }

                Rigidbody *enemyBody = enemy.gameObject->getComponent<Rigidbody>();
                if (enemyBody == nullptr)
                {
                    continue;
                }

                if (enemy.gameObject->getX() <= enemy.minX)
                {
                    enemy.direction = 1.0f;
                }
                else if (enemy.gameObject->getX() >= enemy.maxX)
                {
                    enemy.direction = -1.0f;
                }

                Eigen::Vector2f velocity = enemyBody->getVelocity();
                velocity.x() = enemy.direction * ENEMY_WALK_SPEED;
                enemyBody->setVelocity(velocity);

                Sprite *sprite = enemy.gameObject->getComponent<Sprite>();
                if (sprite != nullptr)
                {
                    sprite->setFlip(enemy.direction < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
                }
            }
        }

        void collectCoins()
        {
            if (player == nullptr || !player->getActive())
            {
                return;
            }

            for (GameObject *coin : coins)
            {
                if (coin == nullptr || !coin->getActive() || !overlaps(player, coin))
                {
                    continue;
                }

                coin->setActive(false);
                collectedCoins++;
                titleDirty = true;
            }
        }

        void handleEnemyInteractions()
        {
            if (player == nullptr || playerBody == nullptr || !player->getActive())
            {
                return;
            }

            const Bounds playerBounds = getBounds(player);
            const float playerBottom = playerBounds.center.y() + playerBounds.halfExtents.y();

            for (PatrolEnemy &enemy : enemies)
            {
                if (enemy.gameObject == nullptr || !enemy.gameObject->getActive() || !overlaps(player, enemy.gameObject))
                {
                    continue;
                }

                const Bounds enemyBounds = getBounds(enemy.gameObject);
                const float enemyTop = enemyBounds.center.y() - enemyBounds.halfExtents.y();
                const bool stomped = playerBody->getVelocity().y() > 10.0f && playerBottom <= enemyTop + STOMP_TOLERANCE;

                if (stomped)
                {
                    enemy.gameObject->setActive(false);
                    Eigen::Vector2f velocity = playerBody->getVelocity();
                    velocity.y() = -PLAYER_JUMP_SPEED * 0.65f;
                    playerBody->setVelocity(velocity);
                    titleDirty = true;
                    continue;
                }

                respawnPlayer();
                return;
            }
        }

        void handleGoal()
        {
            if (gameWon || player == nullptr || goal == nullptr || !goal->getActive())
            {
                return;
            }

            if (!overlaps(player, goal))
            {
                return;
            }

            gameWon = true;
            if (playerBody != nullptr)
            {
                playerBody->setVelocity(Eigen::Vector2f::Zero());
            }
            titleDirty = true;
            std::cout << "Level clear. Coins collected: " << collectedCoins << '/' << coins.size() << '\n';
        }

        void handleFallOut()
        {
            if (player == nullptr || !player->getActive())
            {
                return;
            }

            if (player->getY() > PLAYER_RESPAWN_Y)
            {
                respawnPlayer();
            }
        }

        void respawnPlayer()
        {
            if (player == nullptr || playerBody == nullptr)
            {
                return;
            }

            player->setPosition(playerSpawn);
            player->setRotation(0.0f);
            playerBody->setVelocity(Eigen::Vector2f::Zero());
            playerBody->setAngularVelocity(0.0f);
            playerBody->setForce(Eigen::Vector2f::Zero());
            playerBody->setTorque(0.0f);
            playerBody->wakeUp();
            titleDirty = true;
        }

        void updateCamera()
        {
            if (camera == nullptr || player == nullptr)
            {
                return;
            }

            SDL_FRect cameraRect = camera->getCamera();
            float targetX = player->getX() - SCREEN_WIDTH * 0.5f;
            if (playerBody != nullptr)
            {
                targetX += std::clamp(playerBody->getVelocity().x() * 0.25f, -CAMERA_LEAD, CAMERA_LEAD);
            }

            cameraRect.x = std::clamp(targetX, 0.0f, std::max(0.0f, LEVEL_WIDTH - cameraRect.w));
            cameraRect.y = 0.0f;
            camera->setCamera(cameraRect);
        }

        void updateWindowTitle()
        {
            if (window == nullptr || window->getWindow() == nullptr)
            {
                return;
            }

            std::string title = "Platformator Mario Example - Coins " + std::to_string(collectedCoins) + "/" + std::to_string(coins.size());
            if (gameWon)
            {
                title += " - Level Clear";
            }

            SDL_SetWindowTitle(window->getWindow(), title.c_str());
            titleDirty = false;
        }
    };

    std::filesystem::path getDefaultScenePath()
    {
        return std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path() / "level1.scene";
    }
} // namespace

int main(int argc, char *args[])
{
    GameManager &gameManager = GameManager::getInstance();
    SDLWindow *window = gameManager.getWindow();
    const std::filesystem::path scenePath = argc > 1 ? std::filesystem::path(args[1]) : getDefaultScenePath();

    try
    {
        gameManager.addScene(Scene(scenePath.string()));
        Scene &loadedScene = gameManager.getScenes().back();
        gameManager.loadScene(loadedScene);

        MarioExample marioExample(gameManager, window, loadedScene);
        gameManager.loop();
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Failed to load Mario example scene '" << scenePath.string() << "': " << exception.what() << '\n';
        return 1;
    }

    return 0;
}