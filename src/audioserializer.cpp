#include "audio.h"

#include "gamemanager.h"

void to_json(nlohmann::json &j, const Audio &audio)
{
    j = nlohmann::json{
        {"filePath", audio.getFilePath()},
        {"gain", audio.getGain()},
        {"loopCount", audio.getLoopCount()},
        {"autoPlay", audio.getAutoPlay()},
    };
}

void from_json(const nlohmann::json &j, Audio &audio)
{
    std::string filePath;
    j.at("filePath").get_to(filePath);

    AudioWrapper *audioWrapper = GameManager::getInstance().loadAudio(filePath);
    audio.setAudio(audioWrapper);

    audio.setGain(j.at("gain").get<float>());
    audio.setLoopCount(j.at("loopCount").get<float>());

    bool autoPlay;
    j.at("autoPlay").get_to(autoPlay);
    if (autoPlay)
    {
        audio.play();
    }
}