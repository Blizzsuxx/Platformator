from __future__ import annotations

from collections import deque
from pathlib import Path

from PySide6.QtCore import QObject, QProcess, Signal

from platformator_ui.services.project_paths import ProjectPaths

from .cmake_runner import ProcessSpec, create_build_spec, create_configure_spec, create_run_pipeline
from .process_console import format_step_banner


class RunController(QObject):
    outputReady = Signal(str)
    statusChanged = Signal(str)
    busyChanged = Signal(bool)
    finished = Signal(int)

    def __init__(self, project_paths: ProjectPaths, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._project_paths = project_paths
        self._process = QProcess(self)
        self._process.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        self._process.readyReadStandardOutput.connect(self._drain_output)
        self._process.finished.connect(self._handle_finished)
        self._process.errorOccurred.connect(self._handle_error)
        self._queued_steps: deque[ProcessSpec] = deque()
        self._current_step: ProcessSpec | None = None
        self._busy = False

    def is_busy(self) -> bool:
        return self._busy

    def build_only(self, preset: str = "debug", *, target_name: str | None = None) -> None:
        self._start_pipeline(
            [
                create_configure_spec(self._project_paths, preset=preset),
                create_build_spec(self._project_paths, preset=preset, target_name=target_name),
            ]
        )

    def run_scene(
        self,
        scene_path: Path,
        preset: str = "debug",
        *,
        target_name: str | None = None,
        program_path: Path | None = None,
        runtime_arguments: tuple[str, ...] = (),
    ) -> None:
        self._start_pipeline(
            create_run_pipeline(
                self._project_paths,
                scene_path=scene_path,
                preset=preset,
                target_name=target_name,
                program_path=program_path,
                runtime_arguments=runtime_arguments,
            )
        )

    def stop(self) -> None:
        self._queued_steps.clear()
        if self._process.state() != QProcess.ProcessState.NotRunning:
            self.statusChanged.emit("Stopping active process.")
            self._process.kill()
        else:
            self._set_busy(False)

    def _start_pipeline(self, steps: list[ProcessSpec]) -> None:
        if self.is_busy():
            self.outputReady.emit("\n[RunController] A build or run is already in progress.\n")
            return

        self._queued_steps = deque(steps)
        self._set_busy(True)
        self._start_next_step()

    def _start_next_step(self) -> None:
        if not self._queued_steps:
            self._current_step = None
            self.statusChanged.emit("Idle")
            self._set_busy(False)
            self.finished.emit(0)
            return

        self._current_step = self._queued_steps.popleft()
        self.statusChanged.emit(f"{self._current_step.label} in progress")
        self.outputReady.emit(format_step_banner(self._current_step))
        self._process.setWorkingDirectory(str(self._current_step.working_directory))
        self._process.start(self._current_step.program, list(self._current_step.arguments))

    def _drain_output(self) -> None:
        payload = self._process.readAllStandardOutput().data().decode("utf-8", errors="replace")
        if payload:
            self.outputReady.emit(payload)

    def _handle_finished(self, exit_code: int, exit_status: QProcess.ExitStatus) -> None:
        self._drain_output()
        if exit_status != QProcess.ExitStatus.NormalExit or exit_code != 0:
            label = self._current_step.label if self._current_step is not None else "Process"
            self.outputReady.emit(f"\n[RunController] {label} failed with exit code {exit_code}.\n")
            self._queued_steps.clear()
            self._set_busy(False)
            self.finished.emit(exit_code or 1)
            return

        self._start_next_step()

    def _handle_error(self, error: QProcess.ProcessError) -> None:
        label = self._current_step.label if self._current_step is not None else "Process"
        self.outputReady.emit(f"\n[RunController] {label} could not start: {error!s}.\n")
        self._queued_steps.clear()
        self._set_busy(False)
        self.finished.emit(1)

    def _set_busy(self, busy: bool) -> None:
        if self._busy == busy:
            return
        self._busy = busy
        self.busyChanged.emit(busy)
