from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QMimeData, Qt, QUrl, Signal
from PySide6.QtGui import QDrag
from PySide6.QtWidgets import QAbstractItemView, QFileSystemModel, QLabel, QTreeView, QVBoxLayout, QWidget

from platformator_ui.services.asset_paths import normalize_scene_editor_asset_path


ASSET_REFERENCE_MIME_TYPE = "application/x-platformator-asset-reference"


class AssetBrowserTreeView(QTreeView):
    def __init__(self, file_model: QFileSystemModel, repo_root: Path, parent=None) -> None:
        super().__init__(parent)
        self._model = file_model
        self._repo_root = repo_root
        self.setModel(file_model)
        self.setDragEnabled(True)
        self.setDragDropMode(QAbstractItemView.DragDropMode.DragOnly)

    def startDrag(self, supported_actions) -> None:
        index = self.currentIndex()
        if not index.isValid():
            return

        absolute_path = Path(self._model.filePath(index))
        if absolute_path.is_dir():
            return

        try:
            repo_relative = absolute_path.relative_to(self._repo_root).as_posix()
        except ValueError:
            return

        normalized = normalize_scene_editor_asset_path(repo_relative, repo_root=self._repo_root)
        if not normalized:
            return

        mime_data = QMimeData()
        mime_data.setData(ASSET_REFERENCE_MIME_TYPE, normalized.encode("utf-8"))
        mime_data.setText(normalized)
        mime_data.setUrls([QUrl.fromLocalFile(str(absolute_path))])

        drag = QDrag(self)
        drag.setMimeData(mime_data)
        drag.exec(Qt.DropAction.CopyAction)


class AssetBrowserWidget(QWidget):
    assetActivated = Signal(str)

    def __init__(self, assets_dir: Path, repo_root: Path, parent=None) -> None:
        super().__init__(parent)
        self._assets_dir = assets_dir
        self._repo_root = repo_root

        layout = QVBoxLayout(self)
        layout.addWidget(QLabel(f"Assets: {assets_dir}", self))

        self._model = QFileSystemModel(self)
        self._model.setRootPath(str(assets_dir))

        self._view = AssetBrowserTreeView(self._model, self._repo_root, self)
        self._view.setRootIndex(self._model.index(str(assets_dir)))
        self._view.doubleClicked.connect(self._on_double_clicked)
        layout.addWidget(self._view)

    def _on_double_clicked(self, index) -> None:
        absolute_path = Path(self._model.filePath(index))
        if absolute_path.is_dir():
            return

        try:
            repo_relative = absolute_path.relative_to(self._repo_root).as_posix()
        except ValueError:
            return

        normalized = normalize_scene_editor_asset_path(repo_relative, repo_root=self._repo_root)
        if normalized:
            self.assetActivated.emit(normalized)
