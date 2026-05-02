from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import QMenu, QTreeWidget, QTreeWidgetItem

from platformator_ui.scene.models import GameObjectModel, SceneDocumentModel


class SceneTreeWidget(QTreeWidget):
    objectSelected = Signal(object)
    objectAddRequested = Signal(object)
    objectDeleteRequested = Signal(int)
    objectDuplicateRequested = Signal(int)
    componentRequested = Signal(int, str)

    COMPONENT_ORDER = (
        "Camera",
        "Rigidbody",
        "BoxCollider",
        "CircleCollider",
        "Sprite",
        "Animator",
        "Audio",
        "Script",
    )

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setHeaderLabels(["Scene Objects"])
        self.itemSelectionChanged.connect(self._emit_selection)
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.customContextMenuRequested.connect(self._show_context_menu)

    def set_scene(self, scene_document: SceneDocumentModel) -> None:
        self.clear()
        for game_object in scene_document.objects:
            self.addTopLevelItem(self._build_item(game_object))
        self.expandAll()

    def select_object(self, object_id: int | None) -> None:
        if object_id is None:
            self.clearSelection()
            return

        for item in self.findItems("", Qt.MatchFlag.MatchContains | Qt.MatchFlag.MatchRecursive, 0):
            if item.data(0, Qt.ItemDataRole.UserRole) == object_id:
                self.setCurrentItem(item)
                return

    def _build_item(self, game_object: GameObjectModel) -> QTreeWidgetItem:
        item = QTreeWidgetItem([f"{game_object.name} ({game_object.id})"])
        item.setData(0, Qt.ItemDataRole.UserRole, game_object.id)
        for child in game_object.children:
            item.addChild(self._build_item(child))
        return item

    def _emit_selection(self) -> None:
        current_item = self.currentItem()
        self.objectSelected.emit(current_item.data(0, Qt.ItemDataRole.UserRole) if current_item is not None else None)

    def _show_context_menu(self, position) -> None:
        item = self.itemAt(position)
        if item is None:
            menu = QMenu(self)
            add_object_action = menu.addAction("Add Object")
            add_object_action.triggered.connect(lambda _checked=False: self.objectAddRequested.emit(None))
            menu.exec(self.viewport().mapToGlobal(position))
            return

        object_id = item.data(0, Qt.ItemDataRole.UserRole)
        if not isinstance(object_id, int):
            return

        self.setCurrentItem(item)

        menu = QMenu(self)
        add_child_action = menu.addAction("Add Child Object")
        add_child_action.triggered.connect(lambda _checked=False, object_id=object_id: self.objectAddRequested.emit(object_id))

        duplicate_action = menu.addAction("Duplicate Object")
        duplicate_action.triggered.connect(
            lambda _checked=False, object_id=object_id: self.objectDuplicateRequested.emit(object_id)
        )

        delete_action = menu.addAction("Delete Object")
        delete_action.triggered.connect(lambda _checked=False, object_id=object_id: self.objectDeleteRequested.emit(object_id))

        menu.addSeparator()
        add_component_menu = menu.addMenu("Add Component")
        for component_name in self.COMPONENT_ORDER:
            action = add_component_menu.addAction(component_name)
            action.triggered.connect(
                lambda _checked=False, object_id=object_id, component_name=component_name: self.componentRequested.emit(
                    object_id,
                    component_name,
                )
            )

        menu.exec(self.viewport().mapToGlobal(position))
