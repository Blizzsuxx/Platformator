#include "scenecomponentio.h"
#include "referenceresolver.h"
#include "platformator/animationclip.h"

#include "platformator/scriptregistration.h"
#include "runtimeaccess.h"

#include <limits>
#include <stdexcept>

namespace
{
void validate_frame_duration_json(const nlohmann::json &frame, std::size_t index)
{
    if (!frame.is_object() || !frame.contains("duration") || !frame["duration"].is_number())
        throw std::runtime_error("animation frame " + std::to_string(index) +
                                 " requires a numeric duration");
    const double durationValue = frame["duration"].get<double>();
    if (!is_valid_animation_frame_duration(durationValue) ||
        durationValue > static_cast<double>(std::numeric_limits<float>::max()) ||
        static_cast<float>(durationValue) <= 0.0f)
    {
        throw std::runtime_error("animation frame " + std::to_string(index) +
                                 " duration must convert to a finite positive float");
    }
}

void validate_clip_timing(const nlohmann::json &document)
{
    if (!document.is_object() || !document.contains("frames") || !document["frames"].is_array())
        throw std::runtime_error("animation clip requires a frame array");
    for (std::size_t index = 0; index < document["frames"].size(); ++index)
    {
        validate_frame_duration_json(document["frames"][index], index);
    }
}

void validate_clip_timing(const AnimationClip &clip)
{
    for (std::size_t index = 0; index < clip.frames.size(); ++index)
    {
        if (!is_valid_animation_frame_duration(clip.frames[index].duration))
            throw std::runtime_error("animation frame " + std::to_string(index) +
                                     " duration must be finite and positive");
    }
}
} // namespace

void to_json(nlohmann::json &j, const AnimationFrame &frame)
{
    const TextureWrapper *wrapper = frame.getTextureWrapper();
    j = nlohmann::json{{"duration", frame.duration},
                       {"sourceRect", frame.region.sourceRect},
                       {"hasSourceRect", frame.region.hasSourceRect},
                       {"textureWrapperFilePath", wrapper ? wrapper->getFilePath() : ""}};
}
void deserialize(const nlohmann::json &j, AnimationFrame &frame, platformator::ReferenceResolver &resolver)
{
    validate_frame_duration_json(j, 0);
    const float validatedDuration = static_cast<float>(j.at("duration").get<double>());
    j.at("hasSourceRect").get_to(frame.region.hasSourceRect);
    j.at("sourceRect").get_to(frame.region.sourceRect);
    frame.duration = validatedDuration;
    std::string textureWrapperFilePath;
    j.at("textureWrapperFilePath").get_to(textureWrapperFilePath);
    if (!textureWrapperFilePath.empty())
    {
        frame.setTextureWrapper(resolver.loadTexture(textureWrapperFilePath));
    }
    else
    {
        frame.setTextureWrapper(nullptr);
    }
}

void to_json(nlohmann::json &j, const AnimationClip &clip)
{
    validate_clip_timing(clip);
    j = nlohmann::json{{"frames", clip.frames},
                       {"loop", clip.loop},
                       {"filePath", clip.getFilePath()}};
}

void deserialize(const nlohmann::json &j, AnimationClip &clip, platformator::ReferenceResolver &resolver)
{
    validate_clip_timing(j);
    clip.frames.clear();
    clip.frames.reserve(j.at("frames").size());
    for (const auto &frameJson : j.at("frames"))
    {
        clip.frames.emplace_back();
        deserialize(frameJson, clip.frames.back(), resolver);
    }
    j.at("loop").get_to(clip.loop);

    std::string filePath;
    j.at("filePath").get_to(filePath);
    clip.setFilePath(filePath);
}
void from_json(const nlohmann::json &j, AnimationFrame &frame)
{
    platformator_detail::ActiveReferenceResolver resolver(platformator::Runtime::current());
    deserialize(j, frame, resolver);
}

void from_json(const nlohmann::json &j, AnimationClip &clip)
{
    platformator_detail::ActiveReferenceResolver resolver(platformator::Runtime::current());
    deserialize(j, clip, resolver);
}
