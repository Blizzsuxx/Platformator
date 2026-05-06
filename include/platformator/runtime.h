#pragma once

#include <memory>
#include <string>

#include <SDL3/SDL.h>

#include "platformator/runtimeoptions.h"

class BaseObject;
class Camera;
class GameManager;
class GameObject;
class Scene;
class AnimationClip;
class TextureWrapper;

namespace platformator
{
    class Runtime
    {
    public:
        explicit Runtime(const RuntimeOptions &runtimeOptions = RuntimeOptions{});
        Runtime(const Runtime &) = delete;
        Runtime &operator=(const Runtime &) = delete;
        ~Runtime();

        static Runtime &current();
        static Runtime *tryGetCurrent();

        GameObject *createGameObject();
        void destroyGameObject(GameObject *gameObject);
        void clearScene();
        GameObject *getGameObject(const std::string &name) const;
        BaseObject *getObjectById(int id) const;
        Camera *getMainCamera() const;
        AnimationClip *loadAnimationClip(const std::string &filePath) const;
        void loadScene(Scene &scene);
        void loadScene(const std::string &sceneFilePath);
        TextureWrapper *loadTexture(const std::string &filePath) const;
        void saveScene(Scene &scene);
        void saveScene();
        void saveScene(const std::string &sceneFilePath);
        void run();
        void simulateFrame(double timeDelta);
        SDL_Window *getWindowHandle() const;
        SDL_Renderer *getRenderer() const;

    private:
        static Runtime *&activeRuntime();

        std::unique_ptr<GameManager> gameManager;
        std::string currentSceneFilePath;
    };
} // namespace platformator

#include "platformator/runner.h"