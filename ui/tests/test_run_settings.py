from platformator_ui.services.run_settings import RunWindowSettings


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