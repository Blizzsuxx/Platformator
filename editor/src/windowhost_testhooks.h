#pragma once

namespace platformator_editor
{
enum class WindowHostFailurePoint
{
    None,
    RendererCreation
};

// Scoped, one-shot failure injection for deterministic native-host cleanup tests.
void set_window_host_failure_point_for_testing(WindowHostFailurePoint point) noexcept;
} // namespace platformator_editor
