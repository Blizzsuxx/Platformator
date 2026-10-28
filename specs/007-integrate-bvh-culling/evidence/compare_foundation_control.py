#!/usr/bin/env python3
"""Compare T017 schema-3 control summaries against the pinned T002 baseline."""

import csv
import math
from pathlib import Path


EVIDENCE = Path(__file__).resolve().parent
BASELINE = EVIDENCE / "baseline"
CAPTURES = EVIDENCE / "foundation-control-captures"
SCENARIOS = (
    ("broad_phase", None),
    ("broad_phase_stress", None),
    ("narrow_phase", None),
    ("rigid_body_container", None),
    ("add_and_remove", None),
    ("moving_add_and_remove", None),
    ("persistent_contacts", 0),
    ("persistent_contacts", 2),
)
SCOPES = ("complete_step", "physics", "cleanup", "broad_phase", "narrow_phase", "resolve_collisions")


def read_csv(path):
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def summary_path(root, scenario, endpoints):
    suffix = f"-scripted-{endpoints}" if endpoints is not None else ""
    return root / f"{scenario}{suffix}.csv"


def key(row):
    return (row["scenario"], int(row["requested_count"]), int(row["workers"]), int(row["scripted_endpoints"]))


def main():
    output = EVIDENCE / "foundation-control-delta.csv"
    rows = []
    for workers in (1, 6):
        old_root = BASELINE / f"timing-pinned-{workers}"
        new_root = CAPTURES / "timing" / f"workers-{workers}"
        for scenario, endpoints in SCENARIOS:
            old_rows = {key(row): row for row in read_csv(summary_path(old_root, scenario, endpoints))}
            new_rows = {key(row): row for row in read_csv(summary_path(new_root, scenario, endpoints))}
            if old_rows.keys() != new_rows.keys():
                raise ValueError(f"T002/T017 population mismatch: {scenario}, workers={workers}")
            for identity, old in old_rows.items():
                new = new_rows[identity]
                if old["seed"] != new["seed"] or old["repetitions"] != new["repetitions"]:
                    raise ValueError(f"T002/T017 run parameter mismatch: {identity}")
                for scope in SCOPES:
                    mean_field = f"{scope}_avg_ms"
                    stdev_field = f"{scope}_run_stdev_ms"
                    baseline_mean = float(old[mean_field])
                    foundation_mean = float(new[mean_field])
                    delta = foundation_mean - baseline_mean
                    baseline_stdev = float(old[stdev_field])
                    foundation_stdev = float(new[stdev_field])
                    n_old = int(old["repetitions"])
                    n_new = int(new["repetitions"])
                    standard_error = math.sqrt(baseline_stdev**2 / n_old + foundation_stdev**2 / n_new)
                    rows.append({
                        "workers": workers,
                        "scenario": scenario,
                        "count": identity[1],
                        "scripted_endpoints": identity[3],
                        "scope": scope,
                        "control_mean_ms": f"{baseline_mean:.9g}",
                        "foundation_mean_ms": f"{foundation_mean:.9g}",
                        "delta_ms": f"{delta:.9g}",
                        "delta_percent": f"{delta / baseline_mean * 100:.6g}" if baseline_mean else "",
                        "control_process_stdev_ms": f"{baseline_stdev:.9g}",
                        "foundation_process_stdev_ms": f"{foundation_stdev:.9g}",
                        "delta_standard_errors": f"{delta / standard_error:.6g}" if standard_error else "",
                    })

    fields = list(rows[0])
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)

    print(f"Matched rows: {len(rows) // len(SCOPES)} populations, {len(rows)} scope deltas")
    for workers in (1, 6):
        for scope in ("complete_step", "physics", "broad_phase"):
            selected = [row for row in rows if int(row["workers"]) == workers and row["scope"] == scope]
            values = [float(row["delta_percent"]) for row in selected if row["delta_percent"]]
            severe = [row for row in selected if row["delta_percent"] and
                      float(row["delta_percent"]) >= 5 and float(row["delta_standard_errors"]) >= 2]
            print(f"workers={workers} {scope}: median_delta={sorted(values)[len(values)//2]:.2f}% "
                  f"range={min(values):.2f}%..{max(values):.2f}%; >5% and >=2SE increases={len(severe)}")
            for row in severe:
                print("  ", row)
    print(f"Wrote {output}")


if __name__ == "__main__":
    main()
