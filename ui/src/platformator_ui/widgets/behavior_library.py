from __future__ import annotations

from PySide6.QtCore import QMimeData, Qt
from PySide6.QtWidgets import QAbstractItemView, QListWidget, QListWidgetItem


BEHAVIOR_MIME_TYPE = "application/x-platformator-behavior"


class BehaviorLibraryWidget(QListWidget):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self.setDragEnabled(True)
        self.setDragDropMode(QAbstractItemView.DragDropMode.DragOnly)
        self.setDefaultDropAction(Qt.DropAction.CopyAction)
        self.setAlternatingRowColors(True)

    def set_behavior_names(self, behavior_names: list[str]) -> None:
        self.clear()
        if not behavior_names:
            placeholder = QListWidgetItem("No behaviors discovered")
            placeholder.setFlags(Qt.ItemFlag.NoItemFlags)
            self.addItem(placeholder)
            return

        for behavior_name in sorted(behavior_names, key=str.casefold):
            item = QListWidgetItem(behavior_name)
            item.setData(Qt.ItemDataRole.UserRole, behavior_name)
            self.addItem(item)

    def mimeTypes(self) -> list[str]:
        return [BEHAVIOR_MIME_TYPE]

    def mimeData(self, items: list[QListWidgetItem]) -> QMimeData:
        mime_data = QMimeData()
        if not items:
            return mime_data

        behavior_name = items[0].data(Qt.ItemDataRole.UserRole)
        if isinstance(behavior_name, str) and behavior_name:
            encoded_name = behavior_name.encode("utf-8")
            mime_data.setData(BEHAVIOR_MIME_TYPE, encoded_name)
            mime_data.setText(behavior_name)
        return mime_data

    def supportedDragActions(self) -> Qt.DropAction:
        return Qt.DropAction.CopyAction