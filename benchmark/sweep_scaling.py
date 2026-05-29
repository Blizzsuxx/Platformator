#!/usr/bin/env python3
import argparse
import csv
import os
import subprocess
import tempfile
import time
from typing import Dict, List, Optional


def is_add_remove_scenario(scenario: str) -> bool:
    return scenario in {"add_and_remove", "moving_add_and_remove"}


def is_broad_phase_scenario(scenario: str) -> bool:
    return scenario in {"broad_phase", "broad_phase_stress"}


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

def parse_counts_from_step_arguments(step_size: int, ending_step: int, starting_step: Optional[int]) -> List[int]:
    if step_size <= 0:
        raise ValueError("--step-size must be a positive integer.")
    if ending_step <= 0:
        raise ValueError("--ending-step must be a positive integer.")
    if starting_step is None:
        starting_step = step_size
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


def apply_count_argument(base_command: List[str], args: argparse.Namespace, count: int) -> List[str]:
    command = list(base_command)
    if is_broad_phase_scenario(args.scenario):
        command.extend(["--broad-count", str(count)])
    elif args.scenario == "narrow_phase":
        command.extend(["--narrow-count", str(count)])
    elif args.scenario == "rigid_body_container":
        half_count = count // 2
        command.extend(["--box-count", str(half_count), "--circle-count", str(half_count)])
    elif is_add_remove_scenario(args.scenario):
        command.extend(["--steady-count", str(args.steady_count), "--churn-count", str(count)])
        if args.box_mix is not None:
            command.extend(["--box-count", str(args.box_mix)])
        if args.circle_mix is not None:
            command.extend(["--circle-count", str(args.circle_mix)])
    else:
        raise ValueError(f"Unsupported scenario: {args.scenario}")

    return command


def sweep_parameter_name(scenario: str) -> str:
    if is_broad_phase_scenario(scenario):
        return "broad_count"
    if scenario == "narrow_phase":
        return "narrow_count"
    if scenario == "rigid_body_container":
        return "total_body_count"
    if is_add_remove_scenario(scenario):
        return "churn_count"

    raise ValueError(f"Unsupported scenario: {scenario}")


def build_result_row(args: argparse.Namespace, count: int, metrics: Dict[str, float]) -> Dict[str, float]:
    steady_count = float(args.steady_count if is_add_remove_scenario(args.scenario) else 0)
    add_count = float(count if is_add_remove_scenario(args.scenario) else 0)
    remove_count = float(count if is_add_remove_scenario(args.scenario) else 0)

    return {
        "scenario": args.scenario,
        "sweep_parameter": sweep_parameter_name(args.scenario),
        "requested_count": float(count),
        "swept_value": float(count),
        "steady_count": steady_count,
        "add_count": add_count,
        "remove_count": remove_count,
        "frame_avg_ms": metrics.get("scope.frame.avg", 0.0),
        "frame_p95_ms": metrics.get("scope.frame.p95", 0.0),
        "broad_phase_avg_ms": metrics.get("scope.broad_phase.avg", 0.0),
        "broad_phase_p95_ms": metrics.get("scope.broad_phase.p95", 0.0),
        "narrow_phase_avg_ms": metrics.get("scope.narrow_phase.avg", 0.0),
        "narrow_phase_p95_ms": metrics.get("scope.narrow_phase.p95", 0.0),
        "resolve_collisions_avg_ms": metrics.get("scope.resolve_collisions.avg", 0.0),
        "resolve_collisions_p95_ms": metrics.get("scope.resolve_collisions.p95", 0.0),
        "reported_object_count_avg": metrics.get("counter.object_count.avg", 0.0),
        "reported_object_count_min": metrics.get("counter.object_count.min", 0.0),
        "reported_object_count_max": metrics.get("counter.object_count.max", 0.0),
        "candidate_pair_count_avg": metrics.get("counter.candidate_pair_count.avg", 0.0),
        "pending_narrow_phase_pair_count_avg": metrics.get("counter.pending_narrow_phase_pair_count.avg", 0.0),
        "active_collision_count_avg": metrics.get("counter.active_collision_count.avg", 0.0),
        "occupied_cell_count_avg": metrics.get("counter.occupied_cell_count.avg", 0.0),
        "queued_add_count_avg": metrics.get("counter.queued_add_count.avg", 0.0),
        "queued_remove_count_avg": metrics.get("counter.queued_remove_count.avg", 0.0),
        "queued_sync_count_avg": metrics.get("counter.queued_sync_count.avg", 0.0),
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run Platformator benchmark scenarios for multiple object counts and write one combined CSV."
    )
    parser.add_argument(
        "--runner",
        default="../bin/benchmark-release/platformator_benchmark_runner",
        help="Path to platformator_benchmark_runner binary.",
    )
    parser.add_argument(
        "--scenario",
        required=True,
        choices=["broad_phase", "broad_phase_stress", "narrow_phase", "rigid_body_container", "add_and_remove", "moving_add_and_remove"],
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
        help="Starting step for the sweep (default: same as --step-size).",
    )
    parser.add_argument("--warmup-frames", type=int, default=120)
    parser.add_argument("--measure-frames", type=int, default=600)
    parser.add_argument("--dt", type=float, default=1.0 / 120.0)
    parser.add_argument(
        "--cooldown-seconds",
        type=float,
        default=0.0,
        help="Optional delay between sweep iterations to reduce thermal/scheduling drift on saturated machines.",
    )
    parser.add_argument(
        "--steady-count",
        type=int,
        default=1000,
        help="Stable live body count for the add/remove scenarios (default: 1000).",
    )
    parser.add_argument(
        "--box-mix",
        type=int,
        help="Optional box mix weight for add/remove scenarios. Passed through as --box-count.",
    )
    parser.add_argument(
        "--circle-mix",
        type=int,
        help="Optional circle mix weight for add/remove scenarios. Passed through as --circle-count.",
    )
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
    if args.cooldown_seconds < 0.0:
        raise ValueError("--cooldown-seconds must be >= 0")
    if args.steady_count <= 0:
        raise ValueError("--steady-count must be > 0")
    if args.box_mix is not None and args.box_mix < 0:
        raise ValueError("--box-mix must be >= 0")
    if args.circle_mix is not None and args.circle_mix < 0:
        raise ValueError("--circle-mix must be >= 0")
    if is_add_remove_scenario(args.scenario) and args.box_mix == 0 and args.circle_mix == 0:
        raise ValueError("--box-mix and --circle-mix cannot both be zero for the add/remove scenarios.")

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

        command = apply_count_argument(base_command, args, count)
        command.extend(["--csv-output", temp_csv_path])

        print(f"[Sweep] Running count={count}")
        subprocess.run(command, check=True)

        metrics = parse_benchmark_csv(temp_csv_path)
        os.unlink(temp_csv_path)

        aggregate_rows.append(build_result_row(args, count, metrics))

        if args.cooldown_seconds > 0.0 and count != counts[-1]:
            time.sleep(args.cooldown_seconds)

    field_names = [
        "scenario",
        "sweep_parameter",
        "requested_count",
        "swept_value",
        "steady_count",
        "add_count",
        "remove_count",
        "frame_avg_ms",
        "frame_p95_ms",
        "broad_phase_avg_ms",
        "broad_phase_p95_ms",
        "narrow_phase_avg_ms",
        "narrow_phase_p95_ms",
        "resolve_collisions_avg_ms",
        "resolve_collisions_p95_ms",
        "reported_object_count_avg",
        "reported_object_count_min",
        "reported_object_count_max",
        "candidate_pair_count_avg",
        "pending_narrow_phase_pair_count_avg",
        "active_collision_count_avg",
        "occupied_cell_count_avg",
        "queued_add_count_avg",
        "queued_remove_count_avg",
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
