#!/usr/bin/env python3
import csv
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "benchmark"))

from spatial_results import (  # noqa: E402
    COUNTERS,
    LEGACY_COUNTERS,
    LEGACY_SCOPES,
    SCOPES,
    metric_applicability,
    pure_render_eligible,
    read_schema4_capture,
)
from sweep_scaling import parse_benchmark_csv  # noqa: E402
TRACE = Path(__file__).resolve().parent / "fixtures" / "spatial_trace_cli.jsonl"


def write_minimal_replay_bundle(root):
    output = Path(root) / "run.csv"
    domain = "collision"
    method = "bvh"
    metrics = {
        metric: metric_applicability(metric, "replay", broad_phase=method, render_culling="none",
                                    replay_domain=domain)
        for metric in SCOPES + COUNTERS
    }
    scopes = {metric: (100 if metric == "spatial_replay" else 0) if state == "applicable" else ""
              for metric, state in metrics.items() if metric in SCOPES}
    counters = {metric: 0 if state == "applicable" else "" for metric, state in metrics.items() if metric in COUNTERS}
    manifest = {
        "schema_version": 4, "capture_mode": "performance", "timing_eligible": True,
        "complete_capture": True, "status": "complete",
        "run": {"warmup_steps": 0, "requested_measured_steps": 1, "measured_steps": 1, "seed": 0,
                "dt": 1 / 120, "workers_requested": 1, "workers_effective": 1},
        "input": {"domain": "collision-replay", "case_id": "cli-contract",
                  "case_sha256": hashlib.sha256(TRACE.read_bytes()).hexdigest(), "trace_path": str(TRACE),
                  "bounds_source": "provided-aabb", "generator": {"name": "test", "revision": "1",
                  "source_hash": "b" * 64}, "parameters": {"population": 1},
                  "geometry_preparation": "excluded"},
        "selection": {"requested": {"broad_phase": method, "render_culling": "none"},
                      "effective": {"broad_phase": method, "render_culling": "none"}},
        "sample_counts": {"warmup": 0, "step": 0, "render": 0, "measured_replay": 1},
        "applicability": {"sample_kind": "replay", "replay_domain": domain, "metrics": metrics,
                          "units": {"scopes": "ns", "counters": "count"}},
        "boundaries": {}, "initialization": {"total_ns": 1},
        "validation": {"status": "not_run", "timing_eligible": True},
        "resources": {"status": "unavailable"},
        "build": {"source_sha256": "a" * 64}, "machine": {"logical_cpus": 1}, "dependencies": {"SDL": "3"},
    }
    raw_fields = ["schema_version", "sample_kind", "index"] + [name + "_ns" for name in SCOPES] + list(COUNTERS)
    raw_row = {"schema_version": 4, "sample_kind": "replay", "index": 0}
    raw_row.update({name + "_ns": value for name, value in scopes.items()})
    raw_row.update(counters)
    with output.with_suffix(".samples.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=raw_fields)
        writer.writeheader()
        writer.writerow(raw_row)
    summary_fields = ["schema_version", "category", "name", "samples", "avg", "median", "p95", "p99", "min", "max", "unit"]
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=summary_fields)
        writer.writeheader()
        for category, names in (("scope", SCOPES), ("counter", COUNTERS)):
            for name in names:
                field = name + "_ns" if category == "scope" else name
                value = raw_row[field]
                applicable = value != ""
                scaled = float(value) / (1e6 if category == "scope" else 1) if applicable else ""
                writer.writerow({"schema_version": 4, "category": category, "name": name,
                                 "samples": int(applicable), "avg": scaled, "median": scaled, "p95": scaled,
                                 "p99": scaled, "min": scaled, "max": scaled,
                                 "unit": "ms" if category == "scope" else "count"})
    output.with_suffix(".manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    return output


class SpatialMetricContractTest(unittest.TestCase):
    def test_schema_four_metric_names_extend_without_changing_legacy_names(self):
        self.assertEqual(
            LEGACY_SCOPES,
            ("complete_step", "physics", "cleanup", "scene_transition", "render", "broad_phase",
             "narrow_phase", "resolve_collisions"),
        )
        self.assertEqual(
            LEGACY_COUNTERS,
            ("object_count", "occupied_cell_count", "candidate_pair_count", "pending_narrow_phase_pair_count",
             "active_collision_count", "awake_dynamic_body_count", "queued_add_count", "queued_sync_count",
             "queued_remove_count", "collision_enter_event_count", "collision_exit_event_count"),
        )
        self.assertEqual(len(SCOPES), len(set(SCOPES)))
        self.assertEqual(len(COUNTERS), len(set(COUNTERS)))
        self.assertTrue(set(LEGACY_SCOPES) <= set(SCOPES))
        self.assertTrue(set(LEGACY_COUNTERS) <= set(COUNTERS))

    def test_domain_backend_and_sample_kind_applicability(self):
        self.assertEqual(metric_applicability("complete_step", "step"), "applicable")
        self.assertEqual(metric_applicability("complete_step", "render"), "inapplicable")
        self.assertEqual(metric_applicability("render", "render"), "applicable")
        self.assertEqual(metric_applicability("spatial_replay", "collision_replay"), "applicable")
        self.assertEqual(metric_applicability("bounds_prepare", "collision_replay"), "inapplicable")
        self.assertEqual(metric_applicability("candidate_pair_count", "replay", replay_domain="collision"), "applicable")
        self.assertEqual(metric_applicability("visible_candidate_count", "replay", render_culling="none",
                                               replay_domain="visibility"), "applicable")
        self.assertEqual(metric_applicability("submitted_sprite_count", "replay", replay_domain="visibility"),
                         "inapplicable")
        self.assertEqual(metric_applicability("occupied_cell_count", "step", broad_phase="bvh"), "inapplicable")
        self.assertEqual(metric_applicability("tree_node_count", "render", render_culling="linear"), "inapplicable")
        self.assertEqual(metric_applicability("visibility_cell_count", "render", render_culling="segmented"), "applicable")
        self.assertEqual(metric_applicability("submitted_sprite_count", "visibility_replay"), "inapplicable")
        self.assertEqual(metric_applicability("visible_candidate_count", "visibility_replay", render_culling="none"),
                         "applicable")
        self.assertEqual(metric_applicability("index_update", "render", render_culling="linear"), "applicable")
        self.assertEqual(metric_applicability("index_maintenance", "render", render_culling="segmented"),
                         "applicable")
        self.assertEqual(metric_applicability("result_reduce", "visibility_replay", render_culling="none"),
                         "inapplicable")
        self.assertEqual(metric_applicability("spatial_proxy_count", "visibility_replay", render_culling="none"),
                         "inapplicable")
        self.assertEqual(metric_applicability("bounds_prepare", "visibility_replay", render_culling="none"),
                         "inapplicable")
        self.assertEqual(metric_applicability("envelope_reuse_count", "collision_replay", broad_phase="segmented"),
                         "applicable")
        self.assertEqual(metric_applicability("envelope_reuse_count", "visibility_replay", render_culling="bvh"),
                         "inapplicable")

    def test_zero_cache_work_and_bypass_unknown_are_distinct(self):
        self.assertEqual(metric_applicability("query_count", "render", render_culling="bvh"), "applicable")
        self.assertEqual(metric_applicability("visible_candidate_count", "render", render_culling="bvh"),
                         "applicable")
        self.assertEqual(
            metric_applicability("visible_candidate_count", "render", render_culling="bvh", bypass=True),
            "unavailable",
        )
        self.assertTrue(pure_render_eligible("bvh", bypass_count=0, uncullable_count=0))
        self.assertFalse(pure_render_eligible("bvh", bypass_count=1, uncullable_count=0))
        self.assertFalse(pure_render_eligible("bvh", bypass_count=0, uncullable_count=1))
        self.assertFalse(pure_render_eligible("none", bypass_count=0, uncullable_count=0))

    def test_schema_four_reader_preserves_zero_and_rejects_inapplicable_values(self):
        with tempfile.TemporaryDirectory(prefix="platformator-spatial-reader-") as directory:
            capture = write_minimal_replay_bundle(directory)
            loaded = read_schema4_capture(capture)
            self.assertEqual(loaded["summary"][("counter", "candidate_pair_count")]["avg"], 0)
            raw = capture.with_suffix(".samples.csv")
            with raw.open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            rows[0]["render_ns"] = "0"
            with raw.open("w", newline="", encoding="utf-8") as handle:
                writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
                writer.writeheader()
                writer.writerows(rows)
            with self.assertRaisesRegex(ValueError, "must be blank"):
                read_schema4_capture(capture)

    def test_schema_four_reader_rejects_producer_applicability_override(self):
        with tempfile.TemporaryDirectory(prefix="platformator-spatial-applicability-") as directory:
            capture = write_minimal_replay_bundle(directory)
            manifest_path = capture.with_suffix(".manifest.json")
            manifest = json.loads(manifest_path.read_text())
            manifest["applicability"]["metrics"]["bounds_prepare"] = "applicable"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "contradicts the normative matrix"):
                read_schema4_capture(capture)

    def test_schema_four_reader_rejects_unknown_schema_and_truncated_raw_samples(self):
        with tempfile.TemporaryDirectory(prefix="platformator-spatial-strict-reader-") as directory:
            capture = write_minimal_replay_bundle(directory)
            manifest_path = capture.with_suffix(".manifest.json")
            manifest = json.loads(manifest_path.read_text())
            manifest["unexpected"] = True
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "extra top-level"):
                read_schema4_capture(capture)

            manifest.pop("unexpected")
            manifest["schema_version"] = 5
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "extra top-level"):
                read_schema4_capture(capture)

            manifest["schema_version"] = 4
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            raw_path = capture.with_suffix(".samples.csv")
            raw_path.write_text("schema_version,sample_kind,index," + ",".join(
                [name + "_ns" for name in SCOPES] + list(COUNTERS)) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "summary sample count mismatch|raw sample counts disagree"):
                read_schema4_capture(capture)

    def test_schema_four_dispatch_is_explicit_in_legacy_scaling_reader(self):
        with tempfile.TemporaryDirectory(prefix="platformator-spatial-dispatch-") as directory:
            capture = write_minimal_replay_bundle(directory)
            parsed = parse_benchmark_csv(capture)
            self.assertEqual(parsed["scope.spatial_replay.avg"], 0.0001)


if __name__ == "__main__":
    unittest.main(verbosity=2)
