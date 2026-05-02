import os
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from platformator_ui.main_window import MainWindow
from platformator_ui.services.project_paths import ProjectPaths


def test_main_window_smoke(qtbot) -> None:
    project_paths = ProjectPaths.discover(Path(__file__).resolve())
    window = MainWindow(project_paths)
    qtbot.addWidget(window)

    assert window.scene_document.objects
    assert window.windowTitle().startswith("Platformator UI")