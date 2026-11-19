#!/usr/bin/env python3
import argparse
import csv
import json
import math
from pathlib import Path
from typing import Callable, Dict, List, Optional, Sequence, Tuple, Union

from sweep_scaling import comparison_workload, is_add_remove_scenario, load_manifest


STRING_COLUMNS = {"scenario", "sweep_parameter", "raw_directory", "raw_archive", "validation_mode", "timing_eligible",
                  "comparison_workload"}
SCOPES = ("complete_step", "physics", "cleanup", "scene_transition", "render", "broad_phase", "narrow_phase", "resolve_collisions")

COMPLEXITY_SPECS: Dict[str, Tuple[str, Callable[[float], float], str]] = {
    "1": ("O(1)", lambda n: 1.0, "#666666"),
    "logN": ("O(log N)", lambda n: math.log2(max(n, 2.0)), "#2ca02c"),
    "N": ("O(N)", lambda n: max(n, 1.0), "#1f77b4"),
    "NlogN": ("O(N log N)", lambda n: max(n, 1.0) * math.log2(max(n, 2.0)), "#ff7f0e"),
    "N2": ("O(N^2)", lambda n: max(n, 1.0) ** 2, "#d62728"),
}


def _legacy_comparison_workload(csv_path, row):
    raw_directory = row.get("raw_directory")
    manifests = sorted((Path(csv_path).parent / raw_directory).glob("run-*.manifest.json")) if raw_directory else []
    if len(manifests) != int(row["repetitions"]):
        raise ValueError(f"Cannot verify the comparison workload in {csv_path}. Its run manifests are incomplete.")

    workloads = set()
    for path in manifests:
        manifest = load_manifest(path.with_name(path.name.removesuffix(".manifest.json") + ".csv"))
        if any(manifest[key] != row[key] for key in ("scenario", "workers", "seed", "repetitions")):
            raise ValueError(f"Run manifest does not match the aggregate row in {csv_path}")
        workloads.add(json.dumps(comparison_workload(manifest), sort_keys=True))
    if len(workloads) != 1:
        raise ValueError(f"Run manifests disagree on the comparison workload in {csv_path}")
    return workloads.pop()


def load_rows(csv_path, require_workload=False):
    with open(csv_path, newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        required = {"schema_version", "scenario", "sweep_parameter", "swept_value", "requested_count", "workers", "seed", "repetitions",
                    "validation_mode", "timing_eligible", "steady_count", "object_count_avg"}
        required |= {f"{scope}_{suffix}" for scope in SCOPES for suffix in
                     ("avg_ms", "p95_ms", "run_median_ms", "run_stdev_ms", "run_min_ms", "run_max_ms")}
        if not required <= set(reader.fieldnames or ()):
            raise ValueError("Expected schema-v3 aggregate with complete_step and run spread; historical graphs remain separate")
        rows = []
        for row in reader:
            if row["schema_version"] != "3" or row["validation_mode"] != "boundary-only" or row["timing_eligible"] != "True":
                raise ValueError("Unknown schema or instrumented validation data cannot be plotted as performance")
            parsed = {}
            for key, value in row.items():
                parsed[key] = value if key in STRING_COLUMNS else float(value)
                if key not in STRING_COLUMNS and (not math.isfinite(parsed[key]) or parsed[key] < 0):
                    raise ValueError(f"Invalid metric: {key}")
            if parsed["repetitions"] < 5:
                raise ValueError("At least five independent repetitions are required")
            if require_workload and not parsed.get("comparison_workload"):
                parsed["comparison_workload"] = _legacy_comparison_workload(csv_path, parsed)
            rows.append(parsed)
        if not rows:
            raise ValueError("Input CSV has no data rows")
        if len({(row["scenario"], row["workers"], row["seed"], row["sweep_parameter"]) for row in rows}) != 1:
            raise ValueError("Plot one scenario/worker/seed/sweep combination per input")
        return rows


def format_count_tick(value: float, _: int) -> str:
    if value <= 0.0:
        return ""

    rounded_value = round(value)
    if abs(value - rounded_value) < 1e-9:
        return str(int(rounded_value))

    return f"{value:g}"


def sweep_label(sweep_parameter: str) -> str:
    labels = {
        "broad_count": "Requested broad-phase object count (N)",
        "contact_pair_count": "Persistent confirmed contact pairs (N)",
        "narrow_count": "Requested narrow-phase element count (N)",
        "total_body_count": "Requested total body count (N)",
        "churn_count": "Per-step add/remove count (m)",
        "steady_count": "Steady live object count (N)",
    }
    return labels.get(sweep_parameter, "Requested count (N)")


def select_x_axis(rows: List[Dict[str, Union[str, float]]]) -> Tuple[str, str]:
    return "swept_value", sweep_label(str(rows[0]["sweep_parameter"]))


def validate_comparison(rows, compare_rows) -> None:
    for field in ("scenario", "sweep_parameter", "seed", "steady_count", "scripted_endpoints"):
        if field == rows[0]["sweep_parameter"]:
            continue
        if {row.get(field, 0.0) for row in rows} != {row.get(field, 0.0) for row in compare_rows}:
            raise ValueError(f"Compare CSV must use the same {field} as --input")
    controls = ("dt", "rendering")
    if is_add_remove_scenario(rows[0]["scenario"]):
        controls += ("box_count", "circle_count")

    workloads = []
    for arm in (rows, compare_rows):
        settings = [json.loads(row.get("comparison_workload") or "{}") for row in arm]
        if any(not isinstance(config, dict) or not all(field in config for field in controls) for config in settings):
            raise ValueError("Comparison requires resolved workload settings for both inputs")
        workloads.append(settings)
    for field in controls:
        if len({config[field] for arm in workloads for config in arm}) != 1:
            raise ValueError(f"Compare CSV must use the same resolved {field} as --input")


def build_title(rows: List[Dict[str, Union[str, float]]], compare_rows: Optional[List[Dict[str, Union[str, float]]]] = None) -> str:
    scenario = str(rows[0].get("scenario", "unknown"))
    sweep_parameter = str(rows[0].get("sweep_parameter", "requested_count"))
    workers_primary = int(float(rows[0].get("workers", 1)))

    if compare_rows:
        workers_compare = int(float(compare_rows[0].get("workers", 1)))
        worker_suffix = f" [w={workers_primary} vs w={workers_compare}]"
    else:
        worker_suffix = f" [w={workers_primary}]"

    if scenario in {"add_and_remove", "moving_add_and_remove"}:
        steady_counts = {float(row.get("steady_count", 0.0)) for row in rows}
        if len(steady_counts) == 1:
            steady_count = next(iter(steady_counts))
            return f"Platformator Scaling: {scenario} ({sweep_parameter}, steady={int(steady_count)}){worker_suffix}"

    return f"Platformator Scaling: {scenario}{worker_suffix}"


def compute_reference_curve(
    counts: Sequence[float],
    values: Sequence[float],
    ref_key: str,
    anchor: str = "mid",
    normalized: bool = False,
) -> List[float]:
    """Compute a scaled asymptotic reference curve anchored to observed data."""
    if ref_key not in COMPLEXITY_SPECS:
        raise ValueError(f"Unknown reference curve '{ref_key}'. Expected one of {tuple(COMPLEXITY_SPECS)}")
    _, fn, _ = COMPLEXITY_SPECS[ref_key]
    shape = [fn(c) / c if c > 0.0 else 0.0 for c in counts] if normalized else [fn(c) for c in counts]
    valid_indices = [i for i, (c, v) in enumerate(zip(counts, values)) if c > 0.0 and v > 0.0 and shape[i] > 0.0]
    if not valid_indices:
        return []

    if anchor == "first":
        idx = valid_indices[0]
        scale = values[idx] / shape[idx]
    elif anchor == "mid":
        idx = valid_indices[len(valid_indices) // 2]
        scale = values[idx] / shape[idx]
    elif anchor == "last":
        idx = valid_indices[-1]
        scale = values[idx] / shape[idx]
    elif anchor == "fit":
        # Geometric-mean fit minimizes relative error across log-spaced ranges.
        log_ratios = [math.log(values[i] / shape[i]) for i in valid_indices]
        scale = math.exp(sum(log_ratios) / len(log_ratios))
    else:
        raise ValueError(f"Unknown anchor mode: {anchor}")

    return [scale * v for v in shape]


def _default_series(include_cleanup: bool, include_physics: bool) -> List[Tuple[str, str, str]]:
    series = [
        ("complete_step_avg_ms", "Complete step mean", "o"),
        ("complete_step_p95_ms", "Complete step p95", "s"),
        ("broad_phase_avg_ms", "Broad Phase Avg", "^"),
        ("narrow_phase_avg_ms", "Narrow Phase Avg", "D"),
        ("resolve_collisions_avg_ms", "Collision Resolution Avg", "v"),
    ]
    if include_cleanup:
        series.append(("cleanup_avg_ms", "Cleanup Avg", "P"))
    if include_physics:
        series.append(("physics_avg_ms", "Physics Total Avg", "X"))
    return series


def _configure_axes(ax, counts: Sequence[float], log_x: bool, log_y: bool, x_tick_step: float, x_label: str, y_label: str) -> None:
    from matplotlib.ticker import FuncFormatter, MultipleLocator

    ax.set_xlabel(x_label)
    ax.set_ylabel(y_label)
    ax.grid(True, which="both" if (log_x or log_y) else "major", linestyle="--", alpha=0.35)

    if log_x:
        ax.set_xscale("log")
        ax.set_xticks(counts)
        ax.xaxis.set_major_formatter(FuncFormatter(format_count_tick))
        ax.minorticks_off()
    else:
        if x_tick_step <= 0.0:
            raise ValueError("--x-tick-step must be greater than zero.")
        ax.set_xlim(left=0.0)
        ax.xaxis.set_major_locator(MultipleLocator(x_tick_step))
        ax.xaxis.set_major_formatter(FuncFormatter(format_count_tick))

    if log_y:
        ax.set_yscale("log")


def _plot_references(
    ax, counts: Sequence[float], values: Sequence[float], references: Sequence[str], anchor: str, normalized: bool
) -> None:
    if not references or not any(v > 0.0 for v in values):
        return
    valid_pos = [v for v in values if v > 0.0]
    y_min_bound = min(valid_pos) * 0.25
    y_max_bound = max(valid_pos) * 4.0

    for ref_key in references:
        ref_vals = compute_reference_curve(counts, values, ref_key, anchor=anchor, normalized=normalized)
        if not ref_vals:
            continue
        label, _, color = COMPLEXITY_SPECS[ref_key]
        suffix = " per item" if normalized else ""
        ax.plot(counts, ref_vals, linestyle=":", linewidth=1.6, color=color, alpha=0.85, label=f"ref {label}{suffix}")

    if ax.get_yscale() == "log":
        ax.set_ylim(bottom=max(y_min_bound, 1e-4), top=y_max_bound)


def _plot_series_panel(
    ax,
    rows: List[Dict[str, Union[str, float]]],
    counts: List[float],
    series: List[Tuple[str, str, str]],
    compare_rows: Optional[List[Dict[str, Union[str, float]]]],
    primary_label: str,
    compare_label: str,
    references: Sequence[str],
    ref_target: str,
    ref_anchor: str,
    log_x: bool,
    log_y: bool,
    x_tick_step: float,
    x_label: str,
    normalized: bool = False,
) -> None:
    def transform(vals: List[float]) -> List[float]:
        if not normalized:
            return vals
        return [(v * 1000.0) / c if c > 0 else 0.0 for v, c in zip(vals, counts)]

    for key, label, marker in series:
        raw_vals = [float(row[key]) for row in rows]
        vals = transform(raw_vals)
        if key.endswith("_avg_ms"):
            raw_spread = [float(row[key.replace("_avg_ms", "_run_stdev_ms")]) for row in rows]
            spread = transform(raw_spread)
        else:
            spread = None
        prefix = f"{primary_label} " if compare_rows else ""
        if log_y:
            positive_pairs = [(c, v, s if spread else 0.0) for c, v, s in zip(counts, vals, spread or [0.0] * len(vals)) if v > 0.0]
            if not positive_pairs:
                continue
            pc, pv, ps = zip(*positive_pairs)
            yerr = ps if spread is not None else None
            ax.errorbar(pc, pv, yerr=yerr, marker=marker, linewidth=2, capsize=3, label=f"{prefix}{label}")
        else:
            ax.errorbar(counts, vals, yerr=spread, marker=marker, linewidth=2, capsize=3, label=f"{prefix}{label}")

    if compare_rows:
        compare_counts = [float(row["swept_value"]) for row in compare_rows]
        cmp_vals = [float(row[ref_target]) for row in compare_rows]
        if normalized:
            cmp_vals = [(v * 1000.0) / c if c > 0 else 0.0 for v, c in zip(cmp_vals, compare_counts)]
        cmp_spread = None
        if ref_target.endswith("_avg_ms"):
            raw_s = [float(row[ref_target.replace("_avg_ms", "_run_stdev_ms")]) for row in compare_rows]
            cmp_spread = [(s * 1000.0) / c if (normalized and c > 0) else s for s, c in zip(raw_s, compare_counts)]
        target_name = next((lbl for k, lbl, _ in series if k == ref_target), ref_target)
        ax.errorbar(
            compare_counts,
            cmp_vals,
            yerr=cmp_spread,
            marker="*",
            markersize=8,
            linestyle="--",
            linewidth=2.2,
            capsize=3,
            color="#111111",
            label=f"{compare_label} {target_name}",
        )

    if references:
        ref_vals = transform([float(row[ref_target]) for row in rows])
        _plot_references(ax, counts, ref_vals, references, ref_anchor, normalized)

    y_label = "Time per item (us / N)" if normalized else "Time (ms)"
    _configure_axes(ax, counts, log_x, log_y, x_tick_step, x_label, y_label)
    ax.legend(fontsize=8, loc="best")


def plot_render_culling_report(report_path: Path, output_path: Path, show: bool = False) -> int:
    import matplotlib.pyplot as plt

    report = json.loads(report_path.read_text(encoding="utf-8"))
    if report.get("comparison") != "render-order-culling":
        raise ValueError(f"Unsupported render report comparison: {report.get('comparison')}")

    scope = report.get("scope", {})
    cases = report.get("aggregates_by_case", {})
    res_cases = report.get("resource_summary", {})
    fig, (ax_time, ax_work) = plt.subplots(1, 2, figsize=(15, 6))

    # Left panel: isolated CPU preparation + selection + step times (excluding vsync-paced render wait)
    labels: List[str] = []
    prep_means: List[float] = []
    sel_means: List[float] = []
    step_means: List[float] = []
    for cname, arms in cases.items():
        for arm_name, slices in arms.items():
            for slice_name in ("combined", "clean_frames", "edit_frames"):
                if slice_name not in slices:
                    continue
                if cname != "dense-z-edit" and slice_name != "combined":
                    continue
                if cname == "dense-z-edit" and slice_name == "combined":
                    continue
                s = slices[slice_name]
                short_arm = arm_name.replace("candidate-", "").replace("baseline-", "base-")
                short_slice = "" if slice_name == "combined" else f" [{slice_name.replace('_frames', '')}]"
                labels.append(f"{cname}\n{short_arm}{short_slice}")
                prep = (s.get("draw_order_prepare_ms") or {}).get("mean_ms") or 0.0
                sel = (s.get("visible_selection_ms") or {}).get("mean_ms") or 0.0
                step = (s.get("complete_step_ms") or {}).get("mean_ms") or 0.0
                prep_means.append(prep * 1000.0)   # convert ms -> us
                sel_means.append(sel * 1000.0)
                step_means.append(step * 1000.0)

    x = list(range(len(labels)))
    width = 0.26
    ax_time.bar([i - width for i in x], sel_means, width=width, label="Visible selection (us)", color="#1f77b4")
    ax_time.bar(x, prep_means, width=width, label="Draw-order prepare (us)", color="#ff7f0e")
    ax_time.bar([i + width for i in x], step_means, width=width, label="Complete step (us)", color="#2ca02c")
    ax_time.set_xticks(x)
    ax_time.set_xticklabels(labels, rotation=28, ha="right", fontsize=8)
    ax_time.set_yscale("symlog", linthresh=5)
    ax_time.set_ylabel("Isolated CPU Time (us / frame, symlog)")
    ax_time.set_title("Render Culling & Z-Ordering CPU Subscopes (unpaced by VSync)")
    ax_time.grid(True, axis="y", linestyle="--", alpha=0.35)
    ax_time.legend(fontsize=8, loc="upper left")

    # Right panel: algorithmic work counters from resource proofs (full scans vs candidate visits vs sorts)
    work_labels: List[str] = []
    full_visits: List[int] = []
    cand_visits: List[int] = []
    sorts_list: List[int] = []
    relinks_list: List[int] = []

    for cname, arms in res_cases.items():
        for arm_name, arm_data in arms.items():
            cw = arm_data.get("culling_work") or {}
            if not cw:
                continue
            work_labels.append(f"{cname}\n{arm_name.replace('candidate-', '')}")
            full_visits.append(int(cw.get("full_population_visits", 0)))
            cand_visits.append(int(cw.get("candidate_visits", 0)))
            sorts_list.append(int(cw.get("sprite_order_sorts", 0)))
            relinks_list.append(int(cw.get("index_relinks", 0)))

    for ctrl_name, ctrl_strategies in report.get("work_resource_controls", {}).items():
        for strat, cdata in ctrl_strategies.items():
            cw = cdata.get("culling_work") or {}
            work_labels.append(f"ctrl:{ctrl_name}\n{strat}")
            full_visits.append(int(cw.get("full_population_visits", 0)))
            cand_visits.append(int(cw.get("candidate_visits", 0)))
            sorts_list.append(int(cw.get("sprite_order_sorts", 0)))
            relinks_list.append(int(cw.get("index_relinks", 0)))

    wx = list(range(len(work_labels)))
    ax_work.bar([i - 0.2 for i in wx], [max(v, 0) for v in full_visits], width=0.35, label="Full-population visits", color="#9467bd")
    ax_work.bar([i + 0.2 for i in wx], [max(v, 0) for v in cand_visits], width=0.35, label="Candidate visits", color="#8c564b")
    ax_work.set_yscale("symlog", linthresh=10)
    ax_work.set_xticks(wx)
    ax_work.set_xticklabels(work_labels, rotation=28, ha="right", fontsize=8)
    ax_work.set_ylabel("Work Counter (total over run, symlog)")
    ax_work.set_title(f"Render Culling Work & Controls ({scope.get('measured_steps', '?')} measured frames)")
    ax_work.grid(True, axis="y", linestyle="--", alpha=0.35)
    max_work = max([*full_visits, *cand_visits, 10])
    ax_work.set_ylim(bottom=0, top=max_work * 12.0)
    for i, (s_cnt, r_cnt) in enumerate(zip(sorts_list, relinks_list)):
        top = max(full_visits[i], cand_visits[i], 1)
        ax_work.annotate(f"s={s_cnt}\nr={r_cnt}", (i, top), textcoords="offset points", xytext=(0, 3), ha="center", fontsize=6.5)
    ax_work.legend(fontsize=8, loc="upper right")

    fig.suptitle(
        f"Platformator Render Order & Culling (reps={scope.get('repetitions', '?')}, "
        f"warmup={scope.get('warmup_steps', '?')}, measured={scope.get('measured_steps', '?')})",
        fontsize=12,
    )
    fig.tight_layout()
    fig.savefig(output_path, dpi=180)
    print(f"[Plot] Saved render-culling graph: {output_path}")
    if show:
        plt.show()
    return 0


def parse_references(raw: Optional[str]) -> List[str]:
    if not raw:
        return []
    refs = [tok.strip() for tok in raw.split(",") if tok.strip()]
    unknown = [tok for tok in refs if tok not in COMPLEXITY_SPECS]
    if unknown:
        raise ValueError(f"Unknown reference complexity '{unknown[0]}'. Allowed: {', '.join(COMPLEXITY_SPECS)}")
    return refs


def main() -> int:
    import matplotlib.pyplot as plt

    parser = argparse.ArgumentParser(description="Plot Platformator benchmark scaling CSV or render culling report.")
    parser.add_argument("--input", help="Combined CSV path generated by sweep_scaling.py")
    parser.add_argument("--compare", help="Optional second schema-v3 CSV (e.g. multi-worker sweep) to overlay")
    parser.add_argument("--primary-label", default="", help="Label prefix for primary CSV when --compare is used")
    parser.add_argument("--compare-label", default="", help="Label prefix for compare CSV when --compare is used")
    parser.add_argument("--render-report", help="Plot a render-order-culling report.json from capture_linear_render.py")
    parser.add_argument("--output", default="benchmark_scaling.png", help="Output image path (PNG).")
    parser.add_argument("--log-x", action="store_true", help="Use logarithmic X axis for counts.")
    parser.add_argument("--log-y", action="store_true", help="Use logarithmic Y axis for durations.")
    parser.add_argument("--log-log", action="store_true", help="Use logarithmic X and Y axes.")
    parser.add_argument("--two-panel", action="store_true", help="Plot linear Time (ms) alongside log-log scaling with reference lines.")
    parser.add_argument("--normalized", action="store_true", help="Plot amortized cost per item (us / N) on the scaling panel.")
    parser.add_argument("--include-cleanup", action="store_true", help="Include cleanup_avg_ms series.")
    parser.add_argument("--include-physics", action="store_true", help="Include physics_avg_ms series.")
    parser.add_argument("--references", default="", help="Comma-separated reference complexity curves: 1,logN,N,NlogN,N2")
    parser.add_argument("--ref-target", default="complete_step_avg_ms", help="Metric key to anchor reference curves to.")
    parser.add_argument("--ref-anchor", choices=("first", "mid", "last", "fit"), default="mid", help="How to anchor reference curves.")
    parser.add_argument("--title", default="", help="Optional custom figure title.")
    parser.add_argument("--x-tick-step", type=float, default=100.0, help="Major tick step for a linear X axis (default: 100).")
    parser.add_argument("--show", action="store_true", help="Show interactive window in addition to saving file.")
    args = parser.parse_args()

    if args.render_report:
        return plot_render_culling_report(Path(args.render_report), Path(args.output), show=args.show)

    if not args.input:
        raise ValueError("Provide --input CSV path or --render-report JSON path.")

    rows = load_rows(args.input, require_workload=bool(args.compare))
    compare_rows = load_rows(args.compare, require_workload=True) if args.compare else None
    if compare_rows is not None:
        validate_comparison(rows, compare_rows)

    x_axis_key, x_axis_label = select_x_axis(rows)
    rows.sort(key=lambda item: float(item.get(x_axis_key, item["requested_count"])))
    if compare_rows is not None:
        compare_rows.sort(key=lambda item: float(item.get(x_axis_key, item["requested_count"])))

    counts = [float(row.get(x_axis_key, row["requested_count"])) for row in rows]
    series = _default_series(args.include_cleanup, args.include_physics)
    references = parse_references(args.references)
    log_x = args.log_x or args.log_log
    log_y = args.log_y or args.log_log

    primary_label = args.primary_label or f"w={int(float(rows[0]['workers']))}"
    compare_label = args.compare_label or (f"w={int(float(compare_rows[0]['workers']))}" if compare_rows else "")
    title = args.title or build_title(rows, compare_rows)

    if args.two_panel:
        fig, (ax_lin, ax_scale) = plt.subplots(1, 2, figsize=(15, 6))
        _plot_series_panel(
            ax_lin, rows, counts, series, compare_rows, primary_label, compare_label,
            references=[], ref_target=args.ref_target, ref_anchor=args.ref_anchor,
            log_x=False, log_y=False, x_tick_step=args.x_tick_step, x_label=x_axis_label, normalized=False,
        )
        ax_lin.set_title("Linear Time & Worker Comparison")

        scale_refs = references or ["logN", "N", "NlogN", "N2"]
        _plot_series_panel(
            ax_scale, rows, counts, [(args.ref_target, next((l for k, l, _ in series if k == args.ref_target), args.ref_target), "o")],
            compare_rows, primary_label, compare_label,
            references=scale_refs, ref_target=args.ref_target, ref_anchor=args.ref_anchor,
            log_x=True, log_y=(not args.normalized), x_tick_step=args.x_tick_step, x_label=x_axis_label, normalized=args.normalized,
        )
        ax_scale.set_title("Normalized Cost (us / N)" if args.normalized else f"Log-Log Scaling ({args.ref_target})")
        fig.suptitle(title, fontsize=13)
    else:
        fig, ax = plt.subplots(figsize=(10, 6))
        _plot_series_panel(
            ax, rows, counts, series, compare_rows, primary_label, compare_label,
            references=references, ref_target=args.ref_target, ref_anchor=args.ref_anchor,
            log_x=log_x, log_y=log_y, x_tick_step=args.x_tick_step, x_label=x_axis_label, normalized=args.normalized,
        )
        ax.set_title(title)

    fig.tight_layout()
    fig.savefig(args.output, dpi=180)
    print(f"[Plot] Saved graph: {args.output}")

    if args.show:
        plt.show()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
