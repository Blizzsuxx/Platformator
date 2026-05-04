from __future__ import annotations

from dataclasses import dataclass

from PySide6.QtCore import QSettings

from platformator_ui.settings import (
    DEFAULT_RUN_FULLSCREEN,
    DEFAULT_RUN_KEEP_ASPECT_RATIO,
    DEFAULT_RUN_MAXIMIZED,
    DEFAULT_RUN_WINDOW_HEIGHT,
    DEFAULT_RUN_WINDOW_WIDTH,
    RUN_FULLSCREEN_KEY,
    RUN_KEEP_ASPECT_RATIO_KEY,
    RUN_MAXIMIZED_KEY,
    RUN_WINDOW_HEIGHT_KEY,
    RUN_WINDOW_WIDTH_KEY,
)


def _read_bool(value: object, default: bool) -> bool:
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.strip().lower() in {"1", "true", "yes", "on"}
    return bool(value)


def _read_positive_int(value: object, default: int) -> int:
    try:
        parsed = int(value)  # type: ignore[arg-type]
    except (TypeError, ValueError):
        return default
    return max(1, parsed)


@dataclass(frozen=True)
class RunWindowSettings:
    window_width: int = DEFAULT_RUN_WINDOW_WIDTH
    window_height: int = DEFAULT_RUN_WINDOW_HEIGHT
    fullscreen: bool = DEFAULT_RUN_FULLSCREEN
    maximize_on_startup: bool = DEFAULT_RUN_MAXIMIZED
    keep_aspect_ratio: bool = DEFAULT_RUN_KEEP_ASPECT_RATIO

    def __post_init__(self) -> None:
        if self.window_width <= 0:
            raise ValueError("window_width must be greater than zero")
        if self.window_height <= 0:
            raise ValueError("window_height must be greater than zero")

    def to_cli_args(self) -> tuple[str, ...]:
        arguments = [
            "--window-width",
            str(self.window_width),
            "--window-height",
            str(self.window_height),
        ]

        if self.fullscreen:
            arguments.append("--fullscreen")
        if self.maximize_on_startup:
            arguments.append("--maximized")
        if self.keep_aspect_ratio:
            arguments.append("--keep-aspect-ratio")

        return tuple(arguments)

    def summary(self) -> str:
        mode = "fullscreen" if self.fullscreen else "maximized" if self.maximize_on_startup else "windowed"
        aspect_ratio = "keep aspect ratio" if self.keep_aspect_ratio else "stretch to window"
        return f"{self.window_width}x{self.window_height}, {mode}, {aspect_ratio}"


class RunSettingsStore:
    def __init__(self, settings: QSettings | None = None) -> None:
        self._settings = settings or QSettings()

    def load(self) -> RunWindowSettings:
        return RunWindowSettings(
            window_width=_read_positive_int(self._settings.value(RUN_WINDOW_WIDTH_KEY), DEFAULT_RUN_WINDOW_WIDTH),
            window_height=_read_positive_int(self._settings.value(RUN_WINDOW_HEIGHT_KEY), DEFAULT_RUN_WINDOW_HEIGHT),
            fullscreen=_read_bool(self._settings.value(RUN_FULLSCREEN_KEY), DEFAULT_RUN_FULLSCREEN),
            maximize_on_startup=_read_bool(self._settings.value(RUN_MAXIMIZED_KEY), DEFAULT_RUN_MAXIMIZED),
            keep_aspect_ratio=_read_bool(self._settings.value(RUN_KEEP_ASPECT_RATIO_KEY), DEFAULT_RUN_KEEP_ASPECT_RATIO),
        )

    def save(self, run_window_settings: RunWindowSettings) -> None:
        self._settings.setValue(RUN_WINDOW_WIDTH_KEY, run_window_settings.window_width)
        self._settings.setValue(RUN_WINDOW_HEIGHT_KEY, run_window_settings.window_height)
        self._settings.setValue(RUN_FULLSCREEN_KEY, run_window_settings.fullscreen)
        self._settings.setValue(RUN_MAXIMIZED_KEY, run_window_settings.maximize_on_startup)
        self._settings.setValue(RUN_KEEP_ASPECT_RATIO_KEY, run_window_settings.keep_aspect_ratio)