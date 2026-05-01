#include "texturewrapper.h"

void to_json(nlohmann::json &j, const TextureWrapper &textureWrapper)
{
    j = nlohmann::json{
        {"filePath", textureWrapper.getFilePath()},
    };
}