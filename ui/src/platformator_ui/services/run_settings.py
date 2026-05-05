from __future__ import annotations

from dataclasses import dataclass

from PySide6.QtCore import QSettings

from platformator_ui.settings import (
    DEFAULT_RUN_BUILD_PRESET,
    DEFAULT_RUN_DEBUG_START_PAUSED,
    DEFAULT_RUN_DEBUG_DRAW_COLLIDERS,
    DEFAULT_RUN_DEBUG_DRAW_COLLISION_NORMALS,
    DEFAULT_RUN_DEBUG_DRAW_COLLISION_POINTS,
    DEFAULT_RUN_DEBUG_DRAW_GRID_CELLS,
    DEFAULT_RUN_FULLSCREEN,
    DEFAULT_RUN_KEEP_ASPECT_RATIO,
    DEFAULT_RUN_MAXIMIZED,
    DEFAULT_RUN_WINDOW_HEIGHT,
    DEFAULT_RUN_WINDOW_WIDTH,
    RUN_BUILD_PRESET_KEY,
    RUN_DEBUG_START_PAUSED_KEY,
    RUN_DEBUG_DRAW_COLLIDERS_KEY,
    RUN_DEBUG_DRAW_COLLISION_NORMALS_KEY,
    RUN_DEBUG_DRAW_COLLISION_POINTS_KEY,
    RUN_DEBUG_DRAW_GRID_CELLS_KEY,
    RUN_FULLSCREEN_KEY,
    RUN_KEEP_ASPECT_RATIO_KEY,
    RUN_MAXIMIZED_KEY,
    RUN_WINDOW_HEIGHT_KEY,
    RUN_WINDOW_WIDTH_KEY,
)


SUPPORTED_BUILD_PRESETS = {"debug", "production"}


def _read_text(value: object, default: str) -> str:
    if value is None:
        return default
    text = str(value).strip()
    return text or default


def _read_build_preset(value: object, default: str) -> str:
    build_preset = _read_text(value, default)
    return build_preset if build_preset in SUPPORTED_BUILD_PRESETS else default


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
    build_preset: str = DEFAULT_RUN_BUILD_PRESET
    debug_start_paused: bool = DEFAULT_RUN_DEBUG_START_PAUSED
    debug_draw_colliders: bool = DEFAULT_RUN_DEBUG_DRAW_COLLIDERS
    debug_draw_collision_points: bool = DEFAULT_RUN_DEBUG_DRAW_COLLISION_POINTS
    debug_draw_collision_normals: bool = DEFAULT_RUN_DEBUG_DRAW_COLLISION_NORMALS
    debug_draw_grid_cells: bool = DEFAULT_RUN_DEBUG_DRAW_GRID_CELLS

    def __post_init__(self) -> None:
        if self.window_width <= 0:
            raise ValueError("window_width must be greater than zero")
        if self.window_height <= 0:
            raise ValueError("window_height must be greater than zero")
        if self.build_preset not in SUPPORTED_BUILD_PRESETS:
            raise ValueError(f"Unsupported build_preset: {self.build_preset}")

    def _debug_draw_categories(self) -> tuple[str, ...]:
        categories: list[str] = []
        if self.debug_draw_colliders:
            categories.append("colliders")
        if self.debug_draw_collision_points:
            categories.append("collision-points")
        if self.debug_draw_collision_normals:
            categories.append("collision-normals")
        if self.debug_draw_grid_cells:
            categories.append("grid-cells")
        return tuple(categories)

    def _debug_draw_override(self) -> str | None:
        if self.build_preset == "production":
            return None

        categories = self._debug_draw_categories()
        if len(categories) == 4:
            return None
        if not categories:
            return "none"
        return ",".join(categories)

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

        debug_draw_override = self._debug_draw_override()
        if debug_draw_override is not None:
            arguments.extend(("--debug-draw", debug_draw_override))
        if self.build_preset != "production" and self.debug_start_paused:
            arguments.append("--start-paused")

        return tuple(arguments)

    def summary(self) -> str:
        build_profile = "debug build" if self.build_preset == "debug" else "production build"
        mode = "fullscreen" if self.fullscreen else "maximized" if self.maximize_on_startup else "windowed"
        aspect_ratio = "keep aspect ratio" if self.keep_aspect_ratio else "stretch to window"

        if self.build_preset == "production":
            debug_draw = "debug tools off"
        else:
            categories = self._debug_draw_categories()
            debug_draw = "debug draw default" if len(categories) == 4 else f"debug draw {', '.join(categories) if categories else 'none'}"

        pause_state = ", paused on launch" if self.build_preset != "production" and self.debug_start_paused else ""

        return f"{build_profile}, {self.window_width}x{self.window_height}, {mode}, {aspect_ratio}, {debug_draw}{pause_state}"


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
            build_preset=_read_build_preset(self._settings.value(RUN_BUILD_PRESET_KEY), DEFAULT_RUN_BUILD_PRESET),
            debug_start_paused=_read_bool(self._settings.value(RUN_DEBUG_START_PAUSED_KEY), DEFAULT_RUN_DEBUG_START_PAUSED),
            debug_draw_colliders=_read_bool(self._settings.value(RUN_DEBUG_DRAW_COLLIDERS_KEY), DEFAULT_RUN_DEBUG_DRAW_COLLIDERS),
            debug_draw_collision_points=_read_bool(self._settings.value(RUN_DEBUG_DRAW_COLLISION_POINTS_KEY), DEFAULT_RUN_DEBUG_DRAW_COLLISION_POINTS),
            debug_draw_collision_normals=_read_bool(self._settings.value(RUN_DEBUG_DRAW_COLLISION_NORMALS_KEY), DEFAULT_RUN_DEBUG_DRAW_COLLISION_NORMALS),
            debug_draw_grid_cells=_read_bool(self._settings.value(RUN_DEBUG_DRAW_GRID_CELLS_KEY), DEFAULT_RUN_DEBUG_DRAW_GRID_CELLS),
        )

    def save(self, run_window_settings: RunWindowSettings) -> None:
        self._settings.setValue(RUN_WINDOW_WIDTH_KEY, run_window_settings.window_width)
        self._settings.setValue(RUN_WINDOW_HEIGHT_KEY, run_window_settings.window_height)
        self._settings.setValue(RUN_FULLSCREEN_KEY, run_window_settings.fullscreen)
        self._settings.setValue(RUN_MAXIMIZED_KEY, run_window_settings.maximize_on_startup)
        self._settings.setValue(RUN_KEEP_ASPECT_RATIO_KEY, run_window_settings.keep_aspect_ratio)
        self._settings.setValue(RUN_BUILD_PRESET_KEY, run_window_settings.build_preset)
        self._settings.setValue(RUN_DEBUG_START_PAUSED_KEY, run_window_settings.debug_start_paused)
        self._settings.setValue(RUN_DEBUG_DRAW_COLLIDERS_KEY, run_window_settings.debug_draw_colliders)
        self._settings.setValue(RUN_DEBUG_DRAW_COLLISION_POINTS_KEY, run_window_settings.debug_draw_collision_points)
        self._settings.setValue(RUN_DEBUG_DRAW_COLLISION_NORMALS_KEY, run_window_settings.debug_draw_collision_normals)
        self._settings.setValue(RUN_DEBUG_DRAW_GRID_CELLS_KEY, run_window_settings.debug_draw_grid_cells)