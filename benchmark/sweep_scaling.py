#!/usr/bin/env python3
import argparse
import csv
import os
import subprocess
import tempfile
from time import sleep
from typing import Dict, List


def parse_counts(raw: str) -> List[int]:
    counts: List[int] = []
    for token in raw.split(','):
        token = token.strip()
        if not token:
            continue
        value = int(token)
        if value <= 0:
            raise ValueError("All counts must be positive integers.")
        counts.append(value)

    if not counts:
        raise ValueError("At least one count must be provided.")

    return counts

def parse_counts_from_step_arguments(step_size: int, ending_step: int, starting_step: int) -> List[int]:
    if step_size <= 0:
        raise ValueError("--step-size must be a positive integer.")
    if ending_step <= 0:
        raise ValueError("--ending-step must be a positive integer.")
    if starting_step <= 0:
        raise ValueError("--starting-step must be a positive integer.")
    if starting_step > ending_step:
        raise ValueError("--starting-step must be less than or equal to --ending-step.")

    counts: List[int] = []
    current = starting_step
    while current <= ending_step:
        counts.append(current)
        current += step_size

    return counts


def parse_benchmark_csv(csv_path: str) -> Dict[str, float]:
    metrics: Dict[str, float] = {}
    with open(csv_path, newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            category = row.get("category", "")
            name = row.get("name", "")
            avg_value = row.get("avg", "")
            if not category or not name or not avg_value:
                continue

            key = f"{category}.{name}.avg"
            metrics[key] = float(avg_value)

            median_value = row.get("median", "")
            p95_value = row.get("p95", "")
            p99_value = row.get("p99", "")
            if median_value:
                metrics[f"{category}.{name}.median"] = float(median_value)
            if p95_value:
                metrics[f"{category}.{name}.p95"] = float(p95_value)
            if p99_value:
                metrics[f"{category}.{name}.p99"] = float(p99_value)

            # Export min/max for counters because they are useful for sanity checks.
            min_value = row.get("min", "")
            max_value = row.get("max", "")
            if min_value:
                metrics[f"{category}.{name}.min"] = float(min_value)
            if max_value:
                metrics[f"{category}.{name}.max"] = float(max_value)

    return metrics


def apply_count_argument(base_command: List[str], scenario: str, count: int) -> List[str]:
    command = list(base_command)
    if scenario == "broad_phase":
        command.extend(["--broad-count", str(count)])
    elif scenario == "narrow_phase":
        command.extend(["--narrow-count", str(count)])
    elif scenario == "rigid_body_container":
        # For rigid body scaling, we vary only box count and keep circles at zero.
        command.extend(["--box-count", str(count), "--circle-count", "0"])
    else:
        raise ValueError(f"Unsupported scenario: {scenario}")

    return command


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run Platformator benchmark scenarios for multiple object counts and write one combined CSV."
    )
    parser.add_argument(
        "--runner",
        default="./bin/benchmark-release/platformator_benchmark_runner",
        help="Path to platformator_benchmark_runner binary.",
    )
    parser.add_argument(
        "--scenario",
        required=True,
        choices=["broad_phase", "narrow_phase", "rigid_body_container"],
        help="Benchmark scenario to sweep.",
    )
    parser.add_argument(
        "--step-size",
        required=True,
        type=int,
        help="Step size for the sweep.",
    )
    parser.add_argument(
        "--ending-step",
        required=True,
        type=int,
        help="Ending step for the sweep.",
    )
    parser.add_argument(
        "--starting-step",
        type=int,
        help="Starting step for the sweep (default: same as --step).",
    )
    parser.add_argument("--warmup-frames", type=int, default=120)
    parser.add_argument("--measure-frames", type=int, default=600)
    parser.add_argument("--dt", type=float, default=1.0 / 120.0)
    parser.add_argument(
        "--output",
        default="benchmark_scaling.csv",
        help="Path for the combined output CSV.",
    )

    args = parser.parse_args()

    if args.warmup_frames < 0:
        raise ValueError("--warmup-frames must be >= 0")
    if args.measure_frames <= 0:
        raise ValueError("--measure-frames must be > 0")
    if args.dt <= 0.0:
        raise ValueError("--dt must be > 0")

    counts = parse_counts_from_step_arguments(args.step_size, args.ending_step, args.starting_step)

    if not os.path.isfile(args.runner):
        raise FileNotFoundError(f"Runner not found: {args.runner}")

    base_command = [
        args.runner,
        "--scenario",
        args.scenario,
        "--warmup-frames",
        str(args.warmup_frames),
        "--measure-frames",
        str(args.measure_frames),
        "--dt",
        str(args.dt),
    ]

    aggregate_rows: List[Dict[str, float]] = []

    for count in counts:
        with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as temp_file:
            temp_csv_path = temp_file.name

        command = apply_count_argument(base_command, args.scenario, count)
        command.extend(["--csv-output", temp_csv_path])

        print(f"[Sweep] Running count={count}")
        subprocess.run(command)
        sleep(1)  # Small delay to ensure file is flushed.

        metrics = parse_benchmark_csv(temp_csv_path)
        os.unlink(temp_csv_path)

        row: Dict[str, float] = {
            "scenario": args.scenario,
            "requested_count": float(count),
            "frame_avg_ms": metrics.get("scope.frame.avg", 0.0),
            "frame_p95_ms": metrics.get("scope.frame.p95", 0.0),
            "broad_phase_avg_ms": metrics.get("scope.broad_phase.avg", 0.0),
            "broad_phase_p95_ms": metrics.get("scope.broad_phase.p95", 0.0),
            "narrow_phase_avg_ms": metrics.get("scope.narrow_phase.avg", 0.0),
            "narrow_phase_p95_ms": metrics.get("scope.narrow_phase.p95", 0.0),
            "resolve_collisions_avg_ms": metrics.get("scope.resolve_collisions.avg", 0.0),
            "resolve_collisions_p95_ms": metrics.get("scope.resolve_collisions.p95", 0.0),
            "reported_object_count_avg": metrics.get("counter.object_count.avg", 0.0),
            "candidate_pair_count_avg": metrics.get("counter.candidate_pair_count.avg", 0.0),
            "pending_narrow_phase_pair_count_avg": metrics.get("counter.pending_narrow_phase_pair_count.avg", 0.0),
            "active_collision_count_avg": metrics.get("counter.active_collision_count.avg", 0.0),
            "occupied_cell_count_avg": metrics.get("counter.occupied_cell_count.avg", 0.0),
            "queued_sync_count_avg": metrics.get("counter.queued_sync_count.avg", 0.0),
        }
        aggregate_rows.append(row)

    field_names = [
        "scenario",
        "requested_count",
        "frame_avg_ms",
        "frame_p95_ms",
        "broad_phase_avg_ms",
        "broad_phase_p95_ms",
        "narrow_phase_avg_ms",
        "narrow_phase_p95_ms",
        "resolve_collisions_avg_ms",
        "resolve_collisions_p95_ms",
        "reported_object_count_avg",
        "candidate_pair_count_avg",
        "pending_narrow_phase_pair_count_avg",
        "active_collision_count_avg",
        "occupied_cell_count_avg",
        "queued_sync_count_avg",
    ]

    with open(args.output, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=field_names)
        writer.writeheader()
        writer.writerows(aggregate_rows)

    print(f"[Sweep] Wrote combined CSV: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
