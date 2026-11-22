#!/usr/bin/env python3
"""Generate publication-grade 300-DPI cross-engine comparison plots (EN & SR)
comparing Platformator (w=1, w=6), Godot 4.7 (Node2D & PhysicsServer2D),
Box2D v2.4.2 (1T), and Box2D v3.1.1 (w=1, w=6).

Usage:
    python3 engine/benchmarks/plot_cross_engine.py \
        --input engine/benchmarks/results/cross-engine-20261122/cross_engine_comparison.json \
        --output-dir engine/benchmarks/results/cross-engine-20261122/plots
"""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


SERIES_STYLE = {
    "godot_node":      {"color": "#478cbf", "marker": "o", "ls": "-",  "lw": 2.2, "ms": 5.2, "z": 3},
    "godot_server":    {"color": "#7b68ee", "marker": "D", "ls": "--", "lw": 2.0, "ms": 4.8, "z": 3},
    "unity_w1":        {"color": "#d95f02", "marker": "X", "ls": ":",  "lw": 1.9, "ms": 5.0, "z": 3},
    "unity_w6":        {"color": "#e67e22", "marker": "s", "ls": "-",  "lw": 2.3, "ms": 5.2, "z": 4},
    "platformator_w1": {"color": "#d4ac0d", "marker": "p", "ls": ":",  "lw": 1.9, "ms": 4.8, "z": 4},
    "platformator_w6": {"color": "#1b9e77", "marker": "^", "ls": "-",  "lw": 2.8, "ms": 6.4, "z": 6},
    "box2d_v2":        {"color": "#c0392b", "marker": "v", "ls": "-.", "lw": 2.1, "ms": 5.2, "z": 4},
    "box2d_v3_w1":     {"color": "#8e44ad", "marker": "P", "ls": ":",  "lw": 1.9, "ms": 5.0, "z": 4},
    "box2d_v3_w6":     {"color": "#2c3e50", "marker": "*", "ls": "-",  "lw": 2.4, "ms": 7.2, "z": 5},
}

DEFAULT_SERIES_ORDER = [
    "godot_node",
    "godot_server",
    "unity_w6",
    "platformator_w6",
    "box2d_v2",
    "box2d_v3_w6",
]

ALL_SERIES_ORDER = [
    "godot_node",
    "godot_server",
    "unity_w1",
    "unity_w6",
    "platformator_w1",
    "platformator_w6",
    "box2d_v2",
    "box2d_v3_w1",
    "box2d_v3_w6",
]

TEXT = {
    "en": {
        "labels": {
            "godot_node": "Godot 4.7 (Node2D, 1T)",
            "godot_server": "Godot 4.7 (PhysicsServer2D, 1T)",
            "unity_w1": "Unity 6.3 (Physics2D, 1T)",
            "unity_w6": "Unity 6.3 (Physics2D, 6T Jobs)",
            "platformator_w1": "Platformator (w = 1)",
            "platformator_w6": "Platformator (w = 6)",
            "box2d_v2": "Box2D v2.4.2 (1T, AoS)",
            "box2d_v3_w1": "Box2D v3.1.1 (w = 1, SIMD)",
            "box2d_v3_w6": "Box2D v3.1.1 (w = 6, SIMD)",
        },
        "scenarios": {
            "broad_phase": ("Broad Phase (Coherent Moving Lanes)", "Moving boxes (N)"),
            "rigid_body_container": ("Rigid Body Container (Dense Single Island)", "Dynamic bodies (N)"),
            "ideal_world": ("Ideal Distributed World (Disjoint 20-Body Chambers)", "Dynamic bodies (N)"),
        },
        "short_scenarios": {
            "broad_phase": "Broad Phase\n(N = 4,000)",
            "rigid_body_container": "Rigid Body Container\n(N = 2,000)",
            "ideal_world": "Ideal World\n(N = 5,000)",
        },
        "panel_linear": "Mean Step Time — Linear Scale (ms)",
        "panel_norm": "Per-Body Cost — Log Scale (µs / body)",
        "y_time": "Mean step time (ms)",
        "y_norm": "Cost per body (µs / N)",
        "budget_120": "120 Hz budget (8.33 ms)",
        "budget_60": "60 Hz budget (16.67 ms)",
        "ref_n": "ref O(N)",
        "ref_nlogn": "ref O(N log N)",
        "overview_title": "Cross-Engine 2D Physics Comparison — Platformator, Godot 4.7, Unity 6.3 & Box2D",
        "overview_left": "Mean Step Time at High Load (Log Scale, Lower is Better)",
        "overview_right": "Ratio vs. Platformator (w = 6) at High Load (>1× = Platformator Faster)",
        "parity": "1.0× parity with Platformator (w = 6)",
        "y_ratio": "Step time / Platformator (w = 6)",
        "footer": "Ryzen 5 7535HS (pinned 6 physical cores) · dt = 1/120 s · gravity = 98.1 (9.81 m/s² in Unity/Box2D) · headless · GCC 16.2.1 -O3 LTO",
    },
    "sr": {
        "labels": {
            "godot_node": "Godot 4.7 (Node2D, 1 nit)",
            "godot_server": "Godot 4.7 (PhysicsServer2D, 1 nit)",
            "unity_w1": "Unity 6.3 (Physics2D, 1 nit)",
            "unity_w6": "Unity 6.3 (Physics2D, 6 niti)",
            "platformator_w1": "Platformator (w = 1)",
            "platformator_w6": "Platformator (w = 6)",
            "box2d_v2": "Box2D v2.4.2 (1 nit, AoS)",
            "box2d_v3_w1": "Box2D v3.1.1 (w = 1, SIMD)",
            "box2d_v3_w6": "Box2D v3.1.1 (w = 6, SIMD)",
        },
        "scenarios": {
            "broad_phase": ("Široka faza (koherentno kretanje)", "Broj pokretnih tela (N)"),
            "rigid_body_container": ("Dinamična tela u posudi (jedno ostrvo)", "Broj dinamičnih tela (N)"),
            "ideal_world": ("Idealan raspoređeni svet (komore po 20 tela)", "Broj dinamičnih tela (N)"),
        },
        "short_scenarios": {
            "broad_phase": "Široka faza\n(N = 4.000)",
            "rigid_body_container": "Posuda sa telima\n(N = 2.000)",
            "ideal_world": "Idealan svet\n(N = 5.000)",
        },
        "panel_linear": "Prosečno vreme koraka — linearna skala (ms)",
        "panel_norm": "Cena po telu — logaritamska skala (µs / telo)",
        "y_time": "Prosečno vreme koraka (ms)",
        "y_norm": "Cena po telu (µs / N)",
        "budget_120": "Budžet 120 Hz (8,33 ms)",
        "budget_60": "Budžet 60 Hz (16,67 ms)",
        "ref_n": "ref O(N)",
        "ref_nlogn": "ref O(N log N)",
        "overview_title": "Među-motorno poređenje 2D fizike — Platformator, Godot 4.7, Unity 6.3 i Box2D",
        "overview_left": "Prosečno vreme koraka pri visokom opterećenju (log skala, niže je bolje)",
        "overview_right": "Odnos naspram Platformator (w = 6) (>1× = Platformator brži)",
        "parity": "1,0× paritet sa Platformator (w = 6)",
        "y_ratio": "Vreme koraka / Platformator (w = 6)",
        "footer": "Ryzen 5 7535HS (6 fizičkih jezgara) · dt = 1/120 s · gravitacija = 98,1 (9,81 m/s² u Unity/Box2D) · bez renderovanja · GCC 16.2.1 -O3 LTO",
    },
}


def setup_style():
    plt.rcParams.update({
        "figure.facecolor": "white",
        "axes.facecolor": "white",
        "axes.edgecolor": "#cccccc",
        "axes.linewidth": 0.9,
        "axes.grid": True,
        "grid.color": "#e5e5e5",
        "grid.linestyle": "--",
        "grid.linewidth": 0.6,
        "font.size": 9.8,
        "axes.titlesize": 11.2,
        "axes.labelsize": 10.2,
        "legend.fontsize": 8.6,
        "xtick.labelsize": 9.2,
        "ytick.labelsize": 9.2,
    })


def extract_series(rows, key):
    xs, ys = [], []
    for r in rows:
        d = r.get(key)
        if d is not None and "mean_ms" in d:
            xs.append(r["n"])
            ys.append(d["mean_ms"])
    return xs, ys


def plot_scenario(data: dict, scenario: str, lang: str, out_path: Path, series_order):
    t = TEXT[lang]
    title, xlabel = t["scenarios"][scenario]
    rows = data["sweeps"][scenario]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13.8, 5.8), dpi=300)
    fig.suptitle(f"{title} — Platformator vs. Godot 4.7 vs. Unity 6.3 vs. Box2D", fontsize=12.6, fontweight="bold", y=0.985)

    for key in series_order:
        xs, ys = extract_series(rows, key)
        if not xs:
            continue
        st = SERIES_STYLE[key]
        lbl = t["labels"][key]
        ax1.plot(xs, ys, color=st["color"], marker=st["marker"], linestyle=st["ls"],
                 linewidth=st["lw"], markersize=st["ms"], label=lbl, zorder=st["z"])
        us_per_body = [(y * 1000.0) / float(x) for x, y in zip(xs, ys)]
        ax2.plot(xs, us_per_body, color=st["color"], marker=st["marker"], linestyle=st["ls"],
                 linewidth=st["lw"], markersize=st["ms"], zorder=st["z"])

    ax1.set_title(t["panel_linear"])
    ax1.set_xlabel(xlabel)
    ax1.set_ylabel(t["y_time"])
    ax1.set_ylim(bottom=0)

    ax2.set_title(t["panel_norm"])
    ax2.set_xlabel(xlabel)
    ax2.set_ylabel(t["y_norm"])
    ax2.set_yscale("log")
    ax2.set_xscale("log")

    # Add closest-fit asymptotic complexity references on BOTH left (linear ms) and right (log-log µs/N) panels.
    p6_xs, p6_ys = extract_series(rows, "platformator_w6")
    if p6_xs:
        mid_idx = len(p6_xs) // 2
        anchor_n = float(p6_xs[mid_idx])
        anchor_ms = float(p6_ys[mid_idx])
        anchor_us = (anchor_ms * 1000.0) / anchor_n

        n_lin = np.linspace(float(p6_xs[0]), float(p6_xs[-1]), 160)
        ref_n_ms = anchor_ms * (n_lin / anchor_n)
        ref_nlogn_ms = anchor_ms * (n_lin * np.log2(n_lin)) / (anchor_n * np.log2(anchor_n))

        ax1.plot(n_lin, ref_n_ms, color="#555555", linestyle="--", linewidth=1.35, alpha=0.65,
                 label=t["ref_n"], zorder=2)
        ax1.plot(n_lin, ref_nlogn_ms, color="#888888", linestyle=":", linewidth=1.6, alpha=0.80,
                 label=t["ref_nlogn"], zorder=2)

        ax1.annotate(
            "O(N)",
            xy=(n_lin[-1], ref_n_ms[-1]),
            xytext=(-4, -8),
            textcoords="offset points",
            ha="right",
            va="top",
            fontsize=8.0,
            fontweight="bold",
            color="#444444",
            bbox={"boxstyle": "round,pad=0.14", "fc": "white", "ec": "none", "alpha": 0.78},
            zorder=7,
        )
        ax1.annotate(
            "O(N log N)",
            xy=(n_lin[-1], ref_nlogn_ms[-1]),
            xytext=(-4, 5),
            textcoords="offset points",
            ha="right",
            va="bottom",
            fontsize=8.0,
            fontweight="bold",
            color="#555555",
            bbox={"boxstyle": "round,pad=0.14", "fc": "white", "ec": "none", "alpha": 0.78},
            zorder=7,
        )

        n_grid = np.geomspace(float(p6_xs[0]), float(p6_xs[-1]), 120)
        ref_n_vals = np.full_like(n_grid, anchor_us)
        ref_nlogn_vals = anchor_us * (np.log2(n_grid) / np.log2(anchor_n))

        ax2.plot(n_grid, ref_n_vals, color="#555555", linestyle="--", linewidth=1.35, alpha=0.65, zorder=2)
        ax2.plot(n_grid, ref_nlogn_vals, color="#888888", linestyle=":", linewidth=1.6, alpha=0.80, zorder=2)

        ax2.annotate(
            "O(N)",
            xy=(n_grid[-1], ref_n_vals[-1]),
            xytext=(-4, -10),
            textcoords="offset points",
            ha="right",
            va="top",
            fontsize=8.2,
            fontweight="bold",
            color="#444444",
            bbox={"boxstyle": "round,pad=0.16", "fc": "white", "ec": "none", "alpha": 0.78},
            zorder=7,
        )
        ax2.annotate(
            "O(N log N)",
            xy=(n_grid[-1], ref_nlogn_vals[-1]),
            xytext=(-4, 4),
            textcoords="offset points",
            ha="right",
            va="bottom",
            fontsize=8.2,
            fontweight="bold",
            color="#555555",
            bbox={"boxstyle": "round,pad=0.16", "fc": "white", "ec": "none", "alpha": 0.78},
            zorder=7,
        )

    handles, labels = ax1.get_legend_handles_labels()
    ncol = 4 if len(handles) <= 8 else 5
    fig.legend(handles, labels, loc="upper center", bbox_to_anchor=(0.5, 0.945),
               ncol=ncol, frameon=True, framealpha=0.94, fontsize=8.4)

    fig.text(0.5, 0.01, t["footer"], ha="center", fontsize=8.2, color="#555555")
    plt.tight_layout(rect=[0, 0.035, 1, 0.83])
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=300)
    plt.close(fig)


def plot_overview(data: dict, lang: str, out_path: Path, series_order):
    t = TEXT[lang]
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14.0, 5.6), dpi=300)
    fig.suptitle(t["overview_title"], fontsize=12.6, fontweight="bold", y=0.98)

    # Pick highest N where EVERY selected engine configuration has a valid measurement
    specs = [
        ("broad_phase", 4000),
        ("rigid_body_container", 2000),
        ("ideal_world", 5000),
    ]
    x_labels = [t["short_scenarios"][s] for s, _ in specs]
    x = np.arange(len(specs))
    active_series = [k for k in series_order if all(next(r for r in data["sweeps"][s] if r["n"] == n).get(k) for s, n in specs)]
    n_series = len(active_series)
    width = 0.80 / n_series

    # Left panel: log-scale mean step time across active series
    for idx, key in enumerate(active_series):
        vals = []
        for scen, n in specs:
            row = next(r for r in data["sweeps"][scen] if r["n"] == n)
            vals.append(row[key]["mean_ms"])
        offset = (idx - (n_series - 1) / 2.0) * width
        st = SERIES_STYLE[key]
        bars = ax1.bar(x + offset, vals, width * 0.90, color=st["color"], edgecolor="white", linewidth=0.6,
                       label=t["labels"][key], zorder=3)
        for bar in bars:
            h = bar.get_height()
            txt = f"{h:.2f}" if h < 10.0 else f"{h:.1f}"
            ax1.text(bar.get_x() + bar.get_width() / 2, h * 1.07, txt,
                     ha="center", va="bottom", fontsize=7.4, rotation=90 if n_series > 6 else 0,
                     fontweight="bold", color="#222222")

    ax1.set_yscale("log")
    ax1.set_title(t["overview_left"])
    ax1.set_ylabel(t["y_time"])
    ax1.set_xticks(x)
    ax1.set_xticklabels(x_labels)
    ax1.set_ylim(0.05, 120.0)
    ax1.legend(loc="upper left", framealpha=0.92, fontsize=8.0, ncol=2)

    # Right panel: Ratio relative to Platformator w=6 (log scale, >1 = Platformator faster, <1 = rival faster)
    rivals = [k for k in active_series if k != "platformator_w6"]
    r_width = 0.78 / len(rivals)
    for idx, key in enumerate(rivals):
        ratios = []
        for scen, n in specs:
            row = next(r for r in data["sweeps"][scen] if r["n"] == n)
            ratios.append(row[key]["mean_ms"] / row["platformator_w6"]["mean_ms"])
        offset = (idx - (len(rivals) - 1) / 2.0) * r_width
        st = SERIES_STYLE[key]
        bars = ax2.bar(x + offset, ratios, r_width * 0.90, color=st["color"], edgecolor="white", linewidth=0.6,
                       label=t["labels"][key], zorder=3)
        for bar in bars:
            h = bar.get_height()
            ax2.text(bar.get_x() + bar.get_width() / 2, h * 1.07, f"{h:.2f}×",
                     ha="center", va="bottom", fontsize=7.4, rotation=90 if len(rivals) > 5 else 0,
                     fontweight="bold", color="#222222")

    ax2.axhline(1.0, color="#1b9e77", linestyle="--", linewidth=1.5, label=t["parity"], zorder=5)
    ax2.set_yscale("log")
    ax2.set_title(t["overview_right"])
    ax2.set_ylabel(t["y_ratio"])
    ax2.set_xticks(x)
    ax2.set_xticklabels(x_labels)
    ax2.set_ylim(0.08, 28.0)
    ax2.legend(loc="upper right", framealpha=0.92, fontsize=8.0, ncol=2)

    fig.text(0.5, 0.01, t["footer"], ha="center", fontsize=8.2, color="#555555")
    plt.tight_layout(rect=[0, 0.035, 1, 0.93])
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=300)
    plt.close(fig)


def main():
    p = argparse.ArgumentParser(description="Plot cross-engine comparison (Platformator vs Godot 4 vs Unity 6 vs Box2D).")
    p.add_argument("--input", type=Path, required=True)
    p.add_argument("--output-dir", type=Path, required=True)
    p.add_argument("--include-single-thread", action="store_true",
                   help="Include w=1 duplicates for multithreaded engines (Platformator, Unity, Box2D v3)")
    args = p.parse_args()

    with open(args.input) as f:
        data = json.load(f)

    series_order = ALL_SERIES_ORDER if args.include_single_thread else DEFAULT_SERIES_ORDER

    setup_style()
    scenario_files = [
        ("01_broad_phase_cross_engine.png", "broad_phase"),
        ("02_rigid_body_container_cross_engine.png", "rigid_body_container"),
        ("03_ideal_world_cross_engine.png", "ideal_world"),
    ]

    generated = []
    for lang in ("en", "sr"):
        lang_dir = args.output_dir / lang
        for fname, scen in scenario_files:
            if scen in data.get("sweeps", {}):
                out_path = lang_dir / fname
                plot_scenario(data, scen, lang, out_path, series_order)
                generated.append(out_path)
        ov_path = lang_dir / "04_summary_overview_cross_engine.png"
        plot_overview(data, lang, ov_path, series_order)
        generated.append(ov_path)

    print(f"Generated {len(generated)} cross-engine plots under {args.output_dir}")


if __name__ == "__main__":
    main()
