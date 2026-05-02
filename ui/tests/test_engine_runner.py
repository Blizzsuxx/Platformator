from pathlib import Path

from platformator_ui.engine import create_run_pipeline
from platformator_ui.services.project_paths import ProjectPaths


def test_run_pipeline_targets_debug_build_and_main_binary() -> None:
    project_paths = ProjectPaths.discover(Path(__file__).resolve())
    pipeline = create_run_pipeline(project_paths, scene_path=project_paths.default_scene)

    assert len(pipeline) == 3
    assert pipeline[0].program == "cmake"
    assert pipeline[0].arguments == ("--preset", "debug")
    assert pipeline[1].arguments == ("--build", "--preset", "debug")
    assert pipeline[2].program.endswith("/bin/main")
    assert pipeline[2].arguments == (str(project_paths.default_scene),)
