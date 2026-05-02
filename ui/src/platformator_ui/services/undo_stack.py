from __future__ import annotations

from PySide6.QtGui import QUndoStack


class EditorUndoStack(QUndoStack):
    """Thin wrapper kept for future editor commands and undo macros."""
