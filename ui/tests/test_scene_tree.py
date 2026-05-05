import os

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtWidgets import QApplication

from platformator_ui.scene import SceneIdAllocator, SpriteComponent, add_game_object, create_empty_scene
from platformator_ui.widgets.scene_tree import COMPONENT_ITEM_KIND, ITEM_KIND_ROLE, OBJECT_ITEM_KIND, SceneTreeWidget


def test_scene_tree_shows_components_and_emits_component_selection() -> None:
    app = QApplication.instance() or QApplication([])
    scene_document = create_empty_scene(include_default_camera=False)
    allocator = SceneIdAllocator.from_scene(scene_document)
    game_object = add_game_object(scene_document, allocator, name="Player")
    game_object.components.append(SpriteComponent(id=allocator.allocate(), width=32.0, height=32.0))

    tree = SceneTreeWidget()
    tree.set_scene(scene_document)

    object_item = tree.topLevelItem(0)
    assert object_item.data(0, ITEM_KIND_ROLE) == OBJECT_ITEM_KIND
    assert not object_item.icon(0).isNull()

    component_item = object_item.child(0)
    assert component_item is not None
    assert component_item.data(0, ITEM_KIND_ROLE) == COMPONENT_ITEM_KIND
    assert component_item.text(0) == f"Sprite ({game_object.components[0].id})"
    assert not component_item.icon(0).isNull()

    selected_ids: list[tuple[int | None, int | None]] = []
    tree.editorSelectionChanged.connect(lambda object_id, component_id: selected_ids.append((object_id, component_id)))
    tree.setCurrentItem(component_item)

    assert selected_ids[-1] == (game_object.id, game_object.components[0].id)
    assert tree.currentItem() is component_item
    tree.close()