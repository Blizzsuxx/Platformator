#include "audiowrapper.h"
#include "gamemanager.h"

void to_json(nlohmann::json &j, const AudioWrapper &audioWrapper)
{
    j = nlohmann::json{
        {"filePath", audioWrapper.getFilePath()},
    };
}