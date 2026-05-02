from __future__ import annotations

from copy import deepcopy
import re
from enum import IntEnum
from typing import Any, Mapping, get_args

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMenu,
    QScrollArea,
    QSpinBox,
    QStyle,
    QToolButton,
    QVBoxLayout,
    QWidget,
)
from pydantic import BaseModel

from platformator_ui.scene.models import BaseComponentModel, GameObjectModel, RectModel, ScriptBehaviorModel, ScriptComponentModel, SpriteComponent
from platformator_ui.scene.mutations import add_behavior, remove_component

from .behavior_library import BEHAVIOR_MIME_TYPE


class BehaviorDropLabel(QLabel):
    behaviorDropped = Signal(str)

    def __init__(self, parent=None) -> None:
        super().__init__("Drop a behavior here", parent)
        self.setAcceptDrops(True)
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setMinimumHeight(40)
        self._apply_style(False)

    def dragEnterEvent(self, event) -> None:
        if event.mimeData().hasFormat(BEHAVIOR_MIME_TYPE):
            event.acceptProposedAction()
            self._apply_style(True)
            return
        event.ignore()

    def dragLeaveEvent(self, event) -> None:
        self._apply_style(False)
        super().dragLeaveEvent(event)

    def dropEvent(self, event) -> None:
        self._apply_style(False)
        if not event.mimeData().hasFormat(BEHAVIOR_MIME_TYPE):
            event.ignore()
            return

        behavior_name = bytes(event.mimeData().data(BEHAVIOR_MIME_TYPE)).decode("utf-8").strip()
        if not behavior_name:
            event.ignore()
            return

        self.behaviorDropped.emit(behavior_name)
        event.acceptProposedAction()

    def _apply_style(self, highlighted: bool) -> None:
        border_color = "#3e7cb1" if highlighted else "#6b7280"
        background = "rgba(62, 124, 177, 0.12)" if highlighted else "rgba(107, 114, 128, 0.08)"
        self.setStyleSheet(
            f"border: 1px dashed {border_color}; border-radius: 6px; padding: 8px; background: {background};"
        )


class InspectorWidget(QWidget):
    objectChanged = Signal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._current_object: GameObjectModel | None = None
        self._behavior_templates: dict[str, dict[str, Any]] = {}
        self._is_updating = False

        root_layout = QVBoxLayout(self)

        self.scroll_area = QScrollArea(self)
        self.scroll_area.setWidgetResizable(True)
        root_layout.addWidget(self.scroll_area)

        self.content_widget = QWidget(self)
        self.scroll_area.setWidget(self.content_widget)

        self.content_layout = QVBoxLayout(self.content_widget)
        self.content_layout.setContentsMargins(6, 6, 6, 6)
        self.content_layout.setSpacing(12)

        self.object_group = QGroupBox("Game Object", self.content_widget)
        self.object_form = QFormLayout(self.object_group)
        self.content_layout.addWidget(self.object_group)

        self.object_id_label = QLabel("-", self.object_group)
        self.object_form.addRow("Id", self.object_id_label)

        self.active_checkbox = QCheckBox(self.object_group)
        self.object_form.addRow("Active", self.active_checkbox)

        self.name_edit = QLineEdit(self.object_group)
        self.object_form.addRow("Name", self.name_edit)

        self.tag_edit = QLineEdit(self.object_group)
        self.object_form.addRow("Tag", self.tag_edit)

        self.position_x = self._create_double_spin_box()
        self.position_y = self._create_double_spin_box()
        self.object_form.addRow("Position X", self.position_x)
        self.object_form.addRow("Position Y", self.position_y)

        self.scale_x = self._create_double_spin_box(default=1.0)
        self.scale_y = self._create_double_spin_box(default=1.0)
        self.object_form.addRow("Scale X", self.scale_x)
        self.object_form.addRow("Scale Y", self.scale_y)

        self.rotation = self._create_double_spin_box()
        self.object_form.addRow("Rotation", self.rotation)

        self.components_group = QGroupBox("Components", self.content_widget)
        self.components_layout = QVBoxLayout(self.components_group)
        self.components_layout.setContentsMargins(6, 6, 6, 6)
        self.components_layout.setSpacing(10)
        self.content_layout.addWidget(self.components_group)
        self.content_layout.addStretch(1)

        self.active_checkbox.toggled.connect(self._apply_object_changes)
        self.name_edit.editingFinished.connect(self._apply_object_changes)
        self.tag_edit.editingFinished.connect(self._apply_object_changes)
        self.position_x.editingFinished.connect(self._apply_object_changes)
        self.position_y.editingFinished.connect(self._apply_object_changes)
        self.scale_x.editingFinished.connect(self._apply_object_changes)
        self.scale_y.editingFinished.connect(self._apply_object_changes)
        self.rotation.editingFinished.connect(self._apply_object_changes)

        self.set_object(None)

    def set_behavior_templates(self, behavior_templates: Mapping[str, dict[str, Any]]) -> None:
        self._behavior_templates = {
            behavior_name: deepcopy(defaults)
            for behavior_name, defaults in behavior_templates.items()
        }
        if self._current_object is not None:
            self.set_object(self._current_object)

    def set_object(self, game_object: GameObjectModel | None) -> None:
        self._current_object = game_object
        self._is_updating = True
        try:
            enabled = game_object is not None
            self.object_group.setEnabled(enabled)
            self.components_group.setEnabled(enabled)

            self._clear_layout(self.components_layout)

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
                self.components_group.setTitle("Components")
                self.components_layout.addWidget(QLabel("Select a game object to edit its components.", self.components_group))
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

            self.components_group.setTitle(f"Components ({len(game_object.components)})")
            if not game_object.components:
                self.components_layout.addWidget(QLabel("Selected object has no components.", self.components_group))
            else:
                for component in game_object.components:
                    self.components_layout.addWidget(self._build_component_group(component))
        finally:
            self._is_updating = False

    def _apply_object_changes(self) -> None:
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

    def _build_component_group(self, component: BaseComponentModel) -> QGroupBox:
        group = QGroupBox(self.components_group)
        layout = QVBoxLayout(group)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.setSpacing(8)

        header_layout = QHBoxLayout()
        header_layout.addWidget(self._create_section_label(f"{component.component_label} #{component.id}"))
        header_layout.addStretch(1)
        header_layout.addWidget(self._create_trash_button(
            "Remove component",
            lambda checked=False, component_id=component.id: self._remove_component(component_id),
        ))
        layout.addLayout(header_layout)

        form = QFormLayout()
        layout.addLayout(form)
        self._add_read_only_row(form, "Id", str(component.id))

        if isinstance(component, ScriptComponentModel):
            self._populate_script_component_group(layout, form, component)
            return group

        if isinstance(component, SpriteComponent):
            self._populate_sprite_component_form(form, component)
            return group

        skip_fields = {"id", "type"}
        if hasattr(component, "colliderType"):
            skip_fields.add("colliderType")
        self._populate_model_form(form, component, skip_fields=skip_fields)
        return group

    def _populate_script_component_group(
        self,
        layout: QVBoxLayout,
        form: QFormLayout,
        component: ScriptComponentModel,
    ) -> None:
        self._populate_model_form(form, component, skip_fields={"id", "type", "behaviors"})

        behaviors_group = QGroupBox("Behaviors", self.components_group)
        behaviors_layout = QVBoxLayout(behaviors_group)
        behaviors_layout.setContentsMargins(6, 6, 6, 6)
        behaviors_layout.setSpacing(8)

        controls_layout = QHBoxLayout()
        add_button = QToolButton(behaviors_group)
        add_button.setText("Add Behavior")
        add_button.setPopupMode(QToolButton.ToolButtonPopupMode.InstantPopup)
        add_button.setMenu(self._create_behavior_menu(component, add_button))
        controls_layout.addWidget(add_button)
        controls_layout.addStretch(1)
        behaviors_layout.addLayout(controls_layout)

        if not component.behaviors:
            behaviors_layout.addWidget(QLabel("Script component has no behaviors.", behaviors_group))
        else:
            for behavior in component.behaviors:
                behaviors_layout.addWidget(self._build_behavior_group(component, behavior))

        drop_area = BehaviorDropLabel(behaviors_group)
        drop_area.behaviorDropped.connect(
            lambda behavior_name, component=component: self._append_behavior(component, behavior_name)
        )
        behaviors_layout.addWidget(drop_area)

        layout.addWidget(behaviors_group)

    def _build_behavior_group(
        self,
        script_component: ScriptComponentModel,
        behavior: ScriptBehaviorModel,
    ) -> QGroupBox:
        group = QGroupBox(self.components_group)
        layout = QVBoxLayout(group)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.setSpacing(8)

        header_layout = QHBoxLayout()
        header_layout.addWidget(self._create_section_label(behavior.type or "Behavior"))
        header_layout.addStretch(1)
        header_layout.addWidget(self._create_trash_button(
            "Remove behavior",
            lambda checked=False, script_component=script_component, behavior=behavior: self._remove_behavior(script_component, behavior),
        ))
        layout.addLayout(header_layout)

        form = QFormLayout()
        layout.addLayout(form)
        self._populate_model_form(form, behavior, skip_fields={"type"})
        if form.rowCount() == 0:
            layout.addWidget(QLabel("No serialized fields.", group))
        return group

    def _populate_sprite_component_form(self, form: QFormLayout, component: SpriteComponent) -> None:
        self._populate_model_form(
            form,
            component,
            skip_fields={"id", "type", "sourceRect", "sourceRectEnabled"},
        )

        source_rect_model = component.sourceRect if component.sourceRect is not None else RectModel()
        source_rect_group = self._build_model_group("Source Rect", source_rect_model)
        source_rect_group.setEnabled(component.sourceRectEnabled)

        source_rect_enabled = QCheckBox(self.components_group)
        source_rect_enabled.setChecked(component.sourceRectEnabled)
        source_rect_enabled.toggled.connect(
            lambda checked, component=component, source_rect_model=source_rect_model, source_rect_group=source_rect_group: self._toggle_source_rect(
                component,
                source_rect_model,
                source_rect_group,
                checked,
            )
        )

        form.addRow("Source Rect Enabled", source_rect_enabled)
        form.addRow(source_rect_group)

    def _populate_model_form(self, form: QFormLayout, model: BaseModel, *, skip_fields: set[str] | None = None) -> None:
        excluded = skip_fields or set()
        field_names = list(model.__class__.model_fields.keys())
        extra_fields = sorted((model.model_extra or {}).keys())

        for field_name in [*field_names, *extra_fields]:
            if field_name in excluded:
                continue
            field_info = model.__class__.model_fields.get(field_name)
            annotation = field_info.annotation if field_info is not None else None
            self._add_value_editor(form, field_name, model, getattr(model, field_name), annotation)

    def _add_value_editor(
        self,
        form: QFormLayout,
        field_name: str,
        owner: BaseModel,
        value: Any,
        annotation: Any = None,
    ) -> None:
        label = self._humanize_label(field_name)

        if isinstance(value, bool):
            checkbox = QCheckBox(self.components_group)
            checkbox.setChecked(value)
            checkbox.toggled.connect(
                lambda checked, owner=owner, field_name=field_name: self._assign_value(owner, field_name, checked)
            )
            form.addRow(label, checkbox)
            return

        if isinstance(value, IntEnum):
            enum_type = type(value)
            combo_box = QComboBox(self.components_group)
            for enum_value in enum_type:
                combo_box.addItem(self._humanize_label(enum_value.name), enum_value)
            combo_box.setCurrentIndex(combo_box.findData(value))
            combo_box.currentIndexChanged.connect(
                lambda _index, combo_box=combo_box, owner=owner, field_name=field_name: self._assign_value(
                    owner,
                    field_name,
                    combo_box.currentData(),
                )
            )
            form.addRow(label, combo_box)
            return

        if isinstance(value, int):
            spin_box = self._create_int_spin_box(value)
            spin_box.editingFinished.connect(
                lambda owner=owner, field_name=field_name, spin_box=spin_box: self._assign_value(
                    owner,
                    field_name,
                    spin_box.value(),
                )
            )
            form.addRow(label, spin_box)
            return

        if isinstance(value, float):
            spin_box = self._create_double_spin_box(value)
            spin_box.editingFinished.connect(
                lambda owner=owner, field_name=field_name, spin_box=spin_box: self._assign_value(
                    owner,
                    field_name,
                    spin_box.value(),
                )
            )
            form.addRow(label, spin_box)
            return

        if isinstance(value, str):
            line_edit = QLineEdit(self.components_group)
            line_edit.setText(value)
            line_edit.editingFinished.connect(
                lambda owner=owner, field_name=field_name, line_edit=line_edit: self._assign_value(
                    owner,
                    field_name,
                    line_edit.text(),
                )
            )
            form.addRow(label, line_edit)
            return

        if isinstance(value, BaseModel):
            form.addRow(self._build_model_group(label, value))
            return

        if isinstance(value, list):
            form.addRow(self._build_list_group(label, value))
            return

        optional_model_type = self._resolve_optional_model_type(annotation)
        if value is None and optional_model_type is not None:
            form.addRow(self._build_optional_model_group(label, owner, field_name, optional_model_type))
            return

        self._add_read_only_row(form, label, "-" if value is None else str(value))

    def _build_model_group(self, title: str, model: BaseModel) -> QGroupBox:
        group = QGroupBox(title, self.components_group)
        form = QFormLayout(group)
        self._populate_model_form(form, model)
        return group

    def _build_list_group(self, title: str, values: list[Any]) -> QGroupBox:
        group = QGroupBox(title, self.components_group)
        layout = QVBoxLayout(group)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.setSpacing(8)

        if not values:
            layout.addWidget(QLabel("No entries.", group))
            return group

        item_label = title[:-1] if title.endswith("s") else title
        for index, value in enumerate(values, start=1):
            if isinstance(value, BaseModel):
                value_title = getattr(value, "type", None)
                display_title = f"{item_label} {index}"
                if isinstance(value_title, str) and value_title:
                    display_title = f"{display_title}: {value_title}"
                layout.addWidget(self._build_model_group(display_title, value))
                continue

            layout.addWidget(QLabel(f"{item_label} {index}: {value}", group))

        return group

    def _build_optional_model_group(
        self,
        title: str,
        owner: BaseModel,
        field_name: str,
        model_type: type[BaseModel],
    ) -> QGroupBox:
        current_value = getattr(owner, field_name)
        nested_model = current_value if current_value is not None else model_type()

        group = QGroupBox(title, self.components_group)
        layout = QVBoxLayout(group)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.setSpacing(8)

        enabled_checkbox = QCheckBox("Enabled", group)
        enabled_checkbox.setChecked(current_value is not None)
        layout.addWidget(enabled_checkbox)

        editor_group = self._build_model_group("Values", nested_model)
        editor_group.setEnabled(current_value is not None)
        layout.addWidget(editor_group)

        enabled_checkbox.toggled.connect(
            lambda checked, owner=owner, field_name=field_name, nested_model=nested_model, editor_group=editor_group: self._toggle_optional_model(
                owner,
                field_name,
                nested_model,
                editor_group,
                checked,
            )
        )

        return group

    def _create_behavior_menu(self, component: ScriptComponentModel, parent: QWidget) -> QMenu:
        menu = QMenu(parent)
        behavior_names = self._available_behavior_names(component)
        if not behavior_names:
            empty_action = menu.addAction("No behaviors discovered")
            empty_action.setEnabled(False)
            return menu

        for behavior_name in behavior_names:
            action = menu.addAction(behavior_name)
            action.triggered.connect(
                lambda checked=False, component=component, behavior_name=behavior_name: self._append_behavior(component, behavior_name)
            )
        return menu

    def _available_behavior_names(self, component: ScriptComponentModel) -> list[str]:
        behavior_names = set(self._behavior_templates)
        behavior_names.update(behavior.type for behavior in component.behaviors if behavior.type)
        return sorted(behavior_names, key=str.casefold)

    def _append_behavior(self, script_component: ScriptComponentModel, behavior_type: str) -> None:
        if self._is_updating or self._current_object is None:
            return

        add_behavior(
            script_component,
            behavior_type,
            initial_values=deepcopy(self._behavior_templates.get(behavior_type, {})),
        )
        self.set_object(self._current_object)
        self.objectChanged.emit()

    def _remove_behavior(self, script_component: ScriptComponentModel, behavior: ScriptBehaviorModel) -> None:
        if self._is_updating or self._current_object is None:
            return

        try:
            script_component.behaviors.remove(behavior)
        except ValueError:
            return

        self.set_object(self._current_object)
        self.objectChanged.emit()

    def _remove_component(self, component_id: int) -> None:
        if self._is_updating or self._current_object is None:
            return

        if remove_component(self._current_object, component_id) is None:
            return

        self.set_object(self._current_object)
        self.objectChanged.emit()

    def _toggle_optional_model(
        self,
        owner: BaseModel,
        field_name: str,
        nested_model: BaseModel,
        editor_group: QGroupBox,
        enabled: bool,
    ) -> None:
        if self._is_updating:
            return

        setattr(owner, field_name, nested_model if enabled else None)
        editor_group.setEnabled(enabled)
        self.objectChanged.emit()

    def _toggle_source_rect(
        self,
        component: SpriteComponent,
        source_rect_model: RectModel,
        source_rect_group: QGroupBox,
        enabled: bool,
    ) -> None:
        if self._is_updating:
            return

        component.sourceRectEnabled = enabled
        component.sourceRect = source_rect_model if enabled else None
        source_rect_group.setEnabled(enabled)
        self.objectChanged.emit()

    def _assign_value(self, owner: BaseModel, field_name: str, value: Any) -> None:
        if self._is_updating:
            return

        if isinstance(value, str):
            value = value.strip()
            if isinstance(owner, GameObjectModel) and field_name == "name":
                value = value or "Game Object"

        setattr(owner, field_name, value)
        self.objectChanged.emit()

    def _add_read_only_row(self, form: QFormLayout, label: str, value: str) -> None:
        form.addRow(label, QLabel(value, self.components_group))

    def _create_section_label(self, text: str) -> QLabel:
        label = QLabel(text, self.components_group)
        font = label.font()
        font.setBold(True)
        label.setFont(font)
        return label

    def _create_trash_button(self, tooltip: str, callback) -> QToolButton:
        button = QToolButton(self.components_group)
        button.setAutoRaise(True)
        button.setIcon(self.style().standardIcon(QStyle.StandardPixmap.SP_TrashIcon))
        button.setToolTip(tooltip)
        button.clicked.connect(callback)
        return button

    @staticmethod
    def _create_double_spin_box(default: float = 0.0) -> QDoubleSpinBox:
        spin_box = QDoubleSpinBox()
        spin_box.setRange(-1000000.0, 1000000.0)
        spin_box.setDecimals(4)
        spin_box.setValue(default)
        return spin_box

    @staticmethod
    def _create_int_spin_box(default: int = 0) -> QSpinBox:
        spin_box = QSpinBox()
        spin_box.setRange(-2147483648, 2147483647)
        spin_box.setValue(default)
        return spin_box

    @staticmethod
    def _resolve_optional_model_type(annotation: Any) -> type[BaseModel] | None:
        if isinstance(annotation, type) and issubclass(annotation, BaseModel):
            return annotation

        for candidate in get_args(annotation):
            if isinstance(candidate, type) and issubclass(candidate, BaseModel):
                return candidate
        return None

    @staticmethod
    def _humanize_label(field_name: str) -> str:
        spaced = re.sub(r"(?<!^)(?=[A-Z])", " ", field_name.replace("_", " "))
        return spaced.strip().title()

    @classmethod
    def _clear_layout(cls, layout) -> None:
        while layout.count():
            item = layout.takeAt(0)
            child_widget = item.widget()
            child_layout = item.layout()
            if child_widget is not None:
                child_widget.deleteLater()
            elif child_layout is not None:
                cls._clear_layout(child_layout)
