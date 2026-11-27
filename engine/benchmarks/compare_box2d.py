#!/usr/bin/env python3
"""Reproducible Box2D (v2.4.2 & v3.1.1) vs Platformator (+ Godot 4) benchmark runner.

Runs `box2d_v2_benchmark_runner` (1T) and `box2d_v3_benchmark_runner` (w=1, w=6)
pinned to the same physical cores as Platformator and Godot 4, merges Platformator
and Godot 4 baseline JSON/CSV results, writes a compact JSON report, and generates
EN/SR cross-engine comparison plots.

Usage:
    python3 engine/benchmarks/compare_box2d.py \
        --box2d-build-dir build/box2d-bench \
        --platformator-raw engine/benchmarks/results/thesis-20261121/raw \
        --godot-json engine/benchmarks/results/godot-comparison-20261122/godot_vs_platformator.json \
        --output-dir engine/benchmarks/results/cross-engine-20261122
"""
import argparse
import csv
import json
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
REPO_ROOT = BENCH_DIR.parent.parent
DEFAULT_BOX2D_BUILD = REPO_ROOT / "build" / "box2d-bench"
DEFAULT_PLAT_RAW = BENCH_DIR / "results" / "thesis-20261121" / "raw"
DEFAULT_GODOT_JSON = BENCH_DIR / "results" / "godot-comparison-20261122" / "godot_vs_platformator.json"

DEFAULT_SWEEPS = [
    ("broad_phase", [50, 100, 200, 500, 1000, 2000, 4000]),
    ("rigid_body_container", [50, 100, 200, 500, 1000, 1500, 2000]),
    ("ideal_world", [200, 500, 1000, 2000, 5000, 10000, 15000, 20000]),
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
        "avg_awake_bodies": round(statistics.mean(awakes), 1) if awakes else None,
        "avg_contacts": round(statistics.mean(pairs), 1) if pairs else None,
        "reps": len(avgs),
    }


def run_box2d_once(binary: Path, scenario: str, n: int, workers: int, args):
    cmd = []
    if args.pin_cores and shutil.which("taskset"):
        cmd.extend(["taskset", "-c", args.pin_cores])
    cmd.extend([
        str(binary),
        "--scenario", scenario,
        "--count", str(n),
        "--workers", str(workers),
        "--warmup-frames", str(args.warmup_frames),
        "--measure-frames", str(args.measure_frames),
        "--physics-fps", str(args.physics_fps),
        "--velocity-iterations", str(args.v2_velocity_iterations),
        "--position-iterations", str(args.v2_position_iterations),
        "--substeps", str(args.v3_substeps),
    ])
    t0 = time.monotonic()
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=args.max_run_seconds)
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


def run_box2d_point(binary: Path, scenario: str, n: int, workers: int, args, t_start: float):
    runs = []
    for _ in range(args.reps):
        if time.monotonic() - t_start > args.total_budget_seconds:
            return None, "total_budget_exceeded"
        data, elapsed, err = run_box2d_once(binary, scenario, n, workers, args)
        if err:
            return None, f"{err} (after {elapsed:.1f}s)"
        runs.append(data)
    return {
        "mean_ms": round(statistics.mean(d["mean_ms"] for d in runs), 4),
        "median_ms": round(statistics.mean(d["median_ms"] for d in runs), 4),
        "p95_ms": round(statistics.mean(d["p95_ms"] for d in runs), 4),
        "avg_awake_bodies": round(statistics.mean(d["avg_awake_bodies"] for d in runs), 1),
        "avg_contacts": round(statistics.mean(d["avg_contacts"] for d in runs), 1),
        "reps": len(runs),
        "raw_means": [d["mean_ms"] for d in runs],
    }, None


def parse_args():
    p = argparse.ArgumentParser(description="Benchmark Box2D v2.4.2 & v3.1.1 against Platformator (+ Godot 4).")
    p.add_argument("--box2d-build-dir", type=Path, default=DEFAULT_BOX2D_BUILD)
    p.add_argument("--platformator-raw", type=Path, default=DEFAULT_PLAT_RAW)
    p.add_argument("--godot-json", type=Path, default=DEFAULT_GODOT_JSON)
    p.add_argument("--output-dir", type=Path, required=True)
    p.add_argument("--reps", type=int, default=3)
    p.add_argument("--warmup-frames", type=int, default=120)
    p.add_argument("--measure-frames", type=int, default=600)
    p.add_argument("--physics-fps", type=int, default=120)
    p.add_argument("--v2-velocity-iterations", type=int, default=10)
    p.add_argument("--v2-position-iterations", type=int, default=3)
    p.add_argument("--v3-substeps", type=int, default=4)
    p.add_argument("--v3-workers", type=int, default=6)
    p.add_argument("--pin-cores", default="0,2,4,6,8,10")
    p.add_argument("--max-run-seconds", type=float, default=30.0)
    p.add_argument("--total-budget-seconds", type=float, default=360.0)
    p.add_argument("--skip-plots", action="store_true")
    return p.parse_args()


def main():
    args = parse_args()
    v2_bin = args.box2d_build_dir / "box2d_v2_benchmark_runner"
    v3_bin = args.box2d_build_dir / "box2d_v3_benchmark_runner"
    if not v2_bin.is_file() or not v3_bin.is_file():
        raise SystemExit(f"Box2D benchmark binaries not found in {args.box2d_build_dir}. Build engine/benchmarks/box2d first.")

    godot_data = None
    if args.godot_json and args.godot_json.is_file():
        with open(args.godot_json) as f:
            godot_data = json.load(f)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    out_json = args.output_dir / "cross_engine_comparison.json"

    t_start = time.monotonic()
    results = {
        "meta": {
            "reps": args.reps,
            "warmup": args.warmup_frames,
            "measure": args.measure_frames,
            "fps": args.physics_fps,
            "pin_cores": args.pin_cores,
            "box2d_unit_scale": 0.1,
            "box2d_v2_version": "2.4.2",
            "box2d_v2_iterations": {"velocity": args.v2_velocity_iterations, "position": args.v2_position_iterations},
            "box2d_v3_version": "3.1.1",
            "box2d_v3_substeps": args.v3_substeps,
            "box2d_v3_simd": "AVX2 (worker_pool)",
            "box2d_v3_workers": args.v3_workers,
            "platformator_raw": str(args.platformator_raw),
            "godot_json": str(args.godot_json) if godot_data else None,
        },
        "sweeps": {},
    }

    for scenario, counts in DEFAULT_SWEEPS:
        print(f"\n=== Scenario: {scenario} ===", flush=True)
        print(
            f"{'N':>7} | {'G4 Node':>8} | {'G4 Srv':>8} | {'Plat w1':>8} | {'Plat w6':>8} | "
            f"{'B2 v2.4':>8} | {'B2 v3w1':>8} | {'B2 v3w6':>8}",
            flush=True,
        )
        print("-" * 82, flush=True)

        godot_by_n = {}
        if godot_data and scenario in godot_data.get("sweeps", {}):
            godot_by_n = {r["n"]: r for r in godot_data["sweeps"][scenario]}

        rows = []
        for n in counts:
            p1 = load_platformator(args.platformator_raw, scenario, 1, n)
            p6 = load_platformator(args.platformator_raw, scenario, 6, n)
            g_row = godot_by_n.get(n, {})

            b2_v2, err_v2 = run_box2d_point(v2_bin, scenario, n, 1, args, t_start)
            b2_v3_w1, err_v3w1 = run_box2d_point(v3_bin, scenario, n, 1, args, t_start)
            b2_v3_w6, err_v3w6 = run_box2d_point(v3_bin, scenario, n, args.v3_workers, args, t_start)

            row = {
                "n": n,
                "platformator_w1": p1,
                "platformator_w6": p6,
                "godot_node": g_row.get("godot"),
                "godot_server": g_row.get("godot_server"),
                "box2d_v2": b2_v2,
                "box2d_v3_w1": b2_v3_w1,
                "box2d_v3_w6": b2_v3_w6,
            }
            if err_v2:
                row["box2d_v2_note"] = err_v2
            if err_v3w1:
                row["box2d_v3_w1_note"] = err_v3w1
            if err_v3w6:
                row["box2d_v3_w6_note"] = err_v3w6

            def fmt(d):
                return f"{d['mean_ms']:8.3f}" if d else "     -  "

            print(
                f"{n:>7} | {fmt(row['godot_node'])} | {fmt(row['godot_server'])} | "
                f"{fmt(p1)} | {fmt(p6)} | {fmt(b2_v2)} | {fmt(b2_v3_w1)} | {fmt(b2_v3_w6)}",
                flush=True,
            )
            rows.append(row)
        results["sweeps"][scenario] = rows

    results["elapsed_seconds"] = round(time.monotonic() - t_start, 2)
    with open(out_json, "w") as f:
        json.dump(results, f, indent=2)
    print(f"\nSaved cross-engine results in {results['elapsed_seconds']}s -> {out_json}", flush=True)

    if not args.skip_plots:
        plot_script = BENCH_DIR / "plot_cross_engine.py"
        if plot_script.is_file():
            subprocess.run(
                [sys.executable, str(plot_script), "--input", str(out_json), "--output-dir", str(args.output_dir / "plots")],
                check=True,
            )


if __name__ == "__main__":
    main()
