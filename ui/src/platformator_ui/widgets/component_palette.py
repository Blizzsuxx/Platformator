from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtWidgets import QPushButton, QVBoxLayout, QWidget


class ComponentPaletteWidget(QWidget):
    componentRequested = Signal(str)

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

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(6)

        for component_name in self.COMPONENT_ORDER:
            button = QPushButton(component_name, self)
            button.clicked.connect(lambda _, name=component_name: self.componentRequested.emit(name))
            layout.addWidget(button)

        layout.addStretch(1)
