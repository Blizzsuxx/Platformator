from __future__ import annotations

from collections.abc import Sequence

from PySide6.QtCore import QMimeData, Qt, Signal
from PySide6.QtWidgets import QAbstractItemView, QMenu, QStyle, QTreeWidget, QTreeWidgetItem

from platformator_ui.scene.models import BaseComponentModel, GameObjectModel, SceneDocumentModel


OBJECT_REFERENCE_MIME_TYPE = "application/x-platformator-object-reference"
OBJECT_ID_ROLE = int(Qt.ItemDataRole.UserRole)
ITEM_KIND_ROLE = OBJECT_ID_ROLE + 1
COMPONENT_ID_ROLE = OBJECT_ID_ROLE + 2
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
    editorSelectionChanged = Signal(object, object)
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
        self._component_icons = {
            "CameraComponent": self.style().standardIcon(QStyle.StandardPixmap.SP_ComputerIcon),
            "RigidbodyComponent": self.style().standardIcon(QStyle.StandardPixmap.SP_BrowserReload),
            "BoxColliderComponent": self.style().standardIcon(QStyle.StandardPixmap.SP_TitleBarMaxButton),
            "CircleColliderComponent": self.style().standardIcon(QStyle.StandardPixmap.SP_DialogResetButton),
            "SpriteComponent": self.style().standardIcon(QStyle.StandardPixmap.SP_FileDialogContentsView),
            "AnimatorComponent": self.style().standardIcon(QStyle.StandardPixmap.SP_MediaPlay),
            "AudioComponent": self.style().standardIcon(QStyle.StandardPixmap.SP_MediaVolume),
            "ScriptComponentModel": self.style().standardIcon(QStyle.StandardPixmap.SP_FileDialogDetailedView),
            "UnknownComponentModel": self.style().standardIcon(QStyle.StandardPixmap.SP_FileIcon),
        }

    def mimeData(self, items: Sequence[QTreeWidgetItem]) -> QMimeData:
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

    def select_selection(self, object_id: int | None, component_id: int | None = None) -> None:
        if object_id is None:
            self.clearSelection()
            return

        for item in self.findItems("", Qt.MatchFlag.MatchContains | Qt.MatchFlag.MatchRecursive, 0):
            if item.data(0, OBJECT_ID_ROLE) != object_id:
                continue

            if component_id is None and item.data(0, ITEM_KIND_ROLE) == OBJECT_ITEM_KIND:
                self.setCurrentItem(item)
                return

            if component_id is not None and item.data(0, ITEM_KIND_ROLE) == COMPONENT_ITEM_KIND and item.data(0, COMPONENT_ID_ROLE) == component_id:
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
        item = QTreeWidgetItem([f"{component.component_label} ({component.id})"])
        item.setData(0, OBJECT_ID_ROLE, object_id)
        item.setData(0, ITEM_KIND_ROLE, COMPONENT_ITEM_KIND)
        item.setData(0, COMPONENT_ID_ROLE, component.id)
        item.setIcon(0, self._component_icon_for(component))
        return item

    def _emit_selection(self) -> None:
        current_item = self.currentItem()
        if current_item is None:
            self.editorSelectionChanged.emit(None, None)
            return

        object_id = current_item.data(0, OBJECT_ID_ROLE)
        component_id = current_item.data(0, COMPONENT_ID_ROLE) if current_item.data(0, ITEM_KIND_ROLE) == COMPONENT_ITEM_KIND else None
        self.editorSelectionChanged.emit(object_id, component_id)

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

    def _component_icon_for(self, component: BaseComponentModel):
        return self._component_icons.get(type(component).__name__, self.style().standardIcon(QStyle.StandardPixmap.SP_FileIcon))
