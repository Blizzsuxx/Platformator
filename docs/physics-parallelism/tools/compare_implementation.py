"""Focused production physics comparisons. See the archived commands for exact runs."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shlex
import statistics
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(root / "engine/benchmarks"))

from capture_budget import CaptureBudget
from sweep_scaling import COUNTERS, load_manifest

build = root / "build/parallel-physics-implementation"
archive = root / "docs/physics-parallelism/evidence/implementation"


def fresh_output(parser, output):
    folder = output.resolve()
    try:
        folder.mkdir(parents=True, exist_ok=False)
    except OSError as error:
        parser.error(f"cannot create output directory {folder}: {error.strerror}")
    return folder


def capture(folder, fixtures, variants, *, extra=None, precheck=None, validated_report=None):
    budget = CaptureBudget([archive, folder])
    report = validated_report if validated_report is not None else {
        "complete": False, "budget": {"seconds": 600, "output_mib": 50}, "runs": [], "summary": {},
        **(extra or {}),
    }
    if report["complete"] or report["runs"]:
        raise ValueError("Timing needs a fresh report with no attempted runs")

    def save():
        (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        budget.check()

    def measure(fixture, options, variant, trial, fixture_env):
        binary, extra_env = variants[variant]
        extra_env = {**extra_env, **fixture_env}
        output = folder / f"{fixture}-{variant}-{trial}.csv"
        raw = output.with_suffix(".samples.csv")
        log = output.with_suffix(".log")
        command = ["taskset", "-c", "0,2,4,6,8,10", str(binary), "--asset-root",
                   str(root / "build/benchmark/assets"), "--workers", "6",
                   "--seed", "0", "--dt", str(1/120), "--warmup-frames", "120", "--measure-frames", "600",
                   "--repetitions", "3", "--run-index", str(trial), "--csv-output", str(output), *options]
        if "PLATFORMATOR_IMPLEMENTATION_PROFILE" in extra_env:
            extra_env = {**extra_env, "PLATFORMATOR_IMPLEMENTATION_PROFILE": str(output.with_suffix(".profile.csv"))}
        data = {
            "fixture": fixture, "variant": variant, "trial": trial,
            "command": shlex.join(["env", *(f"{k}={v}" for k, v in extra_env.items()), *command]),
            "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "raw": raw.name,
        }
        report["runs"].append(data)
        save()
        env = {key: value for key, value in os.environ.items()
               if not key.startswith(("PLATFORMATOR_AUDIT_", "PLATFORMATOR_IMPLEMENTATION_"))}
        with log.open("w") as stream, subprocess.Popen(command, env={**env, **extra_env},
                                                       stdout=stream, stderr=subprocess.STDOUT) as process:
            try:
                if budget.wait(process):
                    raise subprocess.CalledProcessError(process.returncode, command)
            finally:
                if process.poll() is None:
                    process.kill()
        manifest = load_manifest(output)
        assert manifest["build"]["configuration"] == "Release" and manifest["timing_eligible"]
        assert manifest["workers"] == 6 and manifest["measured_steps"] == 600 and not manifest["rendering"]
        with raw.open() as stream:
            rows = list(csv.DictReader(stream))
        assert len(rows) == 600
        counters = [[int(row[key]) for key in COUNTERS] for row in rows]
        data["workload"] = manifest["resolved_parameters"]
        data["build"] = manifest["build"]
        data["counters_sha256"] = hashlib.sha256(json.dumps(counters).encode()).hexdigest()
        data["means"] = {key: statistics.mean(int(row[key]) for row in rows)
                         for key in ("complete_step_ns", "narrow_phase_ns", "resolve_collisions_ns",
                                     "active_collision_count", "pending_narrow_phase_pair_count")}
        data["means"]["p95_step_ns"] = sorted(int(row["complete_step_ns"]) for row in rows)[569]
        profile = output.with_suffix(".profile.csv")
        if "PLATFORMATOR_IMPLEMENTATION_PROFILE" in extra_env:
            with profile.open() as stream:
                records = list(csv.DictReader(stream))
            assert len(records) == 720
            data["profile"] = profile.name
            for key in records[0]:
                data["means"][key] = statistics.mean(int(row[key]) for row in records[120:])
        log.unlink()
        save()
        print(f"{fixture} {variant} trial {trial}: {data['means']['complete_step_ns']/1e6:.3f} ms/step", flush=True)
        return data

    try:
        if precheck is not None:
            precheck(folder, budget)
        for fixture, options, strict, fixture_env in fixtures:
            records = {variant: [] for variant in variants}
            for trial in range(3):
                names = list(variants)
                rotated = trial % len(names)
                for variant in names[rotated:] + names[:rotated]:
                    records[variant].append(measure(fixture, options, variant, trial, fixture_env))
            runs = [data for by_variant in records.values() for data in by_variant]
            assert all(data["workload"] == runs[0]["workload"] for data in runs)
            if strict:
                assert all(data["counters_sha256"] == runs[0]["counters_sha256"] for data in runs)
            report["summary"][fixture] = {
                variant: {key: statistics.median(data["means"][key] for data in by_variant)
                          for key in by_variant[0]["means"]} for variant, by_variant in records.items()
            }
            save()
        report["complete"] = True
        save()
    except BaseException as error:
        report["failure"] = repr(error)
        (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        raise


def main():
    parser = argparse.ArgumentParser(description="Compare the saved narrow-phase binaries.")
    parser.add_argument("--output", type=Path, default=archive / "narrow-phase-reproduction",
                        help="New directory for the report and samples")
    folder = fresh_output(parser, parser.parse_args().output)
    capture(folder, [
        ("dense-trigger", ["--scenario", "narrow_phase", "--narrow-count", "200"], True, {}),
        ("solid-container", ["--scenario", "rigid_body_container", "--box-count", "500", "--circle-count", "500"], False, {}),
    ], {"before": (build / "before-narrow", {}), "fast-path": (build / "after-narrow", {})})


if __name__ == "__main__":
    main()
