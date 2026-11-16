import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("sweep_scaling", Path(__file__).resolve().parents[1] / "sweep_scaling.py")
sweep = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sweep)

SCOPES = ["complete_step", "physics", "cleanup", "scene_transition", "render", "broad_phase", "narrow_phase", "resolve_collisions"]
COUNTERS = ["object_count", "occupied_cell_count", "candidate_pair_count", "pending_narrow_phase_pair_count", "active_collision_count",
            "awake_dynamic_body_count", "queued_add_count", "queued_sync_count", "queued_remove_count", "collision_enter_event_count",
            "collision_exit_event_count"]


def fixture(path, value=1, workers=1, seed=0, warmup=0, repetition=0):
    with path.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["schema_version", "category", "name", "samples", "avg", "median", "p95", "p99", "min", "max", "unit"])
        for scope in SCOPES:
            metric = 0 if scope == "render" else value
            writer.writerow([3, "scope", scope, 0 if scope == "render" else 10, metric, metric, metric, metric, metric, metric, "ms"])
        for counter in COUNTERS:
            writer.writerow([3, "counter", counter, 10, 1, "", "", "", 1, 1, "count"])
    path.with_suffix(".manifest.json").write_text(json.dumps({"schema_version": 3, "workers": workers, "seed": seed,
        "dt": 1 / 120, "measured_steps": 10, "requested_measured_steps": 10, "warmup_steps": warmup, "repetitions": 5, "repetition_index": repetition,
        "rendering": False, "render_samples": 0, "complete_capture": True, "scenario": "broad_phase",
        "validation_mode": "boundary-only", "timing_eligible": True,
        "build": {"commit": "fixture", "dirty": False, "source_sha256": "a" * 64, "compiler": "fixture", "flags": "-O3", "configuration": "Release", "sanitizer": "none", "work_probes": False, "debug_tools": False},
        "machine": {"cpu": "fixture"}, "dependencies": {"test": "fixture"}, "boundaries": {name: "fixture" for name in SCOPES},
        "physical_invariants": {"finite": True, "populations_valid": True, "supports_valid": True}}))
    with path.with_suffix(".samples.csv").open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["schema_version", "sample_kind", "index"] + [name + "_ns" for name in SCOPES] + COUNTERS)
        for index in range(10):
            writer.writerow([3, "step", index] + [0 if name == "render" else int(value * 1e6) for name in SCOPES] + [1] * len(COUNTERS))


class SweepContract(unittest.TestCase):
    def test_schema_three_is_live_and_two_is_historical(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.csv"
            for version in (2, 3):
                fixture(path)
                manifest_path = path.with_suffix(".manifest.json")
                manifest = json.loads(manifest_path.read_text())
                manifest["schema_version"] = version
                manifest_path.write_text(json.dumps(manifest))
                for csv_path in (path, path.with_suffix(".samples.csv")):
                    with csv_path.open() as handle:
                        rows = list(csv.reader(handle))
                    for row in rows[1:]:
                        row[0] = str(version)
                    with csv_path.open("w", newline="") as handle:
                        csv.writer(handle).writerows(rows)
                if version == 2:
                    with self.assertRaises(ValueError):
                        sweep.parse_benchmark_csv(path)
                else:
                    sweep.parse_benchmark_csv(path)
                    manifest["schema_version"] = 2
                    manifest_path.write_text(json.dumps(manifest))
                    with self.assertRaises(ValueError):
                        sweep.parse_benchmark_csv(path)

    def test_raw_removed_or_duplicate_counter_rejects(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.csv"
            old_metric = "collision_stay_event_count"
            for extra in (old_metric, "collision_enter_event_count"):
                fixture(path)
                raw = path.with_suffix(".samples.csv")
                with raw.open() as handle:
                    rows = list(csv.reader(handle))
                rows[0].append(extra)
                for row in rows[1:]:
                    row.append("0")
                with raw.open("w", newline="") as handle:
                    csv.writer(handle).writerows(rows)
                with self.assertRaises(ValueError):
                    sweep.parse_benchmark_csv(path)

    def test_persistent_contact_count_and_recipient_arguments(self):
        from types import SimpleNamespace
        for recipients in (0, 1, 2):
            command = sweep.apply_count_argument(["runner"], SimpleNamespace(
                scenario="persistent_contacts", scripted_endpoints=recipients), 100)
            self.assertEqual(command, ["runner", "--contact-pair-count", "100", "--scripted-endpoints", str(recipients)])
        self.assertEqual(sweep.sweep_parameter_name("persistent_contacts"), "contact_pair_count")


    def test_legacy_and_incomplete_data_reject(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.csv"
            path.write_text("category,name,avg\nscope,frame,1\n")
            with self.assertRaises(ValueError):
                sweep.parse_benchmark_csv(str(path))
            fixture(path)
            text = path.read_text().replace("scope,complete_step,", "scope,unknown_metric,")
            path.write_text(text)
            with self.assertRaises(ValueError):
                sweep.parse_benchmark_csv(str(path))

    def test_missing_manifest_and_failed_invariants_reject(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.csv"
            fixture(path)
            path.with_suffix(".manifest.json").unlink()
            with self.assertRaises((ValueError, FileNotFoundError)):
                sweep.parse_benchmark_csv(str(path))
            fixture(path)
            manifest = json.loads(path.with_suffix(".manifest.json").read_text())
            manifest["physical_invariants"]["finite"] = False
            path.with_suffix(".manifest.json").write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                sweep.parse_benchmark_csv(str(path))

    def test_missing_build_provenance_rejects(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.csv"
            fixture(path)
            manifest_path = path.with_suffix(".manifest.json")
            manifest = json.loads(manifest_path.read_text())
            del manifest["build"]["source_sha256"]
            manifest_path.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                sweep.parse_benchmark_csv(path)

    def test_five_processes_per_population_preserve_raw_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "aggregate.csv"
            runner = Path(directory) / "runner"
            runner.touch()
            calls = []
            def run(command, **kwargs):
                calls.append(command)
                self.assertIn("--workers", command)
                self.assertIn("--seed", command)
                path = Path(command[command.index("--csv-output") + 1])
                fixture(path, value=len(calls), workers=6, seed=19,
                    warmup=int(command[command.index("--warmup-frames") + 1]), repetition=int(command[command.index("--run-index") + 1]))
            with patch.object(sweep.subprocess, "run", side_effect=run):
                self.assertEqual(sweep.main(["--runner", str(runner), "--scenario", "broad_phase", "--step-size", "10", "--ending-step", "20",
                    "--workers", "6", "--seed", "19", "--repetitions", "5", "--measure-frames", "10", "--output", str(output)]), 0)
            self.assertEqual(len(calls), 10)
            paths = [Path(command[command.index("--csv-output") + 1]) for command in calls]
            self.assertEqual(len(set(paths)), 10)
            for path in paths:
                self.assertTrue(path.exists() and path.with_suffix(".manifest.json").exists() and path.with_suffix(".samples.csv").exists())
            with output.open() as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual(len(rows), 2)
            self.assertEqual(int(rows[0]["repetitions"]), 5)
            self.assertGreater(float(rows[0]["complete_step_run_stdev_ms"]), 0)


class PlotAndFailureContract(unittest.TestCase):
    def test_odd_population_is_preserved(self):
        from types import SimpleNamespace
        command = sweep.apply_count_argument([], SimpleNamespace(scenario="rigid_body_container"), 11)
        self.assertEqual(command, ["--box-count", "5", "--circle-count", "6"])

    def test_failed_process_never_produces_aggregate(self):
        import subprocess
        with tempfile.TemporaryDirectory() as directory:
            runner = Path(directory) / "runner"
            runner.touch()
            output = Path(directory) / "aggregate.csv"
            with patch.object(sweep.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "runner")):
                with self.assertRaises(subprocess.CalledProcessError):
                    sweep.main(["--runner", str(runner), "--scenario", "broad_phase", "--counts", "10", "--output", str(output)])
            self.assertFalse(output.exists())
            with self.assertRaises(ValueError):
                sweep.main(["--scenario", "broad_phase", "--counts", "10", "--repetitions", "4"])

    def test_plot_rejects_legacy_missing_metrics_and_validation(self):
        spec = importlib.util.spec_from_file_location("plot_scaling", Path(__file__).resolve().parents[1] / "plot_scaling.py")
        plot = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(plot)
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "plot.csv"
            path.write_text("frame_avg_ms,requested_count\n1,10\n")
            with self.assertRaises(ValueError):
                plot.load_rows(path)
            source = Path(directory) / "run.csv"
            fixture(source)
            row = sweep.build_result_row(SimpleNamespace(scenario="broad_phase", workers=1, seed=0, validate_invariants=False), 10,
                                         [sweep.parse_benchmark_csv(source)] * 5)
            def write():
                with path.open("w", newline="") as handle:
                    writer = csv.DictWriter(handle, fieldnames=list(row))
                    writer.writeheader()
                    writer.writerow(row)
            write()
            self.assertEqual(len(plot.load_rows(path)), 1)
            row["validation_mode"] = "per-step"
            write()
            with self.assertRaises(ValueError):
                plot.load_rows(path)


if __name__ == "__main__":
    unittest.main()
