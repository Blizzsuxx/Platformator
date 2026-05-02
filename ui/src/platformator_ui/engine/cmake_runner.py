from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from platformator_ui.services.project_paths import ProjectPaths


@dataclass(frozen=True)
class ProcessSpec:
    label: str
    program: str
    arguments: tuple[str, ...]
    working_directory: Path

    def display_command(self) -> str:
        return " ".join((self.program, *self.arguments))


def create_configure_spec(project_paths: ProjectPaths, preset: str = "debug") -> ProcessSpec:
    return ProcessSpec(
        label="Configure",
        program="cmake",
        arguments=("--preset", preset),
        working_directory=project_paths.repo_root,
    )


def create_build_spec(project_paths: ProjectPaths, preset: str = "debug") -> ProcessSpec:
    return ProcessSpec(
        label="Build",
        program="cmake",
        arguments=("--build", "--preset", preset),
        working_directory=project_paths.repo_root,
    )


def create_run_spec(project_paths: ProjectPaths, scene_path: Path) -> ProcessSpec:
    return ProcessSpec(
        label="Run",
        program=str(project_paths.main_binary),
        arguments=(str(scene_path),),
        working_directory=project_paths.repo_root,
    )


def create_run_pipeline(
    project_paths: ProjectPaths,
    scene_path: Path,
    preset: str = "debug",
) -> list[ProcessSpec]:
    return [
        create_configure_spec(project_paths, preset=preset),
        create_build_spec(project_paths, preset=preset),
        create_run_spec(project_paths, scene_path=scene_path),
    ]
