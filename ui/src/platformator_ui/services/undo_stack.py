from __future__ import annotations

from typing import Callable

from PySide6.QtGui import QUndoCommand, QUndoStack


class SceneSnapshotCommand(QUndoCommand):
    def __init__(
        self,
        text: str,
        *,
        before_scene,
        before_selection: int | None,
        after_scene,
        after_selection: int | None,
        apply_state: Callable[[object, int | None], None],
        merge_key: str | None = None,
    ) -> None:
        super().__init__(text)
        self._before_scene = before_scene
        self._before_selection = before_selection
        self._after_scene = after_scene
        self._after_selection = after_selection
        self._apply_state = apply_state
        self._merge_key = merge_key
        self._skip_initial_redo = True

    def undo(self) -> None:
        self._apply_state(self._before_scene, self._before_selection)

    def redo(self) -> None:
        if self._skip_initial_redo:
            self._skip_initial_redo = False
            return
        self._apply_state(self._after_scene, self._after_selection)

    def id(self) -> int:
        if self._merge_key is None:
            return -1
        return 1

    def mergeWith(self, other: QUndoCommand) -> bool:
        if not isinstance(other, SceneSnapshotCommand):
            return False
        if self._merge_key is None or self._merge_key != other._merge_key:
            return False

        self._after_scene = other._after_scene
        self._after_selection = other._after_selection
        return True


class EditorUndoStack(QUndoStack):
    """Undo stack for scene snapshot and editor commands."""
