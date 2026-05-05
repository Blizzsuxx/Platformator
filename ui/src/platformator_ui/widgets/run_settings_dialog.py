from __future__ import annotations

from PySide6.QtWidgets import QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFormLayout, QSpinBox, QVBoxLayout

from platformator_ui.services.run_settings import RunWindowSettings


class RunSettingsDialog(QDialog):
    def __init__(self, run_window_settings: RunWindowSettings, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Project Settings")

        self.window_width_spin_box = QSpinBox(self)
        self.window_width_spin_box.setRange(1, 16384)
        self.window_width_spin_box.setValue(run_window_settings.window_width)

        self.window_height_spin_box = QSpinBox(self)
        self.window_height_spin_box.setRange(1, 16384)
        self.window_height_spin_box.setValue(run_window_settings.window_height)

        self.fullscreen_check_box = QCheckBox(self)
        self.fullscreen_check_box.setChecked(run_window_settings.fullscreen)

        self.maximized_check_box = QCheckBox(self)
        self.maximized_check_box.setChecked(run_window_settings.maximize_on_startup)

        self.keep_aspect_ratio_check_box = QCheckBox(self)
        self.keep_aspect_ratio_check_box.setChecked(run_window_settings.keep_aspect_ratio)

        self.build_preset_combo_box = QComboBox(self)
        self.build_preset_combo_box.addItem("Debug", "debug")
        self.build_preset_combo_box.addItem("Production", "production")
        selected_index = self.build_preset_combo_box.findData(run_window_settings.build_preset)
        if selected_index >= 0:
            self.build_preset_combo_box.setCurrentIndex(selected_index)

        self.debug_draw_colliders_check_box = QCheckBox(self)
        self.debug_draw_colliders_check_box.setChecked(run_window_settings.debug_draw_colliders)

        self.debug_draw_collision_points_check_box = QCheckBox(self)
        self.debug_draw_collision_points_check_box.setChecked(run_window_settings.debug_draw_collision_points)

        self.debug_draw_collision_normals_check_box = QCheckBox(self)
        self.debug_draw_collision_normals_check_box.setChecked(run_window_settings.debug_draw_collision_normals)

        self.debug_draw_grid_cells_check_box = QCheckBox(self)
        self.debug_draw_grid_cells_check_box.setChecked(run_window_settings.debug_draw_grid_cells)

        form_layout = QFormLayout()
        form_layout.addRow("Build Profile", self.build_preset_combo_box)
        form_layout.addRow("Window Width", self.window_width_spin_box)
        form_layout.addRow("Window Height", self.window_height_spin_box)
        form_layout.addRow("Fullscreen", self.fullscreen_check_box)
        form_layout.addRow("Maximize On Startup", self.maximized_check_box)
        form_layout.addRow("Keep Aspect Ratio", self.keep_aspect_ratio_check_box)
        form_layout.addRow("Draw Colliders", self.debug_draw_colliders_check_box)
        form_layout.addRow("Draw Collision Points", self.debug_draw_collision_points_check_box)
        form_layout.addRow("Draw Collision Normals", self.debug_draw_collision_normals_check_box)
        form_layout.addRow("Draw Grid Cells", self.debug_draw_grid_cells_check_box)

        button_box = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel, self)
        button_box.accepted.connect(self.accept)
        button_box.rejected.connect(self.reject)

        layout = QVBoxLayout(self)
        layout.addLayout(form_layout)
        layout.addWidget(button_box)

        self.build_preset_combo_box.currentIndexChanged.connect(self._update_debug_draw_controls)
        self._update_debug_draw_controls()

    def _selected_build_preset(self) -> str:
        build_preset = self.build_preset_combo_box.currentData()
        return build_preset if isinstance(build_preset, str) else "debug"

    def _update_debug_draw_controls(self) -> None:
        debug_controls_enabled = self._selected_build_preset() != "production"
        self.debug_draw_colliders_check_box.setEnabled(debug_controls_enabled)
        self.debug_draw_collision_points_check_box.setEnabled(debug_controls_enabled)
        self.debug_draw_collision_normals_check_box.setEnabled(debug_controls_enabled)
        self.debug_draw_grid_cells_check_box.setEnabled(debug_controls_enabled)

    def get_run_window_settings(self) -> RunWindowSettings:
        return RunWindowSettings(
            window_width=self.window_width_spin_box.value(),
            window_height=self.window_height_spin_box.value(),
            fullscreen=self.fullscreen_check_box.isChecked(),
            maximize_on_startup=self.maximized_check_box.isChecked(),
            keep_aspect_ratio=self.keep_aspect_ratio_check_box.isChecked(),
            build_preset=self._selected_build_preset(),
            debug_draw_colliders=self.debug_draw_colliders_check_box.isChecked(),
            debug_draw_collision_points=self.debug_draw_collision_points_check_box.isChecked(),
            debug_draw_collision_normals=self.debug_draw_collision_normals_check_box.isChecked(),
            debug_draw_grid_cells=self.debug_draw_grid_cells_check_box.isChecked(),
        )

    @classmethod
    def edit(cls, run_window_settings: RunWindowSettings, parent=None) -> RunWindowSettings | None:
        dialog = cls(run_window_settings, parent)
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return None
        return dialog.get_run_window_settings()