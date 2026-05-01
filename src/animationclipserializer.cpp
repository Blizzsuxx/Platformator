#include "animationclip.h"
#include "gamemanager.h"

void to_json(nlohmann::json &j, const AnimationFrame &frame)
{
    j = nlohmann::json{
        {"duration", frame.duration},
        {"sourceRect", frame.sourceRect},
        {"hasSourceRect", frame.hasSourceRect},
        {"textureWrapperFilePath", frame.textureWrapper ? frame.textureWrapper->getFilePath() : ""}};
}
void from_json(const nlohmann::json &j, AnimationFrame &frame)
{
    j.at("duration").get_to(frame.duration);
    j.at("hasSourceRect").get_to(frame.hasSourceRect);
    j.at("sourceRect").get_to(frame.sourceRect);
    std::string textureWrapperFilePath;
    j.at("textureWrapperFilePath").get_to(textureWrapperFilePath);
    if (!textureWrapperFilePath.empty())
    {
        frame.setTextureWrapper(GameManager::getInstance().loadTexture(textureWrapperFilePath));
    }
    else
    {
        frame.setTextureWrapper(nullptr);
    }
}

void to_json(nlohmann::json &j, const AnimationClip &clip)
{
    j = nlohmann::json{
        {"frames", clip.frames},
        {"framesPerSecond", clip.framesPerSecond},
        {"loop", clip.loop},
        {"width", clip.width},
        {"height", clip.height},
        {"name", clip.name},
        {"filePath", clip.getFilePath()}};
}