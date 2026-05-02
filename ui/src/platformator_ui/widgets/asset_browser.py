from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Signal
from PySide6.QtWidgets import QFileSystemModel, QLabel, QTreeView, QVBoxLayout, QWidget

from platformator_ui.services.asset_paths import normalize_scene_editor_asset_path


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

        self._view = QTreeView(self)
        self._view.setModel(self._model)
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
