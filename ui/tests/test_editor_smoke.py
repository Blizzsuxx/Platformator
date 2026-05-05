import os
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtWidgets import QApplication

from platformator_ui.main_window import MainWindow
from platformator_ui.services.project_paths import ProjectPaths


def test_main_window_smoke() -> None:
    app = QApplication.instance() or QApplication([])
    project_paths = ProjectPaths.discover(Path(__file__).resolve())
    window = MainWindow(project_paths, restore_last_scene=False)

    assert window.scene_document.objects
    assert window.windowTitle().startswith("Platformator UI")
    window.close()