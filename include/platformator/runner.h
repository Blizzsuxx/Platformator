#pragma once

#include <string>

#include "platformator/runtimeoptions.h"

namespace platformator
{
    int run(const RuntimeOptions &runtimeOptions);
    int run(int argc, char *args[], const std::string &defaultScenePath);
} // namespace platformator