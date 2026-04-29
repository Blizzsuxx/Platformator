#include "animationclipfilewriter.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "tomlwriter.h"

namespace
{
    std::string makeClipResourcePath(const std::string &clipPath, const std::string &resourcePath)
    {
        if (resourcePath.empty())
        {
            return "";
        }

        std::error_code errorCode;
        const std::filesystem::path clipDirectory = std::filesystem::absolute(std::filesystem::path(clipPath), errorCode).parent_path();
        const std::filesystem::path assetPath = std::filesystem::absolute(std::filesystem::path(resourcePath), errorCode);
        if (errorCode)
        {
            return std::filesystem::path(resourcePath).lexically_normal().string();
        }

        const std::filesystem::path relativePath = std::filesystem::relative(assetPath, clipDirectory, errorCode);
        if (errorCode || relativePath.empty())
        {
            return assetPath.lexically_normal().string();
        }

        return relativePath.lexically_normal().string();
    }

    bool requiresFrameTable(const AnimationFrame &frame)
    {
        return frame.hasSourceRect || std::abs(frame.duration) > 1e-6f;
    }
}

AnimationClipFileWriter::AnimationClipFileWriter(std::string clipPath) : clipPath(std::move(clipPath))
{
}

void AnimationClipFileWriter::write(const AnimationClip &animationClip) const
{
    if (animationClip.getFrames().empty())
    {
        throw std::runtime_error("Animation clip file '" + clipPath + "' cannot be written without at least one frame.");
    }

    std::ofstream clipFile(clipPath);
    if (!clipFile.is_open())
    {
        throw std::runtime_error("Failed to open animation clip file '" + clipPath + "' for writing.");
    }

    toml::table root;
    root.insert_or_assign("format", "platformator_animset");
    root.insert_or_assign("version", 1);
    root.insert_or_assign("name", animationClip.getName().empty() ? std::filesystem::path(clipPath).stem().string() : animationClip.getName());
    root.insert_or_assign("fps", static_cast<double>(animationClip.getFramesPerSecond()));
    root.insert_or_assign("loop", animationClip.getLoop());
    root.insert_or_assign("size", toml::array{static_cast<double>(animationClip.getWidth()), static_cast<double>(animationClip.getHeight())});

    toml::array framesArray;
    const std::vector<AnimationFrame> &frames = animationClip.getFrames();
    for (const AnimationFrame &frame : frames)
    {
        if (frame.textureWrapper == nullptr)
        {
            throw std::runtime_error("Animation clip file '" + clipPath + "' contains a frame without a texture.");
        }

        const std::string texturePath = makeClipResourcePath(clipPath, frame.textureWrapper->getFilePath());
        if (!requiresFrameTable(frame))
        {
            framesArray.push_back(texturePath);
        }
        else
        {
            toml::table frameTable;
            frameTable.is_inline(true);
            frameTable.insert_or_assign("path", texturePath);
            if (std::abs(frame.duration) > 1e-6f)
            {
                frameTable.insert_or_assign("duration", static_cast<double>(frame.duration));
            }
            if (frame.hasSourceRect)
            {
                frameTable.insert_or_assign("rect", TomlWriter::makeRectArray(frame.sourceRect));
            }

            framesArray.push_back(std::move(frameTable));
        }
    }

    root.insert_or_assign("frames", std::move(framesArray));
    TomlWriter::writeDocument(clipFile, root);
}