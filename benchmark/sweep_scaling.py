#!/usr/bin/env python3
"""Independent schema-v3 runs; raw artifacts are retained beside the aggregate."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import subprocess
import time

SCOPES = ("complete_step", "physics", "cleanup", "scene_transition", "render", "broad_phase", "narrow_phase", "resolve_collisions")
COUNTERS = ("object_count", "occupied_cell_count", "candidate_pair_count", "pending_narrow_phase_pair_count", "active_collision_count",
            "awake_dynamic_body_count", "queued_add_count", "queued_sync_count", "queued_remove_count", "collision_enter_event_count",
            "collision_exit_event_count")
SCENARIOS = ("broad_phase", "broad_phase_stress", "narrow_phase", "rigid_body_container", "add_and_remove", "moving_add_and_remove", "persistent_contacts")


def is_add_remove_scenario(scenario):
    return scenario in {"add_and_remove", "moving_add_and_remove"}


def is_broad_phase_scenario(scenario):
    return scenario in {"broad_phase", "broad_phase_stress"}


def parse_counts(raw):
    counts = [int(token.strip()) for token in raw.split(",") if token.strip()]
    if not counts or any(count <= 0 for count in counts) or len(set(counts)) != len(counts):
        raise ValueError("Counts must be distinct positive integers")
    return counts


def parse_counts_from_step_arguments(step_size, ending_step, starting_step):
    start = step_size if starting_step is None else starting_step
    if step_size is None or ending_step is None or step_size <= 0 or ending_step <= 0 or start <= 0 or start > ending_step:
        raise ValueError("Provide positive --step-size/--ending-step and a valid starting step")
    return list(range(start, ending_step + 1, step_size))


def _number(value, field):
    number = float(value)
    if not math.isfinite(number) or number < 0:
        raise ValueError(f"Invalid finite nonnegative {field}: {value}")
    return number


def load_manifest(path):
    manifest = json.loads(Path(path).with_suffix(".manifest.json").read_text())
    required = {"schema_version", "workers", "seed", "dt", "measured_steps", "requested_measured_steps", "warmup_steps", "repetitions",
                "rendering", "render_samples", "complete_capture", "physical_invariants", "build", "machine", "dependencies", "boundaries", "scenario"}
    if not required <= manifest.keys() or manifest["schema_version"] != 3:
        raise ValueError("Missing/unknown benchmark manifest schema: expected schema 3; historical captures require their original tooling")
    if not manifest["complete_capture"] or manifest["measured_steps"] <= 0 or manifest["measured_steps"] != manifest["requested_measured_steps"]:
        raise ValueError("Benchmark capture is empty, truncated, or stopped before the requested steps")
    if manifest["workers"] <= 0 or manifest["seed"] < 0 or manifest["repetitions"] <= 0 or not math.isfinite(manifest["dt"]) or manifest["dt"] <= 0:
        raise ValueError("Invalid resolved run parameters")
    mode = manifest.get("validation_mode")
    if mode not in ("per-step", "boundary-only") or manifest.get("timing_eligible") is not (mode == "boundary-only"):
        raise ValueError("Missing or contradictory validation instrumentation label")
    build_fields = {"commit", "dirty", "source_sha256", "compiler", "flags", "configuration", "sanitizer", "work_probes", "debug_tools"}
    if not build_fields <= manifest["build"].keys():
        raise ValueError("Incomplete benchmark build provenance")
    fingerprint = manifest["build"]["source_sha256"]
    if not isinstance(fingerprint, str) or len(fingerprint) != 64 or any(c not in "0123456789abcdef" for c in fingerprint):
        raise ValueError("Invalid source fingerprint")
    if not manifest["machine"] or not manifest["dependencies"] or not set(SCOPES) <= manifest["boundaries"].keys():
        raise ValueError("Incomplete machine/dependency/scope metadata")
    if any(manifest["physical_invariants"].get(key) is not True for key in ("finite", "populations_valid", "supports_valid")):
        raise ValueError("Benchmark physical/population validation did not pass")
    return manifest


def parse_benchmark_csv(csv_path):
    """Reject legacy/partial/unknown metrics, missing metadata and incomplete raw data."""
    path = Path(csv_path)
    metrics, seen, sample_counts = {}, set(), {}
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if not {"schema_version", "category", "name", "samples", "avg", "median", "p95", "p99", "min", "max", "unit"} <= set(reader.fieldnames or ()):
            raise ValueError("Expected schema-v3 benchmark CSV; historical partial frame data is not complete_step")
        manifest = load_manifest(path)
        for row in reader:
            category, name = row["category"], row["name"]
            key = (category, name)
            allowed = SCOPES if category == "scope" else COUNTERS if category == "counter" else ()
            if row["schema_version"] != "3" or name not in allowed or key in seen:
                raise ValueError(f"Unknown/duplicate benchmark metric: {key}")
            seen.add(key)
            expected_unit = "ms" if category == "scope" else "count"
            if row["unit"] != expected_unit:
                raise ValueError(f"Unexpected units for {key}")
            samples = int(row["samples"])
            expected = manifest["render_samples"] if key == ("scope", "render") else manifest["measured_steps"]
            if samples != expected:
                raise ValueError(f"Sample count mismatch for {key}")
            sample_counts[key] = samples
            fields = ("avg", "median", "p95", "p99", "min", "max") if category == "scope" else ("avg", "min", "max")
            for field in fields:
                metrics[f"{category}.{name}.{field}"] = _number(row[field], f"{category}.{name}.{field}")
    required = {("scope", name) for name in SCOPES} | {("counter", name) for name in COUNTERS}
    if seen != required:
        raise ValueError(f"Missing benchmark metrics: {required - seen}")
    raw_counts = {"step": 0, "render": 0}
    totals = {name: 0 for name in SCOPES}
    with path.with_suffix(".samples.csv").open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        columns = {"schema_version", "sample_kind", "index"} | {name + "_ns" for name in SCOPES} | set(COUNTERS)
        if columns != set(reader.fieldnames or ()) or len(reader.fieldnames or ()) != len(columns):
            raise ValueError("Raw sample columns must exactly match benchmark schema 3")
        for row in reader:
            kind = row["sample_kind"]
            if row["schema_version"] != "3" or kind not in raw_counts or int(row["index"]) != raw_counts[kind]:
                raise ValueError("Invalid raw sample order/schema")
            raw_counts[kind] += 1
            for name in SCOPES:
                value = _number(row[name + "_ns"], name)
                if (kind == "render") == (name == "render"):
                    totals[name] += value
            for name in COUNTERS:
                _number(row[name], name)
    if raw_counts != {"step": manifest["measured_steps"], "render": manifest["render_samples"]}:
        raise ValueError("Raw sample count does not match its manifest")
    for name in SCOPES:
        count = manifest["render_samples"] if name == "render" else manifest["measured_steps"]
        average = totals[name] / count / 1e6 if count else 0
        if not math.isclose(average, metrics[f"scope.{name}.avg"], rel_tol=1e-6, abs_tol=1e-9):
            raise ValueError(f"Summary/raw average mismatch for {name}")
    return metrics


def apply_count_argument(base_command, args, count):
    command = list(base_command)
    if is_broad_phase_scenario(args.scenario):
        command += ["--broad-count", str(count)]
    elif args.scenario == "persistent_contacts":
        command += ["--contact-pair-count", str(count), "--scripted-endpoints", str(args.scripted_endpoints)]
    elif args.scenario == "narrow_phase":
        command += ["--narrow-count", str(count)]
    elif args.scenario == "rigid_body_container":
        command += ["--box-count", str(count // 2), "--circle-count", str(count - count // 2)]
    elif is_add_remove_scenario(args.scenario):
        command += ["--steady-count", str(args.steady_count), "--churn-count", str(count)]
        if args.box_mix is not None:
            command += ["--box-count", str(args.box_mix)]
        if args.circle_mix is not None:
            command += ["--circle-count", str(args.circle_mix)]
    else:
        raise ValueError(f"Unsupported scenario: {args.scenario}")
    return command


def sweep_parameter_name(scenario):
    if is_broad_phase_scenario(scenario):
        return "broad_count"
    return {"persistent_contacts": "contact_pair_count", "narrow_phase": "narrow_count", "rigid_body_container": "total_body_count",
            "add_and_remove": "churn_count", "moving_add_and_remove": "churn_count"}[scenario]


def build_result_row(args, count, metrics):
    # All required keys were checked; a missing metric must never become zero.
    row = {"schema_version": 3, "scenario": args.scenario, "sweep_parameter": sweep_parameter_name(args.scenario),
           "requested_count": count, "swept_value": count, "workers": args.workers, "seed": args.seed,
           "repetitions": len(metrics), "steady_count": args.steady_count if is_add_remove_scenario(args.scenario) else 0,
           "add_count": count if is_add_remove_scenario(args.scenario) else 0,
           "remove_count": count if is_add_remove_scenario(args.scenario) else 0}
    row["scripted_endpoints"] = args.scripted_endpoints if args.scenario == "persistent_contacts" else 0
    row["validation_mode"] = "per-step" if args.validate_invariants else "boundary-only"
    row["timing_eligible"] = not args.validate_invariants
    for name in SCOPES:
        averages = [run[f"scope.{name}.avg"] for run in metrics]
        row.update({f"{name}_avg_ms": statistics.mean(averages), f"{name}_p95_ms": statistics.mean(run[f"scope.{name}.p95"] for run in metrics),
                    f"{name}_run_median_ms": statistics.median(averages), f"{name}_run_stdev_ms": statistics.stdev(averages) if len(averages) > 1 else 0,
                    f"{name}_run_min_ms": min(averages), f"{name}_run_max_ms": max(averages)})
    for name in COUNTERS:
        row[f"{name}_avg"] = statistics.mean(run[f"counter.{name}.avg"] for run in metrics)
        row[f"{name}_min"] = min(run[f"counter.{name}.min"] for run in metrics)
        row[f"{name}_max"] = max(run[f"counter.{name}.max"] for run in metrics)
    return row


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", default="../bin/cleanup-benchmark/platformator_benchmark_runner")
    parser.add_argument("--scenario", required=True, choices=SCENARIOS)
    parser.add_argument("--counts")
    parser.add_argument("--scripted-endpoints", type=int, choices=(0, 1, 2), default=2)
    parser.add_argument("--step-size", type=int)
    parser.add_argument("--ending-step", type=int)
    parser.add_argument("--starting-step", type=int)
    parser.add_argument("--warmup-frames", type=int, default=120)
    parser.add_argument("--measure-frames", type=int, default=600)
    parser.add_argument("--dt", type=float, default=1 / 120)
    parser.add_argument("--workers", type=int, default=1)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--cooldown-seconds", type=float, default=0)
    parser.add_argument("--steady-count", type=int, default=1000)
    parser.add_argument("--box-mix", type=int)
    parser.add_argument("--circle-mix", type=int)
    parser.add_argument("--asset-root")
    parser.add_argument("--render", action="store_true")
    parser.add_argument("--raw-output-dir", "--raw-dir", dest="raw_dir")
    parser.add_argument("--validate-invariants", action="store_true", help="Per-step contact/support oracle; results are not performance measurements")
    parser.add_argument("--output", default="benchmark_scaling.csv")
    args = parser.parse_args(argv)
    if args.warmup_frames < 0 or args.measure_frames <= 0 or args.workers <= 0 or args.seed < 0 or args.repetitions < (1 if args.validate_invariants else 5) or args.steady_count <= 0:
        raise ValueError("Use nonnegative warmup/seed, positive steps/workers/population, and at least five independent repetitions")
    if not math.isfinite(args.dt) or args.dt <= 0 or not math.isfinite(args.cooldown_seconds) or args.cooldown_seconds < 0:
        raise ValueError("dt/cooldown must be finite, with positive dt and nonnegative cooldown")
    if any(value is not None and value < 0 for value in (args.box_mix, args.circle_mix)) or args.box_mix == args.circle_mix == 0:
        raise ValueError("Invalid box/circle mix")
    if args.counts and (args.step_size is not None or args.ending_step is not None or args.starting_step is not None):
        raise ValueError("Use --counts or step arguments, not both")
    counts = parse_counts(args.counts) if args.counts else parse_counts_from_step_arguments(args.step_size, args.ending_step, args.starting_step)
    runner = Path(args.runner).resolve()
    if not runner.is_file():
        raise FileNotFoundError(runner)
    output = Path(args.output).resolve()
    if output.exists():
        raise FileExistsError(f"Refusing to overwrite prior benchmark results: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    raw = Path(args.raw_dir).resolve() if args.raw_dir else output.with_suffix(".runs")
    base = [str(runner), "--scenario", args.scenario, "--warmup-frames", str(args.warmup_frames), "--measure-frames", str(args.measure_frames),
            "--dt", str(args.dt), "--workers", str(args.workers), "--seed", str(args.seed), "--repetitions", str(args.repetitions)]
    if args.asset_root:
        base += ["--asset-root", str(Path(args.asset_root).resolve())]
    if args.render:
        base += ["--render"]
    if args.validate_invariants:
        base += ["--validate-invariants"]
    rows = []
    for count in counts:
        results = []
        directory = raw / f"population-{count:06d}"
        directory.mkdir(parents=True, exist_ok=True)
        for repetition in range(args.repetitions):
            csv_path = directory / f"run-{repetition:02d}.csv"
            if csv_path.exists() or csv_path.with_suffix(".manifest.json").exists():
                raise FileExistsError(csv_path)
            command = apply_count_argument(base, args, count) + ["--run-index", str(repetition), "--csv-output", str(csv_path)]
            print(f"[Sweep] {args.scenario} count={count} run={repetition + 1}/{args.repetitions}", flush=True)
            with csv_path.with_suffix(".log").open("w") as log:
                subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
            metrics = parse_benchmark_csv(csv_path)
            manifest = load_manifest(csv_path)
            build = manifest["build"]
            if manifest.get("validation_mode") != ("per-step" if args.validate_invariants else "boundary-only"):
                raise ValueError("Validation instrumentation does not match the requested mode")
            if build.get("configuration") != "Release" or build.get("sanitizer") != "none" or build.get("work_probes") or build.get("debug_tools"):
                raise ValueError("Performance sweeps require Release without sanitizer/work/debug instrumentation")
            if (manifest["workers"] != args.workers or manifest["seed"] != args.seed or manifest["measured_steps"] != args.measure_frames
                    or manifest["warmup_steps"] != args.warmup_frames or manifest["repetitions"] != args.repetitions
                    or manifest.get("repetition_index") != repetition or not math.isclose(manifest["dt"], args.dt, rel_tol=1e-12)
                    or manifest["rendering"] != args.render):
                raise ValueError("Runner did not use the requested run parameters")
            results.append(metrics)
            if args.cooldown_seconds:
                time.sleep(args.cooldown_seconds)
        row = build_result_row(args, count, results)
        try:
            row["raw_directory"] = str(directory.relative_to(output.parent))
        except ValueError:
            row["raw_directory"] = str(directory)
        rows.append(row)
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"[Sweep] Wrote {output}; raw records retained in {raw}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
