#!/usr/bin/env python3
"""Reproduce the cleanup reference: correctness first, then 180 shuffled fresh processes.

Linux affinity is explicit. Validation timings are never aggregated as performance.
Run --mode validate, finish all builds/tests, then --mode time into the same directory.
"""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import subprocess
import tarfile
import time
from types import SimpleNamespace

import sweep_scaling as sweep

CASES = {"broad_phase": [100, 500, 1000], "broad_phase_stress": [50, 150, 300],
         "narrow_phase": [10, 100, 200], "rigid_body_container": [100, 500, 1000],
         "add_and_remove": [10, 100, 1000], "moving_add_and_remove": [10, 100, 1000]}
TOLERANCES = {"position": 1e-4, "velocity": 1e-4, "rotation": 1e-5, "angular_velocity": 1e-5,
              "normal": 1e-5, "separation": 1e-4}


def compare(serial, parallel):
    """Compare canonical snapshots; exact identities/support/sleep/features, declared float tolerances."""
    def numbers(a, b, tolerance):
        if isinstance(a, list):
            return len(a) == len(b) and all(numbers(x, y, tolerance) for x, y in zip(a, b))
        return math.isfinite(a) and math.isfinite(b) and abs(a - b) <= tolerance
    for key in ("objects", "colliders", "bodies", "support_total", "maximum_supports"):
        if serial[key] != parallel[key]:
            raise ValueError(f"Serial/parallel invariant differs: {key}")
    for kind in ("body_snapshots", "contact_snapshots"):
        if len(serial[kind]) != len(parallel[kind]):
            raise ValueError(f"Serial/parallel {kind} count differs")
        for a, b in zip(serial[kind], parallel[kind]):
            for key in a:
                if key == "features":
                    valid = len(a[key]) == len(b[key]) and all(x[0] == y[0] and numbers(x[1], y[1], TOLERANCES["separation"])
                                                                 for x, y in zip(a[key], b[key]))
                elif key in TOLERANCES:
                    valid = numbers(a[key], b[key], TOLERANCES[key])
                else:
                    valid = a[key] == b[key]
                if not valid:
                    raise ValueError(f"Serial/parallel {kind} {key} differs: {a.get('name', a.get('a'))}")


def args_for(case, workers, validate):
    return SimpleNamespace(scenario=case, workers=workers, seed=0, steady_count=1000,
                           box_mix=None, circle_mix=None, validate_invariants=validate)


def run(runner, root, case, count, workers, repetition, validate):
    directory = root / ("validation" if validate else "runs") / f"{case}-w{workers}" / f"population-{count:06d}"
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"run-{repetition:02d}.csv"
    if any(path.with_suffix(suffix).exists() for suffix in (".csv", ".log", ".manifest.json", ".samples.csv")):
        raise FileExistsError(path)
    command = ["taskset", "-c", "0" if workers == 1 else "0,2,4,6,8,10", str(runner), "--scenario", case,
               "--workers", str(workers), "--seed", "0", "--warmup-frames", "120", "--measure-frames", "600",
               "--dt", str(1 / 120), "--repetitions", "1" if validate else "5", "--run-index", str(repetition),
               "--csv-output", str(path)]
    command = sweep.apply_count_argument(command, args_for(case, workers, validate), count)
    if validate:
        command.append("--validate-invariants")
    with path.with_suffix(".log").open("w") as log:
        log.write(json.dumps({"command": command}) + "\n")
        log.flush()
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
    metrics = sweep.parse_benchmark_csv(path)
    manifest = sweep.load_manifest(path)
    build = manifest["build"]
    if (build["configuration"] != "Release" or build["sanitizer"] != "none" or build["work_probes"] or build["debug_tools"]
            or manifest["workers"] != workers or manifest["seed"] != 0 or manifest["measured_steps"] != 600
            or manifest["warmup_steps"] != 120 or manifest["rendering"] or manifest["timing_eligible"] == validate
            or manifest["validation_mode"] != ("per-step" if validate else "boundary-only")):
        raise ValueError("Capture metadata does not match the declared configuration")
    return metrics, manifest


def archive(directory):
    target = directory.with_suffix(".tar.gz")
    with tarfile.open(target, "x:gz") as output:
        output.add(directory, arcname=directory.name)
    # Raw files remain recoverable byte-for-byte in the archive; avoid duplicating them.
    import shutil
    shutil.rmtree(directory)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", choices=("validate", "time"), required=True)
    args = parser.parse_args()
    root, runner = args.output.resolve(), args.runner.resolve()
    root.mkdir(parents=True, exist_ok=True)
    report_path = root / "invariants.json"
    if args.mode == "validate":
        if report_path.exists():
            raise FileExistsError(report_path)
        report = {"passed": False, "tolerances": TOLERANCES, "cases": [], "note": "Expensive per-step oracle; durations not performance eligible"}
        for case, counts in CASES.items():
            for count in counts:
                states = []
                for workers in (1, 6):
                    print(f"Validate {case} {count} workers={workers}", flush=True)
                    _, manifest = run(runner, root, case, count, workers, 0, True)
                    states.append(manifest["physical_invariants"])
                    fingerprint = manifest["build"]["source_sha256"]
                    if "source_sha256" in report and fingerprint != report["source_sha256"]:
                        raise ValueError("Source fingerprint changed during validation")
                    report["source_sha256"] = fingerprint
                compare(*states)
                report["cases"].append({"scenario": case, "count": count, "serial_parallel_passed": True,
                                         "objects": states[0]["objects"], "contacts": len(states[0]["contact_snapshots"]),
                                         "supports": states[0]["support_total"]})
        report["passed"] = True
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        for directory in sorted((root / "validation").iterdir()):
            archive(directory)
        return
    report = json.loads(report_path.read_text())
    if report["passed"] is not True or len(report["cases"]) != 18:
        raise ValueError("Full invariant validation is required before timing")
    if (root / "summary.csv").exists() or (root / "capture.json").exists():
        raise FileExistsError("Capture already exists")
    jobs = [(case, count, workers, repetition) for case, counts in CASES.items() for count in counts
            for workers in (1, 6) for repetition in range(5)]
    random.Random(1).shuffle(jobs)
    capture = {"started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "platform": platform.platform(),
               "affinity": {"1": [0], "6": [0, 2, 4, 6, 8, 10]}, "job_shuffle_seed": 1,
               "governor": Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor").read_text().strip(),
               "load_before": os.getloadavg(), "jobs": jobs, "source_sha256": report["source_sha256"]}
    results = {}
    for index, (case, count, workers, repetition) in enumerate(jobs):
        print(f"Time {index + 1}/{len(jobs)} {case} {count} workers={workers} run={repetition}", flush=True)
        metrics, manifest = run(runner, root, case, count, workers, repetition, False)
        if manifest["build"]["source_sha256"] != report["source_sha256"]:
            raise ValueError("Timing source differs from validated source")
        results.setdefault((case, count, workers), []).append(metrics)
    rows = []
    for (case, count, workers), metrics in sorted(results.items()):
        row = sweep.build_result_row(args_for(case, workers, False), count, metrics)
        row["raw_archive"] = f"runs/{case}-w{workers}.tar.gz"
        rows.append(row)
    with (root / "summary.csv").open("x", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    capture.update(finished_utc=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), load_after=os.getloadavg())
    (root / "capture.json").write_text(json.dumps(capture, indent=2) + "\n")
    for directory in sorted((root / "runs").iterdir()):
        archive(directory)
    source = Path(__file__).resolve().parents[1]
    with tarfile.open(root / "source.tar.gz", "x:gz") as output:
        for relative in ("src", "include", "cmake", "tests", "assets", "CMakeLists.txt", "CMakePresets.json", "benchmark/sweep_scaling.py",
                         "benchmark/plot_scaling.py", "benchmark/capture_cleanup.py", "vendored/SDL3_mixer-3.2.0/src/decoder_wav.c"):
            output.add(source / relative, arcname=relative)
    (root / "SHA256SUMS").write_text("".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(root)}\n"
                                             for path in sorted(root.rglob("*")) if path.is_file() and path.name != "SHA256SUMS"))


if __name__ == "__main__":
    main()
