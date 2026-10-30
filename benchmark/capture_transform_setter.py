#!/usr/bin/env python3
"""Capture one bounded baseline/candidate transform-setter pilot."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys
import time

from capture_budget import BudgetExceeded, CaptureBudget


DEFAULT_MAX_SECONDS = 600
DEFAULT_MAX_MIB = 50
REPETITIONS = 3
STEP_WARMUP_FRAMES = 5
STEP_MEASURED_FRAMES = 30
TIMING_CASES = (
    "read-flat-small",
    "read-flat-large",
    "read-deep-small",
    "read-deep-large",
    "read-broad-small",
    "read-broad-large",
    "ancestor-edit-deep",
    "setters",
    "motion-setters",
    "complete-step-1",
    "complete-step-6",
    "complete-step-setters-1",
    "complete-step-setters-6",
    "physics-integrate-move-1",
    "physics-integrate-move-6",
)


class CaptureError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_cache(path: Path) -> dict[str, str]:
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("//") and not line.startswith("#") and "=" in line:
            key, value = line.split("=", 1)
            key = key.split(":", 1)[0]
            values[key] = value
    return values


def build_identity(build_dir: Path, work_probes: bool) -> dict:
    cache = read_cache(build_dir / "CMakeCache.txt")
    binary = build_dir / "platformator_transform_setter_probe"
    manifest = build_dir / "generated/platformator-source-hashes.manifest"
    if not binary.is_file() or not manifest.is_file():
        raise CaptureError(f"Missing probe binary or source manifest in {build_dir}")
    source_hash = next((line.split(":", 1)[1] for line in manifest.read_text().splitlines()
                        if line.startswith("aggregate:")), None)
    if not source_hash:
        raise CaptureError(f"Source manifest has no aggregate hash in {build_dir}")
    expected_probes = "ON" if work_probes else "OFF"
    if cache.get("CMAKE_BUILD_TYPE") != "Release":
        raise CaptureError(f"{build_dir} is not a Release build")
    if cache.get("PLATFORMATOR_ENABLE_WORK_PROBES") != expected_probes:
        raise CaptureError(f"{build_dir} has unexpected work-probe configuration")
    if cache.get("PLATFORMATOR_ENABLE_DEBUG_TOOLS") != "OFF":
        raise CaptureError(f"{build_dir} has debug tools enabled")
    if cache.get("PLATFORMATOR_SANITIZER", "none") not in ("", "none"):
        raise CaptureError(f"{build_dir} has a sanitizer enabled")
    return {
        "build_dir": str(build_dir.resolve()),
        "binary": str(binary.resolve()),
        "binary_sha256": sha256_file(binary),
        "source_sha256": source_hash,
        "configuration": cache.get("CMAKE_BUILD_TYPE"),
        "compiler": cache.get("CMAKE_CXX_COMPILER", "unknown"),
        "compiler_flags": cache.get("CMAKE_CXX_FLAGS_RELEASE", ""),
        "work_probes": work_probes,
        "debug_tools": cache.get("PLATFORMATOR_ENABLE_DEBUG_TOOLS") == "ON",
        "sanitizer": cache.get("PLATFORMATOR_SANITIZER", "none"),
    }


def require_matched_builds(builds: dict[str, dict], scope: str) -> None:
    fields = ("configuration", "compiler", "compiler_flags", "sanitizer", "debug_tools")
    baseline, candidate = builds["baseline"], builds["candidate"]
    differences = {field: (baseline.get(field), candidate.get(field)) for field in fields
                   if baseline.get(field) != candidate.get(field)}
    if differences:
        raise CaptureError(f"{scope} builds are not matched: {differences}")


def correctness_signature(sample: dict) -> dict:
    fields = (
        "checksum", "parent_child_expected_x", "parent_child_actual_x",
        "unchanged_parent_child_expected_x", "unchanged_parent_child_actual_x",
        "changed_parent_child_expected_x", "changed_parent_child_actual_x",
        "unchanged_kinematic_position_checksum", "changed_kinematic_position_checksum",
        "unchanged_kinematic_state_checksum", "changed_kinematic_state_checksum",
        "wake_threshold_checks",
    )
    signature = {field: sample[field] for field in fields if field in sample}
    for pass_name in ("unchanged", "changed_control"):
        values = sample.get(pass_name)
        if isinstance(values, dict):
            signature[pass_name] = {
                field: values[field]
                for field in ("final_velocity_x", "final_angular_velocity")
                if field in values
            }
    return signature


def run_probe(command: list[str], budget: CaptureBudget) -> dict:
    budget.check()
    process = subprocess.Popen(command, cwd=Path(command[0]).resolve().parent,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        while True:
            budget.check()
            try:
                stdout, stderr = process.communicate(timeout=0.25)
                break
            except subprocess.TimeoutExpired:
                continue
    except BaseException:
        if process.poll() is None:
            process.terminate()
            try:
                process.communicate(timeout=1)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()
        raise
    budget.check()
    if len(stdout.encode()) > 1024 * 1024 or len(stderr.encode()) > 1024 * 1024:
        raise CaptureError("Probe output exceeded the 1 MiB per-process diagnostic bound")
    if process.returncode != 0:
        raise CaptureError(f"Command failed ({process.returncode}): {' '.join(command)}\n{stderr[-4000:]}")
    try:
        return json.loads(stdout)
    except json.JSONDecodeError as error:
        raise CaptureError(f"Probe returned invalid JSON: {error}\n{stdout[:2000]}") from error


def workload_identity(sample: dict) -> dict:
    fields = (
        "case", "fixture_version", "seed", "workers", "timing_boundary", "operation_count", "target_count", "depth", "breadth",
        "edits", "reads", "read_edit_ratio", "iterations", "properties_per_iteration", "warmup_frames",
        "measured_frames", "body_count", "setter_body_count", "setter_calls_per_frame", "shape",
    )
    return {field: sample[field] for field in fields if field in sample}


def summarize_run(sample: dict) -> dict:
    if sample.get("case") == "motion-setters":
        return {"sample_count": 2,
                "unchanged_elapsed_ns": sample["unchanged"]["elapsed_ns"],
                "changed_control_elapsed_ns": sample["changed_control"]["elapsed_ns"]}
    if "samples_ns" in sample:
        values = sample["samples_ns"]
        return {"sample_count": len(values), "mean_ns": statistics.fmean(values),
                "median_ns": statistics.median(values), "p95_ns": sorted(values)[int(0.95 * (len(values) - 1))]}
    if "unchanged_samples_ns" in sample and "changed_control_samples_ns" in sample:
        def summarize_values(values):
            return {"sample_count": len(values), "mean_ns": statistics.fmean(values),
                    "median_ns": statistics.median(values),
                    "p95_ns": sorted(values)[int(0.95 * (len(values) - 1))]}
        return {"unchanged": summarize_values(sample["unchanged_samples_ns"]),
                "changed_control": summarize_values(sample["changed_control_samples_ns"])}
    if "elapsed_ns" in sample:
        return {"sample_count": 1, "elapsed_ns": sample["elapsed_ns"]}
    if sample.get("case") == "setters":
        return {"sample_count": 2,
                "unchanged_elapsed_ns": sample["unchanged"]["elapsed_ns"],
                "changed_control_elapsed_ns": sample["changed_control"]["elapsed_ns"]}
    return {"sample_count": 0}


def save_report(path: Path, report: dict, budget: CaptureBudget | None = None) -> None:
    path.write_text(json.dumps(report, separators=(",", ":"), sort_keys=True) + "\n", encoding="utf-8")
    if budget is not None:
        budget.check()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-build", type=Path, required=True)
    parser.add_argument("--candidate-build", type=Path, required=True)
    parser.add_argument("--baseline-work-build", type=Path, required=True)
    parser.add_argument("--candidate-work-build", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path,
                        default=Path(__file__).resolve().parents[1] /
                        "specs/010-optimize-transform-setters/evidence")
    parser.add_argument("--max-seconds", type=float, default=DEFAULT_MAX_SECONDS)
    parser.add_argument("--max-mib", type=float, default=DEFAULT_MAX_MIB)
    args = parser.parse_args()
    if not 0 < args.max_seconds <= DEFAULT_MAX_SECONDS or not 0 < args.max_mib <= DEFAULT_MAX_MIB:
        parser.error("capture limits may be reduced, but not raised above 600 seconds or 50 MiB")

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    raw_path = output_dir / "pilot.raw.json"
    if raw_path.exists():
        raise CaptureError(f"Refusing to replace existing evidence file: {raw_path}")
    budget = CaptureBudget([output_dir], max_mib=args.max_mib, max_seconds=args.max_seconds)
    timing_builds = {
        "baseline": build_identity(args.baseline_build.resolve(), work_probes=False),
        "candidate": build_identity(args.candidate_build.resolve(), work_probes=False),
    }
    work_builds = {
        "baseline": build_identity(args.baseline_work_build.resolve(), work_probes=True),
        "candidate": build_identity(args.candidate_work_build.resolve(), work_probes=True),
    }
    require_matched_builds(timing_builds, "timing")
    require_matched_builds(work_builds, "work")
    report = {
        "schema_version": 1,
        "status": "partial",
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "budget": {"max_seconds": args.max_seconds, "max_mib": args.max_mib},
        "repetitions": REPETITIONS,
        "step_warmup_frames": STEP_WARMUP_FRAMES,
        "step_measured_frames": STEP_MEASURED_FRAMES,
        "builds": {"timing": timing_builds, "work": work_builds},
        "workload_cases": list(TIMING_CASES),
        "commands": [],
        "timing": {},
        "work": {},
    }
    save_report(raw_path, report, budget)

    try:
        for case_id in TIMING_CASES:
            case = {"workload_identity": None, "baseline": [], "candidate": []}
            report["timing"][case_id] = case
            for repetition in range(REPETITIONS):
                variants = ("baseline", "candidate") if repetition % 2 == 0 else ("candidate", "baseline")
                for variant in variants:
                    binary = timing_builds[variant]["binary"]
                    command = [binary, "--case", case_id]
                    report["commands"].append(command)
                    sample = run_probe(command, budget)
                    identity = workload_identity(sample)
                    if case["workload_identity"] is None:
                        case["workload_identity"] = identity
                    elif case["workload_identity"] != identity:
                        raise CaptureError(f"Workload identity changed during {case_id}: {identity}")
                    other_variant = "candidate" if variant == "baseline" else "baseline"
                    other_samples = case[other_variant]
                    if other_samples and correctness_signature(sample) != correctness_signature(other_samples[-1]["raw"]):
                        raise CaptureError(f"Expected-value results differ between revisions for {case_id}")
                    case[variant].append({"repetition": repetition, "summary": summarize_run(sample), "raw": sample})
                    save_report(raw_path, report, budget)

        for variant in ("baseline", "candidate"):
            command = [work_builds[variant]["binary"], "--case", "work", "--workers", "1"]
            report["commands"].append(command)
            report["work"][variant] = run_probe(command, budget)
            save_report(raw_path, report, budget)

        report["status"] = "complete"
        report["finished_utc"] = datetime.now(timezone.utc).isoformat()
        save_report(raw_path, report, budget)
        print(f"Captured {len(TIMING_CASES)} workloads x {REPETITIONS} repetitions per revision; "
              f"raw evidence: {raw_path}")
        return 0
    except (BudgetExceeded, CaptureError, OSError) as error:
        report["failure"] = {"type": type(error).__name__, "message": str(error)}
        report["finished_utc"] = datetime.now(timezone.utc).isoformat()
        save_report(raw_path, report)
        print(f"Partial pilot retained at {raw_path}: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
