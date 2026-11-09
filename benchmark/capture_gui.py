#!/usr/bin/env python3
"""Capture one bounded, matched GUI probe workload for baseline and candidate builds."""

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
import time
from typing import Any

from capture_budget import BudgetExceeded, CaptureBudget


SCHEMA_VERSION = 1
DEFAULT_MAX_OUTPUT_MIB = 50.0
DEFAULT_MAX_SECONDS = 600.0
REPETITIONS = 3
WARMUP_FRAMES = 5
MEASURED_FRAMES = 30
MAX_OUTPUT_MIB = DEFAULT_MAX_OUTPUT_MIB
MAX_SECONDS = DEFAULT_MAX_SECONDS
SOURCE_PATHS = (
    "CMakeLists.txt", "editor/CMakeLists.txt", "editor/src", "editor/benchmark/gui_probe.cpp",
    "include", "src", "vendored/imgui-1.92.9b", "vendored/SDL3_image-3.4.8/src",
)
HARNESS_PATHS = (
    "benchmark/capture_gui.py", "benchmark/capture_budget.py", "editor/benchmark/gui_probe.cpp",
    "editor/src/editorapp.cpp", "editor/src/editorapp.h", "editor/tests/CMakeLists.txt",
)
METRIC_NAMES = {
    "GUI_ACTIVE_FRAME_US", "GUI_WAIT_INCLUSIVE_US", "GUI_IDLE_CPU_US",
}


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
                         candidate.suffix not in (".pyc", ".o", ".a", ".so", ".dll", ".dylib"))
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


def _workload_identity(workload: str, project_root: Path, asset_root: Path,
                       scene: Path | None) -> dict[str, Any]:
    result: dict[str, Any] = {
        "name": workload,
        "input": "synthetic mouse hover over the visible main Scene panel" if workload == "hover"
                 else "no injected interaction; visible editor panels and preview",
        "project_root": str(project_root.resolve(strict=True)),
        "asset_root": str(asset_root.resolve(strict=True)),
        "scene": str(scene.resolve(strict=True)) if scene else None,
        "repetitions": REPETITIONS,
        "warmup_frames": WARMUP_FRAMES,
        "measured_frames": MEASURED_FRAMES,
        "backend": {"video_driver": "dummy", "renderer": "software"},
    }
    if scene is not None:
        result["scene_sha256"] = _sha256_file(scene.resolve(strict=True))
    return result


def _identity(source_root: Path, probe: Path, build_label: str,
              workload: dict[str, Any]) -> dict[str, Any]:
    resolved_probe = probe.resolve(strict=True)
    if not resolved_probe.is_file() or not os.access(resolved_probe, os.X_OK):
        raise CaptureError(f"GUI probe is not executable: {resolved_probe}")
    return {
        "source_root": str(source_root.resolve(strict=True)),
        "source_sha256": _hash_paths(source_root.resolve(strict=True), SOURCE_PATHS),
        "harness_sha256": _hash_paths(Path(__file__).resolve().parents[1], HARNESS_PATHS),
        "probe_binary_sha256": _sha256_file(resolved_probe),
        "probe_path": str(resolved_probe),
        "build_label": build_label,
        "host": {"system": platform.system(), "release": platform.release(),
                 "machine": platform.machine(), "logical_cpus": os.cpu_count()},
        "workload": workload,
        "workload_sha256": hashlib.sha256(
            json.dumps(workload, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest(),
    }


def validate_matched_workload(baseline: dict[str, Any], candidate: dict[str, Any]) -> None:
    if baseline["workload"] != candidate["workload"] or \
            baseline["workload_sha256"] != candidate["workload_sha256"]:
        raise CaptureError("Baseline and candidate do not use the same workload identity")


def _parse_probe_output(text: str, workload: str) -> dict[str, Any]:
    metadata: dict[str, str] = {}
    renderer: tuple[str, str] | None = None
    cold: list[int] = []
    metrics: dict[str, dict[int, int]] = {name: {} for name in METRIC_NAMES}
    events: list[dict[str, int]] = []
    for line in text.splitlines():
        cells = line.strip().split(",")
        if cells[0] == "GUI_PROBE":
            for item in cells[1:]:
                if "=" in item:
                    key, value = item.split("=", 1)
                    metadata[key] = value
        elif cells[0] == "GUI_RENDERER" and len(cells) == 3:
            renderer = (cells[1], cells[2])
        elif cells[0] == "GUI_COLD_PREP_US" and len(cells) == 2:
            cold.append(_nonnegative_int(cells[1], "cold preparation"))
        elif cells[0] in metrics and len(cells) == 3:
            frame = _nonnegative_int(cells[1], "frame index")
            value = _nonnegative_int(cells[2], cells[0])
            if frame in metrics[cells[0]]:
                raise CaptureError(f"Probe repeated {cells[0]} sample {frame}")
            metrics[cells[0]][frame] = value
        elif cells[0] == "GUI_EVENT_PRESENT_US" and len(cells) == 3:
            events.append({"frame": _nonnegative_int(cells[1], "event frame"),
                           "event_present_us": _nonnegative_int(cells[2], "event latency")})

    if metadata.get("version") != "2" or metadata.get("workload") != workload:
        raise CaptureError("GUI probe version or selected workload does not match the request")
    if not metadata.get("configuration") or not metadata.get("compiler"):
        raise CaptureError("GUI probe build identity is incomplete")
    if metadata.get("frames") != str(WARMUP_FRAMES + MEASURED_FRAMES) or \
            metadata.get("warmup_frames") != str(WARMUP_FRAMES):
        raise CaptureError("GUI probe frame parameters differ from the bounded pilot")
    if renderer != ("dummy", "software"):
        raise CaptureError("GUI probe did not use the declared dummy/software renderer")
    if len(cold) != 1:
        raise CaptureError("GUI probe must report exactly one cold preparation sample")
    expected_frames = set(range(MEASURED_FRAMES))
    for name, samples in metrics.items():
        if set(samples) != expected_frames:
            raise CaptureError(f"GUI probe {name} samples are missing, duplicated, or reordered")
    event_frames = [sample["frame"] for sample in events]
    if workload == "hover" and event_frames != [0, 10, 20]:
        raise CaptureError("Hover workload must produce one event-to-present sample every ten measured frames")
    if workload == "idle" and events:
        raise CaptureError("Idle workload unexpectedly produced input-to-present samples")
    return {
        "configuration": metadata["configuration"],
        "compiler": metadata["compiler"],
        "renderer": {"video_driver": renderer[0], "name": renderer[1]},
        "cold_preparation_us": cold[0],
        "frames": [
            {"frame": frame,
             "active_frame_us": metrics["GUI_ACTIVE_FRAME_US"][frame],
             "wait_inclusive_us": metrics["GUI_WAIT_INCLUSIVE_US"][frame],
             "idle_cpu_us": metrics["GUI_IDLE_CPU_US"][frame]}
            for frame in range(MEASURED_FRAMES)
        ],
        "event_present": events,
    }


def _nonnegative_int(value: str, label: str) -> int:
    try:
        result = int(value)
    except ValueError as error:
        raise CaptureError(f"GUI probe {label} is not an integer") from error
    if result < 0:
        raise CaptureError(f"GUI probe {label} cannot be negative")
    return result


def _write_report(path: Path, report: dict[str, Any]) -> None:
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(report, stream, sort_keys=True, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


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


def _run_probe(command: list[str], cwd: Path, stdout_path: Path, stderr_path: Path,
               budget: CaptureBudget) -> None:
    env = dict(os.environ)
    env.update({"SDL_VIDEODRIVER": "dummy", "SDL_RENDER_DRIVER": "software",
                "SDL_AUDIODRIVER": "dummy"})
    with stdout_path.open("xb") as stdout, stderr_path.open("x", encoding="utf-8") as stderr:
        process = subprocess.Popen(command, cwd=cwd, env=env, stdout=stdout, stderr=stderr,
                                   start_new_session=(os.name == "posix"))
        try:
            return_code = budget.wait(process)
        except BaseException:
            _terminate_owned(process)
            raise
    if return_code != 0:
        raise CaptureError(f"GUI probe failed with exit code {return_code}; see {stderr_path}")
    budget.check()


def _command(identity: dict[str, Any], workload: str, project_root: Path,
             asset_root: Path, scene: Path | None) -> list[str]:
    command = [identity["probe_path"], "--frames", str(WARMUP_FRAMES + MEASURED_FRAMES),
               "--warmup-frames", str(WARMUP_FRAMES), "--workload", workload,
               "--project-root", str(project_root), "--asset-root", str(asset_root), "--no-restore"]
    if scene is not None:
        command.extend(("--scene", str(scene)))
    return command


def run_capture(args: argparse.Namespace) -> dict[str, Any]:
    output = args.output.resolve()
    if output.exists():
        raise CaptureError(f"Output directory already exists; use a fresh path: {output}")
    if args.max_output_mib <= 0 or args.max_output_mib > MAX_OUTPUT_MIB or \
            args.max_seconds <= 0 or args.max_seconds > MAX_SECONDS or \
            not math.isfinite(args.max_output_mib) or not math.isfinite(args.max_seconds):
        raise CaptureError("Capture limits must be positive and may not exceed 50 MiB or 600 seconds")

    project_root = args.project_root.resolve(strict=True)
    asset_root = args.asset_root.resolve(strict=True)
    scene = args.scene.resolve(strict=True) if args.scene else None
    workload = _workload_identity(args.workload, project_root, asset_root, scene)
    baseline = _identity(args.baseline_source_root, args.baseline_probe,
                         args.baseline_label, workload)
    candidate = _identity(args.candidate_source_root, args.candidate_probe,
                          args.candidate_label, workload)
    validate_matched_workload(baseline, candidate)

    output.mkdir(parents=True)
    report_path = output / "gui-pilot.json"
    report: dict[str, Any] = {
        "kind": "platformator.gui-pilot",
        "schema_version": SCHEMA_VERSION,
        "status": "active",
        "budget": {"max_output_mib": args.max_output_mib,
                   "max_seconds": args.max_seconds,
                   "started_epoch": time.time()},
        "workload": workload,
        "identities": {"baseline": baseline, "candidate": candidate},
        "runs": [],
        "raw_samples": {"cold_preparation_us": [], "frames": [], "event_present": []},
    }
    budget = CaptureBudget([output], max_mib=args.max_output_mib,
                           max_seconds=args.max_seconds)
    _write_report(report_path, report)
    try:
        for side, identity, source_root in (
            ("baseline", baseline, args.baseline_source_root.resolve(strict=True)),
            ("candidate", candidate, args.candidate_source_root.resolve(strict=True)),
        ):
            for repetition in range(REPETITIONS):
                budget.check()
                stem = f".{side}-{repetition}"
                stdout_path = output / f"{stem}.stdout"
                stderr_path = output / f"{stem}.stderr"
                command = _command(identity, args.workload, project_root, asset_root, scene)
                run = {"side": side, "repetition": repetition, "argv": command,
                       "source_root": str(source_root), "stdout": stdout_path.name,
                       "stderr": stderr_path.name, "exit_code": None,
                       "started_epoch": time.time()}
                report["runs"].append(run)
                _write_report(report_path, report)
                _run_probe(command, source_root, stdout_path, stderr_path, budget)
                run["exit_code"] = 0
                run["completed_epoch"] = time.time()
                parsed = _parse_probe_output(stdout_path.read_text(encoding="utf-8"), args.workload)
                run["probe_build"] = {"configuration": parsed["configuration"],
                                      "compiler": parsed["compiler"],
                                      "renderer": parsed["renderer"]}
                report["raw_samples"]["cold_preparation_us"].append(
                    {"side": side, "repetition": repetition,
                     "sample_us": parsed["cold_preparation_us"]})
                for frame in parsed["frames"]:
                    report["raw_samples"]["frames"].append(
                        {"side": side, "repetition": repetition, **frame})
                for event in parsed["event_present"]:
                    report["raw_samples"]["event_present"].append(
                        {"side": side, "repetition": repetition, **event})
                stdout_path.unlink()
                stderr_path.unlink()
                _write_report(report_path, report)
                budget.check()
        report["status"] = "complete"
        report["budget"]["completed_epoch"] = time.time()
        report["budget"]["elapsed_seconds"] = time.monotonic() - budget.deadline + budget.max_seconds
        budget.check()
        _write_report(report_path, report)
        budget.check()
        return report
    except BaseException as error:
        report["status"] = "failed"
        report["failure"] = str(error)
        report["budget"]["failed_epoch"] = time.time()
        report["budget"]["elapsed_seconds"] = time.monotonic() - (budget.deadline - budget.max_seconds)
        try:
            _write_report(report_path, report)
        except (OSError, BudgetExceeded):
            pass
        raise


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-probe", type=Path, required=True)
    parser.add_argument("--baseline-source-root", type=Path, required=True)
    parser.add_argument("--baseline-label", required=True)
    parser.add_argument("--candidate-probe", type=Path, required=True)
    parser.add_argument("--candidate-source-root", type=Path, required=True)
    parser.add_argument("--candidate-label", required=True)
    parser.add_argument("--workload", choices=("idle", "hover"), required=True)
    parser.add_argument("--project-root", type=Path, required=True)
    parser.add_argument("--asset-root", type=Path, required=True)
    parser.add_argument("--scene", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-output-mib", type=float, default=DEFAULT_MAX_OUTPUT_MIB)
    parser.add_argument("--max-seconds", type=float, default=DEFAULT_MAX_SECONDS)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        result = run_capture(args)
    except (CaptureError, BudgetExceeded, OSError) as error:
        print(f"GUI capture failed: {error}", file=sys.stderr)
        return 2
    print(json.dumps({"status": result["status"], "report": str(args.output / "gui-pilot.json"),
                      "samples": {key: len(value) for key, value in result["raw_samples"].items()}},
                     sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
