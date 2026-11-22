#!/usr/bin/env python3
"""Reproducible Godot 4 vs Platformator benchmark comparison runner.

Runs Godot 4 (GodotPhysics2D) using the clean headless comparison project in
`engine/benchmarks/godot/comparison/` on the exact same scenarios, layouts,
physics constants, and CPU core pinning as Platformator, and compares against
either an existing Platformator sweep directory (`--platformator-raw`) or fresh
runs of `platformator_benchmark_runner`.

Usage:
    python3 engine/benchmarks/compare_godot.py \
        --platformator-raw engine/benchmarks/results/thesis-20261121/raw \
        --output-dir engine/benchmarks/results/godot-comparison-20261122
"""
import argparse
import csv
import json
import os
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
DEFAULT_GODOT_PROJECT = BENCH_DIR / "godot" / "comparison"
DEFAULT_PLAT_RAW = BENCH_DIR / "results" / "thesis-20261121" / "raw"

DEFAULT_SWEEPS = [
    ("broad_phase", [50, 100, 200, 500, 1000, 2000, 4000]),
    ("rigid_body_container", [50, 100, 200, 500, 1000, 1500, 2000]),
    ("ideal_world", [200, 500, 1000, 2000, 5000, 10000, 15000, 20000]),
]

DEFAULT_SERVER_SPOT_CHECKS = [
    ("broad_phase", 4000),
    ("rigid_body_container", 2000),
    ("ideal_world", 5000),
]


def load_platformator(plat_raw: Path, scenario: str, workers: int, n: int):
    pop_dir = plat_raw / f"{scenario}_w{workers}" / f"population-{n:06d}"
    if not pop_dir.is_dir():
        return None
    avgs, meds, p95s, awakes, pairs = [], [], [], [], []
    for path in sorted(pop_dir.glob("run-*.csv")):
        if path.name.endswith(".samples.csv"):
            continue
        with open(path, newline="") as f:
            for row in csv.DictReader(f):
                if row["category"] == "scope" and row["name"] == "complete_step":
                    avgs.append(float(row["avg"]))
                    meds.append(float(row["median"]))
                    p95s.append(float(row["p95"]))
                elif row["category"] == "counter" and row["name"] == "awake_dynamic_body_count":
                    awakes.append(float(row["avg"]))
                elif row["category"] == "counter" and row["name"] == "active_collision_count":
                    pairs.append(float(row["avg"]))
    if not avgs:
        return None
    return {
        "mean_ms": round(statistics.mean(avgs), 4),
        "median_ms": round(statistics.mean(meds), 4),
        "p95_ms": round(statistics.mean(p95s), 4),
        "avg_awake": round(statistics.mean(awakes), 1) if awakes else None,
        "avg_active_collisions": round(statistics.mean(pairs), 1) if pairs else None,
        "reps": len(avgs),
    }


def detect_godot_version(godot_bin: str) -> str:
    try:
        out = subprocess.check_output([godot_bin, "--version"], text=True, stderr=subprocess.STDOUT, timeout=5)
        return out.strip().splitlines()[0]
    except Exception:
        return "unknown"


def run_godot_once(
    godot_bin: str,
    godot_project: Path,
    scenario: str,
    n: int,
    warmup: int,
    measure: int,
    fps: int,
    pin_cores: str,
    max_run_seconds: float,
    server_only: bool = False,
):
    env = os.environ.copy()
    env["XDG_DATA_HOME"] = "/tmp/godot_bench_data"
    env["XDG_CONFIG_HOME"] = "/tmp/godot_bench_cfg"
    os.makedirs(env["XDG_DATA_HOME"], exist_ok=True)
    os.makedirs(env["XDG_CONFIG_HOME"], exist_ok=True)

    cmd = []
    if pin_cores and shutil.which("taskset"):
        cmd.extend(["taskset", "-c", pin_cores])
    cmd.extend([
        godot_bin, "--headless", "--fixed-fps", str(fps),
        "--path", str(godot_project), "res://bench.tscn", "--",
        "--scenario", scenario,
        "--count", str(n),
        "--warmup-frames", str(warmup),
        "--measure-frames", str(measure),
        "--physics-fps", str(fps),
    ])
    if server_only:
        cmd.append("--server-only")

    t0 = time.monotonic()
    try:
        proc = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=max_run_seconds)
    except subprocess.TimeoutExpired:
        return None, time.monotonic() - t0, "timeout"
    elapsed = time.monotonic() - t0
    if proc.returncode != 0:
        return None, elapsed, f"exit_{proc.returncode}: {proc.stderr[-200:]}"

    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("{") and line.endswith("}"):
            return json.loads(line), elapsed, None
    return None, elapsed, f"no_json: {proc.stdout[-200:]}"


def run_godot_point(args, scenario: str, n: int, reps: int, server_only: bool, t_start: float):
    runs = []
    for _ in range(reps):
        if time.monotonic() - t_start > args.total_budget_seconds:
            return None, "total_budget_exceeded"
        data, elapsed, err = run_godot_once(
            args.godot_bin,
            args.godot_project,
            scenario,
            n,
            args.warmup_frames,
            args.measure_frames,
            args.physics_fps,
            args.pin_cores,
            args.max_run_seconds,
            server_only=server_only,
        )
        if err:
            return None, f"{err} (after {elapsed:.1f}s)"
        runs.append(data)
    return {
        "mean_ms": round(statistics.mean(d["mean_ms"] for d in runs), 4),
        "median_ms": round(statistics.mean(d["median_ms"] for d in runs), 4),
        "p95_ms": round(statistics.mean(d["p95_ms"] for d in runs), 4),
        "avg_active_bodies": round(statistics.mean(d["avg_active_bodies"] for d in runs), 1),
        "avg_collision_pairs": round(statistics.mean(d["avg_collision_pairs"] for d in runs), 1),
        "reps": len(runs),
        "raw_means": [d["mean_ms"] for d in runs],
    }, None


def parse_args():
    p = argparse.ArgumentParser(description="Compare Godot 4 (GodotPhysics2D) against Platformator.")
    p.add_argument("--godot-bin", default="godot", help="Path to Godot 4 binary (default: godot)")
    p.add_argument("--godot-project", type=Path, default=DEFAULT_GODOT_PROJECT)
    p.add_argument("--platformator-raw", type=Path, default=DEFAULT_PLAT_RAW)
    p.add_argument("--output-dir", type=Path, required=True)
    p.add_argument("--modes", default="node,server", help="Comma-separated Godot modes to sweep: node,server (default: node,server)")
    p.add_argument("--merge-existing", action="store_true", help="Preserve existing JSON entries for modes not listed in --modes")
    p.add_argument("--reps", type=int, default=3)
    p.add_argument("--warmup-frames", type=int, default=120)
    p.add_argument("--measure-frames", type=int, default=600)
    p.add_argument("--physics-fps", type=int, default=120)
    p.add_argument("--pin-cores", default="0,2,4,6,8,10", help="CPU list for taskset (empty to disable)")
    p.add_argument("--max-run-seconds", type=float, default=30.0)
    p.add_argument("--total-budget-seconds", type=float, default=540.0)
    p.add_argument("--skip-plots", action="store_true")
    return p.parse_args()


def main():
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    out_json = args.output_dir / "godot_vs_platformator.json"
    modes = {m.strip().lower() for m in args.modes.split(",") if m.strip()}
    run_node = "node" in modes
    run_server = "server" in modes

    existing = None
    if args.merge_existing and out_json.is_file():
        with open(out_json) as f:
            existing = json.load(f)

    t_start = time.monotonic()
    godot_version = detect_godot_version(args.godot_bin)
    results = {
        "meta": {
            "godot_version": godot_version,
            "reps": args.reps,
            "warmup": args.warmup_frames,
            "measure": args.measure_frames,
            "fps": args.physics_fps,
            "pin_cores": args.pin_cores,
            "platformator_raw": str(args.platformator_raw),
        },
        "sweeps": {},
        "server_spot_checks": dict(existing.get("server_spot_checks", {})) if existing else {},
    }

    for scenario, counts in DEFAULT_SWEEPS:
        print(f"\n=== Scenario: {scenario} ===", flush=True)
        print(
            f"{'N':>7} | {'G4 Node (ms)':>12} | {'G4 Srv (ms)':>11} | {'Plat w=1':>9} | {'Plat w=6':>9} | {'Node/P6':>7} | {'Srv/P6':>7}",
            flush=True,
        )
        print("-" * 78, flush=True)
        existing_by_n = {}
        if existing and scenario in existing.get("sweeps", {}):
            existing_by_n = {r["n"]: r for r in existing["sweeps"][scenario]}

        rows = []
        node_stopped = False
        server_stopped = False
        for n in counts:
            prev = existing_by_n.get(n, {})
            p1 = prev.get("platformator_w1") or load_platformator(args.platformator_raw, scenario, 1, n)
            p6 = prev.get("platformator_w6") or load_platformator(args.platformator_raw, scenario, 6, n)
            row = {"n": n, "platformator_w1": p1, "platformator_w6": p6}

            # Godot Node2D sweep
            if run_node:
                if node_stopped:
                    row["godot"] = None
                    row["godot_note"] = "skipped_after_timeout"
                else:
                    g, err = run_godot_point(args, scenario, n, args.reps, False, t_start)
                    if err:
                        row["godot"] = None
                        row["godot_note"] = err
                        if "timeout" in err or "budget" in err:
                            node_stopped = True
                    else:
                        row["godot"] = g
            else:
                row["godot"] = prev.get("godot")
                if "godot_note" in prev:
                    row["godot_note"] = prev["godot_note"]

            # Godot PhysicsServer2D (--server-only) sweep
            if run_server:
                if server_stopped:
                    row["godot_server"] = None
                    row["godot_server_note"] = "skipped_after_timeout"
                else:
                    gs, err = run_godot_point(args, scenario, n, args.reps, True, t_start)
                    if err:
                        row["godot_server"] = None
                        row["godot_server_note"] = err
                        if "timeout" in err or "budget" in err:
                            server_stopped = True
                    else:
                        row["godot_server"] = gs
                        if (scenario, n) in DEFAULT_SERVER_SPOT_CHECKS:
                            results["server_spot_checks"][f"{scenario}_{n}"] = gs
            else:
                row["godot_server"] = prev.get("godot_server")
                if "godot_server_note" in prev:
                    row["godot_server_note"] = prev["godot_server_note"]

            gn_s = f"{row['godot']['mean_ms']:.3f}" if row.get("godot") else (row.get("godot_note", "-")[:12])
            gs_s = f"{row['godot_server']['mean_ms']:.3f}" if row.get("godot_server") else (row.get("godot_server_note", "-")[:11])
            p1_s = f"{p1['mean_ms']:.3f}" if p1 else "-"
            p6_s = f"{p6['mean_ms']:.3f}" if p6 else "-"
            rn6 = f"{row['godot']['mean_ms'] / p6['mean_ms']:.2f}x" if (row.get("godot") and p6) else "-"
            rs6 = f"{row['godot_server']['mean_ms'] / p6['mean_ms']:.2f}x" if (row.get("godot_server") and p6) else "-"
            print(f"{n:>7} | {gn_s:>12} | {gs_s:>11} | {p1_s:>9} | {p6_s:>9} | {rn6:>7} | {rs6:>7}", flush=True)
            rows.append(row)
        results["sweeps"][scenario] = rows

    results["elapsed_seconds"] = round(time.monotonic() - t_start, 2)
    with open(out_json, "w") as f:
        json.dump(results, f, indent=2)
    print(f"\nSaved results in {results['elapsed_seconds']}s -> {out_json}", flush=True)

    if not args.skip_plots:
        plot_script = BENCH_DIR / "plot_godot_comparison.py"
        if plot_script.is_file():
            subprocess.run([sys.executable, str(plot_script), "--input", str(out_json), "--output-dir", str(args.output_dir / "plots")], check=True)


if __name__ == "__main__":
    main()
