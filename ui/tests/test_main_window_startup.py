import os
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtWidgets import QApplication

import platformator_ui.main_window as main_window_module
from platformator_ui.main_window import MainWindow
from platformator_ui.services.project_paths import ProjectPaths


def test_main_window_restores_last_scene_and_exposes_settings_menu(monkeypatch, tmp_path: Path) -> None:
    app = QApplication.instance() or QApplication([])
    project_paths = ProjectPaths.discover(Path(__file__).resolve())
    scene_path = tmp_path / "recent.scene"
    scene_path.write_text("[]", encoding="utf-8")

    class StubRecentFilesStore:
        def __init__(self) -> None:
            self.saved_paths: list[Path] = []

        def most_recent_file(self) -> Path | None:
            return scene_path

        def add_file(self, path: Path) -> None:
            self.saved_paths.append(path)

    monkeypatch.setattr(main_window_module, "RecentFilesStore", StubRecentFilesStore)

    window = MainWindow(project_paths)

    assert window.current_scene_path == scene_path

    settings_action = next(action for action in window.menuBar().actions() if action.text() == "Settings")
    settings_menu = settings_action.menu()
    assert settings_menu is not None
    assert [action.text() for action in settings_menu.actions()] == ["Project Settings"]
    window.close()