#!/usr/bin/env python3
"""Run the compact editor resource and timing pilot under one persistent budget."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import signal
import subprocess
import sys
import tempfile
import time
from typing import Any

from capture_budget import BudgetExceeded, CaptureBudget


SCHEMA_VERSION = 1
DEFAULT_MAX_SECONDS = 600.0
DEFAULT_MAX_OUTPUT_MIB = 50.0
RESOURCE_CYCLES = 20
REPETITIONS = 3
WARMUP_FRAMES = 5
MEASURED_FRAMES = 30
WORKLOAD = {
    "interaction": "selection plus one position.x property edit followed by undo",
    "populations": {"tiny": 10, "objects_1000": 1000, "objects_10000": 10000},
    "deep_parent": {"name": "deep_parent_128", "depth": 128},
    "texture_reference": "assets/textures/pilot.png",
    "resource_cycles": RESOURCE_CYCLES,
    "timing_repetitions": REPETITIONS,
    "warmup_frames": WARMUP_FRAMES,
    "measured_frames": MEASURED_FRAMES,
    "long_output_bytes": 1_200_000,
    "project_scan_files": 32,
}
SOURCE_PATHS = (
    "CMakeLists.txt", "CMakePresets.json", "editor/CMakeLists.txt", "editor/src",
    "editor/benchmark/editor_probe.cpp", "src", "include", "vendored/imgui-1.92.9b",
)
HARNESS_PATHS = (
    "benchmark/capture_editor.py", "benchmark/capture_budget.py",
    "benchmark/capture_linear_render.py", "editor/benchmark/editor_probe.cpp",
    "editor/CMakeLists.txt", "editor/tests/CMakeLists.txt",
)
RENDER_ARGUMENTS = (
    "render_baseline_probe", "render_candidate_probe", "render_baseline_runner",
    "render_candidate_runner", "render_baseline_validation_probe",
    "render_candidate_validation_probe", "render_multiworker_count",
)


class CaptureError(RuntimeError):
    pass


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _hash_paths(root: Path, paths: tuple[str, ...]) -> str:
    digest = hashlib.sha256()
    files: list[Path] = []
    for name in paths:
        path = root / name
        if path.is_file():
            files.append(path)
        elif path.is_dir():
            files.extend(candidate for candidate in path.rglob("*")
                         if candidate.is_file() and not candidate.is_symlink() and
                         candidate.suffix not in (".pyc", ".o", ".a", ".so"))
        else:
            raise CaptureError(f"Identity input is missing: {path}")
    for path in sorted(set(files), key=lambda item: item.relative_to(root).as_posix()):
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        digest.update(b"\0")
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        digest.update(b"\0")
    return digest.hexdigest()


def _workload_identity() -> str:
    return hashlib.sha256(json.dumps(WORKLOAD, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def compute_identity(root: Path, probe: Path, build_label: str) -> dict[str, Any]:
    resolved_probe = probe.resolve(strict=True)
    if not resolved_probe.is_file() or not os.access(resolved_probe, os.X_OK):
        raise CaptureError(f"Editor probe is not an executable file: {resolved_probe}")
    cpu_model = "unknown"
    try:
        for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
            if line.lower().startswith("model name"):
                cpu_model = line.split(":", 1)[1].strip()
                break
    except OSError:
        pass
    affinity = sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None
    return {
        "source_sha256": _hash_paths(root, SOURCE_PATHS),
        "harness_sha256": _hash_paths(root, HARNESS_PATHS),
        "probe_binary_sha256": _sha256_file(resolved_probe),
        "probe_path": str(resolved_probe),
        "build_label": build_label,
        "host": {"system": platform.system(), "release": platform.release(),
                 "machine": platform.machine(), "cpu_model": cpu_model,
                 "logical_cpus": os.cpu_count(), "cpu_affinity": affinity},
        "workload_sha256": _workload_identity(),
        "workload": WORKLOAD,
    }


def _atomic_write_json(path: Path, value: dict[str, Any]) -> None:
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, sort_keys=True, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def _load_session(path: Path) -> dict[str, Any]:
    try:
        session = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CaptureError(f"Session journal is unreadable or malformed: {path}") from error
    validate_session(session)
    return session


def validate_session(session: Any) -> None:
    if not isinstance(session, dict) or session.get("kind") != "platformator.editor-pilot-session" or \
            session.get("schema_version") != SCHEMA_VERSION:
        raise CaptureError("Session journal kind or schema is unsupported")
    if session.get("status") not in ("active", "failed", "complete"):
        raise CaptureError("Session journal status is invalid")
    identity = session.get("identity")
    if not isinstance(identity, dict) or any(not isinstance(identity.get(key), str) or not identity[key]
                                             for key in ("source_sha256", "harness_sha256", "probe_binary_sha256",
                                                         "probe_path", "build_label", "workload_sha256")):
        raise CaptureError("Session journal immutable identity is incomplete")
    budget = session.get("budget")
    if not isinstance(budget, dict):
        raise CaptureError("Session journal budget is missing")
    for key in ("max_output_bytes", "max_seconds", "started_monotonic", "deadline_monotonic",
                "started_epoch", "deadline_epoch"):
        value = budget.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise CaptureError(f"Session journal budget field {key} is invalid")
    if budget["max_output_bytes"] <= 0 or budget["max_seconds"] <= 0 or \
            budget["deadline_monotonic"] <= budget["started_monotonic"] or \
            budget["deadline_epoch"] <= budget["started_epoch"]:
        raise CaptureError("Session journal budget limits are invalid")
    if not isinstance(session.get("capture_root"), str) or not session["capture_root"]:
        raise CaptureError("Session journal capture root is missing")
    phases = session.get("phases")
    if not isinstance(phases, dict) or set(phases) != {"resources", "timing"}:
        raise CaptureError("Session journal phase inventory is invalid")
    allowed = {"pending", "running", "complete", "failed"}
    for phase in phases.values():
        if not isinstance(phase, dict) or phase.get("status") not in allowed:
            raise CaptureError("Session journal phase state is invalid")
    commands = session.get("commands")
    if not isinstance(commands, list) or any(not isinstance(item, dict) for item in commands):
        raise CaptureError("Session journal command inventory is invalid")


def _new_session(path: Path, capture_root: Path, identity: dict[str, Any], max_output_mib: float,
                 max_seconds: float) -> dict[str, Any]:
    now_mono = time.monotonic()
    now_epoch = time.time()
    session = {
        "kind": "platformator.editor-pilot-session",
        "schema_version": SCHEMA_VERSION,
        "status": "active",
        "identity": identity,
        "capture_root": str(capture_root.resolve()),
        "budget": {
            "max_output_bytes": int(max_output_mib * 1024 * 1024),
            "max_output_mib": max_output_mib,
            "max_seconds": max_seconds,
            "started_monotonic": now_mono,
            "deadline_monotonic": now_mono + max_seconds,
            "started_epoch": now_epoch,
            "deadline_epoch": now_epoch + max_seconds,
        },
        "accounting": {"output_bytes": 0, "elapsed_seconds": 0.0},
        "phases": {"resources": {"status": "pending"}, "timing": {"status": "pending"}},
        "commands": [],
    }
    validate_session(session)
    _atomic_write_json(path, session)
    return session


def _make_budget(session_path: Path, session: dict[str, Any]) -> CaptureBudget:
    capture_root = Path(session["capture_root"])
    roots = [capture_root, session_path]
    configured = session["budget"]
    budget = CaptureBudget(roots, configured["max_output_mib"], configured["max_seconds"])
    remaining_by_mono = configured["deadline_monotonic"]
    remaining_by_epoch = time.monotonic() + max(0.0, configured["deadline_epoch"] - time.time())
    budget.deadline = min(remaining_by_mono, remaining_by_epoch)
    return budget


def validate_identity(expected: dict[str, Any], current: dict[str, Any]) -> None:
    if expected != current:
        raise CaptureError("Source, harness, workload, build, or probe identity changed after the resource phase")


def _baseline_identity_values(path: Path) -> dict[str, str]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise CaptureError(f"Frozen baseline identity is unavailable: {path}") from error
    values = {}
    for line in lines:
        if not line.startswith("|"):
            continue
        cells = [cell.strip().replace("`", "") for cell in line.strip().strip("|").split("|")]
        if len(cells) == 2:
            values[cells[0]] = cells[1]
    return values


def validate_frozen_identity(identity: dict[str, Any], baseline_path: Path) -> None:
    values = _baseline_identity_values(baseline_path)
    expected = {
        "Source SHA-256": identity.get("source_sha256"),
        "Harness SHA-256": identity.get("harness_sha256"),
        "Workload SHA-256": identity.get("workload_sha256"),
        "Probe binary SHA-256": identity.get("probe_binary_sha256"),
    }
    for label, current in expected.items():
        if not isinstance(current, str) or values.get(label) != current:
            raise CaptureError(f"Current {label.lower()} does not match the pre-capture baseline freeze")
    probe_path = Path(str(identity.get("probe_path", "")))
    build_row = values.get("Probe build", "")
    if (probe_path.name != "platformator_editor_probe" or identity.get("build_label") != "editor-release" or
            identity.get("expected_configuration") != "Release" or
            not build_row.startswith("bin/editor-release/platformator_editor_probe, Release,")):
        raise CaptureError("Current probe build does not match the frozen Release build identity")
    host = identity.get("host")
    if not isinstance(host, dict):
        raise CaptureError("Current host identity is missing")
    affinity = host.get("cpu_affinity")
    if isinstance(affinity, list) and affinity:
        cpu_range = f"CPUs {affinity[0]}–{affinity[-1]}" if affinity == list(range(affinity[0], affinity[-1] + 1)) else \
                    "CPUs " + ",".join(str(cpu) for cpu in affinity)
    else:
        cpu_range = "CPUs unknown"
    host_row = f"{host.get('system')} {host.get('machine')}, kernel {host.get('release')}, " \
               f"{host.get('cpu_model')}, {cpu_range}"
    if values.get("Host") != host_row:
        raise CaptureError("Current host/CPU identity does not match the pre-capture baseline freeze")


def validate_fixed_session_path(root: Path, session_path: Path) -> None:
    fixed_path = (root / "specs/011-add-imgui-editor/evidence/editor-pilot.session.json").resolve()
    if session_path.resolve() != fixed_path:
        raise CaptureError(f"This feature has one frozen session path: {fixed_path}")


def fail_timing_session(session_path: Path, session: dict[str, Any], error: CaptureError) -> None:
    session["status"] = "failed"
    session["phases"]["timing"]["status"] = "failed"
    session["phases"]["timing"]["failure"] = str(error)
    session["phases"]["timing"]["failed_epoch"] = time.time()
    _update_accounting(session_path, session)
    raise error


def reject_identity_mismatch(session_path: Path, session: dict[str, Any], current: dict[str, Any]) -> None:
    try:
        validate_identity(session["identity"], current)
    except CaptureError as error:
        fail_timing_session(session_path, session, error)


def validate_phase_transition(session: dict[str, Any], mode: str) -> None:
    validate_session(session)
    if mode not in ("resources", "timing"):
        raise CaptureError(f"Unknown capture phase: {mode}")
    if mode == "resources":
        raise CaptureError("Resource capture starts a new session and cannot continue an existing journal")
    if session["status"] != "active" or session["phases"]["resources"]["status"] != "complete" or \
            session["phases"]["timing"]["status"] != "pending":
        raise CaptureError("Timing requires one successful matching resource phase; failed or repeated captures are blocked")


def _update_accounting(session_path: Path, session: dict[str, Any]) -> None:
    root = Path(session["capture_root"])
    output_bytes = session_path.stat().st_size if session_path.exists() else 0
    if root.exists():
        for path in root.rglob("*"):
            if path.is_file() and not path.is_symlink():
                output_bytes += path.stat().st_size
    session["accounting"]["output_bytes"] = output_bytes
    session["accounting"]["elapsed_seconds"] = max(0.0, time.monotonic() - session["budget"]["started_monotonic"])
    _atomic_write_json(session_path, session)


def _terminate_owned(process: subprocess.Popen) -> None:
    try:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGINT)
        elif process.poll() is None:
            process.send_signal(signal.SIGINT)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        pass
    try:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGTERM)
        elif process.poll() is None:
            process.terminate()
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        try:
            if os.name == "posix":
                os.killpg(process.pid, signal.SIGKILL)
            else:
                process.kill()
        except ProcessLookupError:
            pass
        process.wait()


def _run_command(command: list[str], output_path: Path, stderr_path: Path, budget: CaptureBudget,
                 session: dict[str, Any]) -> dict[str, Any]:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    record = {"argv": command, "stdout": str(output_path), "stderr": str(stderr_path),
              "started_epoch": time.time(), "exit_code": None}
    session["commands"].append(record)
    try:
        with output_path.open("xb") as stdout, stderr_path.open("x", encoding="utf-8") as stderr:
            process = subprocess.Popen(command, stdout=stdout, stderr=stderr,
                                       cwd=session["identity"].get("project_root"),
                                       start_new_session=(os.name == "posix"))
            try:
                return_code = budget.wait(process)
            except BaseException:
                _terminate_owned(process)
                raise
        record["exit_code"] = return_code
        record["completed_epoch"] = time.time()
        if return_code != 0:
            raise CaptureError(f"Capture subprocess failed with exit code {return_code}; see {stderr_path}")
        return record
    finally:
        _update_accounting(session_path=Path(session["session_path"]), session=session)
        budget.check()


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CaptureError(f"Capture subprocess output is incomplete or invalid: {path}") from error
    if not isinstance(value, dict):
        raise CaptureError(f"Capture subprocess output is not an object: {path}")
    return value


def _validate_probe_identity(value: dict[str, Any], expected_configuration: str) -> None:
    if value.get("probe") != "platformator-editor-probe" or value.get("schema_version") != 1:
        raise CaptureError("Editor probe identity kind or schema is unsupported")
    if str(value.get("configuration", "")).lower() != expected_configuration.lower():
        raise CaptureError("Editor probe build configuration differs from the requested identity")
    if not isinstance(value.get("compiler"), str) or not value["compiler"] or value.get("cxx_standard") != 20:
        raise CaptureError("Editor probe build identity is incomplete")


def _validate_resource_output(value: dict[str, Any]) -> None:
    if value.get("schema_version") != 1 or value.get("mode") != "resources" or value.get("cycles") != RESOURCE_CYCLES:
        raise CaptureError("Resource probe output does not match the frozen cycle workload")
    observations = value.get("observations")
    if not isinstance(observations, list) or len(observations) != RESOURCE_CYCLES:
        raise CaptureError("Resource probe output is missing cycle observations")
    expected = {"tiny": 10, "objects_1000": 1000, "objects_10000": 10000, "deep_parent_128": 128}
    for index, row in enumerate(observations):
        if not isinstance(row, dict) or row.get("cycle") != index:
            raise CaptureError("Resource probe cycles are truncated or reordered")
        workloads = row.get("workloads")
        if not isinstance(workloads, list) or {item.get("workload") for item in workloads if isinstance(item, dict)} != set(expected):
            raise CaptureError("Resource probe workload inventory is incomplete")
        for item in workloads:
            if item.get("objects") != expected[item["workload"]] or not isinstance(item.get("texture_references"), int) or \
                    not isinstance(item.get("history_retained_bytes"), int):
                raise CaptureError("Resource probe workload observation is malformed")
        task = row.get("task")
        process = row.get("process")
        allocation = row.get("allocation")
        if not isinstance(task, dict) or task.get("files") != 32 or task.get("completed_tasks") != 1:
            raise CaptureError("Resource probe task observation is missing or incomplete")
        if not isinstance(process, dict) or process.get("output_bytes") != 1_200_000 or \
                not process.get("console_truncated") or process.get("console_retained_bytes", 0) > 1_048_576:
            raise CaptureError("Resource probe bounded-process observation is malformed")
        if not isinstance(allocation, dict) or any(not isinstance(allocation.get(key), int) or allocation[key] < 0
                                                   for key in ("calls", "bytes")):
            raise CaptureError("Resource probe allocation observation is malformed")


def _validate_timing_output(value: dict[str, Any]) -> None:
    if value.get("schema_version") != 1 or value.get("mode") != "timing" or \
            value.get("repetitions") != REPETITIONS or value.get("warmup_frames") != WARMUP_FRAMES or \
            value.get("measured_frames") != MEASURED_FRAMES:
        raise CaptureError("Timing probe parameters differ from the frozen pilot")
    expected_workloads = {"tiny", "objects_1000", "objects_10000", "deep_parent_128"}
    samples = value.get("samples")
    expected_count = REPETITIONS * MEASURED_FRAMES * len(expected_workloads)
    if not isinstance(samples, list) or len(samples) != expected_count:
        raise CaptureError("Timing probe raw sample count is truncated or unexpected")
    for row in samples:
        if not isinstance(row, dict) or row.get("workload") not in expected_workloads or \
                not isinstance(row.get("repetition"), int) or row["repetition"] not in range(REPETITIONS) or \
                not isinstance(row.get("frame"), int) or row["frame"] not in range(MEASURED_FRAMES) or \
                not isinstance(row.get("select_edit_ns"), int) or row["select_edit_ns"] < 0:
            raise CaptureError("Timing probe contains a malformed raw sample")


def _render_command(args: argparse.Namespace, output: Path, budget: CaptureBudget) -> list[str] | None:
    supplied = [getattr(args, name) is not None for name in RENDER_ARGUMENTS]
    if not any(supplied):
        return None
    if not all(supplied):
        raise CaptureError("A rendering comparison requires every baseline/candidate probe, runner, and worker-count argument")
    remaining_bytes = budget.max_bytes - sum(
        path.stat().st_size for root in budget.roots if root.exists()
        for path in ([root] if root.is_file() else root.rglob("*")) if path.is_file() and not path.is_symlink())
    remaining_seconds = max(0.001, budget.deadline - time.monotonic())
    if remaining_bytes <= 0:
        raise BudgetExceeded("No output budget remains for rendering; partial files are retained")
    command = [sys.executable, str(Path(__file__).with_name("capture_linear_render.py")),
               "--baseline-probe", str(args.render_baseline_probe),
               "--candidate-probe", str(args.render_candidate_probe),
               "--baseline-runner", str(args.render_baseline_runner),
               "--candidate-runner", str(args.render_candidate_runner),
               "--baseline-validation-probe", str(args.render_baseline_validation_probe),
               "--candidate-validation-probe", str(args.render_candidate_validation_probe),
               "--multiworker-count", str(args.render_multiworker_count),
               "--output", str(output),
               "--max-output-mib", format(remaining_bytes / (1024 * 1024), ".9g"),
               "--max-seconds", format(remaining_seconds, ".9g")]
    if args.render_asset_root is not None:
        command.extend(("--asset-root", str(args.render_asset_root)))
    return command


def parser() -> argparse.ArgumentParser:
    command = argparse.ArgumentParser(description=__doc__)
    command.add_argument("--mode", choices=("resources", "timing"), required=True)
    command.add_argument("--session", type=Path, required=True)
    command.add_argument("--probe", type=Path, default=Path("bin/editor-release/platformator_editor_probe"))
    command.add_argument("--build-label", default="editor-release")
    command.add_argument("--expected-configuration", default="Release")
    command.add_argument("--max-output-mib", type=float, default=DEFAULT_MAX_OUTPUT_MIB)
    command.add_argument("--max-seconds", type=float, default=DEFAULT_MAX_SECONDS)
    command.add_argument("--cycles", type=int, default=RESOURCE_CYCLES,
                         help="Frozen resource cycle count (must remain 20)")
    command.add_argument("--render-baseline-probe", type=Path)
    command.add_argument("--render-candidate-probe", type=Path)
    command.add_argument("--render-baseline-runner", type=Path)
    command.add_argument("--render-candidate-runner", type=Path)
    command.add_argument("--render-baseline-validation-probe", type=Path)
    command.add_argument("--render-candidate-validation-probe", type=Path)
    command.add_argument("--render-multiworker-count", type=int)
    command.add_argument("--render-asset-root", type=Path)
    return command


def execute(args: argparse.Namespace, root: Path) -> None:
    session_path = args.session.resolve()
    validate_fixed_session_path(root, session_path)
    capture_root = session_path.with_name(session_path.stem + ".capture")
    if not math.isfinite(args.max_output_mib) or not 0 < args.max_output_mib <= DEFAULT_MAX_OUTPUT_MIB:
        raise CaptureError(f"--max-output-mib must be positive and at most {DEFAULT_MAX_OUTPUT_MIB:g}")
    if not math.isfinite(args.max_seconds) or not 0 < args.max_seconds <= DEFAULT_MAX_SECONDS:
        raise CaptureError(f"--max-seconds must be positive and at most {DEFAULT_MAX_SECONDS:g}")
    if args.cycles != RESOURCE_CYCLES:
        raise CaptureError(f"The frozen resource workload requires exactly {RESOURCE_CYCLES} cycles")

    if args.mode == "resources":
        if session_path.exists() or capture_root.exists():
            raise CaptureError("Resource capture requires a fresh session and capture root; automatic retries are disabled")
        probe_identity = compute_identity(root, args.probe, args.build_label)
        probe_identity["project_root"] = str(root.resolve())
        probe_identity["expected_configuration"] = args.expected_configuration
        validate_frozen_identity(probe_identity, root / "specs/011-add-imgui-editor/evidence/BASELINE.md")
        session_path.parent.mkdir(parents=True, exist_ok=True)
        capture_root.mkdir(parents=True)
        session = _new_session(session_path, capture_root, probe_identity,
                               args.max_output_mib, args.max_seconds)
    else:
        if not session_path.is_file():
            raise CaptureError("Timing requires the completed resource phase in the original session")
        session = _load_session(session_path)
        validate_phase_transition(session, "timing")
        if args.max_output_mib != session["budget"]["max_output_mib"] or args.max_seconds != session["budget"]["max_seconds"]:
            raise CaptureError("Timing must retain the original session limits")
        current_identity = compute_identity(root, args.probe, args.build_label)
        current_identity["project_root"] = str(root.resolve())
        current_identity["expected_configuration"] = args.expected_configuration
        try:
            validate_frozen_identity(current_identity, root / "specs/011-add-imgui-editor/evidence/BASELINE.md")
        except CaptureError as error:
            fail_timing_session(session_path, session, error)
        reject_identity_mismatch(session_path, session, current_identity)
        capture_root = Path(session["capture_root"])
        if not capture_root.is_dir():
            raise CaptureError("The original session capture root is missing")

    session["session_path"] = str(session_path)
    phase = session["phases"][args.mode]
    if phase["status"] != "pending":
        raise CaptureError(f"{args.mode} phase is not pending; no automatic retry is allowed")
    phase["status"] = "running"
    phase["started_epoch"] = time.time()
    _atomic_write_json(session_path, session)
    budget = _make_budget(session_path, session)
    try:
        budget.check()
        identity_output = capture_root / f"{args.mode}.probe-identity.json"
        identity_error = capture_root / f"{args.mode}.probe-identity.stderr.log"
        _run_command([str(Path(session["identity"]["probe_path"])), "--print-build-identity"],
                     identity_output, identity_error, budget, session)
        probe_build_identity = _read_json(identity_output)
        _validate_probe_identity(probe_build_identity, args.expected_configuration)

        if args.mode == "resources":
            with tempfile.TemporaryDirectory(prefix="platformator-editor-probe-") as scratch:
                command = [session["identity"]["probe_path"], "resources", "--cycles", str(RESOURCE_CYCLES),
                           "--scratch", scratch]
                raw_path = capture_root / "resources.raw.json"
                error_path = capture_root / "resources.stderr.log"
                _run_command(command, raw_path, error_path, budget, session)
            resource_output = _read_json(raw_path)
            _validate_resource_output(resource_output)
            if resource_output.get("identity") != probe_build_identity:
                raise CaptureError("Resource observations carry a different build identity")
        else:
            command = [session["identity"]["probe_path"], "timing", "--repetitions", str(REPETITIONS),
                       "--warmup", str(WARMUP_FRAMES), "--frames", str(MEASURED_FRAMES)]
            raw_path = capture_root / "timing.raw.json"
            error_path = capture_root / "timing.stderr.log"
            _run_command(command, raw_path, error_path, budget, session)
            timing_output = _read_json(raw_path)
            _validate_timing_output(timing_output)
            if timing_output.get("identity") != probe_build_identity:
                raise CaptureError("Timing samples carry a different build identity")
            render_command = _render_command(args, capture_root / "render-pilot", budget)
            if render_command is not None:
                render_log = capture_root / "render-pilot.driver.log"
                _run_command(render_command, render_log, render_log.with_suffix(".stderr.log"), budget, session)
                if not (capture_root / "render-pilot" / "report.json").is_file():
                    raise CaptureError("Rendering pilot did not produce its compact success report")

        budget.check()
        phase["status"] = "complete"
        phase["completed_epoch"] = time.time()
        phase["raw_output"] = str(raw_path)
        session["status"] = "complete" if args.mode == "timing" else "active"
        _update_accounting(session_path, session)
        budget.check()
    except BaseException as error:
        phase["status"] = "failed"
        phase["failure"] = f"{type(error).__name__}: {error}"
        phase["failed_epoch"] = time.time()
        session["status"] = "failed"
        try:
            _update_accounting(session_path, session)
        except Exception:
            _atomic_write_json(session_path, session)
        raise


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        root = Path(__file__).resolve().parent.parent
        execute(args, root)
        return 0
    except (BudgetExceeded, CaptureError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"capture_editor: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("capture_editor: interrupted; partial outputs retained and session marked failed", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
