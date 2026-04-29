#pragma once

#include <string>

#include "animationclip.h"

class AnimationClipFileWriter
{
public:
    explicit AnimationClipFileWriter(std::string clipPath);

    void write(const AnimationClip &animationClip) const;

private:
    std::string clipPath;
};