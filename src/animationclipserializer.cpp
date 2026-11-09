#include "scenecomponentio.h"
#include "referenceresolver.h"
#include "platformator/animationclip.h"

#include "platformator/scriptregistration.h"
#include "runtimeaccess.h"

#include <stdexcept>

namespace
{
void validate_clip_timing(const nlohmann::json &document)
{
    if (!document.is_object() || !document.contains("frames") || !document["frames"].is_array() ||
        !document.contains("framesPerSecond") || !document["framesPerSecond"].is_number())
        throw std::runtime_error("animation clip requires a frame array and numeric framesPerSecond");
    const double framesPerSecond = document["framesPerSecond"].get<double>();
    for (std::size_t index = 0; index < document["frames"].size(); ++index)
    {
        const auto &frame = document["frames"][index];
        if (!frame.is_object() || !frame.contains("duration") || !frame["duration"].is_number())
            throw std::runtime_error("animation frame " + std::to_string(index) +
                                     " requires a numeric duration");
        const auto timing = resolve_animation_frame_timing(frame["duration"].get<double>(),
                                                            framesPerSecond);
        if (timing.error == AnimationTimingError::InvalidFrameDuration)
            throw std::runtime_error("animation frame " + std::to_string(index) +
                                     " duration must be finite and nonnegative");
        if (timing.error == AnimationTimingError::InvalidFallbackRate)
            throw std::runtime_error("animation frame " + std::to_string(index) +
                                     " uses zero duration and requires a finite positive FPS fallback");
    }
}

void validate_clip_timing(const AnimationClip &clip)
{
    for (std::size_t index = 0; index < clip.frames.size(); ++index)
    {
        const auto timing = resolve_animation_frame_timing(clip.frames[index].duration,
                                                            clip.framesPerSecond);
        if (timing.error == AnimationTimingError::InvalidFrameDuration)
            throw std::runtime_error("animation frame " + std::to_string(index) +
                                     " duration must be finite and nonnegative");
        if (timing.error == AnimationTimingError::InvalidFallbackRate)
            throw std::runtime_error("animation frame " + std::to_string(index) +
                                     " uses zero duration and requires a finite positive FPS fallback");
    }
}
} // namespace

void to_json(nlohmann::json &j, const AnimationFrame &frame)
{
    j = nlohmann::json{{"duration", frame.duration},
                       {"sourceRect", frame.sourceRect},
                       {"hasSourceRect", frame.hasSourceRect},
                       {"textureWrapperFilePath", frame.textureWrapper ? frame.textureWrapper->getFilePath() : ""}};
}
void deserialize(const nlohmann::json &j, AnimationFrame &frame, platformator::ReferenceResolver &resolver)
{
    j.at("duration").get_to(frame.duration);
    j.at("hasSourceRect").get_to(frame.hasSourceRect);
    j.at("sourceRect").get_to(frame.sourceRect);
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
                       {"framesPerSecond", clip.framesPerSecond},
                       {"loop", clip.loop},
                       {"width", clip.width},
                       {"height", clip.height},
                       {"name", clip.name},
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
    j.at("framesPerSecond").get_to(clip.framesPerSecond);
    j.at("loop").get_to(clip.loop);
    j.at("width").get_to(clip.width);
    j.at("height").get_to(clip.height);
    j.at("name").get_to(clip.name);

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
