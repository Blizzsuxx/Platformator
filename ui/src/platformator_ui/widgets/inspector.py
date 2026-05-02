from __future__ import annotations

import json

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QDoubleSpinBox,
    QFormLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QPlainTextEdit,
    QVBoxLayout,
    QWidget,
)

from platformator_ui.scene.models import GameObjectModel


class InspectorWidget(QWidget):
    objectChanged = Signal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._current_object: GameObjectModel | None = None
        self._is_updating = False

        root_layout = QVBoxLayout(self)
        form_layout = QFormLayout()
        root_layout.addLayout(form_layout)

        self.object_id_label = QLabel("-", self)
        form_layout.addRow("Id", self.object_id_label)

        self.active_checkbox = QCheckBox(self)
        form_layout.addRow("Active", self.active_checkbox)

        self.name_edit = QLineEdit(self)
        form_layout.addRow("Name", self.name_edit)

        self.tag_edit = QLineEdit(self)
        form_layout.addRow("Tag", self.tag_edit)

        self.position_x = self._create_spin_box()
        self.position_y = self._create_spin_box()
        form_layout.addRow("Position X", self.position_x)
        form_layout.addRow("Position Y", self.position_y)

        self.scale_x = self._create_spin_box(default=1.0)
        self.scale_y = self._create_spin_box(default=1.0)
        form_layout.addRow("Scale X", self.scale_x)
        form_layout.addRow("Scale Y", self.scale_y)

        self.rotation = self._create_spin_box()
        form_layout.addRow("Rotation", self.rotation)

        self.components_list = QListWidget(self)
        root_layout.addWidget(QLabel("Components", self))
        root_layout.addWidget(self.components_list)

        self.component_preview = QPlainTextEdit(self)
        self.component_preview.setReadOnly(True)
        self.component_preview.setPlaceholderText("Selected component JSON appears here.")
        root_layout.addWidget(self.component_preview)

        self.active_checkbox.toggled.connect(self._apply_changes)
        self.name_edit.editingFinished.connect(self._apply_changes)
        self.tag_edit.editingFinished.connect(self._apply_changes)
        self.position_x.valueChanged.connect(self._apply_changes)
        self.position_y.valueChanged.connect(self._apply_changes)
        self.scale_x.valueChanged.connect(self._apply_changes)
        self.scale_y.valueChanged.connect(self._apply_changes)
        self.rotation.valueChanged.connect(self._apply_changes)
        self.components_list.currentRowChanged.connect(self._refresh_component_preview)

        self.set_object(None)

    def set_object(self, game_object: GameObjectModel | None) -> None:
        self._current_object = game_object
        self._is_updating = True
        try:
            enabled = game_object is not None
            for widget in (
                self.active_checkbox,
                self.name_edit,
                self.tag_edit,
                self.position_x,
                self.position_y,
                self.scale_x,
                self.scale_y,
                self.rotation,
                self.components_list,
            ):
                widget.setEnabled(enabled)

            self.components_list.clear()
            self.component_preview.clear()

            if game_object is None:
                self.object_id_label.setText("-")
                self.active_checkbox.setChecked(False)
                self.name_edit.setText("")
                self.tag_edit.setText("")
                self.position_x.setValue(0.0)
                self.position_y.setValue(0.0)
                self.scale_x.setValue(1.0)
                self.scale_y.setValue(1.0)
                self.rotation.setValue(0.0)
                return

            self.object_id_label.setText(str(game_object.id))
            self.active_checkbox.setChecked(game_object.active)
            self.name_edit.setText(game_object.name)
            self.tag_edit.setText(game_object.tag)
            self.position_x.setValue(game_object.position.x)
            self.position_y.setValue(game_object.position.y)
            self.scale_x.setValue(game_object.scale.x)
            self.scale_y.setValue(game_object.scale.y)
            self.rotation.setValue(game_object.rotation)

            for component in game_object.components:
                self.components_list.addItem(f"{component.component_label} #{component.id}")

            if game_object.components:
                self.components_list.setCurrentRow(0)
        finally:
            self._is_updating = False

    def _apply_changes(self) -> None:
        if self._is_updating or self._current_object is None:
            return

        self._current_object.active = self.active_checkbox.isChecked()
        self._current_object.name = self.name_edit.text().strip() or "Game Object"
        self._current_object.tag = self.tag_edit.text().strip()
        self._current_object.position.x = self.position_x.value()
        self._current_object.position.y = self.position_y.value()
        self._current_object.scale.x = self.scale_x.value()
        self._current_object.scale.y = self.scale_y.value()
        self._current_object.rotation = self.rotation.value()
        self.objectChanged.emit()

    def _refresh_component_preview(self, row: int) -> None:
        if self._current_object is None or row < 0 or row >= len(self._current_object.components):
            self.component_preview.clear()
            return

        component = self._current_object.components[row]
        self.component_preview.setPlainText(
            json.dumps(component.model_dump(mode="json", exclude_none=False), indent=2)
        )

    @staticmethod
    def _create_spin_box(default: float = 0.0) -> QDoubleSpinBox:
        spin_box = QDoubleSpinBox()
        spin_box.setRange(-100000.0, 100000.0)
        spin_box.setDecimals(3)
        spin_box.setValue(default)
        return spin_box
