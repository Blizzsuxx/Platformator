import os

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtWidgets import QToolButton
from PySide6.QtWidgets import QApplication

from platformator_ui.scene import SceneIdAllocator, add_game_object, create_empty_scene
from platformator_ui.widgets.inspector import InspectorWidget


def test_inspector_add_component_button_matches_context_menu() -> None:
    app = QApplication.instance() or QApplication([])
    scene_document = create_empty_scene(include_default_camera=False)
    allocator = SceneIdAllocator.from_scene(scene_document)
    game_object = add_game_object(scene_document, allocator, name="Player")

    inspector = InspectorWidget(scene_document_provider=lambda: scene_document)
    inspector.set_object(game_object)

    add_button = inspector.findChild(QToolButton, "inspectorAddComponentButton")
    assert add_button is not None

    captured: list[tuple[int, str]] = []
    inspector.componentRequested.connect(lambda object_id, component_name: captured.append((object_id, component_name)))

    sprite_action = next(action for action in add_button.menu().actions() if action.text() == "Sprite")
    sprite_action.trigger()

    assert captured == [(game_object.id, "Sprite")]
    inspector.close()