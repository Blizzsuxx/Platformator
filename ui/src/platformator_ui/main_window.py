from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QSettings, Qt
from PySide6.QtGui import QAction, QCloseEvent, QKeySequence
from PySide6.QtWidgets import (
    QApplication,
    QDockWidget,
    QFileDialog,
    QMainWindow,
    QMessageBox,
    QStatusBar,
    QToolBar,
)

from .engine import RunController
from .scene import (
    AnimatorComponent,
    AudioComponent,
    SceneIdAllocator,
    SceneSerializer,
    ScriptComponentModel,
    SpriteComponent,
    add_game_object,
    create_empty_scene,
    create_named_component,
    duplicate_game_object,
    find_parent_object,
    remove_game_object,
    validate_scene_document,
)
from .services.behavior_catalog import (
    discover_behavior_asset_fields,
    discover_behavior_object_reference_fields,
    discover_behavior_templates,
)
from .services.project_paths import ProjectPaths
from .services.recent_files import RecentFilesStore
from .services.run_settings import RunSettingsStore
from .services.undo_stack import EditorUndoStack, SceneSnapshotCommand
from .settings import APP_NAME, WINDOW_GEOMETRY_KEY, WINDOW_STATE_KEY
from .widgets import AssetBrowserWidget, BehaviorLibraryWidget, InspectorWidget, OutputPanel, RunSettingsDialog, SceneCanvasWidget, SceneTreeWidget


class MainWindow(QMainWindow):
    def __init__(self, project_paths: ProjectPaths, startup_scene_path: Path | None = None, restore_last_scene: bool = True, parent=None) -> None:
        super().__init__(parent)
        self.project_paths = project_paths
        self.recent_files = RecentFilesStore()
        self.run_settings_store = RunSettingsStore()
        self.run_window_settings = self.run_settings_store.load()
        self.undo_stack = EditorUndoStack(self)
        self.run_controller = RunController(project_paths, self)

        self.scene_document = create_empty_scene(include_default_camera=True)
        self.current_scene_path: Path | None = None
        self.current_object_id: int | None = None
        self.current_component_id: int | None = None
        self.behavior_templates: dict[str, dict[str, object]] = {}
        self.behavior_asset_fields = discover_behavior_asset_fields(project_paths.repo_root)
        self.behavior_object_reference_fields = discover_behavior_object_reference_fields(project_paths.repo_root)
        self._docks: dict[str, QDockWidget] = {}
        self._dock_areas: dict[str, Qt.DockWidgetArea] = {}
        self._undo_baseline_scene = self.scene_document.model_copy(deep=True)
        self._undo_baseline_selection = (self.current_object_id, self.current_component_id)
        self._restoring_scene_state = False
        self._dirty = False

        self._create_widgets()
        self._create_actions()
        self._create_menus_and_toolbars()
        self._connect_signals()
        self._restore_window_state()
        self._restore_startup_scene(startup_scene_path, restore_last_scene)
        self._reload_scene_views(select_first=True)
        self._reset_undo_history()

    def new_scene(self) -> None:
        if not self._confirm_discard_changes():
            return

        self.scene_document = create_empty_scene(include_default_camera=True)
        self.current_scene_path = None
        self.current_object_id = None
        self._set_dirty(False)
        self.output_panel.append_text("\n[Editor] Created a new scene.\n")
        self._reload_scene_views(select_first=True)
        self._reset_undo_history()

    def open_scene(self, scene_path: Path | None = None) -> None:
        if not self._confirm_discard_changes():
            return

        chosen_path = scene_path or self._show_open_dialog()
        if chosen_path is None:
            return

        self._load_scene(chosen_path)
        self.statusBar().showMessage(f"Opened {chosen_path}", 4000)
        self.output_panel.append_text(f"\n[Editor] Opened scene {chosen_path}.\n")
        self._reload_scene_views(select_first=True)
        self._reset_undo_history()

    def save_scene(self) -> bool:
        target_path = self.current_scene_path or self._show_save_dialog()
        if target_path is None:
            return False

        issues = validate_scene_document(self.scene_document)
        errors = [issue for issue in issues if issue.severity.value == "error"]
        warnings = [issue for issue in issues if issue.severity.value == "warning"]

        if warnings:
            self.output_panel.append_text("\n[Validation] Warnings before save:\n")
            for issue in warnings:
                self.output_panel.append_text(f"- {issue.location}: {issue.message}\n")

        if errors:
            self.output_panel.append_text("\n[Validation] Save aborted because of errors:\n")
            for issue in errors:
                self.output_panel.append_text(f"- {issue.location}: {issue.message}\n")
            QMessageBox.warning(self, APP_NAME, "Scene contains validation errors. See the output panel.")
            return False

        SceneSerializer.dump(self.scene_document, target_path, repo_root=self.project_paths.repo_root)
        self.current_scene_path = target_path
        self.recent_files.add_file(target_path)
        self._set_dirty(False)
        self.statusBar().showMessage(f"Saved {target_path}", 4000)
        self.output_panel.append_text(f"\n[Editor] Saved scene to {target_path}.\n")
        self._sync_scene_canvas(reset_view=False)
        self.undo_stack.setClean()
        self._sync_undo_baseline()
        return True

    def save_scene_as(self) -> bool:
        target_path = self._show_save_dialog()
        if target_path is None:
            return False
        self.current_scene_path = target_path
        return self.save_scene()

    def build_project(self) -> None:
        target_name, _program_path = self._resolve_run_target()
        self.run_controller.build_only(target_name=target_name)

    def run_scene(self) -> None:
        if not self.save_scene():
            return
        if self.current_scene_path is None:
            return
        target_name, program_path = self._resolve_run_target()
        self.run_controller.run_scene(
            self.current_scene_path,
            target_name=target_name,
            program_path=program_path,
            runtime_arguments=self.run_window_settings.to_cli_args(),
        )

    def edit_project_settings(self) -> None:
        updated_settings = RunSettingsDialog.edit(self.run_window_settings, self)
        if updated_settings is None:
            return

        self.run_window_settings = updated_settings
        self.run_settings_store.save(updated_settings)
        self.output_panel.append_text(f"\n[Settings] Updated project settings: {updated_settings.summary()}.\n")
        self.statusBar().showMessage("Updated project settings", 3000)

    def validate_scene(self) -> None:
        issues = validate_scene_document(self.scene_document)
        if not issues:
            self.output_panel.append_text("\n[Validation] Scene is valid.\n")
            self.statusBar().showMessage("Scene is valid", 3000)
            return

        self.output_panel.append_text("\n[Validation] Issues:\n")
        for issue in issues:
            self.output_panel.append_text(f"- [{issue.severity.value}] {issue.location}: {issue.message}\n")
        self.statusBar().showMessage(f"Validation produced {len(issues)} issue(s)", 4000)

    def closeEvent(self, event: QCloseEvent) -> None:
        if not self._confirm_discard_changes():
            event.ignore()
            return

        settings = QSettings()
        settings.setValue(WINDOW_GEOMETRY_KEY, self.saveGeometry())
        settings.setValue(WINDOW_STATE_KEY, self.saveState())
        super().closeEvent(event)

    def _create_widgets(self) -> None:
        self.setWindowTitle(APP_NAME)
        self.setStatusBar(QStatusBar(self))

        self.scene_canvas = SceneCanvasWidget(self.project_paths, self)
        self.setCentralWidget(self.scene_canvas)

        self.scene_tree = SceneTreeWidget(self)
        self.inspector = InspectorWidget(
            assets_dir=self.project_paths.assets_dir,
            repo_root=self.project_paths.repo_root,
            scene_document_provider=lambda: self.scene_document,
            scene_path_provider=lambda: self.current_scene_path,
            behavior_asset_fields=self.behavior_asset_fields,
            behavior_object_reference_fields=self.behavior_object_reference_fields,
            parent=self,
        )
        self.behavior_library = BehaviorLibraryWidget(self)
        self.asset_browser = AssetBrowserWidget(self.project_paths.assets_dir, self.project_paths.repo_root, self)
        self.output_panel = OutputPanel(self)

        self._add_dock("Hierarchy", self.scene_tree, Qt.DockWidgetArea.LeftDockWidgetArea)
        self._add_dock("Behaviors", self.behavior_library, Qt.DockWidgetArea.LeftDockWidgetArea)
        self._add_dock("Inspector", self.inspector, Qt.DockWidgetArea.RightDockWidgetArea)
        self._add_dock("Assets", self.asset_browser, Qt.DockWidgetArea.BottomDockWidgetArea)
        self._add_dock("Output", self.output_panel, Qt.DockWidgetArea.BottomDockWidgetArea)

    def _create_actions(self) -> None:
        self.new_action = QAction("New Scene", self)
        self.new_action.setShortcuts(QKeySequence.StandardKey.New)
        self.open_action = QAction("Open Scene", self)
        self.open_action.setShortcuts(QKeySequence.StandardKey.Open)
        self.save_action = QAction("Save", self)
        self.save_action.setShortcuts(QKeySequence.StandardKey.Save)
        self.save_as_action = QAction("Save As", self)
        self.save_as_action.setShortcuts(QKeySequence.StandardKey.SaveAs)
        self.undo_action = self.undo_stack.createUndoAction(self, "Undo")
        self.undo_action.setShortcuts(QKeySequence.StandardKey.Undo)
        self.redo_action = self.undo_stack.createRedoAction(self, "Redo")
        self.redo_action.setShortcuts(QKeySequence.StandardKey.Redo)
        self.validate_action = QAction("Validate", self)
        self.validate_action.setShortcut(QKeySequence("F6"))
        self.project_settings_action = QAction("Project Settings", self)
        self.project_settings_action.setShortcut(QKeySequence("Ctrl+,"))
        self.build_action = QAction("Build", self)
        self.build_action.setShortcut(QKeySequence("Ctrl+B"))
        self.run_action = QAction("Build && Run", self)
        self.run_action.setShortcut(QKeySequence("F5"))
        self.zoom_in_action = QAction("Zoom In", self)
        self.zoom_in_action.setShortcuts(QKeySequence.StandardKey.ZoomIn)
        self.zoom_out_action = QAction("Zoom Out", self)
        self.zoom_out_action.setShortcuts(QKeySequence.StandardKey.ZoomOut)
        self.frame_scene_action = QAction("Frame Scene", self)
        self.frame_scene_action.setShortcut(QKeySequence("F"))
        self.frame_selection_action = QAction("Frame Selection", self)
        self.frame_selection_action.setShortcut(QKeySequence("Shift+F"))
        self.reset_layout_action = QAction("Reset Panels", self)
        self.stop_action = QAction("Stop", self)
        self.stop_action.setShortcut(QKeySequence("Shift+F5"))
        self.stop_action.setEnabled(False)

    def _create_menus_and_toolbars(self) -> None:
        file_menu = self.menuBar().addMenu("File")
        file_menu.addAction(self.new_action)
        file_menu.addAction(self.open_action)
        file_menu.addSeparator()
        file_menu.addAction(self.save_action)
        file_menu.addAction(self.save_as_action)

        edit_menu = self.menuBar().addMenu("Edit")
        edit_menu.addAction(self.undo_action)
        edit_menu.addAction(self.redo_action)

        view_menu = self.menuBar().addMenu("View")
        view_menu.addAction(self.zoom_in_action)
        view_menu.addAction(self.zoom_out_action)
        view_menu.addSeparator()
        view_menu.addAction(self.frame_scene_action)
        view_menu.addAction(self.frame_selection_action)
        view_menu.addSeparator()
        view_menu.addAction(self.reset_layout_action)
        view_menu.addSeparator()
        for title in ("Hierarchy", "Behaviors", "Inspector", "Assets", "Output"):
            dock = self._docks.get(title)
            if dock is None:
                continue
            toggle_action = dock.toggleViewAction()
            toggle_action.setText(title)
            view_menu.addAction(toggle_action)

        settings_menu = self.menuBar().addMenu("Settings")
        settings_menu.addAction(self.project_settings_action)

        run_menu = self.menuBar().addMenu("Run")
        run_menu.addAction(self.validate_action)
        run_menu.addSeparator()
        run_menu.addAction(self.build_action)
        run_menu.addAction(self.run_action)
        run_menu.addAction(self.stop_action)

        toolbar = QToolBar("Main", self)
        toolbar.setObjectName("MainToolbar")
        toolbar.setMovable(False)
        toolbar.addAction(self.new_action)
        toolbar.addAction(self.open_action)
        toolbar.addAction(self.save_action)
        toolbar.addAction(self.undo_action)
        toolbar.addAction(self.redo_action)
        toolbar.addSeparator()
        toolbar.addAction(self.zoom_in_action)
        toolbar.addAction(self.zoom_out_action)
        toolbar.addAction(self.frame_scene_action)
        toolbar.addAction(self.frame_selection_action)
        toolbar.addSeparator()
        toolbar.addAction(self.validate_action)
        toolbar.addAction(self.build_action)
        toolbar.addAction(self.run_action)
        toolbar.addAction(self.stop_action)
        self.addToolBar(toolbar)

    def _connect_signals(self) -> None:
        self.new_action.triggered.connect(self.new_scene)
        self.open_action.triggered.connect(lambda: self.open_scene())
        self.save_action.triggered.connect(self.save_scene)
        self.save_as_action.triggered.connect(self.save_scene_as)
        self.zoom_in_action.triggered.connect(self.scene_canvas.zoom_in)
        self.zoom_out_action.triggered.connect(self.scene_canvas.zoom_out)
        self.frame_scene_action.triggered.connect(self.scene_canvas.frame_scene)
        self.frame_selection_action.triggered.connect(self.scene_canvas.frame_selection)
        self.reset_layout_action.triggered.connect(self._reset_dock_layout)
        self.validate_action.triggered.connect(self.validate_scene)
        self.project_settings_action.triggered.connect(self.edit_project_settings)
        self.build_action.triggered.connect(self.build_project)
        self.run_action.triggered.connect(self.run_scene)
        self.stop_action.triggered.connect(self.run_controller.stop)

        self.scene_tree.editorSelectionChanged.connect(self._on_selection_changed)
        self.scene_tree.objectAddRequested.connect(self._on_object_add_requested)
        self.scene_tree.objectDeleteRequested.connect(self._on_object_delete_requested)
        self.scene_tree.objectDuplicateRequested.connect(self._on_object_duplicate_requested)
        self.scene_tree.componentRequested.connect(self._on_component_requested)
        self.scene_canvas.editorSelectionChanged.connect(self._on_selection_changed)
        self.scene_canvas.sceneChanged.connect(self._on_canvas_scene_changed)
        self.scene_canvas.objectMoveFinished.connect(self._on_canvas_object_move_finished)
        self.inspector.objectChanged.connect(self._on_scene_changed)
        self.inspector.componentRequested.connect(self._on_component_requested)
        self.asset_browser.assetActivated.connect(self._on_asset_activated)

        self.run_controller.outputReady.connect(self.output_panel.append_text)
        self.run_controller.statusChanged.connect(lambda text: self.statusBar().showMessage(text, 3000))
        self.run_controller.busyChanged.connect(self._on_run_busy_changed)
        self.undo_stack.cleanChanged.connect(self._on_undo_clean_changed)

    def _reload_scene_views(self, *, select_first: bool = False) -> None:
        if select_first and self.scene_document.objects:
            self.current_object_id = self.scene_document.objects[0].id
            self.current_component_id = None
        elif select_first:
            self.current_object_id = None
            self.current_component_id = None

        self._coerce_selection()
        self._refresh_behavior_templates()
        self._refresh_scene_tree()
        self._sync_inspector()
        self._sync_scene_canvas(reset_view=True)
        self._update_window_title()

    def _sync_inspector(self) -> None:
        self.inspector.set_selection(
            self.scene_document.find_object_by_id(self.current_object_id) if self.current_object_id is not None else None,
            self.current_component_id,
        )

    def _sync_scene_canvas(self, *, reset_view: bool = False) -> None:
        self.scene_canvas.set_scene(self.scene_document, self.current_scene_path, reset_view=reset_view)
        self.scene_canvas.set_selected_item(self.current_object_id, self.current_component_id)

    def _on_selection_changed(self, object_id: object, component_id: object) -> None:
        self.current_object_id = object_id if isinstance(object_id, int) else None
        self.current_component_id = component_id if isinstance(component_id, int) else None
        self._coerce_selection()
        if self.sender() is not self.scene_tree:
            self.scene_tree.blockSignals(True)
            try:
                self.scene_tree.select_selection(self.current_object_id, self.current_component_id)
            finally:
                self.scene_tree.blockSignals(False)
        if self.sender() is not self.scene_canvas:
            self.scene_canvas.set_selected_item(self.current_object_id, self.current_component_id)
        self._sync_inspector()

    def _on_scene_changed(self) -> None:
        sender = self.sender()
        self._set_dirty(True)
        selection_changed = self._coerce_selection()
        self._refresh_scene_tree()
        if sender is not self.inspector or selection_changed:
            self._sync_inspector()
        self._sync_scene_canvas(reset_view=False)
        self._update_window_title()
        if sender is self.inspector and not self._restoring_scene_state:
            self._record_scene_snapshot("Edit Properties", merge_key=self._current_inspector_merge_key())

    def _on_canvas_scene_changed(self) -> None:
        self._set_dirty(True)
        self._coerce_selection()
        self._sync_inspector()
        self._update_window_title()

    def _on_canvas_object_move_finished(self, object_id: int) -> None:
        if self._restoring_scene_state:
            return
        self._record_scene_snapshot("Move Object", merge_key=f"move:{object_id}")

    def _on_component_requested(self, object_id: int, component_name: str) -> None:
        self.current_object_id = object_id
        self.current_component_id = None
        current_object = self.scene_document.find_object_by_id(object_id)
        if current_object is None:
            QMessageBox.information(self, APP_NAME, "Select a game object before adding a component.")
            return

        allocator = SceneIdAllocator.from_scene(self.scene_document)
        new_component = create_named_component(component_name, allocator)

        for existing_component in current_object.components:
            collider_conflict = component_name.lower() in {"boxcollider", "circlecollider"} and existing_component.type == new_component.type
            exact_conflict = existing_component.type == new_component.type and component_name.lower() not in {"boxcollider", "circlecollider"}
            if collider_conflict or exact_conflict:
                QMessageBox.warning(self, APP_NAME, f"{current_object.name} already has a {existing_component.component_label} component.")
                return

        current_object.components.append(new_component)
        self.current_component_id = new_component.id
        self.output_panel.append_text(f"\n[Editor] Added {component_name} to {current_object.name}.\n")
        self._on_scene_changed()
        self._record_scene_snapshot(f"Add {component_name}")

    def _on_object_add_requested(self, parent_id: object) -> None:
        typed_parent_id = parent_id if isinstance(parent_id, int) else None
        allocator = SceneIdAllocator.from_scene(self.scene_document)
        new_object = add_game_object(self.scene_document, allocator, parent_id=typed_parent_id)
        self.current_object_id = new_object.id
        self.current_component_id = None

        if typed_parent_id is None:
            self.output_panel.append_text(f"\n[Editor] Added {new_object.name}.\n")
        else:
            parent = self.scene_document.find_object_by_id(typed_parent_id)
            parent_name = parent.name if parent is not None else "selected parent"
            self.output_panel.append_text(f"\n[Editor] Added {new_object.name} under {parent_name}.\n")

        self._on_scene_changed()
        self._record_scene_snapshot("Add Object")

    def _on_object_duplicate_requested(self, object_id: int) -> None:
        allocator = SceneIdAllocator.from_scene(self.scene_document)
        duplicated_object = duplicate_game_object(self.scene_document, object_id, allocator)
        if duplicated_object is None:
            return

        self.current_object_id = duplicated_object.id
        self.current_component_id = None
        self.output_panel.append_text(f"\n[Editor] Duplicated {duplicated_object.name}.\n")
        self._on_scene_changed()
        self._record_scene_snapshot("Duplicate Object")

    def _on_object_delete_requested(self, object_id: int) -> None:
        parent = find_parent_object(self.scene_document, object_id)
        removed_object = remove_game_object(self.scene_document, object_id)
        if removed_object is None:
            return

        removed_ids = {removed_object.id, *(child.id for child in removed_object.iter_children_recursive())}
        if self.current_object_id in removed_ids:
            if parent is not None:
                self.current_object_id = parent.id
            elif self.scene_document.objects:
                self.current_object_id = self.scene_document.objects[0].id
            else:
                self.current_object_id = None
            self.current_component_id = None

        self.output_panel.append_text(f"\n[Editor] Deleted {removed_object.name}.\n")
        self._on_scene_changed()
        self._record_scene_snapshot("Delete Object")

    def _on_asset_activated(self, asset_path: str) -> None:
        current_object = self.scene_document.find_object_by_id(self.current_object_id) if self.current_object_id is not None else None
        if current_object is None:
            self.output_panel.append_text(f"\n[Assets] Selected {asset_path}, but no object is selected.\n")
            return

        suffix = Path(asset_path).suffix.lower()
        assigned = False

        if suffix in {".png", ".jpg", ".jpeg", ".webp"}:
            sprite = current_object.find_component_by_kind(create_named_component("sprite", SceneIdAllocator.from_scene(self.scene_document)).type)
            if isinstance(sprite, SpriteComponent):
                sprite.textureFilePath = asset_path
                assigned = True
        elif suffix in {".wav", ".ogg", ".mp3"}:
            audio = current_object.find_component_by_kind(create_named_component("audio", SceneIdAllocator.from_scene(self.scene_document)).type)
            if isinstance(audio, AudioComponent):
                audio.filePath = asset_path
                assigned = True
        elif suffix == ".animset":
            animator = current_object.find_component_by_kind(create_named_component("animator", SceneIdAllocator.from_scene(self.scene_document)).type)
            if isinstance(animator, AnimatorComponent):
                animator.animationClipFilePath = asset_path
                assigned = True

        if assigned:
            self.output_panel.append_text(f"\n[Assets] Assigned {asset_path} to {current_object.name}.\n")
            self._on_scene_changed()
            self._record_scene_snapshot("Assign Asset")
            return

        self.output_panel.append_text(
            f"\n[Assets] Selected {asset_path}, but no matching component was available on {current_object.name}.\n"
        )

    def _on_run_busy_changed(self, busy: bool) -> None:
        self.stop_action.setEnabled(busy)
        self.build_action.setEnabled(not busy)
        self.run_action.setEnabled(not busy)

    def _on_undo_clean_changed(self, is_clean: bool) -> None:
        self._set_dirty(not is_clean)

    def _show_open_dialog(self) -> Path | None:
        chosen_path, _ = QFileDialog.getOpenFileName(
            self,
            "Open Scene",
            str(self.project_paths.scenes_dir),
            "Scene Files (*.scene);;JSON Files (*.json);;All Files (*)",
        )
        return Path(chosen_path) if chosen_path else None

    def _show_save_dialog(self) -> Path | None:
        chosen_path, _ = QFileDialog.getSaveFileName(
            self,
            "Save Scene",
            str(self.current_scene_path or self.project_paths.scenes_dir / "untitled.scene"),
            "Scene Files (*.scene);;JSON Files (*.json);;All Files (*)",
        )
        if not chosen_path:
            return None

        path = Path(chosen_path)
        if path.suffix == "":
            path = path.with_suffix(".scene")
        return path

    def _confirm_discard_changes(self) -> bool:
        if not self._dirty:
            return True

        response = QMessageBox.question(
            self,
            APP_NAME,
            "The current scene has unsaved changes. Discard them?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        return response == QMessageBox.StandardButton.Yes

    def _set_dirty(self, dirty: bool) -> None:
        if self._dirty == dirty:
            return
        self._dirty = dirty
        self._update_window_title()

    def _refresh_scene_tree(self) -> None:
        self.scene_tree.blockSignals(True)
        try:
            self.scene_tree.set_scene(self.scene_document)
            self.scene_tree.select_selection(self.current_object_id, self.current_component_id)
        finally:
            self.scene_tree.blockSignals(False)

    def _refresh_behavior_templates(self) -> None:
        self.behavior_templates = discover_behavior_templates(self.project_paths.repo_root, self.scene_document)
        self.behavior_library.set_behavior_names(list(self.behavior_templates))
        self.inspector.set_behavior_templates(self.behavior_templates)

    def _current_inspector_merge_key(self) -> str | None:
        focus_widget = QApplication.focusWidget()
        if focus_widget is None or not self.inspector.isAncestorOf(focus_widget):
            return None

        class_name = focus_widget.metaObject().className()
        if class_name not in {"QLineEdit", "QSpinBox", "QDoubleSpinBox", "QComboBox"}:
            return None

        return f"inspector:{self.current_object_id}:{self.current_component_id}:{id(focus_widget)}"

    def _capture_scene_snapshot(self):
        return self.scene_document.model_copy(deep=True), (self.current_object_id, self.current_component_id)

    def _sync_undo_baseline(self) -> None:
        self._undo_baseline_scene, self._undo_baseline_selection = self._capture_scene_snapshot()

    def _reset_undo_history(self) -> None:
        self.undo_stack.clear()
        self.undo_stack.setClean()
        self._sync_undo_baseline()

    def _record_scene_snapshot(self, description: str, *, merge_key: str | None = None) -> None:
        if self._restoring_scene_state:
            return

        before_scene = self._undo_baseline_scene.model_copy(deep=True)
        before_selection = self._undo_baseline_selection
        after_scene, after_selection = self._capture_scene_snapshot()

        if before_scene.model_dump(mode="json") == after_scene.model_dump(mode="json") and before_selection == after_selection:
            self._sync_undo_baseline()
            return

        self.undo_stack.push(
            SceneSnapshotCommand(
                description,
                before_scene=before_scene,
                before_selection=before_selection,
                after_scene=after_scene.model_copy(deep=True),
                after_selection=after_selection,
                apply_state=self._apply_scene_snapshot,
                merge_key=merge_key,
            )
        )
        self._undo_baseline_scene = after_scene.model_copy(deep=True)
        self._undo_baseline_selection = after_selection

    def _apply_scene_snapshot(self, scene_snapshot, selection) -> None:
        self._restoring_scene_state = True
        try:
            self.scene_document = scene_snapshot.model_copy(deep=True)
            self.current_object_id, self.current_component_id = selection
            self._coerce_selection()
            self._refresh_behavior_templates()
            self._refresh_scene_tree()
            self._sync_inspector()
            self._sync_scene_canvas(reset_view=False)
            self._update_window_title()
            self._sync_undo_baseline()
        finally:
            self._restoring_scene_state = False

    def _update_window_title(self) -> None:
        scene_name = self.current_scene_path.name if self.current_scene_path is not None else "Untitled"
        dirty_marker = "*" if self._dirty else ""
        self.setWindowTitle(f"{APP_NAME} - {scene_name}{dirty_marker}")

    def _load_scene(self, scene_path: Path) -> None:
        self.scene_document = SceneSerializer.load(scene_path, repo_root=self.project_paths.repo_root)
        self.current_scene_path = scene_path
        self.current_object_id = None
        self.current_component_id = None
        self.recent_files.add_file(scene_path)
        self._set_dirty(False)

    def _restore_startup_scene(self, startup_scene_path: Path | None, restore_last_scene: bool) -> None:
        scene_path = startup_scene_path
        if scene_path is None and restore_last_scene:
            scene_path = self.recent_files.most_recent_file()

        if scene_path is not None:
            self._load_scene(scene_path)

    def _coerce_selection(self) -> bool:
        previous_selection = (self.current_object_id, self.current_component_id)

        if self.current_object_id is None:
            self.current_component_id = None
            return previous_selection != (self.current_object_id, self.current_component_id)

        game_object = self.scene_document.find_object_by_id(self.current_object_id)
        if game_object is None:
            self.current_object_id = None
            self.current_component_id = None
            return previous_selection != (self.current_object_id, self.current_component_id)

        if self.current_component_id is not None and game_object.find_component_by_id(self.current_component_id) is None:
            self.current_component_id = None

        return previous_selection != (self.current_object_id, self.current_component_id)

    def _resolve_run_target(self) -> tuple[str, Path]:
        if self._uses_mario_runtime():
            return self.project_paths.mario_example_binary.name, self.project_paths.mario_example_binary
        return self.project_paths.main_binary.name, self.project_paths.main_binary

    def _uses_mario_runtime(self) -> bool:
        mario_examples_dir = self.project_paths.repo_root / "examples" / "mario"
        if self.current_scene_path is not None:
            try:
                self.current_scene_path.resolve(strict=False).relative_to(mario_examples_dir.resolve(strict=False))
                return True
            except ValueError:
                pass

        for game_object in self.scene_document.iter_objects():
            for component in game_object.components:
                if not isinstance(component, ScriptComponentModel):
                    continue
                for behavior in component.behaviors:
                    if behavior.type.startswith("Mario"):
                        return True

        return False

    def _restore_window_state(self) -> None:
        settings = QSettings()
        geometry = settings.value(WINDOW_GEOMETRY_KEY)
        if geometry is not None:
            self.restoreGeometry(geometry)
        state = settings.value(WINDOW_STATE_KEY)
        if state is not None:
            self.restoreState(state)

    def _reset_dock_layout(self) -> None:
        for title, dock in self._docks.items():
            dock.setFloating(False)
            dock.show()
            self.addDockWidget(self._dock_areas[title], dock)

    def _add_dock(self, title: str, widget, area: Qt.DockWidgetArea) -> None:
        dock = QDockWidget(title, self)
        dock.setObjectName(title)
        dock.setWidget(widget)
        self._docks[title] = dock
        self._dock_areas[title] = area
        self.addDockWidget(area, dock)