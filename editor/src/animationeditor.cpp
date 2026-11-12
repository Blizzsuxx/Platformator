#include "animationeditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <system_error>

#include "platformator/animationclip.h"

namespace platformator_editor
{
namespace
{
bool preview_path_is_within(const std::filesystem::path &root,
                            const std::filesystem::path &candidate)
{
    const auto relative = candidate.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty())
        return candidate.lexically_normal() == root.lexically_normal();
    if (relative.is_absolute())
        return false;
    return std::none_of(relative.begin(), relative.end(), [](const auto &part) {
        return part == "..";
    });
}

double numeric_member(const nlohmann::json &object, std::string_view name)
{
    const auto found = object.find(std::string(name));
    return found != object.end() && found->is_number()
        ? found->get<double>() : std::numeric_limits<double>::quiet_NaN();
}

void validate_animation_frame_runtime_schema(const nlohmann::json &frame,
                                             const std::string &framePath,
                                             std::vector<AnimationValidationIssue> &issues)
{
    const auto issue = [&](std::string suffix, std::string message) {
        issues.push_back({framePath + std::move(suffix), std::move(message)});
    };
    const auto texture = frame.find("textureWrapperFilePath");
    if (texture == frame.end() || !texture->is_string())
        issue("/textureWrapperFilePath", "Frame texture path must be a string (empty means unassigned).");

    const auto hasRect = frame.find("hasSourceRect");
    if (hasRect == frame.end() || !hasRect->is_boolean())
        issue("/hasSourceRect", "Frame hasSourceRect must be a boolean.");

    const auto rect = frame.find("sourceRect");
    if (rect == frame.end() || !rect->is_object())
    {
        issue("/sourceRect", "Frame sourceRect must contain numeric x, y, w and h fields.");
        return;
    }
    std::array<double, 4> values{};
    constexpr std::array<const char *, 4> keys{"x", "y", "w", "h"};
    for (std::size_t index = 0; index < keys.size(); ++index)
    {
        const auto value = rect->find(keys[index]);
        if (value == rect->end() || !value->is_number())
        {
            issue(std::string("/sourceRect/") + keys[index], "Source rectangle fields must be finite numbers representable as floats.");
            continue;
        }
        values[index] = value->get<double>();
        if (!std::isfinite(values[index]) ||
            std::abs(values[index]) > static_cast<double>(std::numeric_limits<float>::max()))
            issue(std::string("/sourceRect/") + keys[index], "Source rectangle fields must be finite numbers representable as floats.");
    }
    if (hasRect != frame.end() && hasRect->is_boolean() && hasRect->get<bool>() &&
        std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); }) &&
        (values[0] < 0.0 || values[1] < 0.0 || values[2] <= 0.0 || values[3] <= 0.0 ||
         static_cast<float>(values[2]) <= 0.0f || static_cast<float>(values[3]) <= 0.0f))
        issue("/sourceRect", "An enabled source rectangle needs nonnegative origin and positive size.");
}
}

nlohmann::json make_default_animation_frame()
{
    return nlohmann::json{
        {"duration", 0.1},
        {"textureWrapperFilePath", ""},
        {"hasSourceRect", false},
        {"sourceRect", {{"x", 0.0}, {"y", 0.0}, {"w", 0.0}, {"h", 0.0}}}};
}

AnimationClipValidation validate_animation_clip_document(const nlohmann::json &document)
{
    AnimationClipValidation result;
    if (!document.is_object())
    {
        result.issues.push_back({"", "Animation asset root must be a JSON object."});
        return result;
    }
    const auto loop = document.find("loop");
    if (loop == document.end() || !loop->is_boolean())
        result.issues.push_back({"/loop", "Loop must be a boolean."});

    const auto filePath = document.find("filePath");
    if (filePath == document.end() || !filePath->is_string())
        result.issues.push_back({"/filePath", "filePath must be a string."});

    const auto frames = document.find("frames");
    if (frames == document.end() || !frames->is_array())
    {
        result.issues.push_back({"/frames", "Frames must be an array."});
        return result;
    }

    for (std::size_t index = 0; index < frames->size(); ++index)
    {
        const auto &frame = (*frames)[index];
        const std::string framePath = "/frames/" + std::to_string(index);
        if (!frame.is_object())
        {
            result.issues.push_back({framePath, "Animation frame must be an object."});
            continue;
        }
        validate_animation_frame_runtime_schema(frame, framePath, result.issues);
        const auto duration = frame.find("duration");
        if (duration == frame.end() || !duration->is_number())
        {
            result.issues.push_back({framePath + "/duration", "Frame duration must be numeric."});
            continue;
        }
        const double durationValue = duration->get<double>();
        if (!is_valid_animation_frame_duration(durationValue))
            result.issues.push_back({framePath + "/duration",
                                     "Frame duration must be finite and positive."});
        else if (durationValue > static_cast<double>(std::numeric_limits<float>::max()) ||
                 static_cast<float>(durationValue) <= 0.0f)
            result.issues.push_back({framePath + "/duration",
                                     "Frame duration must be representable as a positive runtime float."});
    }
    return result;
}

AnimationPreviewDocument prepare_animation_preview(
    const nlohmann::json &document, const std::filesystem::path &assetRoot)
{
    AnimationPreviewDocument result;
    if (!document.is_object())
    {
        result.error = "Animation preview root must be an object.";
        return result;
    }
    const auto frames = document.find("frames");
    if (frames == document.end() || !frames->is_array())
    {
        result.error = "Animation preview frames must be an array.";
        return result;
    }
    if (frames->size() > AnimationPreviewDocument::MaximumFrames)
    {
        result.error = "Animation preview exceeds the 512-frame limit.";
        return result;
    }
    const auto loop = document.find("loop");
    result.loop = loop != document.end() && loop->is_boolean() ? loop->get<bool>() : true;

    std::size_t projectedBytes = 64;
    result.frames.reserve(frames->size());
    result.frameEndTimes.reserve(frames->size());
    for (std::size_t index = 0; index < frames->size(); ++index)
    {
        const auto &frame = (*frames)[index];
        if (!frame.is_object())
        {
            result.error = "Animation preview frame " + std::to_string(index + 1) +
                           " must be an object.";
            result.frames.clear();
            result.frameEndTimes.clear();
            return result;
        }
        const double duration = numeric_member(frame, "duration");
        if (!is_valid_animation_frame_duration(duration))
        {
            result.error = "Animation preview contains an invalid frame duration.";
            result.frames.clear();
            result.frameEndTimes.clear();
            return result;
        }
        if (!std::isfinite(result.totalDuration + duration))
        {
            result.error = "Animation preview duration exceeds the supported numeric range.";
            result.frames.clear();
            result.frameEndTimes.clear();
            return result;
        }

        AnimationPreviewFrame previewFrame;
        previewFrame.durationSeconds = duration;
        const auto texture = frame.find("textureWrapperFilePath");
        std::string reference;
        if (texture != frame.end() && texture->is_string())
        {
            reference = texture->get<std::string>();
            std::filesystem::path path(reference);
            if (!path.empty() && !path.is_absolute())
            {
                if (path.begin() != path.end() && *path.begin() == "assets")
                    path = path.lexically_relative("assets");
                path = (assetRoot / path).lexically_normal();
            }
            if (!path.empty() && preview_path_is_within(assetRoot, path))
                previewFrame.texturePath = std::move(path);
        }
        projectedBytes += 128;
        if (reference.size() > AnimationPreviewDocument::MaximumBytes -
                               std::min(projectedBytes, AnimationPreviewDocument::MaximumBytes))
        {
            result.error = "Animation preview projection exceeds the 1 MiB limit.";
            result.frames.clear();
            result.frameEndTimes.clear();
            return result;
        }
        projectedBytes += reference.size();
        if (projectedBytes > AnimationPreviewDocument::MaximumBytes)
        {
            result.error = "Animation preview projection exceeds the 1 MiB limit.";
            result.frames.clear();
            result.frameEndTimes.clear();
            return result;
        }

        const auto hasRect = frame.find("hasSourceRect");
        const auto rect = frame.find("sourceRect");
        if (hasRect != frame.end() && hasRect->is_boolean() && hasRect->get<bool>() &&
            rect != frame.end() && rect->is_object())
        {
            const double x = numeric_member(*rect, "x");
            const double y = numeric_member(*rect, "y");
            const double width = numeric_member(*rect, "w");
            const double height = numeric_member(*rect, "h");
            if (std::isfinite(x) && std::isfinite(y) && std::isfinite(width) &&
                std::isfinite(height) && x >= 0.0 && y >= 0.0 && width > 0.0 && height > 0.0)
            {
                previewFrame.hasSourceRect = true;
                previewFrame.sourceX = x;
                previewFrame.sourceY = y;
                previewFrame.sourceWidth = width;
                previewFrame.sourceHeight = height;
            }
        }
        result.totalDuration += duration;
        result.frameEndTimes.push_back(result.totalDuration);
        result.frames.push_back(std::move(previewFrame));
    }
    result.valid = true;
    return result;
}

std::size_t animation_preview_frame_at_time(const AnimationPreviewDocument &preview,
                                            double seconds) noexcept
{
    if (!preview.valid)
        return 0;
    return animation_preview_frame_at_time(preview.frameEndTimes, preview.totalDuration,
                                           preview.loop, seconds);
}

std::size_t animation_preview_frame_at_time(const std::vector<double> &frameEndTimes,
                                            double totalDuration, bool loop,
                                            double seconds) noexcept
{
    if (frameEndTimes.empty() || !std::isfinite(seconds) || seconds < 0.0 ||
        !std::isfinite(totalDuration) || totalDuration <= 0.0)
        return 0;
    if (loop)
        seconds = std::fmod(seconds, totalDuration);
    else if (seconds >= totalDuration)
        return frameEndTimes.size() - 1;
    const auto found = std::upper_bound(frameEndTimes.begin(), frameEndTimes.end(), seconds);
    return found == frameEndTimes.end() ? frameEndTimes.size() - 1
        : static_cast<std::size_t>(std::distance(frameEndTimes.begin(), found));
}
} // namespace platformator_editor
