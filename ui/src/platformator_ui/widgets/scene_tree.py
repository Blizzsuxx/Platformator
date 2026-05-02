from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import QTreeWidget, QTreeWidgetItem

from platformator_ui.scene.models import GameObjectModel, SceneDocumentModel


class SceneTreeWidget(QTreeWidget):
    objectSelected = Signal(object)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setHeaderLabels(["Scene Objects"])
        self.itemSelectionChanged.connect(self._emit_selection)

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
