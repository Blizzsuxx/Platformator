#include "audio.h"

#include "runtimeaccess.h"

void to_json(nlohmann::json &j, const Audio &audio)
{
    j = nlohmann::json{
        {"id", audio.getId()},
        {"filePath", audio.getFilePath()},
        {"gain", audio.getGain()},
        {"loopCount", audio.getLoopCount()},
        {"type", ComponentType::AUDIO},
        {"autoPlay", audio.getAutoPlay()},
    };
}

void from_json(const nlohmann::json &j, Audio &audio)
{
    audio.setId(j.at("id").get<int>());

    std::string filePath;
    j.at("filePath").get_to(filePath);

    if (!filePath.empty())
    {
        AudioWrapper *audioWrapper = platformator_detail::RuntimeAccess::loadAudio(filePath);
        audio.setAudio(audioWrapper);
    }
    else
    {
        audio.setAudio(nullptr);
    }

    audio.setGain(j.at("gain").get<float>());
    audio.setLoopCount(j.at("loopCount").get<float>());

    bool autoPlay;
    j.at("autoPlay").get_to(autoPlay);
    if (autoPlay)
    {
        audio.play();
    }
}