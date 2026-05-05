import os
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtCore import QPointF, Qt
from PySide6.QtWidgets import QApplication

from platformator_ui.main_window import MainWindow
from platformator_ui.scene import BoxColliderComponent, SceneIdAllocator, SpriteComponent, Vector2Model, add_game_object, create_empty_scene
from platformator_ui.services.project_paths import ProjectPaths


class _MouseEventStub:
    def __init__(
        self,
        position: QPointF,
        *,
        button: Qt.MouseButton = Qt.MouseButton.NoButton,
        buttons: Qt.MouseButton = Qt.MouseButton.NoButton,
        modifiers: Qt.KeyboardModifier = Qt.KeyboardModifier.NoModifier,
    ) -> None:
        self._position = position
        self._button = button
        self._buttons = buttons
        self._modifiers = modifiers

    def position(self) -> QPointF:
        return QPointF(self._position)

    def button(self) -> Qt.MouseButton:
        return self._button

    def buttons(self) -> Qt.MouseButton:
        return self._buttons

    def modifiers(self) -> Qt.KeyboardModifier:
        return self._modifiers


def test_main_window_syncs_component_selection_into_inspector() -> None:
    app = QApplication.instance() or QApplication([])
    project_paths = ProjectPaths.discover(Path(__file__).resolve())
    window = MainWindow(project_paths, restore_last_scene=False)

    scene_document = create_empty_scene(include_default_camera=False)
    allocator = SceneIdAllocator.from_scene(scene_document)
    game_object = add_game_object(scene_document, allocator, name="Player")
    sprite = SpriteComponent(id=allocator.allocate(), width=32.0, height=32.0)
    game_object.components.append(sprite)

    window.scene_document = scene_document
    window._reload_scene_views(select_first=True)
    window._on_selection_changed(game_object.id, sprite.id)

    assert window.current_object_id == game_object.id
    assert window.current_component_id == sprite.id
    assert window.inspector.object_group.isHidden()
    assert window.inspector.components_group.title() == "Sprite"
    window.close()


def test_main_window_keeps_object_selection_when_dragging_handle_with_collider() -> None:
    app = QApplication.instance() or QApplication([])
    project_paths = ProjectPaths.discover(Path(__file__).resolve())
    window = MainWindow(project_paths, restore_last_scene=False)

    scene_document = create_empty_scene(include_default_camera=False)
    allocator = SceneIdAllocator.from_scene(scene_document)
    game_object = add_game_object(scene_document, allocator, name="Player")
    collider = BoxColliderComponent(id=allocator.allocate(), width=32.0, height=32.0, offset=Vector2Model())
    game_object.components.append(collider)

    window.scene_document = scene_document
    window._reload_scene_views(select_first=True)
    window._on_selection_changed(game_object.id, None)

    object_state = window.scene_canvas._find_object_state(game_object.id)
    assert object_state is not None
    handle_point = window.scene_canvas._world_to_screen(object_state.world_position)

    window.scene_canvas.mousePressEvent(
        _MouseEventStub(handle_point, button=Qt.MouseButton.LeftButton, buttons=Qt.MouseButton.LeftButton)
    )

    assert window.current_object_id == game_object.id
    assert window.current_component_id is None
    assert not window.inspector.object_group.isHidden()
    window.close()