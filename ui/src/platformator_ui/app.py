from __future__ import annotations

import sys
from pathlib import Path

from PySide6.QtCore import QCoreApplication
from PySide6.QtWidgets import QApplication

from .main_window import MainWindow
from .services.project_paths import ProjectPaths
from .settings import APP_NAME, ORGANIZATION_DOMAIN, ORGANIZATION_NAME


def create_application(argv: list[str] | None = None) -> QApplication:
    app = QApplication(argv or sys.argv)
    QCoreApplication.setApplicationName(APP_NAME)
    QCoreApplication.setOrganizationName(ORGANIZATION_NAME)
    QCoreApplication.setOrganizationDomain(ORGANIZATION_DOMAIN)
    app.setStyle("Fusion")
    return app


def launch_editor(scene_path: Path | None = None) -> int:
    app = create_application()
    project_paths = ProjectPaths.discover(Path(__file__).resolve())
    window = MainWindow(project_paths=project_paths)
    window.show()

    if scene_path is not None:
        window.open_scene(scene_path)

    return app.exec()
