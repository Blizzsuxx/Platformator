#pragma once

#include <string>
#include <vector>

#include "animationclip.h"
#include "gameobject.h"

struct Scene
{
public:
    Scene(std::string filepath);
    ~Scene();
    std::string filePath;

    static AnimationClip loadAnimationClipFile(const std::string &filePath);
    static void saveAnimationClipFile(const AnimationClip &animationClip, const std::string &filePath);

    // Loads a TOML scene file.
    std::vector<GameObject *> loadScene();

    // Saves the current GameObjects back into the TOML scene format supported by loadScene().
    void saveScene(const std::vector<GameObject *> &gameObjects) const;
};