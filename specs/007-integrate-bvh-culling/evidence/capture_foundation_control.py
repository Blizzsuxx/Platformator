#!/usr/bin/env python3
"""Replay the pinned schema-3 segmented/none control matrix for T017."""

import argparse
from pathlib import Path
import shlex
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[3]
EVIDENCE = Path(__file__).resolve().parent
SCENARIOS = (
    ("broad_phase", "100,500,1000", None),
    ("broad_phase_stress", "50,150,300", None),
    ("narrow_phase", "10,100,200", None),
    ("rigid_body_container", "100,500,1000", None),
    ("add_and_remove", "10,100,1000", None),
    ("moving_add_and_remove", "10,100,1000", None),
    ("persistent_contacts", "100", 0),
    ("persistent_contacts", "100", 2),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("validation", "timing"), required=True)
    parser.add_argument("--runner", default="bin/cleanup-benchmark/platformator_benchmark_runner")
    args = parser.parse_args()

    runner = Path(args.runner)
    if not runner.is_absolute():
        runner = ROOT / runner
    runner = runner.resolve()
    if not runner.is_file():
        parser.error(f"benchmark runner does not exist: {runner}")

    output_root = EVIDENCE / "foundation-control-captures" / args.mode
    if output_root.exists():
        parser.error(f"refusing to overwrite capture directory: {output_root}")
    output_root.mkdir(parents=True)

    measured, warmup, repetitions = (30, 10, 1) if args.mode == "validation" else (600, 120, 5)
    command_log = output_root / "commands.txt"
    with command_log.open("w", encoding="utf-8") as commands:
        for workers in (1, 6):
            worker_root = output_root / f"workers-{workers}"
            worker_root.mkdir()
            for scenario, counts, endpoints in SCENARIOS:
                suffix = f"-scripted-{endpoints}" if endpoints is not None else ""
                output = worker_root / f"{scenario}{suffix}.csv"
                command = [
                    sys.executable,
                    str(ROOT / "benchmark/sweep_scaling.py"),
                    "--runner",
                    str(runner),
                    "--scenario",
                    scenario,
                    "--counts",
                    counts,
                    "--workers",
                    str(workers),
                    "--seed",
                    "0",
                    "--dt",
                    str(1 / 120),
                    "--warmup-frames",
                    str(warmup),
                    "--measure-frames",
                    str(measured),
                    "--repetitions",
                    str(repetitions),
                    "--steady-count",
                    "1000",
                    "--output",
                    str(output),
                ]
                if endpoints is not None:
                    command.extend(("--scripted-endpoints", str(endpoints)))
                if args.mode == "validation":
                    command.append("--validate-invariants")
                commands.write(shlex.join(command) + "\n")
                commands.flush()
                print(f"[T017] {args.mode} workers={workers} scenario={scenario}{suffix}", flush=True)
                subprocess.run(command, cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
