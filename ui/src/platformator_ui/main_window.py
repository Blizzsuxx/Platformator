from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QSettings, Qt
from PySide6.QtGui import QAction, QCloseEvent
from PySide6.QtWidgets import (
    QDockWidget,
    QFileDialog,
    QMainWindow,
    QMessageBox,
    QStatusBar,
    QToolBar,
)

from .engine import RunController
from .scene import (
    SceneIdAllocator,
    SceneSerializer,
    add_game_object,
    create_empty_scene,
    create_named_component,
    duplicate_game_object,
    find_parent_object,
    remove_game_object,
    validate_scene_document,
)
from .services.behavior_catalog import discover_behavior_templates
from .services.project_paths import ProjectPaths
from .services.recent_files import RecentFilesStore
from .services.undo_stack import EditorUndoStack
from .settings import APP_NAME, WINDOW_GEOMETRY_KEY, WINDOW_STATE_KEY
from .widgets import AssetBrowserWidget, BehaviorLibraryWidget, InspectorWidget, OutputPanel, SceneCanvasWidget, SceneTreeWidget


class MainWindow(QMainWindow):
    def __init__(self, project_paths: ProjectPaths, parent=None) -> None:
        super().__init__(parent)
        self.project_paths = project_paths
        self.recent_files = RecentFilesStore()
        self.undo_stack = EditorUndoStack(self)
        self.run_controller = RunController(project_paths, self)

        self.scene_document = create_empty_scene(include_default_camera=True)
        self.current_scene_path: Path | None = None
        self.current_object_id: int | None = None
        self.behavior_templates: dict[str, dict[str, object]] = {}
        self._dirty = False

        self._create_widgets()
        self._create_actions()
        self._create_menus_and_toolbars()
        self._connect_signals()
        self._restore_window_state()
        self._reload_scene_views(select_first=True)

    def new_scene(self) -> None:
        if not self._confirm_discard_changes():
            return

        self.scene_document = create_empty_scene(include_default_camera=True)
        self.current_scene_path = None
        self.current_object_id = None
        self._set_dirty(False)
        self.output_panel.append_text("\n[Editor] Created a new scene.\n")
        self._reload_scene_views(select_first=True)

    def open_scene(self, scene_path: Path | None = None) -> None:
        if not self._confirm_discard_changes():
            return

        chosen_path = scene_path or self._show_open_dialog()
        if chosen_path is None:
            return

        self.scene_document = SceneSerializer.load(chosen_path, repo_root=self.project_paths.repo_root)
        self.current_scene_path = chosen_path
        self.current_object_id = None
        self.recent_files.add_file(chosen_path)
        self._set_dirty(False)
        self.statusBar().showMessage(f"Opened {chosen_path}", 4000)
        self.output_panel.append_text(f"\n[Editor] Opened scene {chosen_path}.\n")
        self._reload_scene_views(select_first=True)

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
        self._reload_json_preview()
        return True

    def save_scene_as(self) -> bool:
        target_path = self._show_save_dialog()
        if target_path is None:
            return False
        self.current_scene_path = target_path
        return self.save_scene()

    def build_project(self) -> None:
        self.run_controller.build_only()

    def run_scene(self) -> None:
        if not self.save_scene():
            return
        if self.current_scene_path is None:
            return
        self.run_controller.run_scene(self.current_scene_path)

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
        self.inspector = InspectorWidget(self)
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
        self.open_action = QAction("Open Scene", self)
        self.save_action = QAction("Save", self)
        self.save_as_action = QAction("Save As", self)
        self.validate_action = QAction("Validate", self)
        self.build_action = QAction("Build", self)
        self.run_action = QAction("Build && Run", self)
        self.stop_action = QAction("Stop", self)
        self.stop_action.setEnabled(False)

    def _create_menus_and_toolbars(self) -> None:
        file_menu = self.menuBar().addMenu("File")
        file_menu.addAction(self.new_action)
        file_menu.addAction(self.open_action)
        file_menu.addSeparator()
        file_menu.addAction(self.save_action)
        file_menu.addAction(self.save_as_action)

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
        self.validate_action.triggered.connect(self.validate_scene)
        self.build_action.triggered.connect(self.build_project)
        self.run_action.triggered.connect(self.run_scene)
        self.stop_action.triggered.connect(self.run_controller.stop)

        self.scene_tree.objectSelected.connect(self._on_object_selected)
        self.scene_tree.objectAddRequested.connect(self._on_object_add_requested)
        self.scene_tree.objectDeleteRequested.connect(self._on_object_delete_requested)
        self.scene_tree.objectDuplicateRequested.connect(self._on_object_duplicate_requested)
        self.scene_tree.componentRequested.connect(self._on_component_requested)
        self.scene_canvas.objectSelected.connect(self._on_object_selected)
        self.scene_canvas.sceneChanged.connect(self._on_canvas_scene_changed)
        self.inspector.objectChanged.connect(self._on_scene_changed)
        self.asset_browser.assetActivated.connect(self._on_asset_activated)

        self.run_controller.outputReady.connect(self.output_panel.append_text)
        self.run_controller.statusChanged.connect(lambda text: self.statusBar().showMessage(text, 3000))
        self.run_controller.busyChanged.connect(self._on_run_busy_changed)

    def _reload_scene_views(self, *, select_first: bool = False) -> None:
        if select_first and self.scene_document.objects:
            self.current_object_id = self.scene_document.objects[0].id
        self._refresh_behavior_templates()
        self._refresh_scene_tree()
        self._sync_inspector()
        self._sync_scene_canvas(reset_view=True)
        self._update_window_title()

    def _sync_inspector(self) -> None:
        self.inspector.set_object(self.scene_document.find_object_by_id(self.current_object_id) if self.current_object_id is not None else None)

    def _sync_scene_canvas(self, *, reset_view: bool = False) -> None:
        self.scene_canvas.set_scene(self.scene_document, self.current_scene_path, reset_view=reset_view)
        self.scene_canvas.set_selected_object(self.current_object_id)

    def _on_object_selected(self, object_id: object) -> None:
        self.current_object_id = object_id if isinstance(object_id, int) else None
        if self.sender() is not self.scene_tree:
            self.scene_tree.blockSignals(True)
            try:
                self.scene_tree.select_object(self.current_object_id)
            finally:
                self.scene_tree.blockSignals(False)
        if self.sender() is not self.scene_canvas:
            self.scene_canvas.set_selected_object(self.current_object_id)
        self._sync_inspector()

    def _on_scene_changed(self) -> None:
        self._set_dirty(True)
        if self.sender() is not self.scene_tree:
            self._refresh_scene_tree()
        if self.sender() is not self.inspector:
            self._sync_inspector()
        if self.sender() is not self.scene_canvas:
            self._sync_scene_canvas(reset_view=False)
        self._update_window_title()

    def _on_canvas_scene_changed(self) -> None:
        self._set_dirty(True)
        self._sync_inspector()
        self._update_window_title()

    def _on_component_requested(self, object_id: int, component_name: str) -> None:
        self.current_object_id = object_id
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
        self.output_panel.append_text(f"\n[Editor] Added {component_name} to {current_object.name}.\n")
        self._on_scene_changed()

    def _on_object_add_requested(self, parent_id: object) -> None:
        typed_parent_id = parent_id if isinstance(parent_id, int) else None
        allocator = SceneIdAllocator.from_scene(self.scene_document)
        new_object = add_game_object(self.scene_document, allocator, parent_id=typed_parent_id)
        self.current_object_id = new_object.id

        if typed_parent_id is None:
            self.output_panel.append_text(f"\n[Editor] Added {new_object.name}.\n")
        else:
            parent = self.scene_document.find_object_by_id(typed_parent_id)
            parent_name = parent.name if parent is not None else "selected parent"
            self.output_panel.append_text(f"\n[Editor] Added {new_object.name} under {parent_name}.\n")

        self._on_scene_changed()

    def _on_object_duplicate_requested(self, object_id: int) -> None:
        allocator = SceneIdAllocator.from_scene(self.scene_document)
        duplicated_object = duplicate_game_object(self.scene_document, object_id, allocator)
        if duplicated_object is None:
            return

        self.current_object_id = duplicated_object.id
        self.output_panel.append_text(f"\n[Editor] Duplicated {duplicated_object.name}.\n")
        self._on_scene_changed()

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

        self.output_panel.append_text(f"\n[Editor] Deleted {removed_object.name}.\n")
        self._on_scene_changed()

    def _on_asset_activated(self, asset_path: str) -> None:
        current_object = self.scene_document.find_object_by_id(self.current_object_id) if self.current_object_id is not None else None
        if current_object is None:
            self.output_panel.append_text(f"\n[Assets] Selected {asset_path}, but no object is selected.\n")
            return

        suffix = Path(asset_path).suffix.lower()
        assigned = False

        if suffix in {".png", ".jpg", ".jpeg", ".webp"}:
            sprite = current_object.find_component_by_kind(create_named_component("sprite", SceneIdAllocator.from_scene(self.scene_document)).type)
            if sprite is not None and hasattr(sprite, "textureFilePath"):
                sprite.textureFilePath = asset_path
                assigned = True
        elif suffix in {".wav", ".ogg", ".mp3"}:
            audio = current_object.find_component_by_kind(create_named_component("audio", SceneIdAllocator.from_scene(self.scene_document)).type)
            if audio is not None and hasattr(audio, "filePath"):
                audio.filePath = asset_path
                assigned = True
        elif suffix == ".animset":
            animator = current_object.find_component_by_kind(create_named_component("animator", SceneIdAllocator.from_scene(self.scene_document)).type)
            if animator is not None and hasattr(animator, "animationClipFilePath"):
                animator.animationClipFilePath = asset_path
                assigned = True

        if assigned:
            self.output_panel.append_text(f"\n[Assets] Assigned {asset_path} to {current_object.name}.\n")
            self._on_scene_changed()
            return

        self.output_panel.append_text(
            f"\n[Assets] Selected {asset_path}, but no matching component was available on {current_object.name}.\n"
        )

    def _on_run_busy_changed(self, busy: bool) -> None:
        self.stop_action.setEnabled(busy)
        self.build_action.setEnabled(not busy)
        self.run_action.setEnabled(not busy)

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
            self.scene_tree.select_object(self.current_object_id)
        finally:
            self.scene_tree.blockSignals(False)

    def _refresh_behavior_templates(self) -> None:
        self.behavior_templates = discover_behavior_templates(self.project_paths.repo_root, self.scene_document)
        self.behavior_library.set_behavior_names(list(self.behavior_templates))
        self.inspector.set_behavior_templates(self.behavior_templates)

    def _update_window_title(self) -> None:
        scene_name = self.current_scene_path.name if self.current_scene_path is not None else "Untitled"
        dirty_marker = "*" if self._dirty else ""
        self.setWindowTitle(f"{APP_NAME} - {scene_name}{dirty_marker}")

    def _restore_window_state(self) -> None:
        settings = QSettings()
        geometry = settings.value(WINDOW_GEOMETRY_KEY)
        if geometry is not None:
            self.restoreGeometry(geometry)
        state = settings.value(WINDOW_STATE_KEY)
        if state is not None:
            self.restoreState(state)

    def _add_dock(self, title: str, widget, area: Qt.DockWidgetArea) -> None:
        dock = QDockWidget(title, self)
        dock.setObjectName(title)
        dock.setWidget(widget)
        self.addDockWidget(area, dock)