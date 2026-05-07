#pragma once

#include <string>
#include <vector>

#include "platformator/gameobject.h"

class Scene
{
public:
	explicit Scene(std::string filepath);
	~Scene();

	std::string filePath;

	std::vector<GameObject *> loadScene();
	void saveScene(const std::vector<GameObject *> &gameObjects) const;
};