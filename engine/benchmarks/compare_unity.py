#!/usr/bin/env python3
"""Reproducible Unity 6 (Physics2D, 1T & 6T Jobs) benchmark comparison runner.

Runs the standalone Linux64 Release Player built from `engine/benchmarks/unity/`
(`build/unity-bench/UnityBench.x86_64`) on the exact same 3 scenarios, constants,
and pinned 6 physical cores as Platformator, Godot 4.7, and Box2D (v2.4.2 & v3.1.1),
and merges the Unity results into `cross_engine_comparison.json`.

Usage:
    python3 engine/benchmarks/compare_unity.py \
        --unity-player build/unity-bench/UnityBench.x86_64 \
        --cross-json engine/benchmarks/results/cross-engine-20261122/cross_engine_comparison.json
"""
import argparse
import json
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
REPO_ROOT = BENCH_DIR.parent.parent
DEFAULT_UNITY_PLAYER = REPO_ROOT / "build" / "unity-bench" / "UnityBench.x86_64"
DEFAULT_CROSS_JSON = BENCH_DIR / "results" / "cross-engine-20261122" / "cross_engine_comparison.json"

DEFAULT_SWEEPS = [
    ("broad_phase", [50, 100, 200, 500, 1000, 2000, 4000]),
    ("rigid_body_container", [50, 100, 200, 500, 1000, 1500, 2000]),
    ("ideal_world", [200, 500, 1000, 2000, 5000, 10000, 15000, 20000]),
]


def run_unity_once(player: Path, scenario: str, n: int, workers: int, args):
    cmd = []
    if args.pin_cores and shutil.which("taskset"):
        cmd.extend(["taskset", "-c", args.pin_cores])
    cmd.extend([
        str(player),
        "-batchmode", "-nographics", "-logFile", "-",
        "--scenario", scenario,
        "--count", str(n),
        "--workers", str(workers),
        "--warmup-frames", str(args.warmup_frames),
        "--measure-frames", str(args.measure_frames),
        "--physics-fps", str(args.physics_fps),
        "--velocity-iterations", str(args.velocity_iterations),
        "--position-iterations", str(args.position_iterations),
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
        if line.startswith("{") and "\"engine\":\"unity_" in line and line.endswith("}"):
            return json.loads(line), elapsed, None
    return None, elapsed, f"no_json: {proc.stdout[-300:]}"


def run_unity_point(player: Path, scenario: str, n: int, workers: int, args, t_start: float):
    runs = []
    for _ in range(args.reps):
        if time.monotonic() - t_start > args.total_budget_seconds:
            return None, "total_budget_exceeded"
        data, elapsed, err = run_unity_once(player, scenario, n, workers, args)
        if err:
            return None, f"{err} (after {elapsed:.1f}s)"
        runs.append(data)
    return {
        "mean_ms": round(statistics.mean(d["mean_ms"] for d in runs), 4),
        "median_ms": round(statistics.mean(d["median_ms"] for d in runs), 4),
        "p95_ms": round(statistics.mean(d["p95_ms"] for d in runs), 4),
        "avg_awake_bodies": round(statistics.mean(d["avg_awake_bodies"] for d in runs), 1),
        "multithreaded": runs[0].get("multithreaded", False),
        "job_worker_count": runs[0].get("job_worker_count", 0),
        "engine": runs[0].get("engine", "unity"),
        "reps": len(runs),
        "raw_means": [d["mean_ms"] for d in runs],
    }, None


def parse_args():
    p = argparse.ArgumentParser(description="Benchmark Unity 6 Physics2D (1T & 6T) and merge into cross-engine JSON.")
    p.add_argument("--unity-player", type=Path, default=DEFAULT_UNITY_PLAYER)
    p.add_argument("--cross-json", type=Path, default=DEFAULT_CROSS_JSON)
    p.add_argument("--reps", type=int, default=3)
    p.add_argument("--warmup-frames", type=int, default=120)
    p.add_argument("--measure-frames", type=int, default=600)
    p.add_argument("--physics-fps", type=int, default=120)
    p.add_argument("--velocity-iterations", type=int, default=10)
    p.add_argument("--position-iterations", type=int, default=3)
    p.add_argument("--mt-workers", type=int, default=6)
    p.add_argument("--pin-cores", default="0,2,4,6,8,10")
    p.add_argument("--max-run-seconds", type=float, default=35.0)
    p.add_argument("--total-budget-seconds", type=float, default=420.0)
    p.add_argument("--skip-plots", action="store_true")
    return p.parse_args()


def main():
    args = parse_args()
    if not args.unity_player.is_file():
        raise SystemExit(f"Unity player not found: {args.unity_player}")
    if not args.cross_json.is_file():
        raise SystemExit(f"Cross-engine JSON not found: {args.cross_json}")

    with open(args.cross_json) as f:
        data = json.load(f)

    t_start = time.monotonic()
    detected_engine = None

    for scenario, counts in DEFAULT_SWEEPS:
        print(f"\n=== Scenario: {scenario} ===", flush=True)
        print(
            f"{'N':>7} | {'Unity 1T':>9} | {'Unity 6T':>9} | {'Plat w1':>8} | {'Plat w6':>8} | {'G4 Node':>8} | {'B2 v2.4':>8} | {'U1/P6':>7} | {'U6/P6':>7}",
            flush=True,
        )
        print("-" * 92, flush=True)

        rows_by_n = {r["n"]: r for r in data["sweeps"][scenario]}
        w1_stopped = False
        w6_stopped = False

        for n in counts:
            row = rows_by_n[n]

            if w1_stopped:
                u1, err1 = None, "skipped_after_timeout"
            else:
                u1, err1 = run_unity_point(args.unity_player, scenario, n, 1, args, t_start)
                if err1 and ("timeout" in err1 or "budget" in err1):
                    w1_stopped = True

            if w6_stopped:
                u6, err6 = None, "skipped_after_timeout"
            else:
                u6, err6 = run_unity_point(args.unity_player, scenario, n, args.mt_workers, args, t_start)
                if err6 and ("timeout" in err6 or "budget" in err6):
                    w6_stopped = True

            if u1 and not detected_engine:
                detected_engine = u1.get("engine")

            row["unity_w1"] = u1
            row["unity_w6"] = u6
            if err1:
                row["unity_w1_note"] = err1
            else:
                row.pop("unity_w1_note", None)
            if err6:
                row["unity_w6_note"] = err6
            else:
                row.pop("unity_w6_note", None)

            p1 = row.get("platformator_w1")
            p6 = row.get("platformator_w6")
            gn = row.get("godot_node")
            b2 = row.get("box2d_v2")

            def fmt(d, w=8):
                return f"{d['mean_ms']:{w}.3f}" if d else (" " * (w - 3) + "-  ")

            r1 = f"{u1['mean_ms'] / p6['mean_ms']:.2f}x" if (u1 and p6) else "-"
            r6 = f"{u6['mean_ms'] / p6['mean_ms']:.2f}x" if (u6 and p6) else "-"
            print(
                f"{n:>7} | {fmt(u1, 9)} | {fmt(u6, 9)} | {fmt(p1)} | {fmt(p6)} | {fmt(gn)} | {fmt(b2)} | {r1:>7} | {r6:>7}",
                flush=True,
            )

    data["meta"]["unity_engine"] = detected_engine or "unity_6000.3.22f1"
    data["meta"]["unity_iterations"] = {"velocity": args.velocity_iterations, "position": args.position_iterations}
    data["meta"]["unity_mt_workers"] = args.mt_workers
    data["meta"]["unity_elapsed_seconds"] = round(time.monotonic() - t_start, 2)

    with open(args.cross_json, "w") as f:
        json.dump(data, f, indent=2)
    print(f"\nMerged Unity results in {data['meta']['unity_elapsed_seconds']}s -> {args.cross_json}", flush=True)

    if not args.skip_plots:
        plot_script = BENCH_DIR / "plot_cross_engine.py"
        if plot_script.is_file():
            subprocess.run(
                [sys.executable, str(plot_script), "--input", str(args.cross_json), "--output-dir", str(args.cross_json.parent / "plots")],
                check=True,
            )


if __name__ == "__main__":
    main()
