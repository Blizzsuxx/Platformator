#include "scenecomponentio.h"
#include "referenceresolver.h"
#include "platformator/audio.h"

#include "runtimeaccess.h"

void to_json(nlohmann::json &j, const Audio &audio)
{
    j = nlohmann::json{
        {"id", audio.getId()},          {"filePath", audio.getFilePath()},
        {"gain", audio.getGain()},      {"loopCount", audio.getLoopCount()},
        {"type", ComponentType::AUDIO}, {"autoPlay", audio.getAutoPlay()},
    };
}

void deserialize(const nlohmann::json &j, Audio &audio, platformator::ReferenceResolver &resolver)
{
    audio.setId(j.at("id").get<int>());

    std::string filePath;
    j.at("filePath").get_to(filePath);

    if (!filePath.empty())
    {
        AudioWrapper *audioWrapper = resolver.loadAudio(filePath);
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
    audio.setAutoPlay(autoPlay);
}
void from_json(const nlohmann::json &j, Audio &audio)
{
    platformator_detail::ActiveReferenceResolver resolver(platformator::Runtime::current());
    deserialize(j, audio, resolver);
}
