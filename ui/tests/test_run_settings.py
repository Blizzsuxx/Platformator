from PySide6.QtCore import QSettings

from platformator_ui.services.run_settings import RunWindowSettings
from platformator_ui.services.run_settings import RunSettingsStore


def test_run_window_settings_to_cli_args_omits_disabled_flags() -> None:
    run_window_settings = RunWindowSettings(window_width=800, window_height=600)

    assert run_window_settings.to_cli_args() == (
        "--window-width",
        "800",
        "--window-height",
        "600",
    )


def test_run_window_settings_to_cli_args_includes_enabled_flags() -> None:
    run_window_settings = RunWindowSettings(
        window_width=1024,
        window_height=768,
        fullscreen=True,
        maximize_on_startup=True,
        keep_aspect_ratio=True,
    )

    assert run_window_settings.to_cli_args() == (
        "--window-width",
        "1024",
        "--window-height",
        "768",
        "--fullscreen",
        "--maximized",
        "--keep-aspect-ratio",
    )


def test_run_window_settings_to_cli_args_includes_debug_draw_override_for_subset() -> None:
    run_window_settings = RunWindowSettings(
        debug_start_paused=True,
        debug_draw_colliders=True,
        debug_draw_collision_points=False,
        debug_draw_collision_normals=True,
        debug_draw_grid_cells=False,
    )

    assert run_window_settings.to_cli_args() == (
        "--window-width",
        "640",
        "--window-height",
        "480",
        "--debug-draw",
        "colliders,collision-normals",
        "--start-paused",
    )


def test_run_window_settings_to_cli_args_omits_debug_draw_for_production() -> None:
    run_window_settings = RunWindowSettings(
        build_preset="production",
        debug_start_paused=True,
        debug_draw_colliders=False,
        debug_draw_collision_points=False,
        debug_draw_collision_normals=False,
        debug_draw_grid_cells=False,
    )

    assert run_window_settings.to_cli_args() == (
        "--window-width",
        "640",
        "--window-height",
        "480",
    )


def test_run_settings_store_falls_back_to_default_build_preset_for_invalid_value() -> None:
    settings = QSettings(QSettings.Format.IniFormat, QSettings.Scope.UserScope, "PlatformatorTests", "RunSettings")
    settings.clear()
    settings.setValue("run/buildPreset", "staging")

    store = RunSettingsStore(settings)

    assert store.load().build_preset == "debug"