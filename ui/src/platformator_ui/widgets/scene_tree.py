from __future__ import annotations

from PySide6.QtCore import QMimeData, Qt, Signal
from PySide6.QtWidgets import QAbstractItemView, QMenu, QStyle, QTreeWidget, QTreeWidgetItem

from platformator_ui.scene.models import BaseComponentModel, GameObjectModel, SceneDocumentModel


OBJECT_REFERENCE_MIME_TYPE = "application/x-platformator-object-reference"
OBJECT_ID_ROLE = int(Qt.ItemDataRole.UserRole)
ITEM_KIND_ROLE = OBJECT_ID_ROLE + 1
OBJECT_ITEM_KIND = "object"
COMPONENT_ITEM_KIND = "component"

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


class SceneTreeWidget(QTreeWidget):
    objectSelected = Signal(object)
    objectAddRequested = Signal(object)
    objectDeleteRequested = Signal(int)
    objectDuplicateRequested = Signal(int)
    componentRequested = Signal(int, str)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setHeaderLabels(["Scene Objects"])
        self.setDragEnabled(True)
        self.setDragDropMode(QAbstractItemView.DragDropMode.DragOnly)
        self.itemSelectionChanged.connect(self._emit_selection)
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.customContextMenuRequested.connect(self._show_context_menu)
        self._object_icon = self.style().standardIcon(QStyle.StandardPixmap.SP_DirClosedIcon)
        self._component_icon = self.style().standardIcon(QStyle.StandardPixmap.SP_FileIcon)

    def mimeData(self, items: list[QTreeWidgetItem]) -> QMimeData:
        mime_data = QMimeData()
        if not items:
            return mime_data

        if items[0].data(0, ITEM_KIND_ROLE) != OBJECT_ITEM_KIND:
            return mime_data

        object_id = items[0].data(0, OBJECT_ID_ROLE)
        if isinstance(object_id, int):
            encoded_id = str(object_id).encode("utf-8")
            mime_data.setData(OBJECT_REFERENCE_MIME_TYPE, encoded_id)
            mime_data.setText(str(object_id))
        return mime_data

    def mimeTypes(self) -> list[str]:
        return [OBJECT_REFERENCE_MIME_TYPE]

    def supportedDragActions(self) -> Qt.DropAction:
        return Qt.DropAction.CopyAction

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
            if item.data(0, ITEM_KIND_ROLE) == OBJECT_ITEM_KIND and item.data(0, OBJECT_ID_ROLE) == object_id:
                self.setCurrentItem(item)
                return

    def _build_item(self, game_object: GameObjectModel) -> QTreeWidgetItem:
        item = QTreeWidgetItem([f"{game_object.name} ({game_object.id})"])
        item.setData(0, OBJECT_ID_ROLE, game_object.id)
        item.setData(0, ITEM_KIND_ROLE, OBJECT_ITEM_KIND)
        item.setIcon(0, self._object_icon)

        for component in game_object.components:
            item.addChild(self._build_component_item(game_object.id, component))

        for child in game_object.children:
            item.addChild(self._build_item(child))
        return item

    def _build_component_item(self, object_id: int, component: BaseComponentModel) -> QTreeWidgetItem:
        item = QTreeWidgetItem([f"{type(component).__name__.removesuffix('Component')} ({component.id})"])
        item.setData(0, OBJECT_ID_ROLE, object_id)
        item.setData(0, ITEM_KIND_ROLE, COMPONENT_ITEM_KIND)
        item.setIcon(0, self._component_icon)
        return item

    def _emit_selection(self) -> None:
        current_item = self.currentItem()
        if current_item is None:
            self.objectSelected.emit(None)
            return

        object_id = current_item.data(0, OBJECT_ID_ROLE)
        if current_item.data(0, ITEM_KIND_ROLE) == COMPONENT_ITEM_KIND and isinstance(object_id, int):
            self.blockSignals(True)
            try:
                self.select_object(object_id)
            finally:
                self.blockSignals(False)

        self.objectSelected.emit(object_id)

    def _show_context_menu(self, position) -> None:
        item = self.itemAt(position)
        if item is None:
            menu = QMenu(self)
            add_object_action = menu.addAction("Add Object")
            add_object_action.triggered.connect(lambda _checked=False: self.objectAddRequested.emit(None))
            menu.exec(self.viewport().mapToGlobal(position))
            return

        object_id = item.data(0, OBJECT_ID_ROLE)
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
        for component_name in COMPONENT_ORDER:
            action = add_component_menu.addAction(component_name)
            action.triggered.connect(
                lambda _checked=False, object_id=object_id, component_name=component_name: self.componentRequested.emit(
                    object_id,
                    component_name,
                )
            )

        menu.exec(self.viewport().mapToGlobal(position))
