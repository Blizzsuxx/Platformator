#!/usr/bin/env python3
"""Capture a pre-cleanup reference; standard library only, one process at a time."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
CASES = {
    "broad_phase": (100, 500, 1000),
    "broad_phase_stress": (50, 150, 300),
    "narrow_phase": (10, 100, 200),
    "rigid_body_container": (100, 500, 1000),
    "add_and_remove": (10, 100, 1000),
    "moving_add_and_remove": (10, 100, 1000),
}
SIZE_NAMES = ("small", "medium", "large")


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def command_output(command):
    return subprocess.check_output(command, cwd=ROOT, text=True).strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save_json(path, data):
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")


def describe(values):
    values = sorted(values)
    return {
        "mean": statistics.mean(values),
        "median": statistics.median(values),
        "p95": values[math.ceil(len(values) * 0.95) - 1],
        "min": values[0],
        "max": values[-1],
        "stdev": statistics.stdev(values) if len(values) > 1 else 0,
    }


def parameters(scenario, count):
    if scenario in ("broad_phase", "broad_phase_stress"):
        return ["--broad-count", str(count)], count
    if scenario == "narrow_phase":
        return ["--narrow-count", str(count)], count * 4
    if scenario == "rigid_body_container":
        return ["--box-count", str(count // 2), "--circle-count", str(count // 2)], count + 4
    return ["--steady-count", "1000", "--churn-count", str(count)], 1000


def physical_cpus():
    allowed = os.sched_getaffinity(0)
    topology = command_output(["lscpu", "-p=CPU,CORE,SOCKET"])
    selected = {}
    for line in topology.splitlines():
        if line.startswith("#"):
            continue
        cpu, core, socket = map(int, line.split(","))
        if cpu in allowed:
            selected.setdefault((socket, core), cpu)
    return list(selected.values())


def read_run(directory, expected, measured):
    with (directory / "legacy.csv").open() as f:
        legacy = {(r["category"], r["name"]): r for r in csv.DictReader(f)}
    if int(legacy["meta", "frame_count"]["avg"]) != measured:
        raise ValueError("Incorrect recorded frame count")
    for category in ("scope", "counter"):
        for (cat, name), row in legacy.items():
            if cat == category and not math.isfinite(float(row["avg"])):
                raise ValueError(f"Nonfinite {category}: {name}")
    population = legacy["counter", "object_count"]
    if float(population["min"]) != expected or float(population["max"]) != expected:
        raise ValueError(f"Unexpected population: {population}, expected {expected}")
    with (directory / "steps.csv").open() as f:
        steps = [int(r["host_step_ns"]) / 1e6 for r in csv.DictReader(f)]
    if len(steps) != measured or min(steps) <= 0:
        raise ValueError("Missing or invalid host-step samples")
    with (directory / "state.csv").open() as f:
        state = list(csv.DictReader(f))
    if len(state) != expected or len({r["name"] for r in state}) != expected:
        raise ValueError("Unexpected final collider population/names")
    fields = ("x", "y", "rotation", "vx", "vy", "angular_velocity")
    if any(not math.isfinite(float(r[k])) for r in state for k in fields):
        raise ValueError("Nonfinite final body state")
    bad_supports = [r for r in state if not 0 <= int(r["support_count"]) <= expected]
    result = {"host_step_" + k + "_ms": v for k, v in describe(steps).items()}
    for name in ("frame", "broad_phase", "narrow_phase", "resolve_collisions"):
        result["legacy_" + name + "_avg_ms"] = float(legacy["scope", name]["avg"])
        result["legacy_" + name + "_p95_ms"] = float(legacy["scope", name]["p95"])
    for (cat, name), row in legacy.items():
        if cat == "counter":
            result[name + "_avg"] = float(row["avg"])
    result["final_support_sum"] = sum(int(r["support_count"]) for r in state)
    result["final_support_max"] = max(int(r["support_count"]) for r in state)
    result["final_support_range_violation_count"] = len(bad_supports)
    result["final_sleeping_count"] = sum(int(r["sleeping"]) for r in state)
    canonical = json.dumps(sorted(state, key=lambda r: r["name"]), sort_keys=True).encode()
    result["final_state_sha256"] = hashlib.sha256(canonical).hexdigest()
    if result["host_step_mean_ms"] < result["legacy_frame_avg_ms"]:
        raise ValueError("Outer timer does not contain the legacy frame interval")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, default=ROOT / "bin/pre-cleanup-baseline/platformator_baseline_capture")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--warmup-frames", type=int, default=120)
    parser.add_argument("--measure-frames", type=int, default=600)
    parser.add_argument("--dt", type=float, default=1 / 120)
    parser.add_argument("--sizes", nargs="+", choices=SIZE_NAMES, default=list(SIZE_NAMES))
    parser.add_argument("--profiles", nargs="+", type=int, choices=(1, 6), default=[1, 6])
    parser.add_argument("--resume", action="store_true", help="Reuse completed captures without rerunning or hiding failures")
    args = parser.parse_args()
    if args.repetitions < 1 or args.measure_frames < 1 or args.warmup_frames < 0 or not math.isfinite(args.dt) or args.dt <= 0:
        parser.error("Invalid repetition, step or timestep setting")
    output, runner = args.output.resolve(), args.runner.resolve()
    output.mkdir(parents=True, exist_ok=args.resume)
    previous_manifest = json.loads((output / "manifest.json").read_text()) if args.resume else None
    cpus = physical_cpus()
    if len(cpus) < max(args.profiles):
        raise RuntimeError("Not enough distinct physical cores for the requested profiles")
    cpu_text = Path("/proc/cpuinfo").read_text()
    machine_files = {}
    for name in ("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "/sys/devices/system/cpu/cpufreq/boost", "/sys/firmware/acpi/platform_profile", "/sys/fs/cgroup/cpu.max"):
        if Path(name).exists():
            machine_files[name] = Path(name).read_text().strip()
    source_files = sorted(p for folder in ("src", "include", "cmake") for p in (ROOT / folder).rglob("*") if p.is_file())
    source_files += [ROOT / "CMakeLists.txt", ROOT / "CMakePresets.json"]
    source_hashes = {str(p.relative_to(ROOT)): digest(p) for p in source_files}
    save_json(output / "source-sha256.json", source_hashes)
    build_dir = runner.parent
    for name in ("CMakeCache.txt", "compile_commands.json"):
        (output / name).write_bytes((build_dir / name).read_bytes())
    (output / "ldd.txt").write_text(command_output(["ldd", str(runner)]) + "\n")
    (output / "lscpu.txt").write_text(command_output(["lscpu"]) + "\n")
    (output / "source.patch").write_text(command_output(["git", "diff", "HEAD", "--", "src", "include", "cmake", "CMakeLists.txt", "CMakePresets.json", "vendored"]))
    build_commands = command_output(["ninja", "-C", str(build_dir), "-t", "commands", "platformator_baseline_capture"])
    (output / "build-commands.txt").write_text(build_commands + "\n")
    manifest = {
        "started_utc": utc_now(), "source_commit": command_output(["git", "rev-parse", "HEAD"]),
        "git_status": command_output(["git", "status", "--short"]),
        "source_tree_sha256": hashlib.sha256(json.dumps(source_hashes, sort_keys=True).encode()).hexdigest(),
        "runner": str(runner), "runner_sha256": digest(runner),
        "harness_sha256": {name: digest(HERE / name) for name in ("capture.cpp", "CMakeLists.txt", "run.py")},
        "compiler": command_output(["c++", "--version"]), "cmake": command_output(["cmake", "--version"]),
        "machine": platform.uname()._asdict(), "cpu_model": next(l.split(":", 1)[1].strip() for l in cpu_text.splitlines() if l.startswith("model name")),
        "initial_loadavg": os.getloadavg(), "power_policy": machine_files,
        "available_affinity": sorted(os.sched_getaffinity(0)), "physical_cpu_selection": cpus,
        "profiles": args.profiles, "warmup_steps": args.warmup_frames, "measured_steps": args.measure_frames,
        "dt": args.dt, "repetitions": args.repetitions, "job_shuffle_seed": 1,
        "workload_seed": "none: existing fixed placement/velocity patterns",
        "build": "Release, -O3 -DNDEBUG, benchmark instrumentation ON, debug tools OFF, no sanitizers/LTO",
        "timing": {"legacy_frame": "Original recorder: excludes deferred destruction and post-step transient cleanup",
                   "host_step": "steady_clock around one public Runtime simulateFrame[WithCustomCallback] call, including callbacks, legacy instrumentation, transient cleanup and deferred destruction; excludes rendering/startup/shutdown/output"},
        "correctness": "Population, finite final state, plausible support counts and output structure only; existing physics/lifecycle bugs remain. Final snapshots are retained; timing is not a correctness certification.",
        "controlled_environment": {"SDL_AUDIODRIVER": "dummy", "SDL_VIDEODRIVER": "dummy", "SDL_RENDER_DRIVER": "software"},
    }
    if previous_manifest:
        for key in ("source_commit", "source_tree_sha256", "runner_sha256", "profiles", "warmup_steps", "measured_steps", "dt", "repetitions"):
            if previous_manifest[key] != manifest[key]:
                raise RuntimeError(f"Cannot resume with changed {key}")
        previous_manifest.setdefault("orchestrator_revisions", []).append({
            "resumed_utc": utc_now(), "run_py_sha256": digest(HERE / "run.py"),
            "reason": "Original support-range check stopped at job 72. Preserve that failure as an explicit physics diagnostic and continue unchanged timed captures. No engine, workload, binary, or measurement setting changed.",
        })
        manifest = previous_manifest
    save_json(output / "manifest.json", manifest)
    jobs = [(scenario, size, counts[SIZE_NAMES.index(size)], threads, rep)
            for scenario, counts in CASES.items() for size in args.sizes
            for threads in args.profiles for rep in range(1, args.repetitions + 1)]
    random.Random(1).shuffle(jobs)
    save_json(output / "schedule.json", jobs)
    records = []
    for index, (scenario, size, count, threads, rep) in enumerate(jobs, 1):
        run_id = f"{scenario}-{size}-n{count}-t{threads}-r{rep}"
        directory = output / "raw" / run_id
        existing_run = json.loads((directory / "manifest.json").read_text()) if args.resume and (directory / "manifest.json").exists() else None
        directory.mkdir(parents=True, exist_ok=bool(existing_run))
        extra, expected = parameters(scenario, count)
        affinity = cpus[:threads]
        command = ["taskset", "-c", ",".join(map(str, affinity)), str(runner),
                   "--scenario", scenario, *extra, "--warmup-frames", str(args.warmup_frames),
                   "--measure-frames", str(args.measure_frames), "--dt", repr(args.dt),
                   "--csv-output", str(directory / "legacy.csv")]
        env = os.environ.copy()
        env.update(manifest["controlled_environment"])
        env.update(PLATFORMATOR_BASELINE_WORKERS=str(threads),
                   PLATFORMATOR_BASELINE_STEPS_CSV=str(directory / "steps.csv"),
                   PLATFORMATOR_BASELINE_STATE_CSV=str(directory / "state.csv"))
        run = {"run_id": run_id, "scenario": scenario, "size": size, "count": count, "threads": threads,
               "repetition": rep, "command": command, "cpu_affinity": affinity,
               "started_utc": utc_now(), "loadavg_before": os.getloadavg(),
               "source_commit": manifest["source_commit"], "runner_sha256": manifest["runner_sha256"],
               "expected_population": expected}
        if existing_run:
            run = existing_run
        else:
            run["orchestrator_sha256"] = digest(HERE / "run.py")
            save_json(directory / "manifest.json", run)
            start = time.monotonic()
            with (directory / "stdout.log").open("w") as stdout, (directory / "stderr.log").open("w") as stderr:
                completed = subprocess.run(command, cwd=ROOT, env=env, stdout=stdout, stderr=stderr, timeout=300)
            run.update(returncode=completed.returncode, process_wall_seconds=time.monotonic() - start, finished_utc=utc_now())
            save_json(directory / "manifest.json", run)
        if run["returncode"]:
            raise RuntimeError(f"Run failed: {run_id}; see its stdout/stderr")
        stats = read_run(directory, expected, args.measure_frames)
        run["validation"] = "PASS: sample count, population, finite state, timer nesting"
        bad_count = stats["final_support_range_violation_count"]
        run["support_range_check"] = f"FAIL: {bad_count} bodies exceed physically possible supports" if bad_count else "PASS: range only; support correctness not established"
        save_json(directory / "manifest.json", run)
        record = {k: run[k] for k in ("run_id", "scenario", "size", "count", "threads", "repetition", "process_wall_seconds")}
        record.update(stats)
        records.append(record)
        save_json(output / "runs.json", records)
        print(f"[{index}/{len(jobs)}] {run_id}: host={stats['host_step_mean_ms']:.4f} ms, legacy={stats['legacy_frame_avg_ms']:.4f} ms" + (f"; SUPPORT CHECK FAIL ({bad_count} bodies)" if bad_count else "") + (" [retained]" if existing_run else ""), flush=True)
    with (output / "runs.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)
    summary = []
    for scenario, counts in CASES.items():
        for size in args.sizes:
            for threads in args.profiles:
                group = [r for r in records if (r["scenario"], r["size"], r["threads"]) == (scenario, size, threads)]
                row = {"scenario": scenario, "size": size, "count": counts[SIZE_NAMES.index(size)], "threads": threads, "repetitions": len(group)}
                for metric in ("host_step_mean_ms", "legacy_frame_avg_ms", "legacy_broad_phase_avg_ms", "legacy_narrow_phase_avg_ms", "legacy_resolve_collisions_avg_ms"):
                    row.update({metric + "_run_" + k: v for k, v in describe([r[metric] for r in group]).items()})
                row["distinct_final_states"] = len({r["final_state_sha256"] for r in group})
                row["final_support_sum_min"] = min(r["final_support_sum"] for r in group)
                row["final_support_sum_max"] = max(r["final_support_sum"] for r in group)
                row["support_range_failed_runs"] = sum(r["final_support_range_violation_count"] > 0 for r in group)
                summary.append(row)
    with (output / "summary.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    save_json(output / "summary.json", summary)
    manifest.update(finished_utc=utc_now(), completed_runs=len(records), final_loadavg=os.getloadavg())
    save_json(output / "manifest.json", manifest)
    print(f"Saved {len(records)} runs and {len(summary)} condition summaries to {output}")


if __name__ == "__main__":
    main()
