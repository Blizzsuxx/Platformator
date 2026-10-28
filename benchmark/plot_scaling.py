#!/usr/bin/env python3
import argparse
import csv
import json
import math
from typing import Dict, List, Tuple, Union


STRING_COLUMNS = {"scenario", "sweep_parameter", "raw_directory", "raw_archive", "validation_mode", "timing_eligible"}
SCOPES = ("complete_step", "physics", "cleanup", "scene_transition", "render", "broad_phase", "narrow_phase", "resolve_collisions")


def load_rows(csv_path, *, domain=None, broad_phase=None, render_culling=None, workers=None,
              changed_percent=None, visible_percent=None, variant=None):
    with open(csv_path, newline="", encoding="utf-8") as handle:
        versions = {row.get("schema_version") for row in csv.DictReader(handle)}
    if versions == {"4"}:
        with open(csv_path, newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            required = {"schema_version", "capture_mode", "timing_eligible", "case_id", "domain", "parameters",
                        "population", "case_sha256", "broad_phase", "render_culling", "workers",
                        "bvh_maintenance", "bvh_padding", "bvh_query_execution", "source_sha256",
                        "requested_process_count", "eligible_process_count",
                        }
            if not required <= set(reader.fieldnames or ()):
                from spatial_results import read_schema4_capture

                read_schema4_capture(csv_path)
                raise ValueError("Valid schema-4 per-process capture; plot a sweep_spatial summary, not a raw run")
            raw_rows = list(reader)
        rows = []
        for row in raw_rows:
            if row["schema_version"] != "4" or row["capture_mode"] != "performance" or row["timing_eligible"] != "True":
                raise ValueError("Schema-4 plot input must contain timing-eligible performance summaries")
            if int(row["requested_process_count"]) < 5 or int(row["eligible_process_count"]) != int(row["requested_process_count"]):
                raise ValueError("Schema-4 plot requires five or more eligible fresh processes for every row")
            if domain and row["domain"] != domain:
                continue
            if broad_phase and row["broad_phase"] != broad_phase:
                continue
            if render_culling and row["render_culling"] != render_culling:
                continue
            if workers and int(row["workers"]) != workers:
                continue
            parameters = json.loads(row["parameters"])
            if changed_percent is not None and parameters.get("changed_percent") != changed_percent:
                continue
            if visible_percent is not None and parameters.get("visible_percent") != visible_percent:
                continue
            if variant and parameters.get("variant") != variant:
                continue
            if not row["population"]:
                raise ValueError("Schema-4 row has no population x-axis")
            scope = "spatial_replay" if row["domain"] in {"collision", "visibility"} else "complete_step"
            if f"scope.{scope}.avg.across_process_mean" not in row or f"scope.{scope}.p95.across_process_mean" not in row:
                raise ValueError(f"Schema-4 {row['domain']} summary lacks {scope} process statistics")
            average, p95 = row[f"scope.{scope}.avg.across_process_mean"], row[f"scope.{scope}.p95.across_process_mean"]
            if not average or not p95:
                raise ValueError(f"Schema-4 summary lacks applicable {scope} timing metrics")
            row["schema_version"] = 4
            row["sweep_parameter"] = "population"
            row["requested_count"] = float(row["population"])
            row["swept_value"] = float(row["population"])
            row["scenario"] = row["domain"]
            row["repetitions"] = float(row["eligible_process_count"])
            row["complete_step_avg_ms"] = float(average)
            row["complete_step_p95_ms"] = float(p95)
            row["complete_step_run_stdev_ms"] = float(row.get(f"scope.{scope}.avg.across_process_stdev") or 0)
            rows.append(row)
        if not rows:
            raise ValueError("No schema-4 rows match the requested filters")
        configurations = {(row["domain"], row["broad_phase"], row["render_culling"], row["workers"],
                           row["bvh_maintenance"], row["bvh_padding"], row["bvh_query_execution"],
                           row["source_sha256"]) for row in rows}
        parameter_shapes = []
        for row in rows:
            value = json.loads(row["parameters"])
            value.pop("population", None)
            parameter_shapes.append(value)
        if len(configurations) != 1 or any(value != parameter_shapes[0] for value in parameter_shapes[1:]):
            raise ValueError("Plot one schema-4 method/profile/worker and one workload variant/fraction per input")
        if len({row["requested_count"] for row in rows}) != len(rows):
            raise ValueError("Schema-4 population values must be unique after filtering")
        return rows
    if versions != {"3"}:
        raise ValueError("Aggregate cannot mix schema 3/4 rows or use an unknown schema")
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
        "broad_count": "Requested broad-phase object count",
        "contact_pair_count": "Persistent confirmed contact pairs",
        "narrow_count": "Requested narrow-phase object count",
        "total_body_count": "Requested total body count",
        "churn_count": "Per-step add/remove count",
        "steady_count": "Steady live object count",
        "population": "Spatial replay population",
    }
    return labels.get(sweep_parameter, "Requested count")


def select_x_axis(rows: List[Dict[str, Union[str, float]]]) -> Tuple[str, str]:
    return "swept_value", sweep_label(str(rows[0]["sweep_parameter"]))


def build_title(rows: List[Dict[str, Union[str, float]]]) -> str:
    scenario = str(rows[0].get("scenario", "unknown"))
    sweep_parameter = str(rows[0].get("sweep_parameter", "requested_count"))

    if scenario in {"add_and_remove", "moving_add_and_remove"}:
        steady_counts = {float(row.get("steady_count", 0.0)) for row in rows}
        if len(steady_counts) == 1:
            steady_count = next(iter(steady_counts))
            return f"Platformator Scaling: {scenario} ({sweep_parameter}, steady={int(steady_count)})"

    return f"Platformator Scaling: {scenario}"


def main() -> int:
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter, MultipleLocator

    parser = argparse.ArgumentParser(description="Plot Platformator benchmark scaling CSV.")
    parser.add_argument("--input", required=True, help="Combined CSV path generated by sweep_scaling.py")
    parser.add_argument(
        "--output",
        default="benchmark_scaling.png",
        help="Output image path (PNG).",
    )
    parser.add_argument(
        "--log-x",
        action="store_true",
        help="Use logarithmic X axis for counts.",
    )
    parser.add_argument(
        "--x-tick-step",
        type=float,
        default=100.0,
        help="Major tick step for a linear X axis (default: 100).",
    )
    parser.add_argument(
        "--show",
        action="store_true",
        help="Show interactive window in addition to saving file.",
    )
    parser.add_argument("--domain", choices=("collision", "visibility", "engine"))
    parser.add_argument("--broad-phase", choices=("segmented", "bvh"))
    parser.add_argument("--render-culling", choices=("none", "linear", "bvh", "segmented"))
    parser.add_argument("--workers", type=int)
    parser.add_argument("--changed-percent", type=float)
    parser.add_argument("--visible-percent", type=float)
    parser.add_argument("--variant")
    args = parser.parse_args()

    rows = load_rows(args.input, domain=args.domain, broad_phase=args.broad_phase,
                     render_culling=args.render_culling, workers=args.workers,
                     changed_percent=args.changed_percent, visible_percent=args.visible_percent,
                     variant=args.variant)
    if not rows:
        raise ValueError("Input CSV has no data rows.")

    x_axis_key, x_axis_label = select_x_axis(rows)
    rows.sort(key=lambda item: float(item.get(x_axis_key, item["requested_count"])))
    counts = [float(row.get(x_axis_key, row["requested_count"])) for row in rows]
    if rows[0].get("schema_version") == 4:
        series = [("complete_step_avg_ms", "Mean process time (ms)", "o"),
                  ("complete_step_p95_ms", "Mean per-process p95 (ms)", "s")]
    else:
        series = [
            ("complete_step_avg_ms", "Complete step mean", "o"),
            ("complete_step_p95_ms", "Complete step p95 (mean across runs)", "s"),
            ("broad_phase_avg_ms", "Broad Phase Avg", "^"),
            ("narrow_phase_avg_ms", "Narrow Phase Avg", "D"),
            ("resolve_collisions_avg_ms", "Collision Resolution Avg", "v"),
        ]

    fig, ax = plt.subplots(figsize=(10, 6))

    for key, label, marker in series:
        values = [float(row[key]) for row in rows]
        spread = [float(row.get(key.replace("_avg_ms", "_run_stdev_ms"), 0) or 0) for row in rows] if key.endswith("_avg_ms") else None
        ax.errorbar(counts, values, yerr=spread, marker=marker, linewidth=2, capsize=3, label=label)

    ax.set_title(build_title(rows))
    ax.set_xlabel(x_axis_label)
    ax.set_ylabel("Time (ms)")
    ax.grid(True, linestyle="--", alpha=0.4)

    if args.log_x:
        ax.set_xscale("log")
        ax.set_xticks(counts)
        ax.xaxis.set_major_formatter(FuncFormatter(format_count_tick))
        ax.minorticks_off()
    else:
        if args.x_tick_step <= 0.0:
            raise ValueError("--x-tick-step must be greater than zero.")

        ax.set_xlim(left=0.0)
        ax.xaxis.set_major_locator(MultipleLocator(args.x_tick_step))
        ax.xaxis.set_major_formatter(FuncFormatter(format_count_tick))

    ax.legend()

    fig.tight_layout()
    fig.savefig(args.output, dpi=180)
    print(f"[Plot] Saved graph: {args.output}")

    if args.show:
        plt.show()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
