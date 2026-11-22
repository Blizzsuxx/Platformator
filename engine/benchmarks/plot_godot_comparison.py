#!/usr/bin/env python3
"""Generate publication-grade 300-DPI comparison plots (EN & SR) for
Platformator (w=1, w=6) vs Godot 4 (GodotPhysics2D SceneTree & PhysicsServer2D).

Usage:
    python3 engine/benchmarks/plot_godot_comparison.py \
        --input engine/benchmarks/results/godot-comparison-20261122/godot_vs_platformator.json \
        --output-dir engine/benchmarks/results/godot-comparison-20261122/plots
"""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


COLORS = {
    "godot": "#478cbf",        # Official Godot blue
    "godot_server": "#7b68ee", # Medium slate blue for raw PhysicsServer2D
    "plat_w1": "#e67e22",      # Warm amber for Platformator 1 worker
    "plat_w6": "#1b9e77",      # Rich teal/green for Platformator 6 workers
    "budget_120": "#d62728",
    "budget_60": "#7f7f7f",
}

TEXT = {
    "en": {
        "scenarios": {
            "broad_phase": ("Broad Phase (Coherent Moving Lanes)", "Moving boxes (N)"),
            "rigid_body_container": ("Rigid Body Container (Dense Single Island)", "Dynamic bodies (N)"),
            "ideal_world": ("Ideal Distributed World (Disjoint 20-Body Chambers)", "Dynamic bodies (N)"),
        },
        "short_names": {
            "broad_phase": "Broad Phase\n(N = 4,000)",
            "rigid_body_container": "Rigid Body Container\n(N = 2,000)",
            "ideal_world": "Ideal World\n(N = 5,000)",
        },
        "godot_node": "Godot 4.7 (Node2D, 1T)",
        "godot_server": "Godot 4.7 (PhysicsServer2D, 1T)",
        "plat_w1": "Platformator (w = 1)",
        "plat_w6": "Platformator (w = 6)",
        "p95_suffix": " (p95)",
        "panel_time": "Mean & p95 Step Time (ms)",
        "panel_speedup": "Speedup over Godot 4 (Node2D)",
        "y_time": "Step time (ms)",
        "y_speedup": "Speedup factor (Godot / Platformator)",
        "budget_120": "120 Hz budget (8.33 ms)",
        "budget_60": "60 Hz budget (16.67 ms)",
        "parity_line": "1.0× parity",
        "timeout_note": "Godot timed out at N = 20,000\n(>42 ms/step)",
        "overview_title": "Platformator vs. Godot 4.7 (GodotPhysics2D) — Cross-Engine Summary",
        "overview_left": "Step Time at High-Load Spot Checks (Lower is Better)",
        "overview_right": "Real-Time Capacity on Ideal World (Mean Step Time)",
        "footer": "Ryzen 5 7535HS (pinned 6 physical cores) · dt = 1/120 s · gravity = 98.1 · 10 solver iterations · headless · zero GDScript per-tick loops",
    },
    "sr": {
        "scenarios": {
            "broad_phase": ("Široka faza (koherentno kretanje)", "Broj pokretnih tela (N)"),
            "rigid_body_container": ("Dinamična tela u posudi (jedno ostrvo)", "Broj dinamičnih tela (N)"),
            "ideal_world": ("Idealan raspoređeni svet (komore po 20 tela)", "Broj dinamičnih tela (N)"),
        },
        "short_names": {
            "broad_phase": "Široka faza\n(N = 4.000)",
            "rigid_body_container": "Posuda sa telima\n(N = 2.000)",
            "ideal_world": "Idealan svet\n(N = 5.000)",
        },
        "godot_node": "Godot 4.7 (Node2D, 1 nit)",
        "godot_server": "Godot 4.7 (PhysicsServer2D, 1 nit)",
        "plat_w1": "Platformator (w = 1)",
        "plat_w6": "Platformator (w = 6)",
        "p95_suffix": " (p95)",
        "panel_time": "Prosečno i p95 vreme koraka (ms)",
        "panel_speedup": "Ubrzanje u odnosu na Godot 4 (Node2D)",
        "y_time": "Vreme koraka (ms)",
        "y_speedup": "Faktor ubrzanja (Godot / Platformator)",
        "budget_120": "Budžet 120 Hz (8,33 ms)",
        "budget_60": "Budžet 60 Hz (16,67 ms)",
        "parity_line": "1,0× paritet",
        "timeout_note": "Godot prekoračio limit za N = 20.000\n(>42 ms/korak)",
        "overview_title": "Platformator naspram Godot 4.7 (GodotPhysics2D) — Zbirno poređenje",
        "overview_left": "Vreme koraka pri visokom opterećenju (niže je bolje)",
        "overview_right": "Kapacitet u realnom vremenu — Idealan svet (prosek)",
        "footer": "Ryzen 5 7535HS (6 fizičkih jezgara) · dt = 1/120 s · gravitacija = 98,1 · 10 iteracija rešavača · bez renderovanja · bez GDScript petlje",
    },
}


def setup_style():
    plt.rcParams.update({
        "figure.facecolor": "white",
        "axes.facecolor": "white",
        "axes.edgecolor": "#cccccc",
        "axes.linewidth": 0.9,
        "axes.grid": True,
        "grid.color": "#e4e4e4",
        "grid.linestyle": "--",
        "grid.linewidth": 0.6,
        "font.size": 10,
        "axes.titlesize": 11.5,
        "axes.labelsize": 10.5,
        "legend.fontsize": 9.2,
        "xtick.labelsize": 9.5,
        "ytick.labelsize": 9.5,
    })


def plot_scenario(data: dict, scenario: str, lang: str, out_path: Path):
    t = TEXT[lang]
    title, xlabel = t["scenarios"][scenario]
    rows = data["sweeps"][scenario]

    ns_all = [r["n"] for r in rows]
    p1_mean = [r["platformator_w1"]["mean_ms"] for r in rows]
    p1_p95 = [r["platformator_w1"]["p95_ms"] for r in rows]
    p6_mean = [r["platformator_w6"]["mean_ms"] for r in rows]
    p6_p95 = [r["platformator_w6"]["p95_ms"] for r in rows]

    g_rows = [r for r in rows if r.get("godot") is not None]
    ns_g = [r["n"] for r in g_rows]
    g_mean = [r["godot"]["mean_ms"] for r in g_rows]
    g_p95 = [r["godot"]["p95_ms"] for r in g_rows]

    gs_rows = [r for r in rows if r.get("godot_server") is not None]
    ns_gs = [r["n"] for r in gs_rows]
    gs_mean = [r["godot_server"]["mean_ms"] for r in gs_rows]
    gs_p95 = [r["godot_server"]["p95_ms"] for r in gs_rows]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13.2, 5.2), dpi=300)
    fig.suptitle(f"Platformator vs. Godot 4.7 — {title}", fontsize=13, fontweight="bold", y=0.98)

    # Left panel: Step time (mean solid, p95 shaded band)
    ax1.plot(ns_g, g_mean, "-o", color=COLORS["godot"], linewidth=2.3, markersize=5.5, label=t["godot_node"], zorder=4)
    ax1.fill_between(ns_g, g_mean, g_p95, color=COLORS["godot"], alpha=0.13, zorder=2)

    if gs_rows:
        ax1.plot(ns_gs, gs_mean, "-D", color=COLORS["godot_server"], linewidth=2.1, markersize=5.0, label=t["godot_server"], zorder=4)
        ax1.fill_between(ns_gs, gs_mean, gs_p95, color=COLORS["godot_server"], alpha=0.12, zorder=2)
    else:
        for key, sv in data.get("server_spot_checks", {}).items():
            if key.startswith(scenario + "_") and isinstance(sv, dict) and "mean_ms" in sv:
                spot_n = int(key.split("_")[-1])
                ax1.scatter(
                    [spot_n], [sv["mean_ms"]],
                    marker="D", s=65, color=COLORS["godot_server"], edgecolor="black", linewidth=0.7,
                    label=t["godot_server"], zorder=6,
                )

    ax1.plot(ns_all, p1_mean, "-s", color=COLORS["plat_w1"], linewidth=2.1, markersize=5, label=t["plat_w1"], zorder=4)
    ax1.fill_between(ns_all, p1_mean, p1_p95, color=COLORS["plat_w1"], alpha=0.11, zorder=2)

    ax1.plot(ns_all, p6_mean, "-^", color=COLORS["plat_w6"], linewidth=2.4, markersize=5.8, label=t["plat_w6"], zorder=5)
    ax1.fill_between(ns_all, p6_mean, p6_p95, color=COLORS["plat_w6"], alpha=0.13, zorder=2)

    max_y = max(max(g_p95), max(p1_mean), max(p6_mean))
    if max_y >= 6.0:
        ax1.axhline(8.333, color=COLORS["budget_120"], linestyle="--", linewidth=1.2, alpha=0.85, label=t["budget_120"])
    if max_y >= 14.0:
        ax1.axhline(16.667, color=COLORS["budget_60"], linestyle=":", linewidth=1.3, alpha=0.9, label=t["budget_60"])

    # Annotate timeout if present
    timed_out = [r["n"] for r in rows if r.get("godot") is None]
    if timed_out:
        to_n = timed_out[0]
        # Dashed continuation stub showing Godot exceeded the 30s cap at N = 20,000
        ax1.plot([ns_g[-1], to_n], [g_mean[-1], 45.0], "--", color=COLORS["godot"], linewidth=1.6, alpha=0.75, zorder=3)
        ax1.scatter([to_n], [45.0], marker="x", s=70, color=COLORS["godot"], linewidths=2.2, zorder=5)
        ax1.annotate(
            t["timeout_note"],
            xy=(to_n, 45.0),
            xytext=(ns_g[-1] * 0.68, 64.0),
            fontsize=8.2,
            color=COLORS["godot"],
            fontweight="bold",
            arrowprops={"arrowstyle": "->", "color": COLORS["godot"], "lw": 1.1},
            bbox={"boxstyle": "round,pad=0.25", "fc": "white", "ec": COLORS["godot"], "alpha": 0.92},
        )

    ax1.set_title(t["panel_time"])
    ax1.set_xlabel(xlabel)
    ax1.set_ylabel(t["y_time"])
    ax1.set_ylim(bottom=0)
    ax1.legend(loc="upper left", framealpha=0.92)

    # Right panel: Speedup over Godot 4 (Node2D)
    s_w1 = [r["godot"]["mean_ms"] / r["platformator_w1"]["mean_ms"] for r in g_rows]
    s_w6 = [r["godot"]["mean_ms"] / r["platformator_w6"]["mean_ms"] for r in g_rows]

    x_idx = np.arange(len(ns_g))
    width = 0.37
    gap = 0.03
    bars1 = ax2.bar(x_idx - (width + gap) / 2, s_w1, width, color=COLORS["plat_w1"], edgecolor="white", linewidth=0.6, label=t["plat_w1"], zorder=3)
    bars6 = ax2.bar(x_idx + (width + gap) / 2, s_w6, width, color=COLORS["plat_w6"], edgecolor="white", linewidth=0.6, label=t["plat_w6"], zorder=3)
    ax2.axhline(1.0, color="#333333", linestyle="--", linewidth=1.1, label=t["parity_line"], zorder=4)

    max_s = max(max(s_w1), max(s_w6))
    for bar in list(bars1) + list(bars6):
        h = bar.get_height()
        ax2.text(
            bar.get_x() + bar.get_width() / 2,
            h + max_s * 0.015,
            f"{h:.2f}×",
            ha="center",
            va="bottom",
            fontsize=7.2,
            fontweight="bold",
            color="#222222",
        )

    ax2.set_title(t["panel_speedup"])
    ax2.set_xlabel(xlabel)
    ax2.set_ylabel(t["y_speedup"])
    ax2.set_xticks(x_idx)
    ax2.set_xticklabels([f"{n:,}" for n in ns_g], rotation=20 if len(ns_g) > 6 else 0)
    ax2.set_ylim(0, max_s * 1.24)
    # Place legend on the less crowded side
    left_peak = max(s_w1[:3] + s_w6[:3])
    right_peak = max(s_w1[-3:] + s_w6[-3:])
    ax2.legend(loc="upper left" if right_peak > left_peak else "upper right", framealpha=0.92)

    fig.text(0.5, 0.01, t["footer"], ha="center", fontsize=8.3, color="#555555")
    plt.tight_layout(rect=[0, 0.035, 1, 0.93])
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=300)
    plt.close(fig)


def plot_overview(data: dict, lang: str, out_path: Path):
    t = TEXT[lang]
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13.6, 5.4), dpi=300)
    fig.suptitle(t["overview_title"], fontsize=13, fontweight="bold", y=0.98)

    # Left panel: Grouped bar chart comparing all 4 configs at the 3 spot-check scales
    spot_specs = [
        ("broad_phase", 4000),
        ("rigid_body_container", 2000),
        ("ideal_world", 5000),
    ]
    labels, g_node, g_srv, p1_vals, p6_vals = [], [], [], [], []
    for scen, n in spot_specs:
        row = next(r for r in data["sweeps"][scen] if r["n"] == n)
        srv = data["server_spot_checks"][f"{scen}_{n}"]
        labels.append(t["short_names"][scen])
        g_node.append(row["godot"]["mean_ms"])
        g_srv.append(srv["mean_ms"])
        p1_vals.append(row["platformator_w1"]["mean_ms"])
        p6_vals.append(row["platformator_w6"]["mean_ms"])

    x = np.arange(len(labels))
    w = 0.19
    b1 = ax1.bar(x - 1.5 * w, g_node, w, color=COLORS["godot"], edgecolor="white", label=t["godot_node"], zorder=3)
    b2 = ax1.bar(x - 0.5 * w, g_srv, w, color=COLORS["godot_server"], edgecolor="white", label=t["godot_server"], zorder=3)
    b3 = ax1.bar(x + 0.5 * w, p1_vals, w, color=COLORS["plat_w1"], edgecolor="white", label=t["plat_w1"], zorder=3)
    b4 = ax1.bar(x + 1.5 * w, p6_vals, w, color=COLORS["plat_w6"], edgecolor="white", label=t["plat_w6"], zorder=3)

    for bars in (b1, b2, b3, b4):
        for bar in bars:
            h = bar.get_height()
            ax1.text(
                bar.get_x() + bar.get_width() / 2,
                h + 0.15,
                f"{h:.2f}",
                ha="center",
                va="bottom",
                fontsize=7.6,
                fontweight="bold",
                color="#222222",
            )

    ax1.axhline(8.333, color=COLORS["budget_120"], linestyle="--", linewidth=1.2, alpha=0.85, label=t["budget_120"])
    ax1.set_title(t["overview_left"])
    ax1.set_ylabel(t["y_time"])
    ax1.set_xticks(x)
    ax1.set_xticklabels(labels)
    ax1.set_ylim(0, max(g_node) * 1.18)
    ax1.legend(loc="upper left", framealpha=0.92)

    # Right panel: Ideal World real-time capacity curve with 120 Hz & 60 Hz budget callouts
    rows = data["sweeps"]["ideal_world"]
    ns_all = [r["n"] for r in rows]
    p1_m = [r["platformator_w1"]["mean_ms"] for r in rows]
    p6_m = [r["platformator_w6"]["mean_ms"] for r in rows]
    g_rows = [r for r in rows if r.get("godot") is not None]
    ns_g = [r["n"] for r in g_rows]
    g_m = [r["godot"]["mean_ms"] for r in g_rows]
    gs_rows = [r for r in rows if r.get("godot_server") is not None]
    ns_gs = [r["n"] for r in gs_rows]
    gs_m = [r["godot_server"]["mean_ms"] for r in gs_rows]

    ax2.plot(ns_g, g_m, "-o", color=COLORS["godot"], linewidth=2.4, markersize=5.5, label=t["godot_node"], zorder=4)
    if gs_rows:
        ax2.plot(ns_gs, gs_m, "-D", color=COLORS["godot_server"], linewidth=2.1, markersize=5.0, label=t["godot_server"], zorder=4)
    else:
        srv_5k = data["server_spot_checks"].get("ideal_world_5000")
        if srv_5k and "mean_ms" in srv_5k:
            ax2.scatter([5000], [srv_5k["mean_ms"]], marker="D", s=65, color=COLORS["godot_server"], edgecolor="black", linewidth=0.7, label=t["godot_server"], zorder=6)
    ax2.plot(ns_all, p1_m, "-s", color=COLORS["plat_w1"], linewidth=2.0, markersize=5.0, label=t["plat_w1"], zorder=4)
    ax2.plot(ns_all, p6_m, "-^", color=COLORS["plat_w6"], linewidth=2.5, markersize=6.0, label=t["plat_w6"], zorder=5)

    ax2.axhline(8.333, color=COLORS["budget_120"], linestyle="--", linewidth=1.3, label=t["budget_120"])
    ax2.axhline(16.667, color=COLORS["budget_60"], linestyle=":", linewidth=1.4, label=t["budget_60"])

    # Annotate approximate 120 Hz crossing (~4.1k Godot vs ~9.8k Platformator w=6)
    ax2.annotate(
        "~4.1k @ 120 Hz",
        xy=(4100, 8.333),
        xytext=(1200, 13.5),
        fontsize=8.3,
        fontweight="bold",
        color=COLORS["godot"],
        arrowprops={"arrowstyle": "->", "color": COLORS["godot"], "lw": 1.1},
        bbox={"boxstyle": "round,pad=0.22", "fc": "white", "ec": COLORS["godot"], "alpha": 0.92},
    )
    ax2.annotate(
        "~9.8k @ 120 Hz\n(~2.4× Godot)",
        xy=(9800, 8.333),
        xytext=(11200, 3.0),
        fontsize=8.3,
        fontweight="bold",
        color=COLORS["plat_w6"],
        arrowprops={"arrowstyle": "->", "color": COLORS["plat_w6"], "lw": 1.1},
        bbox={"boxstyle": "round,pad=0.22", "fc": "white", "ec": COLORS["plat_w6"], "alpha": 0.92},
    )

    ax2.set_title(t["overview_right"])
    ax2.set_xlabel(t["scenarios"]["ideal_world"][1])
    ax2.set_ylabel(t["y_time"])
    ax2.set_ylim(0, max(max(g_m), max(p1_m)) * 1.08)
    ax2.legend(loc="upper left", framealpha=0.92)

    fig.text(0.5, 0.01, t["footer"], ha="center", fontsize=8.3, color="#555555")
    plt.tight_layout(rect=[0, 0.035, 1, 0.93])
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=300)
    plt.close(fig)


def main():
    p = argparse.ArgumentParser(description="Plot Godot 4 vs Platformator benchmark comparison.")
    p.add_argument("--input", type=Path, required=True, help="Path to godot_vs_platformator.json")
    p.add_argument("--output-dir", type=Path, required=True, help="Output directory for plots/{en,sr}")
    args = p.parse_args()

    with open(args.input) as f:
        data = json.load(f)

    setup_style()
    scenario_order = [
        ("01_broad_phase_vs_godot.png", "broad_phase"),
        ("02_rigid_body_container_vs_godot.png", "rigid_body_container"),
        ("03_ideal_world_vs_godot.png", "ideal_world"),
    ]

    generated = []
    for lang in ("en", "sr"):
        lang_dir = args.output_dir / lang
        for filename, scen in scenario_order:
            if scen in data.get("sweeps", {}):
                out_path = lang_dir / filename
                plot_scenario(data, scen, lang, out_path)
                generated.append(out_path)
        overview_path = lang_dir / "04_summary_overview_vs_godot.png"
        plot_overview(data, lang, overview_path)
        generated.append(overview_path)

    print(f"Generated {len(generated)} plots under {args.output_dir}")


if __name__ == "__main__":
    main()
