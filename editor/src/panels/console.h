#pragma once

#include "../process.h"

namespace platformator_editor
{
class ConsolePanel
{
  public:
    void draw(const PipelineStatus &status, const BoundedConsole &console);
};
} // namespace platformator_editor
