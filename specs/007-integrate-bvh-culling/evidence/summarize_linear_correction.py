#!/usr/bin/env python3
"""Validate and summarize the fresh T095 ordered-linear comparison bundles."""

import csv
import json
from collections import Counter
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "benchmark"))
from sweep_spatial import image_qualification_matches, validation_key  # noqa: E402

BUNDLE = HERE / "convergence" / "linear-correction"
SOURCE = None
METHODS = ["none", "linear", "bvh", "segmented"]


def rows(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def metric(value, applicable=True):
    if not applicable or value in (None, ""):
        return "—"
    return f"{float(value):.6f}"


def records(suite, mode):
    directory = BUNDLE / suite / mode
    return json.loads((directory / "eligibility.json").read_text()), directory


def verify_bundle(suite, expected_cases):
    global SOURCE
    validation, validation_dir = records(suite, "validation")
    timing, timing_dir = records(suite, "timing")
    resources, resource_dir = records(suite, "resources")
    expected_configs = expected_cases * len(METHODS) * 2
    if len(validation) != expected_configs or len(timing) != expected_configs * 5 or len(resources) != expected_configs:
        raise AssertionError(f"Unexpected process counts for {suite}")
    for mode_rows in (validation, timing, resources):
        if not all(record.get("eligible") for record in mode_rows):
            raise AssertionError(f"Ineligible capture in {suite}")
    validation_keys = Counter(validation_key(json.loads((validation_dir / record["manifest"]).read_text()))
                              for record in validation)
    timing_keys = Counter(validation_key(json.loads((timing_dir / record["manifest"]).read_text()))
                          for record in timing)
    resource_keys = Counter(validation_key(json.loads((resource_dir / record["manifest"]).read_text()))
                            for record in resources)
    if any(timing_keys[key] != 5 * count for key, count in validation_keys.items()):
        raise AssertionError(f"Unmatched timing identity in {suite}")
    if resource_keys != validation_keys:
        raise AssertionError(f"Unmatched resource identity in {suite}")
    for record in validation:
        manifest = json.loads((validation_dir / record["manifest"]).read_text())
        if suite != "replay" and not image_qualification_matches(manifest):
            raise AssertionError(f"Missing exact rendered image proof: {record['case_id']}")
        source = manifest["build"]["source_sha256"]
        if SOURCE is None:
            SOURCE = source
        elif SOURCE != source:
            raise AssertionError("Correction bundles use different source fingerprints")
    return {
        "validation": len(validation), "timing": len(timing), "resources": len(resources),
        "cases": sorted({record["case_id"] for record in validation}),
        "source": SOURCE,
    }


def main():
    counts = {
        "replay": verify_bundle("replay", 4),
        "engine": verify_bundle("engine", 2),
        "engine-dynamic": verify_bundle("engine-dynamic", 2),
    }
    cases = []
    comparisons = []
    for suite in ("replay", "engine", "engine-dynamic"):
        case_path = BUNDLE / suite / "cases" / "cases.json"
        if suite == "engine-dynamic":
            case_path = BUNDLE / suite / "cases" / "cases.json"
        case_manifest = json.loads(case_path.read_text())
        for case in case_manifest["cases"]:
            validation_count = sum(record["case_id"] == case["case_id"]
                                   for record in records(suite, "validation")[0])
            timing_count = sum(record["case_id"] == case["case_id"]
                               for record in records(suite, "timing")[0])
            resource_count = sum(record["case_id"] == case["case_id"]
                                 for record in records(suite, "resources")[0])
            cases.append({
                "suite": suite, "case_id": case["case_id"], "case_sha256": case["sha256"],
                "domain": case["domain"], "scenario": case.get("scenario", ""),
                "parameters": json.dumps(case["parameters"], sort_keys=True, separators=(",", ":")),
                "workers": "1,6", "methods": ",".join(METHODS),
                "validation_processes": validation_count, "timing_processes": timing_count,
                "resource_processes": resource_count, "source_sha256": SOURCE,
            })
        for summary in rows(BUNDLE / suite / "timing" / "summary.csv"):
            metric_scope = "spatial_replay" if suite == "replay" else "render"
            comparisons.append({
                "suite": suite, "case_id": summary["case_id"],
                "case_sha256": summary["case_sha256"], "scenario": summary.get("scenario", ""),
                "workers": summary["workers"], "broad_phase": summary["broad_phase"],
                "render_culling": summary["render_culling"],
                "scope": metric_scope,
                "mean_across_process_means_ms": summary.get(f"scope.{metric_scope}.avg.across_process_mean", ""),
                "mean_process_stdev_ms": summary.get(f"scope.{metric_scope}.avg.across_process_stdev", ""),
                "mean_process_p95_ms": summary.get(f"scope.{metric_scope}.p95.across_process_mean", ""),
                "render_culling_ms": summary.get("scope.render_culling.avg.across_process_mean", ""),
                "index_query_ms": summary.get("scope.index_query.avg.across_process_mean", ""),
                "result_reduce_ms": summary.get("scope.result_reduce.avg.across_process_mean", ""),
                "visible_candidate_count": summary.get("counter.visible_candidate_count.avg.across_process_mean", ""),
                "submitted_sprite_count": summary.get("counter.submitted_sprite_count.avg.across_process_mean", ""),
                "visibility_cache_hit_count": summary.get("counter.visibility_cache_hit_count.avg.across_process_mean", ""),
                "initialization_ms": summary.get("initialization.total_ms.across_process_mean", ""),
                "eligible_process_count": summary["eligible_process_count"],
                "source_sha256": summary["source_sha256"],
            })

    def write_csv(name, values):
        with (HERE / name).open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(values[0]))
            writer.writeheader()
            writer.writerows(values)

    write_csv("linear-correction-coverage.csv", cases)
    write_csv("linear-correction-comparisons.csv", comparisons)
    replay_dynamic = [row for row in comparisons if row["suite"] == "replay" and
                      row["case_id"] in {"visibility-n1000-change10-moving", "visibility-n1000-change10-pan"}]
    live_dynamic = [row for row in comparisons if row["suite"] == "engine-dynamic"]
    lines = [
        "# Ordered linear visibility correction evidence", "",
        f"Final source fingerprint: `{SOURCE}`.",
        "All validation, timing, and resource captures share that fingerprint and qualification identity.",
        "Timing entries are means of five fresh-process means; p95 values are means of per-process p95 values.",
        "These measurements describe the corrected source and do not form a causal before/after ratio against the older source.", "",
        "## Capture scope", "",
        "Replay covers 1,000 sprites at 0% and 100% visibility, plus moving and panning 10%-changed traces (10 warmup and 30 measured steps).",
        "Actual engine runs cover static 0% and 100% visibility as cache controls, and moving fixtures with 10% changed sprites at 1% and 100% initial visibility (120 warmup and 600 measured steps).",
        "Every applicable case uses none, linear, BVH, and segmented culling at one and six workers. Each timing configuration has five fresh processes; every resource capture links to its timing identity.",
        "All actual-render validation used the offscreen software renderer and passed exact same-frame pixel and order checks (720 frames per validation run).", "",
        "| Bundle | Cases | Validation | Timing | Resources |",
        "|---|---:|---:|---:|---:|",
    ]
    for suite, label in (("replay", "Replay"), ("engine", "Live static controls"),
                         ("engine-dynamic", "Live invalidated frames")):
        count = counts[suite]
        lines.append(f"| {label} | {len(count['cases'])} | {count['validation']} | {count['timing']} | {count['resources']} |")
    lines += ["", "## Actual live invalidated frames", "",
              "The moving fixture invalidates visibility each measured frame. `index_query` is zero for linear because its single query is fused with the authoritative-order exact-refinement pass. That O(N) pass is charged once to `result_reduce`; no sample is a cache hit.", "",
              "| Initial visible | Workers | Culling | Render ms | Culling ms | Index query ms | Result reduce ms | Visible candidates | Submitted | Cache hits |",
              "|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|"]
    for row in sorted(live_dynamic, key=lambda r: (r["case_id"], int(r["workers"]), r["render_culling"])):
        visible = "100%" if "visible100" in row["case_id"] else "1%"
        lines.append(f"| {visible} | {row['workers']} | {row['render_culling']} | "
                     f"{metric(row['mean_across_process_means_ms'])} | "
                     f"{metric(row['render_culling_ms'], row['render_culling'] != 'none')} | "
                     f"{metric(row['index_query_ms'], row['render_culling'] in {'linear', 'bvh'})} | "
                     f"{metric(row['result_reduce_ms'], row['render_culling'] != 'none')} | "
                     f"{metric(row['visible_candidate_count'], row['render_culling'] != 'none')} | "
                     f"{metric(row['submitted_sprite_count'])} | "
                     f"{metric(row['visibility_cache_hit_count'], row['render_culling'] != 'none')} |")
    lines += ["", "## Replay mutation comparisons", "",
              "The dynamic replay rows isolate recorded visibility maintenance and consumer publication. Unchanged 0% and 100% cases are retained as cache behavior controls.", "",
              "| Trace | Workers | Culling | Spatial replay ms | Index query ms | Result reduce ms |",
              "|---|---:|---|---:|---:|---:|"]
    for row in sorted(replay_dynamic, key=lambda r: (r["case_id"], int(r["workers"]), r["render_culling"])):
        lines.append(f"| {row['case_id'].removeprefix('visibility-n1000-')} | {row['workers']} | {row['render_culling']} | "
                     f"{metric(row['mean_across_process_means_ms'])} | "
                     f"{metric(row['index_query_ms'], row['render_culling'] in {'linear', 'bvh'})} | "
                     f"{metric(row['result_reduce_ms'], row['render_culling'] != 'none')} |")
    lines += ["", "The work-probe regression test confirms invalidated linear frames perform no candidate sort at sparse or full visibility, preserve submission order, exact-check all 1,001 sprites in the full case, and retain unchanged-frame caching. Work probes are disabled in these timing binaries.", "",
              "Raw process files, manifests, input traces, checksums, and eligibility records are retained locally under `convergence/linear-correction/`. Reproduce with [capture_linear_correction.py](capture_linear_correction.py); regenerate this report and its tables with [summarize_linear_correction.py](summarize_linear_correction.py). Compact ledgers are [linear-correction-coverage.csv](linear-correction-coverage.csv) and [linear-correction-comparisons.csv](linear-correction-comparisons.csv).", "",
              "## Limits", "",
              "This is a bounded correction check, not a full rerun of the 21-case replay or 11-case rendered visibility matrices. It covers one 1,000-sprite population, software rendering, and workers 1/6 on this host. No hardware-renderer or cross-machine conclusion follows."]
    (HERE / "linear-correction.md").write_text("\n".join(lines) + "\n")
    (HERE / "convergence" / "linear-correction" / "capture-counts.json").write_text(
        json.dumps(counts, indent=2) + "\n")
    print(json.dumps({"source_sha256": SOURCE, "bundles": counts}, indent=2))


if __name__ == "__main__":
    main()
