from __future__ import annotations

from PySide6.QtWidgets import QPlainTextEdit


class OutputPanel(QPlainTextEdit):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setReadOnly(True)
        self.setPlaceholderText("Build and run output appears here.")

    def append_text(self, text: str) -> None:
        self.moveCursor(self.textCursor().MoveOperation.End)
        self.insertPlainText(text)
        self.moveCursor(self.textCursor().MoveOperation.End)
