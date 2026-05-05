import os
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtCore import QPoint, QPointF, Qt
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication

from platformator_ui.scene import BoxColliderComponent, CameraComponent, SceneIdAllocator, Vector2Model, add_game_object, create_empty_scene
from platformator_ui.services.project_paths import ProjectPaths
from platformator_ui.widgets.scene_canvas import SceneCanvasWidget


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


def test_scene_canvas_selects_clicked_visual_component() -> None:
    app = QApplication.instance() or QApplication([])
    project_paths = ProjectPaths.discover(Path(__file__).resolve())

    scene_document = create_empty_scene(include_default_camera=False)
    allocator = SceneIdAllocator.from_scene(scene_document)
    game_object = add_game_object(scene_document, allocator, name="Collider")
    collider = BoxColliderComponent(id=allocator.allocate(), width=32.0, height=32.0, offset=Vector2Model())
    game_object.components.append(collider)

    canvas = SceneCanvasWidget(project_paths)
    canvas.resize(480, 320)
    canvas.set_scene(scene_document, reset_view=True)
    canvas.show()
    app.processEvents()

    selected: list[tuple[int | None, int | None]] = []
    canvas.editorSelectionChanged.connect(lambda object_id, component_id: selected.append((object_id, component_id)))

    object_state = canvas._find_object_state(game_object.id)
    assert object_state is not None
    click_point = canvas._world_to_screen(canvas._collider_world_center(object_state, 0.0, 0.0) + QPointF(12.0, 0.0))
    QTest.mouseClick(canvas, Qt.MouseButton.LeftButton, pos=QPoint(round(click_point.x()), round(click_point.y())))

    assert selected[-1] == (game_object.id, collider.id)
    canvas.close()


def test_scene_canvas_cycles_through_overlapping_component_targets() -> None:
    app = QApplication.instance() or QApplication([])
    project_paths = ProjectPaths.discover(Path(__file__).resolve())

    scene_document = create_empty_scene(include_default_camera=False)
    allocator = SceneIdAllocator.from_scene(scene_document)
    game_object = add_game_object(scene_document, allocator, name="Overlap")
    collider = BoxColliderComponent(id=allocator.allocate(), width=32.0, height=32.0, offset=Vector2Model())
    camera = CameraComponent(id=allocator.allocate(), width=32.0, height=32.0)
    game_object.components.extend((collider, camera))

    canvas = SceneCanvasWidget(project_paths)
    canvas.resize(480, 320)
    canvas.set_scene(scene_document, reset_view=True)
    canvas.show()
    app.processEvents()

    selected: list[tuple[int | None, int | None]] = []
    canvas.editorSelectionChanged.connect(lambda object_id, component_id: selected.append((object_id, component_id)))

    click_point = canvas._world_to_screen(QPointF(8.0, 8.0))
    screen_point = QPoint(round(click_point.x()), round(click_point.y()))

    QTest.mouseClick(canvas, Qt.MouseButton.LeftButton, pos=screen_point)
    QTest.mouseClick(canvas, Qt.MouseButton.LeftButton, pos=screen_point)

    assert selected[-2] == (game_object.id, collider.id)
    assert selected[-1] == (game_object.id, camera.id)
    canvas.close()


def test_scene_canvas_ctrl_drag_snaps_object_to_grid() -> None:
    app = QApplication.instance() or QApplication([])
    project_paths = ProjectPaths.discover(Path(__file__).resolve())

    scene_document = create_empty_scene(include_default_camera=False)
    allocator = SceneIdAllocator.from_scene(scene_document)
    game_object = add_game_object(scene_document, allocator, name="SnapObject")

    canvas = SceneCanvasWidget(project_paths)
    canvas.resize(480, 320)
    canvas.set_scene(scene_document, reset_view=True)
    canvas.set_selected_item(game_object.id)

    object_state = canvas._find_object_state(game_object.id)
    assert object_state is not None
    handle_point = canvas._world_to_screen(object_state.world_position)

    canvas.mousePressEvent(_MouseEventStub(handle_point, button=Qt.MouseButton.LeftButton, buttons=Qt.MouseButton.LeftButton))
    canvas.mouseMoveEvent(
        _MouseEventStub(
            canvas._world_to_screen(QPointF(77.0, 90.0)),
            buttons=Qt.MouseButton.LeftButton,
            modifiers=Qt.KeyboardModifier.ControlModifier,
        )
    )

    assert game_object.position.x == 64.0
    assert game_object.position.y == 64.0
    canvas.close()


def test_scene_canvas_ctrl_drag_snaps_collider_to_grid() -> None:
    app = QApplication.instance() or QApplication([])
    project_paths = ProjectPaths.discover(Path(__file__).resolve())

    scene_document = create_empty_scene(include_default_camera=False)
    allocator = SceneIdAllocator.from_scene(scene_document)
    game_object = add_game_object(scene_document, allocator, name="SnapCollider")
    collider = BoxColliderComponent(id=allocator.allocate(), width=32.0, height=32.0, offset=Vector2Model())
    game_object.components.append(collider)

    canvas = SceneCanvasWidget(project_paths)
    canvas.resize(480, 320)
    canvas.set_scene(scene_document, reset_view=True)

    press_point = canvas._world_to_screen(QPointF(12.0, 0.0))
    move_point = canvas._world_to_screen(QPointF(82.0, 95.0))

    canvas.mousePressEvent(_MouseEventStub(press_point, button=Qt.MouseButton.LeftButton, buttons=Qt.MouseButton.LeftButton))
    canvas.mouseMoveEvent(
        _MouseEventStub(
            move_point,
            buttons=Qt.MouseButton.LeftButton,
            modifiers=Qt.KeyboardModifier.ControlModifier,
        )
    )

    assert collider.offset.x == 64.0
    assert collider.offset.y == 64.0
    canvas.close()