from pathlib import Path

from platformator_ui.engine import create_run_pipeline
from platformator_ui.services.project_paths import ProjectPaths
from platformator_ui.services.run_settings import RunWindowSettings


def test_run_pipeline_targets_debug_build_and_main_binary() -> None:
    project_paths = ProjectPaths.discover(Path(__file__).resolve())
    pipeline = create_run_pipeline(project_paths, scene_path=project_paths.default_scene)

    assert len(pipeline) == 3
    assert pipeline[0].program == "cmake"
    assert pipeline[0].arguments == ("--preset", "debug")
    assert pipeline[1].arguments == ("--build", "--preset", "debug", "--target", project_paths.main_binary.name)
    assert pipeline[2].program.endswith("/bin/main")
    assert pipeline[2].arguments == (str(project_paths.default_scene),)


def test_run_pipeline_appends_window_settings_arguments() -> None:
    project_paths = ProjectPaths.discover(Path(__file__).resolve())
    runtime_arguments = RunWindowSettings(
        window_width=1280,
        window_height=720,
        fullscreen=True,
        maximize_on_startup=True,
        keep_aspect_ratio=True,
    ).to_cli_args()

    pipeline = create_run_pipeline(
        project_paths,
        scene_path=project_paths.default_scene,
        runtime_arguments=runtime_arguments,
    )

    assert pipeline[2].arguments == (
        str(project_paths.default_scene),
        "--window-width",
        "1280",
        "--window-height",
        "720",
        "--fullscreen",
        "--maximized",
        "--keep-aspect-ratio",
    )


def test_run_pipeline_can_target_production_preset() -> None:
    project_paths = ProjectPaths.discover(Path(__file__).resolve())
    pipeline = create_run_pipeline(project_paths, scene_path=project_paths.default_scene, preset="production")

    assert pipeline[0].arguments == ("--preset", "production")
    assert pipeline[1].arguments == ("--build", "--preset", "production", "--target", project_paths.main_binary.name)
