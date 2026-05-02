from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QSettings

from platformator_ui.settings import MAX_RECENT_FILES, RECENT_FILES_KEY


class RecentFilesStore:
    def __init__(self) -> None:
        self._settings = QSettings()

    def list_files(self) -> list[Path]:
        values = self._settings.value(RECENT_FILES_KEY, []) or []
        if isinstance(values, str):
            values = [values]
        return [Path(value) for value in values]

    def add_file(self, path: Path) -> None:
        normalized = str(path.resolve())
        recent_files = [str(entry.resolve()) for entry in self.list_files() if entry.exists()]
        recent_files = [entry for entry in recent_files if entry != normalized]
        recent_files.insert(0, normalized)
        self._settings.setValue(RECENT_FILES_KEY, recent_files[:MAX_RECENT_FILES])
