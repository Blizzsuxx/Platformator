#pragma once

#include "platform/nativeprocess.h"

namespace platformator_editor
{
struct ProcessState
{
    std::unique_ptr<NativeProcess> native;
};
} // namespace platformator_editor
