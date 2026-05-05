import os
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtGui import QKeySequence
from PySide6.QtWidgets import QApplication

from platformator_ui.main_window import MainWindow
from platformator_ui.services.project_paths import ProjectPaths


def _has_standard_shortcut(action, standard_key: QKeySequence.StandardKey) -> bool:
    expected = QKeySequence(standard_key)
    return any(sequence.matches(expected) == QKeySequence.SequenceMatch.ExactMatch for sequence in action.shortcuts())


def test_main_window_assigns_core_and_editor_shortcuts() -> None:
    app = QApplication.instance() or QApplication([])
    project_paths = ProjectPaths.discover(Path(__file__).resolve())
    window = MainWindow(project_paths, restore_last_scene=False)

    assert _has_standard_shortcut(window.save_action, QKeySequence.StandardKey.Save)
    assert _has_standard_shortcut(window.undo_action, QKeySequence.StandardKey.Undo)
    assert _has_standard_shortcut(window.redo_action, QKeySequence.StandardKey.Redo)
    assert _has_standard_shortcut(window.open_action, QKeySequence.StandardKey.Open)
    assert _has_standard_shortcut(window.new_action, QKeySequence.StandardKey.New)

    assert window.run_action.shortcut().toString(QKeySequence.SequenceFormat.PortableText) == "F5"
    assert window.stop_action.shortcut().toString(QKeySequence.SequenceFormat.PortableText) == "Shift+F5"
    assert window.build_action.shortcut().toString(QKeySequence.SequenceFormat.PortableText) == "Ctrl+B"
    assert window.frame_scene_action.shortcut().toString(QKeySequence.SequenceFormat.PortableText) == "F"
    assert window.frame_selection_action.shortcut().toString(QKeySequence.SequenceFormat.PortableText) == "Shift+F"
    assert window.project_settings_action.shortcut().toString(QKeySequence.SequenceFormat.PortableText) == "Ctrl+,"

    window.close()