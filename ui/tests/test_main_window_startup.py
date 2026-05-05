import os
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtWidgets import QApplication

import platformator_ui.main_window as main_window_module
from platformator_ui.main_window import MainWindow
from platformator_ui.scene import SceneIdAllocator, ScriptBehaviorModel, ScriptComponentModel, add_game_object, create_empty_scene
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


def test_main_window_refreshes_behavior_metadata_when_sources_change(monkeypatch) -> None:
    app = QApplication.instance() or QApplication([])
    project_paths = ProjectPaths.discover(Path(__file__).resolve())

    current_templates: dict[str, dict[str, object]] = {"HotReloadBehavior": {"speed": 0.0}}
    source_state = {"value": (("src/hot_reload_behavior.h", 1, 10),)}

    monkeypatch.setattr(main_window_module, "clear_behavior_discovery_caches", lambda: None)
    monkeypatch.setattr(main_window_module, "snapshot_behavior_source_state", lambda _repo_root: source_state["value"])
    monkeypatch.setattr(main_window_module, "discover_behavior_templates", lambda _repo_root, _scene_document: current_templates)
    monkeypatch.setattr(main_window_module, "discover_behavior_asset_fields", lambda _repo_root: {})
    monkeypatch.setattr(main_window_module, "discover_behavior_object_reference_fields", lambda _repo_root: {})

    window = MainWindow(project_paths, restore_last_scene=False)

    scene_document = create_empty_scene(include_default_camera=False)
    allocator = SceneIdAllocator.from_scene(scene_document)
    game_object = add_game_object(scene_document, allocator, name="Player")
    behavior = ScriptBehaviorModel.model_validate({"type": "HotReloadBehavior", "speed": 5.0})
    game_object.components.append(ScriptComponentModel(id=allocator.allocate(), behaviors=[behavior]))

    window.scene_document = scene_document
    window._reload_scene_views(select_first=True)

    current_templates = {"HotReloadBehavior": {"speed": 0.0, "grounded": False}}
    source_state["value"] = (("src/hot_reload_behavior.h", 2, 20),)
    window._maybe_refresh_behavior_metadata()

    assert window.behavior_templates["HotReloadBehavior"] == {"speed": 0.0, "grounded": False}
    assert behavior.grounded is False
    window.close()