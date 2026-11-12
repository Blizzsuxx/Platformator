#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

namespace platformator_editor
{
struct AnimationValidationIssue
{
    std::string jsonPointer;
    std::string message;
};

struct AnimationClipValidation
{
    std::vector<AnimationValidationIssue> issues;

    bool valid() const noexcept { return issues.empty(); }
};

AnimationClipValidation validate_animation_clip_document(const nlohmann::json &document);
nlohmann::json make_default_animation_frame();

struct AnimationPreviewFrame
{
    std::filesystem::path texturePath;
    double durationSeconds = 0.0;
    bool hasSourceRect = false;
    double sourceX = 0.0;
    double sourceY = 0.0;
    double sourceWidth = 0.0;
    double sourceHeight = 0.0;
};

struct AnimationPreviewDocument
{
    static constexpr std::size_t MaximumBytes = 1024U * 1024U;
    static constexpr std::size_t MaximumFrames = 512;

    std::vector<AnimationPreviewFrame> frames;
    std::vector<double> frameEndTimes;
    double totalDuration = 0.0;
    bool loop = true;
    bool valid = false;
    std::string error;
};

AnimationPreviewDocument prepare_animation_preview(
    const nlohmann::json &document, const std::filesystem::path &assetRoot);
std::size_t animation_preview_frame_at_time(const AnimationPreviewDocument &preview,
                                            double seconds) noexcept;
std::size_t animation_preview_frame_at_time(const std::vector<double> &frameEndTimes,
                                            double totalDuration, bool loop,
                                            double seconds) noexcept;
} // namespace platformator_editor
