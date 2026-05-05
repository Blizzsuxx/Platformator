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


def create_build_spec(
    project_paths: ProjectPaths,
    preset: str = "debug",
    *,
    target_name: str | None = None,
) -> ProcessSpec:
    resolved_target_name = target_name or project_paths.main_binary.name
    return ProcessSpec(
        label="Build",
        program="cmake",
        arguments=("--build", "--preset", preset, "--target", resolved_target_name),
        working_directory=project_paths.repo_root,
    )


def create_run_spec(
    project_paths: ProjectPaths,
    scene_path: Path,
    *,
    preset: str = "debug",
    program_path: Path | None = None,
    runtime_arguments: tuple[str, ...] = (),
) -> ProcessSpec:
    return ProcessSpec(
        label="Run",
        program=str(program_path or project_paths.main_binary_for_preset(preset)),
        arguments=(str(scene_path), *runtime_arguments),
        working_directory=project_paths.repo_root,
    )


def create_run_pipeline(
    project_paths: ProjectPaths,
    scene_path: Path,
    preset: str = "debug",
    *,
    target_name: str | None = None,
    program_path: Path | None = None,
    runtime_arguments: tuple[str, ...] = (),
) -> list[ProcessSpec]:
    return [
        create_configure_spec(project_paths, preset=preset),
        create_build_spec(project_paths, preset=preset, target_name=target_name),
        create_run_spec(project_paths, scene_path=scene_path, preset=preset, program_path=program_path, runtime_arguments=runtime_arguments),
    ]
