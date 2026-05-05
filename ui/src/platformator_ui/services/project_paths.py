from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class ProjectPaths:
    repo_root: Path
    ui_root: Path
    assets_dir: Path
    scenes_dir: Path
    build_dir: Path
    cmake_presets_file: Path
    main_binary: Path
    mario_example_binary: Path
    default_scene: Path

    def build_dir_for_preset(self, preset: str) -> Path:
        return self.repo_root / "bin" / preset

    def main_binary_for_preset(self, preset: str) -> Path:
        return self.build_dir_for_preset(preset) / self.main_binary.name

    def mario_example_binary_for_preset(self, preset: str) -> Path:
        return self.build_dir_for_preset(preset) / self.mario_example_binary.name

    @classmethod
    def discover(cls, start: Path | None = None) -> "ProjectPaths":
        search_root = (start or Path.cwd()).resolve()
        if search_root.is_file():
            search_root = search_root.parent

        for candidate in (search_root, *search_root.parents):
            cmake_presets = candidate / "CMakePresets.json"
            assets_dir = candidate / "assets"
            if cmake_presets.exists() and assets_dir.exists():
                ui_root = candidate / "ui"
                build_dir = candidate / "bin" / "debug"
                scenes_dir = assets_dir / "scenes"
                return cls(
                    repo_root=candidate,
                    ui_root=ui_root,
                    assets_dir=assets_dir,
                    scenes_dir=scenes_dir,
                    build_dir=build_dir,
                    cmake_presets_file=cmake_presets,
                    main_binary=build_dir / "main",
                    mario_example_binary=build_dir / "mario_example",
                    default_scene=scenes_dir / "default.scene",
                )

        raise FileNotFoundError("Could not locate the Platformator repository root from the given start path.")
