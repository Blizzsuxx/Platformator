import csv
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import tracemalloc
import types
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import capture_linear_render as capture


class LinearRenderCaptureTests(unittest.TestCase):
    def setUp(self):
        self.case = {"profile": "crossing", "sprite_count": 10, "workers": 1, "headless": False}
        self.args = types.SimpleNamespace(seed=0, dt=1 / 120, warmup_frames=2, measure_frames=2,
                                          repetitions=5, workers=1, multiworker_count=2)
        self.mapping = {"renderer": "software", "output_width": 320, "output_height": 240,
                        "actual_output_width": 320, "actual_output_height": 240,
                        "current_output_width": 320, "current_output_height": 240,
                        "vsync": 0, "vsync_known": True, "mapping_eligible": True,
                        "scale": [1.0, 1.0], "viewport": [0, 0, 320, 240], "viewport_set": False,
                        "target_active": False, "logical_presentation": [0, 0, 0]}
        self.identity = "a" * 64

    def proof(self, mode="linear"):
        return {"profile": "crossing", "sprite_count": 10, "workers": 1, "seed": 0,
                "dt": self.args.dt, "warmup_frames": 2, "measure_frames": 2, "mode": mode,
                "trajectory_parameters": capture.expected_trajectory(self.case, self.args),
                "fixture_sha256": self.identity, "harness_sha256": "b" * 64,
                "source_sha256": "c" * 64, "renderer_mapping": self.mapping,
                "headless": False, "timing_eligible": False, "validation_mode": "per-step",
                "crossing_counts": {"entries": 2, "exits": 2},
                "_frames": [
                    {"frame": 0, "image_fnv64": "0123456789abcdef", "submissions": [[0, 0], [1, 0]],
                     "oracle_visible": [[0, 0]]},
                    {"frame": 1, "image_fnv64": "0123456789abcdef", "submissions": [[0, 0], [1, 0]],
                     "oracle_visible": [[1, 0]]}], "_path": "unused"}

    def test_capture_defaults_are_small_and_exploratory(self):
        args = capture.parser().parse_args(["--output", "unused", "--multiworker-count", "2"])
        self.assertEqual((args.comparison, args.repetitions, args.warmup_frames, args.measure_frames),
                         ("incremental", 3, 5, 30))
        self.assertEqual((args.max_output_mib, args.max_seconds), (50, 600))
        with mock.patch.object(capture, "run_incremental_capture") as run:
            self.assertEqual(capture.main(["--output", "unused", "--multiworker-count", "2"]), 0)
            self.assertEqual(run.call_args.args[0].phase, "pilot")

    def test_full_campaign_requires_explicit_opt_in(self):
        with mock.patch.object(capture, "run_incremental_capture") as run:
            self.assertEqual(capture.main(["--output", "unused", "--multiworker-count", "2",
                                           "--phase", "validate"]), 2)
            run.assert_not_called()

    def test_capture_budgets_cannot_be_disabled(self):
        for value in (0, -1, float("inf"), float("nan")):
            for field in ("max_mib", "max_seconds"):
                with self.subTest(value=value, field=field), self.assertRaises(ValueError):
                    capture.CaptureBudget([], **{field: value})

    def test_size_budget_counts_nested_phase_roots_once_and_preserves_partial_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            phase = root / "phase"
            phase.mkdir()
            data = phase / "frames.jsonl"
            data.write_bytes(b"x" * 1024)
            budget = capture.CaptureBudget([root, phase], max_mib=1536 / 1024**2)
            budget.check()
            data.write_bytes(b"x" * 2048)
            with self.assertRaises(capture.BudgetExceeded):
                budget.check()
            self.assertTrue(data.exists())

    def test_identity_inspection_is_bounded_and_keeps_stderr_separate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "probe"
            binary.write_text("#!/bin/sh\necho diagnostic >&2\nprintf '%s\\n' '{\"work_probes\": true}'\n")
            binary.chmod(0o755)
            with capture.capture_budget(capture.CaptureBudget([root])):
                identity = capture._read_probe_identity(binary, root / "identity.log", os.environ.copy())
            self.assertEqual(identity, {"work_probes": True})
            self.assertIn("diagnostic", (root / "identity.log").read_text())

    def test_time_budget_stops_owned_process(self):
        with tempfile.TemporaryDirectory() as directory:
            budget = capture.CaptureBudget([Path(directory)], max_seconds=0.1)
            with mock.patch.object(capture, "_stop_owned_process", wraps=capture._stop_owned_process) as stop:
                with self.assertRaises(capture.BudgetExceeded), capture.capture_budget(budget):
                    capture._run([sys.executable, "-c", "import time; time.sleep(30)"],
                                 Path(directory) / "run.log", os.environ.copy())
                self.assertEqual(stop.call_count, 1)
                self.assertIsNotNone(stop.call_args.args[0].poll())

    def test_size_budget_also_checks_fast_finished_processes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            budget = capture.CaptureBudget([root], max_mib=1024 / 1024**2)
            with self.assertRaises(capture.BudgetExceeded), capture.capture_budget(budget):
                capture._run([sys.executable, "-c", "print('x' * 4096)"], root / "run.log", os.environ.copy())

    def test_final_budget_failure_removes_success_markers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "output"
            def oversized_report(args, strategy):
                args.output.mkdir()
                (args.output / "report.json").write_text("x" * 2048)
                (args.output / "partial.csv").write_text("keep")
            with mock.patch.object(capture, "run_incremental_capture", side_effect=oversized_report):
                self.assertEqual(capture.main(["--output", str(root), "--multiworker-count", "2",
                                               "--max-output-mib", str(1024 / 1024**2)]), 2)
            self.assertFalse((root / "report.json").exists())
            self.assertTrue((root / "partial.csv").exists())

    def test_engine_control_matrix_adds_physical_core_headless_controls(self):
        jobs = capture.engine_control_matrix(6)
        workers_by_scenario = {}
        for job in jobs:
            workers_by_scenario.setdefault(job["scenario"], set()).add(job["workers"])
        self.assertEqual(workers_by_scenario, {
            "persistent_contacts": {1, 6},
            "broad_phase_stress": {1, 6},
            "broad_phase": {1},
            "moving_add_and_remove": {1},
        })
        incremental_workers = {}
        for job in capture.engine_control_matrix(6, include_rendered_multiworker=True):
            incremental_workers.setdefault(job["scenario"], set()).add(job["workers"])
        self.assertEqual(incremental_workers, {name: {1, 6} for name in workers_by_scenario})
        with self.assertRaises(capture.CaptureError):
            capture.engine_control_matrix(0)

    def test_focused_case_selection_is_explicit_and_validated(self):
        selected = capture.focused_incremental_cases("mostly-offscreen,all-visible", 6)
        self.assertEqual([case["pilot_name"] for case in selected], ["mostly-offscreen", "all-visible"])
        self.assertEqual(selected[0]["layout"], "uniform-sparse")
        self.assertEqual(selected[1]["profile"], "visible")
        for selection in ("", "mostly-offscreen,mostly-offscreen", "unknown"):
            with self.subTest(selection=selection), self.assertRaises(capture.CaptureError):
                capture.focused_incremental_cases(selection, 6)
        with self.assertRaises(capture.CaptureError):
            capture.focused_incremental_cases("crossing-multiworker", 1)

    def test_pilot_root_does_not_require_full_phase_roots(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "fresh-pilot"
            capture.validate_incremental_phase_roots("pilot", None, None, output)
            output.mkdir()
            with self.assertRaises(capture.CaptureError):
                capture.validate_incremental_phase_roots("pilot", None, None, output)

    def test_focused_timing_manifest_matches_resource_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            csv_path = root / "run.csv"
            resource_path = root / "resource.json"
            resource_path.write_text("{}")
            resource = {"_sha256": capture.sha256(resource_path), "_path": str(resource_path),
                        "source_sha256": self.identity, "fixture_sha256": self.identity,
                        "harness_sha256": "b" * 64,
                        "trajectory_parameters": capture.expected_trajectory(self.case, self.args),
                        "renderer_mapping": self.mapping}
            build = {"configuration": "Release", "sanitizer": "none", "work_probes": False,
                     "debug_tools": False, "source_sha256": self.identity}
            manifest = {"build": build, "scenario": "linear_render_culling", "workers": 1,
                        "seed": 0, "dt": self.args.dt, "warmup_steps": 2, "measured_steps": 2,
                        "requested_measured_steps": 2, "repetitions": 3, "repetition_index": 0,
                        "timing_eligible": True, "validation_mode": "boundary-only",
                        "stopped_on_quit": False, "complete_capture": True, "total_steps": 2,
                        "resolved_parameters": resource["trajectory_parameters"],
                        "linear_render_culling": {"mode": "cached-linear", "requested_mode": "cached-linear",
                                                   "effective_policy": "cached-linear", "enabled": True},
                        "requested_render_strategy": "cached-linear", "rendering": True,
                        "render_samples": 2, "total_render_samples": 2,
                        "fixture_sha256": self.identity, "harness_sha256": "b" * 64,
                        "renderer_mapping": self.mapping,
                        "resource_observation": {"sha256": resource["_sha256"]}}
            csv_path.with_suffix(".manifest.json").write_text(json.dumps(manifest))
            args_values = vars(self.args).copy()
            args_values.update({"repetitions": 3, "_pilot_run_index": 0})
            args = types.SimpleNamespace(**args_values)
            metrics = {"scope.render.avg": 1.0, "scope.complete_step.avg": 2.0}
            result = capture._validate_focused_timing_manifest(
                csv_path, self.case, args, "cached-linear", resource,
                {"source_sha256": self.identity, "fixture_sha256": self.identity,
                 "harness_sha256": "b" * 64}, metrics)
            self.assertTrue(result["timing_eligible"])
            manifest["repetition_index"] = 1
            csv_path.with_suffix(".manifest.json").write_text(json.dumps(manifest))
            with self.assertRaises(capture.CaptureError):
                capture._validate_focused_timing_manifest(
                    csv_path, self.case, args, "cached-linear", resource,
                    {"source_sha256": self.identity, "fixture_sha256": self.identity,
                     "harness_sha256": "b" * 64}, metrics)

    def test_cancellation_stops_owned_process_group(self):
        process = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"],
                                   start_new_session=(os.name == "posix"))
        capture._stop_owned_process(process)
        self.assertIsNotNone(process.poll())

    def test_rejects_less_than_five_independent_processes(self):
        for count in (0, 1, 4):
            with self.subTest(count=count), self.assertRaises(capture.CaptureError):
                capture.validate_repetitions(count)
        capture.validate_repetitions(5)

    def test_rejects_wrong_render_proof_identity_fields(self):
        fields = {"profile": "sparse", "sprite_count": 11, "workers": 2, "seed": 1,
                  "dt": 0.02, "warmup_frames": 3, "measure_frames": 3, "mode": "disabled"}
        for field, value in fields.items():
            proof = self.proof()
            proof[field] = value
            with self.subTest(field=field), self.assertRaises(capture.CaptureError):
                capture._check_common_identity(proof, self.case, self.args, "linear", "render")
        proof = self.proof()
        proof["trajectory_parameters"]["trajectory_period_steps"] = 60
        with self.assertRaises(capture.CaptureError):
            capture._check_common_identity(proof, self.case, self.args, "linear", "render")

    def test_render_proof_rejects_missing_corrupt_and_stale_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(capture.CaptureError):
                capture.validate_render_proof(root, self.case, self.args, "linear")
            prefix = root / f"{capture.case_key(self.case)}-linear"
            evidence = self.proof()
            evidence.pop("_frames")
            evidence.pop("_path")
            evidence["frames_sha256"] = self.identity
            evidence["representative_sha256"] = self.identity
            (Path(str(prefix) + ".evidence.json")).write_text(json.dumps(evidence))
            (Path(str(prefix) + ".frames.jsonl")).write_text('{"frame":0}\n')
            (Path(str(prefix) + ".representative.ppm")).write_bytes(b"image")
            with self.assertRaises(capture.CaptureError):
                capture.validate_render_proof(root, self.case, self.args, "linear")

    def test_render_proof_rejects_wrong_headless_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prefix = root / f"{capture.case_key(self.case)}-linear"
            frames_path = Path(str(prefix) + ".frames.jsonl")
            frames = [{"frame": index, "image_fnv64": "0123456789abcdef",
                       "submissions": [[0, 0]], "oracle_visible": [[0, 0]]} for index in range(2)]
            frames_path.write_text("".join(json.dumps(frame) + "\n" for frame in frames))
            image_path = Path(str(prefix) + ".representative.ppm")
            image_path.write_bytes(b"image")
            proof = self.proof()
            proof.update({"headless": True, "frames_sha256": capture.sha256(frames_path),
                          "representative_sha256": capture.sha256(image_path)})
            Path(str(prefix) + ".evidence.json").write_text(json.dumps(proof))
            with self.assertRaises(capture.CaptureError):
                capture.validate_render_proof(root, self.case, self.args, "linear")

    def test_render_proof_rejects_malformed_membership_and_output_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prefix = root / f"{capture.case_key(self.case)}-linear"
            frames_path = Path(str(prefix) + ".frames.jsonl")
            image_path = Path(str(prefix) + ".representative.ppm")
            image_path.write_bytes(b"image")
            proof = self.proof()
            proof.pop("_frames"); proof.pop("_path")
            proof["representative_sha256"] = capture.sha256(image_path)

            def store(frames, mapping=None):
                frames_path.write_text("".join(json.dumps(frame) + "\n" for frame in frames))
                current = dict(proof)
                current["frames_sha256"] = capture.sha256(frames_path)
                if mapping is not None:
                    current["renderer_mapping"] = mapping
                Path(str(prefix) + ".evidence.json").write_text(json.dumps(current))

            valid_frames = [
                {"frame": index, "image_fnv64": "0123456789abcdef", "submissions": [[0, 0]],
                 "oracle_visible": [[0, 0]], "unresolved": 0, "overflow": 0}
                for index in range(2)]
            store(valid_frames)
            checked = capture.validate_render_proof(root, self.case, self.args, "linear")
            self.assertNotIn("_frames", checked)
            self.assertEqual(checked["_frame_count"], self.args.measure_frames)
            self.assertEqual(checked["_submission_total"], self.args.measure_frames)
            for malformed in ([[[-1, 0]], [[0, True]], [[0]], ["0:0"]],):
                for identities in malformed:
                    frames = json.loads(json.dumps(valid_frames))
                    frames[0]["submissions"] = identities
                    store(frames)
                    with self.subTest(identities=identities), self.assertRaises(capture.CaptureError):
                        capture.validate_render_proof(root, self.case, self.args, "linear")
            bad_mapping = {**self.mapping, "actual_output_width": 640}
            store(valid_frames, bad_mapping)
            with self.assertRaises(capture.CaptureError):
                capture.validate_render_proof(root, self.case, self.args, "linear")

    def test_render_proof_streams_frames_without_retaining_identity_arrays(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            case = dict(self.case)
            args = types.SimpleNamespace(**vars(self.args))
            args.measure_frames = 4000
            prefix = root / f"{capture.case_key(case)}-linear"
            frames_path = Path(str(prefix) + ".frames.jsonl")
            frame = {"frame": 0, "image_fnv64": "0123456789abcdef", "submissions": [[0, 0], [1, 0]],
                     "oracle_visible": [[0, 0]], "unresolved": 0, "overflow": 0}
            with frames_path.open("w", encoding="utf-8") as stream:
                for index in range(args.measure_frames):
                    frame["frame"] = index
                    stream.write(json.dumps(frame) + "\n")
            image_path = Path(str(prefix) + ".representative.ppm")
            image_path.write_bytes(b"image")
            proof = self.proof()
            proof.pop("_frames")
            proof.pop("_path")
            proof["measure_frames"] = args.measure_frames
            proof["trajectory_parameters"] = capture.expected_trajectory(case, args)
            proof["frames_sha256"] = capture.sha256(frames_path)
            proof["representative_sha256"] = capture.sha256(image_path)
            Path(str(prefix) + ".evidence.json").write_text(json.dumps(proof), encoding="utf-8")

            tracemalloc.start()
            checked = capture.validate_render_proof(root, case, args, "linear")
            _, peak = tracemalloc.get_traced_memory()
            tracemalloc.stop()
            self.assertNotIn("_frames", checked)
            self.assertEqual(checked["_frame_count"], args.measure_frames)
            self.assertEqual(checked["_submission_total"], args.measure_frames * 2)
            self.assertEqual(checked["_oracle_visible_total"], args.measure_frames)
            self.assertLess(peak, 2_000_000, "Frame validation should retain only one frame of identities at a time")

    def test_resource_proof_rejects_missing_stale_and_incomplete_observations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(capture.CaptureError):
                capture.validate_resource_proof(root, self.case, self.args, "linear")
            path = root / f"{capture.case_key(self.case)}-cached-linear.resources.json"
            proof = self.proof("cached-linear")
            proof.update({"allocation_calls": 0, "allocated_bytes": 0,
                          "allocation_calls_per_frame": 0, "allocated_bytes_per_frame": 0,
                          "process_rss_before_kib": 1000, "process_rss_after_kib": 1000,
                          "process_rss_after_reset_kib": 1000,
                          "render_view_preparations": 2, "render_eligibility_checks": 2,
                          "render_visibility_checks": 20, "render_submissions": 4,
                          "actual_changed_sprite_total": 4, "actual_changed_sprites_per_step_max": 2,
                          "actual_changed_fraction": 0.2,
                          "cold_observation": {"first_warmup_step_elapsed_ns": 1234,
                                               "fixture_construction_and_registration_ns": 4321,
                                               "fixture_construction_allocation_calls": 20,
                                               "fixture_construction_allocated_bytes": 4096},
                          "capacity_growth_observation": {"attempted": False, "reason": "inactive mode"},
                          "reset_observation": {"records_registered_after_clear": 0, "owned_bytes_after_clear": 0,
                                                "record_capacity_after_clear": 0,
                                                "elapsed_ns": 4321, "allocation_calls": 3, "allocated_bytes": 128},
                          "headless": False,
                          "allocation_probe": "C++ new/new[] counts; SDL allocations not attributed"})
            path.write_text(json.dumps(proof))
            self.assertEqual(capture.validate_resource_proof(root, self.case, self.args, "cached-linear")["allocation_calls"], 0)
            # Applicable historical indexed proof requires cell_capacity_after_clear == 0.
            hist_path = root / f"{capture.case_key(self.case)}-indexed.resources.json"
            hist = json.loads(json.dumps(proof))
            hist["mode"] = "indexed"
            hist_path.write_text(json.dumps(hist))
            with self.assertRaises(capture.CaptureError):
                capture.validate_resource_proof(root, self.case, self.args, "indexed")
            hist["reset_observation"]["cell_capacity_after_clear"] = 0
            hist_path.write_text(json.dumps(hist))
            self.assertEqual(capture.validate_resource_proof(root, self.case, self.args, "indexed")["mode"], "indexed")
            hist["reset_observation"]["cell_capacity_after_clear"] = 1
            hist_path.write_text(json.dumps(hist))
            with self.assertRaises(capture.CaptureError):
                capture.validate_resource_proof(root, self.case, self.args, "indexed")
            path.write_text(json.dumps(proof))
            for change in ({"mode": "disabled"}, {"trajectory_parameters": {"profile": "visible"}},
                           {"allocation_calls": -1}, {"render_submissions": None},
                           {"render_visibility_checks": 2.5}, {"render_submissions": 21},
                           {"actual_changed_sprite_total": -1}, {"actual_changed_fraction": 0.1},
                           {"cold_observation": {}}, {"capacity_growth_observation": {}},
                           {"reset_observation": {"owned_bytes_after_clear": 1}},
                           {"reset_observation": {**proof["reset_observation"], "record_capacity_after_clear": 1}},
                           {"source_sha256": "not-a-fingerprint"}):
                invalid = dict(proof); invalid.update(change)
                path.write_text(json.dumps(invalid))
                with self.subTest(change=change), self.assertRaises(capture.CaptureError):
                    capture.validate_resource_proof(root, self.case, self.args, "cached-linear")
            path.write_text("corrupt JSON")
            with self.assertRaises(json.JSONDecodeError):
                capture.validate_resource_proof(root, self.case, self.args, "cached-linear")

    def test_resource_mode_evidence_requires_matching_allocation_and_work_counts(self):
        resource = {}
        for mode in capture.MODES:
            resource[mode] = {"fixture_sha256": self.identity, "harness_sha256": "b" * 64,
                              "source_sha256": "d" * 64 if mode == "pre-feature" else "c" * 64,
                              "allocation_calls": 0, "allocated_bytes": 0,
                              "render_view_preparations": 0 if mode == "pre-feature" else 2,
                              "render_eligibility_checks": 2 if mode == "linear" else 0,
                              "render_visibility_checks": 20 if mode == "linear" else 0,
                              "render_submissions": 20 if mode != "linear" else 4}
        capture.validate_resource_mode_evidence(resource, self.case, 2)
        changed = json.loads(json.dumps(resource)); changed["linear"]["allocation_calls"] = 1
        with self.assertRaises(capture.CaptureError):
            capture.validate_resource_mode_evidence(changed, self.case, 2)
        changed = json.loads(json.dumps(resource)); changed["disabled"]["render_view_preparations"] = 0
        with self.assertRaises(capture.CaptureError):
            capture.validate_resource_mode_evidence(changed, self.case, 2)
        headless_case = {**self.case, "headless": True}
        headless = json.loads(json.dumps(resource))
        for proof in headless.values():
            proof.update(render_view_preparations=0, render_eligibility_checks=0,
                         render_visibility_checks=0, render_submissions=0)
        capture.validate_resource_mode_evidence(headless, headless_case, 2)

    def test_resource_mode_evidence_matches_work_submissions_to_checked_membership(self):
        case = {**self.case, "sprite_count": 2}
        resource = {}
        proofs = {mode: self.proof(mode) for mode in capture.MODES}
        for mode in capture.MODES:
            resource[mode] = {"fixture_sha256": self.identity, "harness_sha256": "b" * 64,
                              "source_sha256": "d" * 64 if mode == "pre-feature" else "c" * 64,
                              "allocation_calls": 0, "allocated_bytes": 0,
                              "render_view_preparations": 0 if mode == "pre-feature" else 2,
                              "render_eligibility_checks": 2 if mode == "linear" else 0,
                              "render_visibility_checks": 4 if mode == "linear" else 0,
                              "render_submissions": 4}
        capture.validate_resource_mode_evidence(resource, case, 2, proofs)
        changed = json.loads(json.dumps(resource))
        changed["linear"]["render_submissions"] = 3
        with self.assertRaises(capture.CaptureError):
            capture.validate_resource_mode_evidence(changed, case, 2, proofs)

    def test_engine_control_checks_full_identity_policy_counts_and_source(self):
        control = {"scenario": "broad_phase", "workers": 1, "rendered": True,
                   "resolved_parameters": {"body_count": 128}}
        manifest = {"scenario": "broad_phase", "workers": 1, "seed": self.args.seed,
                    "dt": self.args.dt, "warmup_steps": self.args.warmup_frames,
                    "measured_steps": self.args.measure_frames,
                    "requested_measured_steps": self.args.measure_frames, "repetitions": self.args.repetitions,
                    "repetition_index": 0, "rendering": True, "render_samples": 2,
                    "total_render_samples": 2, "total_steps": 2, "complete_capture": True,
                    "timing_eligible": True, "validation_mode": "boundary-only", "stopped_on_quit": False,
                    "resolved_parameters": {"body_count": 128},
                    "linear_render_culling": {"requested_mode": "linear", "requested_enabled": True,
                                               "effective_enabled": True, "effective_policy": "linear-filtering"},
                    "physical_invariants": {"finite": True, "populations_valid": True, "supports_valid": True},
                    "build": {"configuration": "Release", "work_probes": False, "debug_tools": False,
                              "sanitizer": "none", "source_sha256": "c" * 64}}
        self.assertEqual(capture.validate_engine_control_manifest(manifest, control, self.args, "linear", 0,
                                                                  "c" * 64, "c" * 64), "c" * 64)
        new_manifest = json.loads(json.dumps(manifest))
        new_manifest["linear_render_culling"] = {
            "requested_selector": "cached-linear",
            "resolved_startup_strategy": "cached-linear",
            "requested_mode": "cached-linear",
            "requested_enabled": True,
            "headless": False,
            "disabled": False,
            "effective_policy": "cached-linear",
            "effective_enabled": True,
            "observed_mapping_fallbacks": 0,
            "observed_numeric_fallbacks": 0,
            "observed_frame_fallbacks": 0,
        }
        self.assertEqual(capture.validate_engine_control_manifest(new_manifest, control, self.args, "linear", 0,
                                                                  "c" * 64, "c" * 64), "c" * 64)
        for field, value in (("scenario", "moving_add_and_remove"), ("workers", 2), ("seed", 1),
                             ("dt", 0.02), ("warmup_steps", 3), ("measured_steps", 4),
                             ("repetition_index", 1), ("render_samples", 1),
                             ("timing_eligible", False), ("validation_mode", "per-step")):
            changed = json.loads(json.dumps(manifest)); changed[field] = value
            with self.subTest(field=field), self.assertRaises(capture.CaptureError):
                capture.validate_engine_control_manifest(changed, control, self.args, "linear", 0,
                                                         "c" * 64, "c" * 64)
        changed = json.loads(json.dumps(manifest)); changed["linear_render_culling"]["effective_enabled"] = False
        with self.assertRaises(capture.CaptureError):
            capture.validate_engine_control_manifest(changed, control, self.args, "linear", 0,
                                                     "c" * 64, "c" * 64)
        changed = json.loads(json.dumps(manifest)); changed["build"]["source_sha256"] = "d" * 64
        with self.assertRaises(capture.CaptureError):
            capture.validate_engine_control_manifest(changed, control, self.args, "linear", 0,
                                                     "c" * 64, "c" * 64)

    def test_mismatch_recovery_uses_checked_validation_probes_and_keeps_original_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            identities = {
                "baseline": {"source_sha256": "d" * 64, "fixture_sha256": self.identity,
                             "harness_sha256": "b" * 64, "work_probes": True},
                "candidate": {"source_sha256": "c" * 64, "fixture_sha256": self.identity,
                              "harness_sha256": "b" * 64, "work_probes": True},
            }
            binaries = {}
            for mode, identity in identities.items():
                path = root / f"{mode}-validation-probe"
                path.write_text("#!/bin/sh\nprintf '%s\\n' '" + json.dumps(identity) + "'\n")
                path.chmod(0o755)
                binaries[mode] = path
            args = types.SimpleNamespace(
                baseline_validation_probe=binaries["baseline"], candidate_validation_probe=binaries["candidate"],
                seed=0, warmup_frames=2, measure_frames=2, repetitions=5, dt=self.args.dt, asset_root=None)
            output = root / "out"; output.mkdir()
            logs = output / "logs"; logs.mkdir()
            proofs = {"pre-feature": {**identities["baseline"]},
                      "disabled": {**identities["candidate"]}, "linear": {**identities["candidate"]}}
            called = []
            real_run = capture._run

            def fake_run(command, log_path, env, **kwargs):
                if "--print-build-identity" in command:
                    return real_run(command, log_path, env, **kwargs)
                called.append(command[0])
                prefix = Path(command[command.index("--evidence-output") + 1])
                Path(str(prefix) + ".mismatch.ppm").write_bytes(b"ppm")

            with mock.patch.object(capture, "_run", side_effect=fake_run):
                images = capture.capture_mismatch_images(args, self.case, 1, output, logs, os.environ.copy(), proofs)
            self.assertEqual(called, [str(binaries["baseline"]), str(binaries["candidate"]),
                                      str(binaries["candidate"])])
            self.assertEqual(len(images), 3)
            mismatch = capture.ImageMismatch(1)

            def fail_recovery():
                raise capture.CaptureError("validation executable rejected its identity")

            with self.assertRaisesRegex(capture.ImageMismatch,
                                        "Image mismatch at frame 1; mismatch image recovery failed: validation executable rejected its identity"):
                capture.preserve_image_mismatch(mismatch, fail_recovery)

    def test_validation_comparison_rejects_different_renderer_harness_and_image(self):
        proofs = {mode: self.proof(mode) for mode in capture.MODES}
        proofs["pre-feature"]["source_sha256"] = "d" * 64
        proofs["disabled"]["harness_sha256"] = "d" * 64
        with self.assertRaises(capture.CaptureError):
            capture.validate_mode_evidence(proofs, self.case)
        proofs["disabled"]["harness_sha256"] = "b" * 64
        proofs["pre-feature"]["renderer_mapping"] = {"renderer": "opengl"}
        with self.assertRaises(capture.CaptureError):
            capture.validate_mode_evidence(proofs, self.case)
        proofs["pre-feature"]["renderer_mapping"] = self.mapping
        proofs["linear"]["_frames"][1]["image_fnv64"] = "fedcba9876543210"
        with self.assertRaises(capture.ImageMismatch):
            capture.validate_mode_evidence(proofs, self.case)

    def test_validation_comparison_rejects_order_duplicates_and_missing_oracle_sprite(self):
        proofs = {mode: self.proof(mode) for mode in capture.MODES}
        proofs["pre-feature"]["source_sha256"] = "d" * 64
        proofs["linear"]["_frames"][0]["submissions"] = [[1, 0], [0, 0]]
        with self.assertRaises(capture.CaptureError):
            capture.validate_mode_evidence(proofs, self.case)
        proofs = {mode: self.proof(mode) for mode in capture.MODES}
        proofs["pre-feature"]["source_sha256"] = "d" * 64
        proofs["linear"]["_frames"][0]["submissions"] = [[0, 0], [0, 0]]
        with self.assertRaises(capture.CaptureError):
            capture.validate_mode_evidence(proofs, self.case)
        proofs = {mode: self.proof(mode) for mode in capture.MODES}
        proofs["pre-feature"]["source_sha256"] = "d" * 64
        proofs["linear"]["_frames"][0]["submissions"] = [[1, 0]]
        with self.assertRaises(capture.CaptureError):
            capture.validate_mode_evidence(proofs, self.case)

    def _write_valid_schema3(self, csv_path, headless=False):
        scopes = ("complete_step", "physics", "cleanup", "scene_transition", "render", "broad_phase",
                  "narrow_phase", "resolve_collisions")
        counters = ("object_count", "occupied_cell_count", "candidate_pair_count", "pending_narrow_phase_pair_count",
                    "active_collision_count", "awake_dynamic_body_count", "queued_add_count", "queued_sync_count",
                    "queued_remove_count", "collision_enter_event_count", "collision_exit_event_count")
        with csv_path.open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(("schema_version", "category", "name", "samples", "avg", "median", "p95", "p99", "min", "max", "unit"))
            for name in scopes:
                value = 0.0 if headless and name == "render" else 0.5 if name == "render" else 1.0
                samples = 0 if headless and name == "render" else 2
                writer.writerow((3, "scope", name, samples, value, value, value, value, value, value, "ms"))
            for name in counters:
                writer.writerow((3, "counter", name, 2, 0, "", "", "", 0, 0, "count"))
        raw_path = csv_path.with_suffix(".samples.csv")
        with raw_path.open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["schema_version", "sample_kind", "index", *[name + "_ns" for name in scopes], *counters])
            for index in range(2):
                writer.writerow([3, "step", index, *[0 if name == "render" else 1_000_000 for name in scopes], *([0] * 11)])
            if not headless:
                for index in range(2):
                    writer.writerow([3, "render", index, *[500_000 if name == "render" else 0 for name in scopes], *([0] * 11)])
        mapping = ({"renderer": "none", "output_width": 320, "output_height": 240,
                    "actual_output_width": 0, "actual_output_height": 0, "current_output_width": 0,
                    "current_output_height": 0, "mapping_eligible": False, "vsync_known": False}
                   if headless else self.mapping)
        manifest = {"schema_version": 3, "scenario": "linear_render_culling", "workers": 1, "seed": 0,
                    "dt": self.args.dt, "measured_steps": 2, "requested_measured_steps": 2, "warmup_steps": 2,
                    "repetitions": 5, "repetition_index": 0, "rendering": not headless,
                    "render_samples": 0 if headless else 2,
                    "complete_capture": True, "total_steps": 2, "total_render_samples": 0 if headless else 2,
                    "validation_mode": "boundary-only", "timing_eligible": True, "stopped_on_quit": False,
                    "physical_invariants": {"finite": True, "populations_valid": True, "supports_valid": True},
                    "build": {"commit": "a", "dirty": False, "source_sha256": "c" * 64, "compiler": "g++",
                              "flags": "-O3", "configuration": "Release", "sanitizer": "none",
                              "work_probes": False, "debug_tools": False},
                    "machine": {"cpu": "cpu"}, "dependencies": {"SDL": "3"},
                    "boundaries": {name: "scope" for name in scopes},
                    "linear_render_culling": {"mode": "linear", "requested_mode": "linear",
                                               "effective_policy": "headless-no-rendering" if headless else "linear",
                                               "enabled": not headless},
                    "fixture_sha256": self.identity, "harness_sha256": "b" * 64,
                    "resolved_parameters": capture.expected_trajectory(self.case, self.args),
                    "renderer_mapping": mapping, "total_render_samples": 0 if headless else 2}
        csv_path.with_suffix(".manifest.json").write_text(json.dumps(manifest))
        return manifest

    def test_timing_manifest_accepts_matching_schema3_and_rejects_instrumentation_proofs_and_samples(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "linear.csv"
            manifest = self._write_valid_schema3(path)
            proof_path = Path(directory) / "proof.json"
            proof_path.write_text("proof")
            proof = self.proof()
            proof.update({"source_sha256": "c" * 64, "_path": str(proof_path)})
            resource_path = Path(directory) / "resource.json"
            resource_path.write_text("resource")
            resource = {"_sha256": capture.sha256(resource_path), "source_sha256": "c" * 64}
            manifest["validation_proof"] = {"sha256": capture.sha256(proof_path)}
            manifest["resource_observation"] = {"sha256": capture.sha256(resource_path)}
            path.with_suffix(".manifest.json").write_text(json.dumps(manifest))
            capture.validate_timing_manifest(path, self.case, self.args, "linear", proof, resource, {}, 0)

            for field, value in (("work_probes", True), ("debug_tools", True)):
                changed = json.loads(json.dumps(manifest)); changed["build"][field] = value
                path.with_suffix(".manifest.json").write_text(json.dumps(changed))
                with self.subTest(field=field), self.assertRaises(capture.CaptureError):
                    capture.validate_timing_manifest(path, self.case, self.args, "linear", proof, resource, {}, 0)
            changed = json.loads(json.dumps(manifest)); changed["stopped_on_quit"] = True
            path.with_suffix(".manifest.json").write_text(json.dumps(changed))
            with self.assertRaises(capture.CaptureError):
                capture.validate_timing_manifest(path, self.case, self.args, "linear", proof, resource, {}, 0)
            path.with_suffix(".manifest.json").write_text(json.dumps(manifest))
            path.with_suffix(".samples.csv").write_text("corrupt raw data\n")
            with self.assertRaises(ValueError):
                capture.validate_timing_manifest(path, self.case, self.args, "linear", proof, resource, {}, 0)

    def test_timing_manifest_rejects_mode_source_mapping_and_capture_identity_mismatches(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "linear.csv"
            manifest = self._write_valid_schema3(path)
            proof_path = Path(directory) / "proof.json"; proof_path.write_text("proof")
            proof = self.proof(); proof.update({"source_sha256": "c" * 64, "_path": str(proof_path)})
            resource_path = Path(directory) / "resource.json"; resource_path.write_text("resource")
            resource = {"_sha256": capture.sha256(resource_path), "source_sha256": "c" * 64}
            manifest["validation_proof"] = {"sha256": capture.sha256(proof_path)}
            manifest["resource_observation"] = {"sha256": capture.sha256(resource_path)}

            mutations = [
                ("linear_render_culling", {"mode": "disabled", "requested_mode": "linear",
                                            "effective_policy": "linear", "enabled": True}),
                ("linear_render_culling", {"mode": "linear", "requested_mode": "disabled",
                                            "effective_policy": "linear", "enabled": True}),
                ("build", {**manifest["build"], "source_sha256": "d" * 64}),
                ("renderer_mapping", {**self.mapping, "output_width": 640}),
                ("renderer_mapping", {**self.mapping, "mapping_eligible": False}),
                ("resolved_parameters", {**manifest["resolved_parameters"], "sprite_count": 11}),
                ("seed", 1), ("dt", 0.02), ("warmup_steps", 3), ("workers", 2),
                ("render_samples", 1), ("complete_capture", False),
            ]
            for field, value in mutations:
                changed = json.loads(json.dumps(manifest)); changed[field] = value
                path.with_suffix(".manifest.json").write_text(json.dumps(changed))
                with self.subTest(field=field, value=value), self.assertRaises((capture.CaptureError, ValueError)):
                    capture.validate_timing_manifest(path, self.case, self.args, "linear", proof, resource, {}, 0)

    def test_timing_manifest_accepts_headless_zero_render_samples(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "headless.csv"
            manifest = self._write_valid_schema3(path, headless=True)
            proof_path = Path(directory) / "proof.json"; proof_path.write_text("proof")
            proof = self.proof(); proof.update({"source_sha256": "c" * 64,
                                                "renderer_mapping": manifest["renderer_mapping"],
                                                "_path": str(proof_path)})
            resource_path = Path(directory) / "resource.json"; resource_path.write_text("resource")
            resource = {"_sha256": capture.sha256(resource_path), "source_sha256": "c" * 64}
            manifest["validation_proof"] = {"sha256": capture.sha256(proof_path)}
            manifest["resource_observation"] = {"sha256": capture.sha256(resource_path)}
            path.with_suffix(".manifest.json").write_text(json.dumps(manifest))
            headless_case = {**self.case, "headless": True}
            capture.validate_timing_manifest(path, headless_case, self.args, "linear", proof, resource, {}, 0)

    def test_reused_output_directory_and_write_errors_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binaries = []
            for name in ("baseline-probe", "candidate-probe", "baseline-runner", "candidate-runner"):
                path = root / name
                path.write_text("#!/bin/sh\nexit 0\n")
                path.chmod(0o755)
                binaries.append(path)
            validation = root / "validation"; validation.mkdir()
            resources = root / "resources"; resources.mkdir()
            output = root / "existing"; output.mkdir()
            args = types.SimpleNamespace(baseline_probe=binaries[0], candidate_probe=binaries[1],
                                         baseline_runner=binaries[2], candidate_runner=binaries[3],
                                         baseline_validation_probe=binaries[0], candidate_validation_probe=binaries[1],
                                         validation_root=validation, resource_root=resources, output=output,
                                         multiworker_count=2, workers=1, seed=0, warmup_frames=0,
                                         measure_frames=1, repetitions=5, dt=0.01, asset_root=None)
            with self.assertRaises(capture.CaptureError):
                capture.run_capture(args)
            with self.assertRaises(OSError):
                capture._run(["true"], root / "missing" / "run.log", os.environ.copy())

    def test_incremental_options_reject_unknown_comparison_phase_and_strategy(self):
        for values in (("unknown", "validate", "cached-linear"),
                       ("incremental", "unknown", "cached-linear"),
                       ("incremental", "validate", "indexed"),
                       ("incremental", "validate", "current-linear"),
                       ("incremental", "validate", "automatic")):
            with self.subTest(values=values), self.assertRaises(capture.CaptureError):
                capture.validate_incremental_options(*values)
        self.assertEqual(capture.validate_incremental_options("incremental", "validate", "cached-linear"),
                         ("incremental", "validate", "cached-linear"))

    def test_incremental_case_identity_rejects_stale_case_and_substituted_inputs(self):
        case = {"strategy": "indexed", "profile": "sparse", "population": 1000,
                "changed_fraction": 0.1, "layout": "spread", "camera_schedule": "fixed",
                "output_schedule": "320x240", "seed": 0, "dt": 1 / 120,
                "warmup_steps": 2, "measured_steps": 3, "workers": 1}
        identity = {name: self.identity for name in
                    ("renderer", "fixture", "harness", "source", "assets", "build", "binary", "proof")}
        proof = {"proof_kind": "platformator.incremental-render-culling", "proof_version": 1,
                 "comparison": "incremental", "case": dict(case), "identity": dict(identity)}
        capture.validate_incremental_case_identity(proof, case, identity)
        for field, value in (("population", 1001), ("changed_fraction", 0.01),
                             ("layout", "fixed-area"), ("output_schedule", "resize-at-240")):
            changed = json.loads(json.dumps(proof))
            changed["case"][field] = value
            with self.subTest(field=field), self.assertRaises(capture.CaptureError):
                capture.validate_incremental_case_identity(changed, case, identity)
        for field in identity:
            changed = json.loads(json.dumps(proof))
            changed["identity"][field] = "f" * 64
            with self.subTest(identity=field), self.assertRaises(capture.CaptureError):
                capture.validate_incremental_case_identity(changed, case, identity)
        for missing in ("population", "changed_fraction", "layout", "output_schedule"):
            changed = json.loads(json.dumps(proof))
            del changed["case"][missing]
            with self.subTest(missing=missing), self.assertRaises(capture.CaptureError):
                capture.validate_incremental_case_identity(changed, case, identity)
        for population, fraction in ((-1, 0.1), (1, -0.1), (1, 1.1)):
            changed_case = dict(case, population=population, changed_fraction=fraction)
            changed = {**proof, "case": changed_case}
            with self.subTest(population=population, fraction=fraction), self.assertRaises(capture.CaptureError):
                capture.validate_incremental_case_identity(changed, changed_case, identity)

    def test_incremental_timing_rejects_instrumentation_negative_and_truncated_samples(self):
        case_key = "indexed-sparse-n1000-ch10"
        expected_count = 5
        build = {"configuration": "Release", "sanitizer": "none",
                 "work_probes": False, "debug_tools": False,
                 "source_sha256": self.identity, "binary_sha256": "b" * 64}
        raw = [{"case_key": case_key, "run_index": index, "render_ms": 1.0,
                "complete_step_ms": 2.0, "paired_step_render_ms": 3.0} for index in range(expected_count)]
        paired = [row["paired_step_render_ms"] for row in raw]
        capture.validate_incremental_timing_samples(raw, paired, expected_count, build)
        for invalid_build in ({**build, "work_probes": True},
                              {**build, "sanitizer": "thread"},
                              {**build, "configuration": "Debug"}):
            with self.subTest(build=invalid_build), self.assertRaises(capture.CaptureError):
                capture.validate_incremental_timing_samples(raw, paired, expected_count, invalid_build)
        with self.assertRaises(capture.CaptureError):
            capture.validate_incremental_timing_samples(raw[:-1], paired[:-1], expected_count, build)
        negative = json.loads(json.dumps(raw)); negative[2]["complete_step_ms"] = -1
        with self.assertRaises(capture.CaptureError):
            capture.validate_incremental_timing_samples(negative, paired, expected_count, build)
        with self.assertRaises(capture.CaptureError):
            capture.validate_incremental_timing_samples(raw, paired[:-1], expected_count, build)

    def test_incremental_render_group_accepts_empty_selection_and_rejects_order_inversion(self):
        case = {"profile": "offscreen", "sprite_count": 2, "workers": 1, "headless": False,
                "output_schedule": "320x240"}
        full_frame = {"image_fnv64": "0123456789abcdef", "submissions": [[0, 0], [1, 0]],
                      "oracle_visible": []}
        empty_frame = {"image_fnv64": "0123456789abcdef", "submissions": [],
                       "oracle_visible": []}
        proofs = {mode: {"_frames": [dict(empty_frame)]} for mode in capture.INCREMENTAL_STRATEGIES}
        proofs["disabled"] = {"_frames": [dict(full_frame)]}
        capture.validate_incremental_render_group(proofs, case, self.args)

        case["profile"] = "visible"
        proofs = {mode: {"_frames": [{"image_fnv64": "0123456789abcdef",
                                      "submissions": [[0, 0], [1, 0]],
                                      "oracle_visible": [[0, 0]]}]}
                  for mode in capture.INCREMENTAL_STRATEGIES}
        proofs["disabled"] = {"_frames": [dict(full_frame)]}
        proofs["cached-linear"]["_frames"][0]["submissions"] = [[1, 0], [0, 0]]
        with self.assertRaises(capture.CaptureError):
            capture.validate_incremental_render_group(proofs, case, self.args)

    def test_incremental_raw_reader_requires_complete_schema_and_paired_host_rows(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "run.samples.csv"
            paired = root / "run.paired.csv"
            scopes = capture.INCREMENTAL_BENCHMARK_SCOPES
            counters = capture.INCREMENTAL_BENCHMARK_COUNTERS
            headers = ["schema_version", "sample_kind", "index", *[name + "_ns" for name in scopes], *counters]
            with raw.open("w", newline="") as stream:
                writer = csv.writer(stream); writer.writerow(headers)
                for kind, index in (("step", 0), ("step", 1), ("render", 0), ("render", 1)):
                    values = [3, kind, index]
                    values += [2_000_000 if name == "complete_step" and kind == "step" else
                               1_000_000 if name == "render" and kind == "render" else 0 for name in scopes]
                    values += [0] * len(counters)
                    writer.writerow(values)
            paired.write_text("step,paired_step_render_ns\n0,3000000\n1,3000000\n")
            rows, paired_values = capture.read_incremental_timing_samples(raw, paired, 2, 2, False)
            self.assertEqual([row["sample_index"] for row in rows], [0, 1])
            self.assertEqual([row["render_ms"] for row in rows], [1.0, 1.0])
            self.assertEqual(paired_values, [3.0, 3.0])
            paired.write_text("step,paired_step_render_ns\n0,-1\n1,3000000\n")
            with self.assertRaises(capture.CaptureError):
                capture.read_incremental_timing_samples(raw, paired, 2, 2, False)

    def test_incremental_matrix_contains_loaded_scenes_and_giant_motion_contract(self):
        cases = capture.incremental_matrix(6, 1, Path(__file__).resolve().parents[1] / "assets")
        scenes = {(case["profile"], case["workers"], case["sprite_count"]) for case in cases
                  if case["profile"].startswith("scene-")}
        self.assertEqual(scenes, {("scene-default", 1, 6), ("scene-default", 6, 6),
                                  ("scene-stacking", 1, 0), ("scene-stacking", 6, 0)})
        giant = next(case for case in cases if case["profile"] == "giant")
        self.assertEqual(giant["changed_percent"], 10)
        self.assertEqual(giant["size_distribution"], "giant-side-list")
        self.assertEqual(len(capture.frozen_control_case_ids(cases)), 4)

    def test_incremental_matrix_covers_required_population_fraction_and_inactive_controls(self):
        cases = capture.incremental_matrix(6, 1, Path(__file__).resolve().parents[1] / "assets")
        def selected(profile, workers=1, headless=False):
            return {(case["sprite_count"], case["changed_percent"], case["layout"])
                    for case in cases if case["profile"] == profile and case["workers"] == workers and
                    case["headless"] is headless}

        self.assertEqual({row[0] for row in selected("visible")}, {0, 1, 1000, 10000})
        self.assertEqual({row[0] for row in selected("visible-motion")}, {1, 1000, 10000})
        self.assertEqual({row[0] for row in selected("offscreen")}, {1000, 10000, 50000})
        self.assertEqual({row[0] for row in selected("spread")}, {1000, 10000, 50000})
        self.assertEqual({(row[0], row[1]) for row in selected("changed")},
                         {(count, percent) for count in (1000, 10000, 50000)
                          for percent in (1, 10, 100)})
        self.assertEqual({row[0] for row in selected("camera-pan")}, {1000, 10000, 50000})
        self.assertEqual({row[0] for row in selected("crossing")}, {1000, 10000, 50000})
        self.assertTrue(any(case["profile"] == "crossing" and case["headless"] and case["workers"] == 1
                            for case in cases))
        self.assertTrue(any(case["profile"] == "changed" and case["sprite_count"] == 10000 and
                            case["changed_percent"] == 1 and case["workers"] == 6 for case in cases))
        self.assertTrue(any(case["profile"] == "camera-pan" and case["sprite_count"] == 50000 and
                            case["workers"] == 6 for case in cases))
        self.assertTrue(any(case["profile"] == "crossing" and case["sprite_count"] == 50000 and
                            case["workers"] == 6 for case in cases))
        expected_worker_layouts = {"visible": "fixed-area-visible", "changed": "constant-density-spread",
                                   "camera-pan": "constant-density-spread", "crossing": "continuous-crossing",
                                   "churn": "stable-slot-churn"}
        for profile, layout in expected_worker_layouts.items():
            case = next(case for case in cases if case["profile"] == profile and case["workers"] == 6 and
                        (profile != "visible" or case["sprite_count"] == 1000))
            self.assertEqual(case["layout"], layout)
            self.assertEqual(capture.expected_trajectory(case, self.args)["layout"], layout)

    def test_expected_trajectory_identity_covers_every_supported_profile(self):
        self.assertEqual(set(capture.INCREMENTAL_PROFILES), set(capture.TRAJECTORY_PERIOD_STEPS))
        for profile in capture.INCREMENTAL_PROFILES:
            case = {"profile": profile, "sprite_count": 1000, "workers": 1,
                    "changed_fraction": 0.1, "changed_percent": 10, "layout": "profile-defined"}
            identity = capture.expected_trajectory(case, self.args)
            self.assertEqual(identity["profile"], profile)
            self.assertEqual(identity["trajectory_period_steps"], capture.TRAJECTORY_PERIOD_STEPS[profile])
        self.assertEqual(capture.expected_trajectory(
            {"profile": "giant", "sprite_count": 1000, "changed_fraction": 0.1, "changed_percent": 10},
            self.args)["trajectory_period_steps"], 240)

    def test_incremental_phase_requires_fresh_roots_and_matching_prerequisites(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            validation = root / "validation"; validation.mkdir()
            resources = root / "resources"; resources.mkdir()
            output = root / "time"
            with self.assertRaises(capture.CaptureError):
                capture.validate_incremental_phase_roots("time", validation, resources, output)
            for phase_root in (validation, resources):
                (phase_root / "phase.json").write_text(json.dumps(
                    {"comparison": "incremental", "phase": "validate" if phase_root == validation else "resources",
                     "complete": True, "case_count": 1, "case_keys": ["case-a"],
                     "strategies": list(capture.INCREMENTAL_STRATEGIES),
                     "frozen_control_keys": ["case-a"]}))
            capture.validate_incremental_phase_roots("time", validation, resources, output)
            resource_marker = json.loads((resources / "phase.json").read_text())
            resource_marker["phase"] = "validate"
            (resources / "phase.json").write_text(json.dumps(resource_marker))
            with self.assertRaises(capture.CaptureError):
                capture.validate_incremental_phase_roots("time", validation, resources, output)
            resource_marker["phase"] = "resources"
            (resources / "phase.json").write_text(json.dumps(resource_marker))
            output.mkdir()
            with self.assertRaises(capture.CaptureError):
                capture.validate_incremental_phase_roots("time", validation, resources, output)

    def test_incremental_phase_inventory_rejects_stale_cases_and_missing_frozen_controls(self):
        cases = capture.incremental_matrix(6, 1, Path(__file__).resolve().parents[1] / "assets")
        control_ids = capture.frozen_control_case_ids(cases)
        entries = []
        for case in cases:
            case_id = capture.incremental_case_key(case)
            entry = {"case_id": case_id, "case": case}
            if case_id in control_ids:
                entry["frozen_proofs"] = {"current-linear": "proof-a", "disabled": "proof-b"}
            entries.append(entry)
        marker = {"phase": "validate", "seed": self.args.seed, "dt": self.args.dt,
                  "warmup_steps": self.args.warmup_frames, "measured_steps": self.args.measure_frames,
                  "complete": True, "strategies": list(capture.INCREMENTAL_STRATEGIES),
                  "frozen_control_keys": control_ids,
                  "case_keys": [entry["case_id"] for entry in entries]}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "phase.json").write_text(json.dumps(marker))
            (root / "case-index.json").write_text(json.dumps(entries))
            inventory = capture._incremental_phase_inventory(
                root, marker, "validate", cases, list(capture.INCREMENTAL_STRATEGIES), self.args)
            self.assertEqual(len(inventory), len(cases))
            stale_marker = {**marker, "case_keys": marker["case_keys"][:-1]}
            with self.assertRaises(capture.CaptureError):
                capture._incremental_phase_inventory(
                    root, stale_marker, "validate", cases, list(capture.INCREMENTAL_STRATEGIES), self.args)
            missing_controls = {**marker, "frozen_control_keys": control_ids[:-1]}
            with self.assertRaises(capture.CaptureError):
                capture._incremental_phase_inventory(
                    root, missing_controls, "validate", cases, list(capture.INCREMENTAL_STRATEGIES), self.args)
            wrong_phase = {**marker, "phase": "resources"}
            with self.assertRaises(capture.CaptureError):
                capture._incremental_phase_inventory(
                    root, wrong_phase, "validate", cases, list(capture.INCREMENTAL_STRATEGIES), self.args)

    def test_incremental_mismatch_recovery_preserves_original_diagnostic(self):
        mismatch = capture.ImageMismatch(17)

        def fail_recovery():
            raise OSError("validation binary is unavailable")

        with self.assertRaises(capture.CaptureError) as caught:
            capture.preserve_image_mismatch(mismatch, fail_recovery)
        self.assertIn("Image mismatch at frame 17", str(caught.exception))
        self.assertIn("validation binary is unavailable", str(caught.exception))

    def test_incremental_mismatch_recovery_uses_matching_candidate_and_frozen_probes(self):
        case = next(case for case in capture.incremental_matrix(
            2, 1, Path(__file__).resolve().parents[1] / "assets")
                    if case["profile"] == "crossing" and case["sprite_count"] == 10000 and
                    case["workers"] == 1 and not case["headless"])
        candidate = {"fixture_sha256": "a" * 64, "harness_sha256": "b" * 64,
                     "source_sha256": "c" * 64}
        baseline = {"fixture_sha256": "a" * 64, "harness_sha256": "b" * 64,
                    "source_sha256": "d" * 64}
        candidate_proofs = {mode: dict(candidate) for mode in (*capture.INCREMENTAL_STRATEGIES, "disabled")}
        frozen_proofs = {mode: dict(baseline) for mode in ("current-linear", "disabled")}
        args = types.SimpleNamespace(seed=0, dt=1 / 120, warmup_frames=2, measure_frames=3,
                                     repetitions=5, asset_root=None,
                                     candidate_validation_probe=Path("candidate-probe"),
                                     baseline_validation_probe=Path("baseline-probe"))
        identity_calls = []
        def fake_identity(binary, proof, log_path, env):
            identity_calls.append((str(binary), proof["source_sha256"]))
            return {"source_sha256": proof["source_sha256"], "fixture_sha256": proof["fixture_sha256"],
                    "harness_sha256": proof["harness_sha256"], "work_probes": True}

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "out"; output.mkdir()
            logs = output / "logs"; logs.mkdir()
            called = []
            def fake_run(command, log_path, env):
                called.append(command)
                prefix = Path(command[command.index("--evidence-output") + 1])
                Path(str(prefix) + ".mismatch.ppm").write_bytes(b"ppm")

            with mock.patch.object(capture, "_validation_probe_identity", side_effect=fake_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_run):
                images = capture.capture_incremental_mismatch_images(
                    args, case, 2, output, logs, os.environ.copy(), candidate_proofs, frozen_proofs)
            self.assertEqual(len(images), 4)
            self.assertEqual(len(called), 4)
            self.assertTrue(all("--capture-frame-index" in command and command[-1] == "2" for command in called))
            self.assertIn(("candidate-probe", "c" * 64), identity_calls)
            self.assertIn(("baseline-probe", "d" * 64), identity_calls)
            mismatch_record = json.loads((output / "mismatches" / capture.incremental_case_key(case) /
                                          "mismatch.json").read_text())
            self.assertEqual(mismatch_record["frame"], 2)
            self.assertEqual(len(mismatch_record["images"]), 4)

    def test_render_order_case_selection_arms_workers_and_baseline_arguments(self):
        cases = capture.render_order_cases("authored-static,sparse-camera-pan,dense-z-edit", 1, 4)
        self.assertEqual([c["case_name"] for c in cases],
                         ["authored-static", "sparse-camera-pan", "dense-z-edit"])
        for bad in ("", "authored-static,authored-static", "authored-static,unknown-case"):
            with self.subTest(bad=bad), self.assertRaises(capture.CaptureError):
                capture.render_order_cases(bad, 1, 4)

        static_arms = capture.render_order_arms(cases[0])
        self.assertEqual([(a["arm"], a["strategy"], a["workers"], a["is_baseline"]) for a in static_arms],
                         [("baseline-indexed", "indexed", 1, True),
                          ("candidate-cached-linear", "cached-linear", 1, False)])
        pan_arms = capture.render_order_arms(cases[1])
        self.assertEqual([(a["arm"], a["strategy"], a["workers"], a["is_baseline"]) for a in pan_arms],
                         [("baseline-indexed", "indexed", 1, True),
                          ("candidate-cached-linear", "cached-linear", 1, False)])
        dense_arms = capture.render_order_arms(cases[2])
        self.assertEqual([(a["arm"], a["strategy"], a["ordering_variant"], a["workers"]) for a in dense_arms],
                         [("candidate-rebuild-w1", "cached-linear", "rebuild", 1),
                          ("candidate-forced-sort-w1", "cached-linear", "forced-sort", 1),
                          ("candidate-repair-w1", "cached-linear", "repair", 1),
                          ("candidate-rebuild-w4", "cached-linear", "rebuild", 4)])

        args = types.SimpleNamespace(seed=0, warmup_frames=5, measure_frames=30, dt=1 / 120,
                                     repetitions=3, asset_root=Path("/tmp/assets"))
        base_cmd = capture._build_render_order_command(
            Path("/tmp/base"), cases[0], static_arms[0], args, phase="time", csv_path=Path("/tmp/b.csv"))
        self.assertNotIn("--ordering-variant", base_cmd)
        self.assertNotIn("--validation-proof", base_cmd)
        self.assertNotIn("--resource-proof", base_cmd)
        self.assertEqual(base_cmd[base_cmd.index("--render-strategy") + 1], "indexed")
        cand_cmd = capture._build_render_order_command(
            Path("/tmp/cand"), cases[2], dense_arms[2], args, phase="time", csv_path=Path("/tmp/c.csv"),
            validation_path=Path("/tmp/v.json"), resource_path=Path("/tmp/r.json"))
        self.assertEqual(cand_cmd[cand_cmd.index("--ordering-variant") + 1], "repair")
        self.assertIn("--validation-proof", cand_cmd)
        self.assertIn("--resource-proof", cand_cmd)

    def test_render_order_per_phase_samples_and_identity_rejections(self):
        rep_samples = [
            [{"sample_index": idx, "paired_step_render_ms": 1.0 if idx % 2 == 0 else 3.0,
              "render_ms": 0.4 if idx % 2 == 0 else 1.4,
              "complete_step_ms": 0.6 if idx % 2 == 0 else 1.6} for idx in range(6)]
            for _ in range(3)
        ]
        clean = capture._slice_stats(rep_samples, lambda r: r["sample_index"] % 2 == 0)
        edit = capture._slice_stats(rep_samples, lambda r: r["sample_index"] % 2 == 1)
        combined = capture._slice_stats(rep_samples, lambda _r: True)
        self.assertAlmostEqual(clean["paired_step_render_ms"]["mean_ms"], 1.0)
        self.assertAlmostEqual(edit["paired_step_render_ms"]["mean_ms"], 3.0)
        self.assertAlmostEqual(combined["paired_step_render_ms"]["mean_ms"], 2.0)
        self.assertEqual(clean["samples_per_run"], 3)
        self.assertEqual(edit["samples_per_run"], 3)

        case = capture.render_order_cases("dense-z-edit", 1, 4)[0]
        arm = capture.render_order_arms(case)[2]
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            csv_path = root / "dense.csv"
            build_id = {"configuration": "Release", "sanitizer": "none", "work_probes": False,
                        "debug_tools": False, "source_sha256": "s" * 64, "fixture_sha256": "f" * 64,
                        "harness_sha256": "h" * 64}
            traj = {"profile": "dense-z-edit", "sprite_count": 10000}
            render_proof = {"trajectory_parameters": traj, "renderer_mapping": self.mapping, "_sha256": "vp"}
            resource_proof = {"trajectory_parameters": traj, "_sha256": "rp"}
            manifest = {"build": build_id, "scenario": "linear_render_culling", "workers": 1, "seed": 0,
                        "dt": self.args.dt, "warmup_steps": 2, "measured_steps": 2, "requested_measured_steps": 2,
                        "repetitions": 3, "repetition_index": 0, "timing_eligible": True,
                        "validation_mode": "boundary-only", "stopped_on_quit": False, "complete_capture": True,
                        "total_steps": 2, "rendering": True, "render_samples": 2, "total_render_samples": 2,
                        "linear_render_culling": {"mode": "cached-linear", "requested_mode": "cached-linear",
                                                   "effective_policy": "cached-linear", "enabled": True,
                                                   "observed_frame_fallbacks": 0},
                        "resolved_parameters": traj, "ordering_variant": "repair",
                        "fixture_sha256": "f" * 64, "harness_sha256": "h" * 64,
                        "renderer_mapping": self.mapping,
                        "validation_proof": {"sha256": "vp"}, "resource_observation": {"sha256": "rp"}}
            csv_path.with_suffix(".manifest.json").write_text(json.dumps(manifest))
            metrics = {"scope.render.avg": 0.5, "scope.complete_step.avg": 1.0}
            capture.validate_render_order_timing_manifest(
                csv_path, case, arm, types.SimpleNamespace(seed=0, dt=self.args.dt, warmup_frames=2,
                                                           measure_frames=2, repetitions=3),
                0, build_id, metrics, render_proof, resource_proof, self.mapping)
            for mut_fn in (
                lambda m: m["resolved_parameters"].update({"sprite_count": 9999}),
                lambda m: m["build"].update({"work_probes": True}),
                lambda m: m["build"].update({"source_sha256": "x" * 64}),
                lambda m: m["renderer_mapping"].update({"output_width": 640}),
                lambda m: m.update({"ordering_variant": "rebuild"}),
                lambda m: m.update({"workers": 4}),
                lambda m: m["linear_render_culling"].update({"observed_frame_fallbacks": 1}),
            ):
                bad = json.loads(json.dumps(manifest))
                mut_fn(bad)
                csv_path.with_suffix(".manifest.json").write_text(json.dumps(bad))
                with self.assertRaises(capture.CaptureError):
                    capture.validate_render_order_timing_manifest(
                        csv_path, case, arm, types.SimpleNamespace(seed=0, dt=self.args.dt, warmup_frames=2,
                                                                   measure_frames=2, repetitions=3),
                        0, build_id, metrics, render_proof, resource_proof, self.mapping)

    def test_render_order_synthetic_pilot_failures_and_partial_preservation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            assets = root / "assets"; assets.mkdir(); (assets / "scene.json").write_text("{}")
            bins = {}
            for name in ("base", "cand", "val"):
                p = root / name; p.write_text("#!/bin/sh\nexit 0\n"); p.chmod(0o755); bins[name] = p

            base_id = {**capture.RECORDED_FROZEN_BASELINE_IDENTITY,
                       "commit": "a" * 40, "dirty": False, "compiler": "g++", "flags": "-O3"}
            cand_id = {"configuration": "Release", "sanitizer": "none", "work_probes": False, "debug_tools": False,
                       "commit": "a" * 40, "dirty": False, "compiler": "g++", "flags": "-O3",
                       "source_sha256": "c" * 64,
                       "fixture_sha256": capture.RECORDED_FROZEN_BASELINE_IDENTITY["fixture_sha256"],
                       "harness_sha256": "d" * 64}
            val_id = {**cand_id, "work_probes": True}

            def make_identity(binary, log_path, env):
                if binary == bins["base"]: return dict(base_id)
                if binary == bins["cand"]: return dict(cand_id)
                return dict(val_id)

            def make_fake_run(fail_parity=False, fail_mid_control=False, base_traj_override=None,
                              extra_present_wait_ns=0, prepare_ns_by_arm=None,
                              omit_candidate_prepare_col=False, omit_candidate_selection_col=False,
                              oversized_in_progress_bytes=0, control_override=None):
                calls = []
                def fake_run(cmd, log_path, env, **kwargs):
                    calls.append(list(cmd))
                    Path(log_path).write_text("ok\n")
                    profile = cmd[cmd.index("--profile") + 1]
                    strategy = cmd[cmd.index("--render-strategy") + 1]
                    ordering = cmd[cmd.index("--ordering-variant") + 1] if "--ordering-variant" in cmd else "rebuild"
                    count = int(cmd[cmd.index("--sprite-count") + 1])
                    workers = int(cmd[cmd.index("--workers") + 1])
                    reps = int(cmd[cmd.index("--repetitions") + 1])
                    run_idx = int(cmd[cmd.index("--run-index") + 1])
                    meas = int(cmd[cmd.index("--measure-frames") + 1])
                    layout = cmd[cmd.index("--layout") + 1] if "--layout" in cmd else "profile-defined"
                    is_base = cmd[0] == str(bins["base"])
                    traj = {"profile": profile, "sprite_count": count,
                            "layout": layout, "camera_schedule": "fixed"}
                    if is_base and base_traj_override:
                        traj.update(base_traj_override)
                    if "--validate-rendering" in cmd:
                        ev = Path(cmd[cmd.index("--evidence-output") + 1])
                        if oversized_in_progress_bytes > 0:
                            Path(str(ev) + ".frames.jsonl").write_bytes(b"x" * oversized_in_progress_bytes)
                            capture.check_budget(reserve_bytes=kwargs.get("reserve_bytes", 0))
                        subs = [[i, 0] for i in range(min(count, 4))] if profile != "dense-z-edit" else [[i, 0] for i in range(count)]
                        Path(str(ev) + ".frames.jsonl").write_text("\n".join(
                            json.dumps({"frame": f, "image_fnv64": f"h{f}", "output_width": 320,
                                        "output_height": 240, "submissions": subs, "oracle_visible": subs,
                                        "unresolved": 0, "overflow": 0}) for f in range(meas)) + "\n")
                        Path(str(ev) + ".representative.ppm").write_bytes(b"P6\n1 1\n255\n\x00\x00\x00")
                        if fail_parity and strategy == "cached-linear":
                            subs_bad = [subs[0], subs[0]]
                            Path(str(ev) + ".frames.jsonl").write_text("\n".join(
                                json.dumps({"frame": f, "image_fnv64": f"h{f}", "output_width": 320,
                                            "output_height": 240, "submissions": subs_bad, "oracle_visible": subs,
                                            "unresolved": 0, "overflow": 0}) for f in range(meas)) + "\n")
                            Path(str(ev) + ".mismatch.ppm").write_bytes(b"P6\n1 1\n255\n\xff\x00\x00")
                        Path(str(ev) + ".evidence.json").write_text(json.dumps({
                            "trajectory_parameters": traj, "renderer_mapping": self.mapping,
                            "fixture_sha256": val_id["fixture_sha256"],
                            "harness_sha256": val_id["harness_sha256"]}))
                    elif "--validate-resources" in cmd:
                        res = Path(cmd[cmd.index("--evidence-output") + 1] + ".resources.json")
                        cw = {"sprite_order_preparations": 0 if profile == "rejected-z-edit" else 2,
                              "sprite_order_sorts": 0 if (profile == "rejected-z-edit" or ordering == "repair") else 2,
                              "bounds_refreshes": 0,
                              "candidate_visits": 0, "full_population_visits": 0,
                              "dirty_enqueues": 0, "visible_membership_checks": 0,
                              "visible_membership_changes": 0}
                        if profile == "rejected-z-edit":
                            z_tot = min(100, max(0, count - 64)) * meas
                            pose_tot = 0
                            spr_tot = z_tot
                            max_step = min(100, max(0, count - 64))
                        elif profile == "crossing":
                            z_tot = 0
                            pose_tot = count * meas
                            spr_tot = pose_tot
                            max_step = count
                        else:
                            z_tot = 0
                            pose_tot = 0
                            spr_tot = 0
                            max_step = 0
                        payload = {
                            "profile": profile, "sprite_count": count, "workers": workers,
                            "seed": 0, "dt": 0.008333333333333333, "warmup_frames": 2,
                            "measure_frames": meas, "mode": strategy,
                            "trajectory_parameters": traj, "allocation_calls": 0, "allocated_bytes": 0,
                            "actual_changed_sprite_total": spr_tot,
                            "actual_changed_pose_total": pose_tot,
                            "actual_changed_z_total": z_tot,
                            "actual_changed_sprites_per_step_max": max_step,
                            "actual_changed_fraction": (spr_tot / (count * meas)) if count * meas else 0.0,
                            "render_view_preparations": meas, "render_eligibility_checks": meas,
                            "render_visibility_checks": 0, "render_submissions": min(count * meas, 100),
                            "cold_observation": {}, "capacity_growth_observation": {}, "reset_observation": {},
                            "culling_work": cw,
                        }
                        if control_override and (profile, strategy) in control_override:
                            payload.update(control_override[(profile, strategy)])
                        Path(res).write_text(json.dumps(payload))
                    else:
                        csv_path = Path(cmd[cmd.index("--csv-output") + 1])
                        vp = cmd[cmd.index("--validation-proof") + 1] if "--validation-proof" in cmd else None
                        rp = cmd[cmd.index("--resource-proof") + 1] if "--resource-proof" in cmd else None
                        b = base_id if is_base else cand_id
                        manifest = {
                            "schema_version": 3, "build": b, "scenario": "linear_render_culling", "workers": workers, "seed": 0,
                            "dt": 0.008333333333333333, "warmup_steps": 2, "measured_steps": meas,
                            "requested_measured_steps": meas, "repetitions": reps, "repetition_index": run_idx,
                            "timing_eligible": True, "validation_mode": "boundary-only", "stopped_on_quit": False,
                            "complete_capture": True, "total_steps": meas, "rendering": True,
                            "render_samples": meas, "total_render_samples": meas, "ordering_variant": ordering,
                            "linear_render_culling": {"mode": strategy, "requested_mode": strategy,
                                                       "effective_policy": strategy, "enabled": True,
                                                       "observed_frame_fallbacks": 0},
                            "resolved_parameters": traj,
                            "fixture_sha256": b["fixture_sha256"], "harness_sha256": b["harness_sha256"],
                            "renderer_mapping": self.mapping, "machine": {"cpu": "x"}, "affinity": [0],
                            "dependencies": {"SDL": "3"},
                            "boundaries": {s: "scope" for s in capture.INCREMENTAL_BENCHMARK_SCOPES},
                            "physical_invariants": {"finite": True, "populations_valid": True, "supports_valid": True},
                            "validation_proof": {"sha256": capture.sha256(Path(vp))} if vp else {},
                            "resource_observation": {"sha256": capture.sha256(Path(rp))} if rp else {},
                        }
                        csv_path.with_suffix(".manifest.json").write_text(json.dumps(manifest))
                        render_wait_ms = (extra_present_wait_ns / 1_000_000) if (not is_base) else 0.0
                        with csv_path.open("w", newline="") as f:
                            w = csv.writer(f)
                            w.writerow(["schema_version", "category", "name", "samples", "avg", "median", "p95", "p99", "min", "max", "unit"])
                            for sc in capture.INCREMENTAL_BENCHMARK_SCOPES:
                                val_ms = (0.5 + render_wait_ms) if sc == "render" else 1.0
                                w.writerow([3, "scope", sc, meas, val_ms, val_ms, val_ms + 0.1, val_ms + 0.1, 0.4, val_ms + 0.1, "ms"])
                            for ct in capture.INCREMENTAL_BENCHMARK_COUNTERS:
                                w.writerow([3, "counter", ct, meas, count, 0, count, count, count, count, "count"])
                        with csv_path.with_suffix(".samples.csv").open("w", newline="") as f:
                            w = csv.writer(f)
                            w.writerow(["schema_version", "sample_kind", "index",
                                        *[s + "_ns" for s in capture.INCREMENTAL_BENCHMARK_SCOPES],
                                        *capture.INCREMENTAL_BENCHMARK_COUNTERS])
                            for i in range(meas):
                                w.writerow([3, "step", i, *[0 if s == "render" else 1_000_000 for s in capture.INCREMENTAL_BENCHMARK_SCOPES],
                                            *([0] * len(capture.INCREMENTAL_BENCHMARK_COUNTERS))])
                            for i in range(meas):
                                r_ns = 500_000 + (0 if is_base else extra_present_wait_ns)
                                w.writerow([3, "render", i, *[r_ns if s == "render" else 0 for s in capture.INCREMENTAL_BENCHMARK_SCOPES],
                                            *([0] * len(capture.INCREMENTAL_BENCHMARK_COUNTERS))])
                        with csv_path.with_suffix(".paired.csv").open("w", newline="") as f:
                            w = csv.writer(f)
                            if is_base or omit_candidate_prepare_col:
                                w.writerow(["step", "paired_step_render_ns"])
                                for i in range(meas):
                                    w.writerow([i, 1_500_000 if (i % 2 == 0 or profile != "dense-z-edit") else 2_500_000])
                            elif omit_candidate_selection_col:
                                w.writerow(["step", "paired_step_render_ns", "draw_order_prepare_ns"])
                                for i in range(meas):
                                    paired_ns = (1_500_000 if (i % 2 == 0 or profile != "dense-z-edit") else 2_500_000) + extra_present_wait_ns
                                    w.writerow([i, paired_ns, 50_000])
                            else:
                                w.writerow(["step", "paired_step_render_ns", "draw_order_prepare_ns", "visible_selection_ns"])
                                for i in range(meas):
                                    paired_ns = (1_500_000 if (i % 2 == 0 or profile != "dense-z-edit") else 2_500_000) + extra_present_wait_ns
                                    prep_key = (profile, strategy, ordering, workers, i % 2)
                                    prep_ns = (prepare_ns_by_arm[prep_key]
                                               if prepare_ns_by_arm and prep_key in prepare_ns_by_arm
                                               else (50_000 if (i % 2 == 0 or profile != "dense-z-edit") else 300_000))
                                    sel_ns = 700 if profile != "dense-z-edit" else 20_000
                                    w.writerow([i, paired_ns, prep_ns, sel_ns])
                return fake_run, calls

            # 1. Happy path removes large traces and writes separated comparisons.
            out_ok = root / "ok"
            fake_ok, calls_ok = make_fake_run(fail_parity=False)
            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_ok):
                rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "authored-static,dense-z-edit",
                                   "--baseline-probe", str(bins["base"]), "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "2",
                                   "--warmup-frames", "2", "--measure-frames", "4", "--output", str(out_ok)])
            self.assertEqual(rc, 0)
            rep = json.loads((out_ok / "report.json").read_text())
            self.assertEqual(rep["scope"]["cases"], ["authored-static", "dense-z-edit"])
            self.assertIn("endpoint_implementation_and_policy", rep["comparisons"])
            self.assertIn("combined implementation + policy",
                          rep["comparisons"]["endpoint_implementation_and_policy"]["authored-static"]["comparison_scope"])
            self.assertNotIn("implementation_overhead", rep["comparisons"])
            self.assertNotIn("culling_strategy", rep["comparisons"])
            self.assertNotIn("default_policy", rep["comparisons"])
            self.assertIn("clean_frames", rep["comparisons"]["dense_z_ordering"])
            self.assertIn("edit_frames", rep["comparisons"]["dense_z_ordering"])
            self.assertEqual(list((out_ok / "proofs").glob("*.frames.jsonl")), [])
            self.assertEqual(list((out_ok / "proofs").glob("*.ppm")), [])

            # 2. Correctness failure preserves partial artifacts without timing or retries.
            out_fail = root / "fail_parity"
            fake_bad, calls_bad = make_fake_run(fail_parity=True)
            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_bad):
                rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "authored-static",
                                   "--baseline-probe", str(bins["base"]), "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "2",
                                   "--warmup-frames", "2", "--measure-frames", "4", "--output", str(out_fail)])
            self.assertEqual(rc, 2)
            self.assertFalse((out_fail / "report.json").exists())
            self.assertTrue((out_fail / "logs").is_dir())
            self.assertFalse(any("--csv-output" in c and str(out_fail / "raw") in c[c.index("--csv-output") + 1] for c in calls_bad))

            # 3. Byte budget failure stops without emitting report.json and preserves partial files.
            out_budget = root / "fail_budget"
            fake_b, _ = make_fake_run(fail_parity=False)
            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_b):
                rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "authored-static",
                                   "--baseline-probe", str(bins["base"]), "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "2",
                                   "--warmup-frames", "2", "--measure-frames", "4",
                                   "--max-output-mib", "0.0005", "--output", str(out_budget)])
            self.assertEqual(rc, 2)
            self.assertFalse((out_budget / "report.json").exists())
            self.assertTrue(out_budget.exists())

            # 4. Time budget failure stops without emitting report.json and preserves partial files.
            out_time = root / "fail_time"
            calls_time = []
            def fake_slow_run(cmd, log_path, env, **kwargs):
                calls_time.append(list(cmd))
                capture.time.sleep(0.08)
                fake_ok(cmd, log_path, env, **kwargs)
            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_slow_run):
                rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "authored-static",
                                   "--baseline-probe", str(bins["base"]), "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "2",
                                   "--warmup-frames", "2", "--measure-frames", "4",
                                   "--max-seconds", "0.02", "--output", str(out_time)])
            self.assertEqual(rc, 2)
            self.assertFalse((out_time / "report.json").exists())
            self.assertFalse((out_time / "report.md").exists())
            self.assertTrue(out_time.exists())
            self.assertEqual(len(calls_time), 1)
            self.assertEqual(list(out_time.rglob("*.frames.jsonl")), [])
            self.assertEqual(list(out_time.rglob("*.ppm")), [])
            partial_time = json.loads((out_time / "partial-report.json").read_text())
            self.assertFalse(partial_time["complete"])
            self.assertIn("unavailable_comparisons", partial_time)

            # T048 check on early validation parity failure: zero surviving traces/PPMs + compact partial report.
            self.assertEqual(list(out_fail.rglob("*.frames.jsonl")), [])
            self.assertEqual(list(out_fail.rglob("*.ppm")), [])
            partial_fail = json.loads((out_fail / "partial-report.json").read_text())
            self.assertFalse(partial_fail["complete"])
            self.assertEqual(partial_fail["failure_stage"], "parity")
            self.assertTrue(len(partial_fail["completed_arms"]) > 0)

            # 5. T047: Reject aliased baseline path, candidate source reused as baseline, wrong frozen identity,
            #    and changed baseline layout / camera_schedule with matching profile and sprite_count.
            alias_path = root / "cand_alias"
            alias_path.symlink_to(bins["cand"])
            out_alias = root / "fail_alias_path"
            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_ok):
                rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "authored-static",
                                   "--baseline-probe", str(alias_path), "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "1",
                                   "--warmup-frames", "2", "--measure-frames", "2", "--output", str(out_alias)])
            self.assertEqual(rc, 2)
            self.assertIn("aliases", json.loads((out_alias / "partial-report.json").read_text())["failure_reason"])

            def wrong_identity_reader(wrong_fields):
                def reader(binary, log_path, env):
                    if binary == bins["base"]:
                        return {**base_id, **wrong_fields}
                    return make_identity(binary, log_path, env)
                return reader

            for label, bad_fields in (
                ("aliased_source", {"source_sha256": cand_id["source_sha256"]}),
                ("wrong_frozen_source", {"source_sha256": "e" * 64}),
                ("wrong_frozen_harness", {"harness_sha256": "e" * 64}),
            ):
                out_bad_id = root / f"fail_{label}"
                with mock.patch.object(capture, "_read_probe_identity", side_effect=wrong_identity_reader(bad_fields)), \
                        mock.patch.object(capture, "_run", side_effect=fake_ok):
                    rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "authored-static",
                                       "--baseline-probe", str(bins["base"]), "--candidate-probe", str(bins["cand"]),
                                       "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                       "--workers", "1", "--multiworker-count", "4", "--repetitions", "1",
                                       "--warmup-frames", "2", "--measure-frames", "2", "--output", str(out_bad_id)])
                self.assertEqual(rc, 2, label)
                self.assertFalse((out_bad_id / "report.json").exists())
                self.assertEqual(json.loads((out_bad_id / "partial-report.json").read_text())["failure_stage"], "identity")

            for field_name, bad_val in (("layout", "mutated-layout"), ("camera_schedule", "continuous-pan-240")):
                out_traj = root / f"fail_traj_{field_name}"
                fake_bad_traj, _ = make_fake_run(base_traj_override={field_name: bad_val})
                with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                        mock.patch.object(capture, "_run", side_effect=fake_bad_traj):
                    rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "authored-static",
                                       "--baseline-probe", str(bins["base"]), "--candidate-probe", str(bins["cand"]),
                                       "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                       "--workers", "1", "--multiworker-count", "4", "--repetitions", "1",
                                       "--warmup-frames", "2", "--measure-frames", "2", "--output", str(out_traj)])
                self.assertEqual(rc, 2, field_name)
                partial_traj = json.loads((out_traj / "partial-report.json").read_text())
                self.assertIn("resolved all-zero trajectory", partial_traj["failure_reason"])

            # 6. T048: Mid-timing byte budget preserves completed compact evidence within max_output_mib.
            out_mid_budget = root / "fail_mid_budget"
            fake_mid, calls_mid = make_fake_run(fail_parity=False)
            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_mid):
                rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "authored-static",
                                   "--baseline-probe", str(bins["base"]), "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "2",
                                   "--warmup-frames", "2", "--measure-frames", "4",
                                   "--max-output-mib", "0.025", "--output", str(out_mid_budget)])
            self.assertEqual(rc, 2)
            self.assertFalse((out_mid_budget / "report.json").exists())
            self.assertEqual(list(out_mid_budget.rglob("*.frames.jsonl")), [])
            self.assertEqual(list(out_mid_budget.rglob("*.ppm")), [])
            partial_mid = json.loads((out_mid_budget / "partial-report.json").read_text())
            self.assertFalse(partial_mid["complete"])
            self.assertEqual(partial_mid["failure_stage"], "time")
            self.assertTrue(len(partial_mid["completed_arms"]) > 0)
            total_mid_bytes = sum(p.stat().st_size for p in out_mid_budget.rglob("*") if p.is_file())
            self.assertLessEqual(total_mid_bytes, int(0.025 * 1024 * 1024))

            # 7. T052: Synthetic timings varying presentation wait independently of preparation time.
            out_prep = root / "prep_vs_present"
            prep_map = {
                ("dense-z-edit", "cached-linear", "rebuild", 1, 0): 120_000,
                ("dense-z-edit", "cached-linear", "rebuild", 1, 1): 900_000,
                ("dense-z-edit", "cached-linear", "repair", 1, 0): 120_000,
                ("dense-z-edit", "cached-linear", "repair", 1, 1): 240_000,
            }
            fake_prep, _ = make_fake_run(extra_present_wait_ns=15_000_000, prepare_ns_by_arm=prep_map)
            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_prep):
                rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "authored-static,dense-z-edit",
                                   "--baseline-probe", str(bins["base"]), "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "2",
                                   "--warmup-frames", "2", "--measure-frames", "4", "--output", str(out_prep)])
            self.assertEqual(rc, 0)
            rep_prep = json.loads((out_prep / "report.json").read_text())
            base_prep_stat = rep_prep["aggregates_by_case"]["authored-static"]["baseline-indexed"]["combined"]["draw_order_prepare_ms"]
            self.assertFalse(base_prep_stat["available"])
            self.assertFalse(rep_prep["comparisons"]["endpoint_implementation_and_policy"]["authored-static"]["draw_order_prepare_ms"]["available"])
            rebuild_edit_prep = rep_prep["aggregates_by_case"]["dense-z-edit"]["candidate-rebuild-w1"]["edit_frames"]["draw_order_prepare_ms"]
            repair_edit_prep = rep_prep["aggregates_by_case"]["dense-z-edit"]["candidate-repair-w1"]["edit_frames"]["draw_order_prepare_ms"]
            repair_edit_paired = rep_prep["aggregates_by_case"]["dense-z-edit"]["candidate-repair-w1"]["edit_frames"]["paired_step_render_ms"]
            self.assertTrue(rebuild_edit_prep["available"] and repair_edit_prep["available"])
            self.assertAlmostEqual(rebuild_edit_prep["mean_ms"], 0.9, places=4)
            self.assertAlmostEqual(repair_edit_prep["mean_ms"], 0.24, places=4)
            # Paired step+render still reflects the 15 ms presentation wait (~17.5 ms) independently.
            self.assertGreater(repair_edit_paired["mean_ms"], 15.0)
            self.assertIn("unavailable (frozen baseline)", (out_prep / "report.md").read_text())
            # T063: Verify work-only control totals are included in compact report under cached-linear only.
            self.assertEqual(list(rep_prep["work_resource_controls"]["rejected-z-edit"].keys()), ["cached-linear"])
            self.assertEqual(list(rep_prep["work_resource_controls"]["fully-moving-crossing"].keys()), ["cached-linear"])
            self.assertEqual(rep_prep["work_resource_controls"]["rejected-z-edit"]["cached-linear"]["actual_changed_z_total"], 400)
            self.assertEqual(rep_prep["work_resource_controls"]["fully-moving-crossing"]["cached-linear"]["actual_changed_pose_total"], 40000)

            # 8. T060: Dense worker-arm collisions rejected before creating output directory;
            #    all-zero-only selections remain compatible; manifest worker label mismatches rejected.
            for w, mw in ((4, 1), (1, 1), (2, 4)):
                out_w = root / f"dense_bad_w_{w}_{mw}"
                with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                        mock.patch.object(capture, "_run", side_effect=fake_ok):
                    rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "dense-z-edit",
                                       "--candidate-probe", str(bins["cand"]),
                                       "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                       "--workers", str(w), "--multiworker-count", str(mw), "--repetitions", "1",
                                       "--warmup-frames", "2", "--measure-frames", "4", "--output", str(out_w)])
                self.assertEqual(rc, 2, (w, mw))
                self.assertFalse(out_w.exists(), f"Output directory must not be created for invalid ({w}, {mw})")

            for w, mw in ((1, 4), (4, 1), (1, 1), (2, 4)):
                all_zero_cases = capture.render_order_cases("authored-static,sparse-camera-pan", w, mw)
                for c in all_zero_cases:
                    arms = capture.render_order_arms(c)
                    self.assertEqual(len({a["arm"] for a in arms}), len(arms))
                    self.assertEqual(len({(a["arm"], a["workers"]) for a in arms}), len(arms))
            dense_valid = capture.render_order_cases("dense-z-edit", 1, 4)
            dense_arms_valid = capture.render_order_arms(dense_valid[0])
            self.assertEqual(len({a["arm"] for a in dense_arms_valid}), 4)
            self.assertEqual(len({(a["ordering_variant"], a["workers"]) for a in dense_arms_valid}), 4)

            # 9. T061: Final-publication budget failure and oversized in-progress output.
            out_pub = root / "fail_final_pub"
            fake_pub, calls_pub = make_fake_run(fail_parity=False)
            # First run succeeds at large budget so we know exact pre-report byte size.
            out_probe_size = root / "probe_size"
            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_pub):
                self.assertEqual(capture.main(["--comparison", "render-order-culling", "--focused-cases", "authored-static",
                                               "--baseline-probe", str(bins["base"]), "--candidate-probe", str(bins["cand"]),
                                               "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                               "--workers", "1", "--multiworker-count", "4", "--repetitions", "1",
                                               "--warmup-frames", "2", "--measure-frames", "2", "--output", str(out_probe_size)]), 0)
            rep_size = (out_probe_size / "report.json").stat().st_size + (out_probe_size / "report.md").stat().st_size
            total_ok_bytes = sum(p.stat().st_size for p in out_probe_size.rglob("*") if p.is_file())
            pre_report_bytes = total_ok_bytes - rep_size
            tight_mib = (pre_report_bytes + capture.PARTIAL_SUMMARY_RESERVE_BYTES + 256) / (1024 * 1024)
            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_pub):
                rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "authored-static",
                                   "--baseline-probe", str(bins["base"]), "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "1",
                                   "--warmup-frames", "2", "--measure-frames", "2",
                                   "--max-output-mib", repr(tight_mib), "--output", str(out_pub)])
            self.assertEqual(rc, 2)
            self.assertFalse((out_pub / "report.json").exists())
            self.assertFalse((out_pub / "report.md").exists())
            partial_pub = json.loads((out_pub / "partial-report.json").read_text())
            self.assertFalse(partial_pub["complete"])
            self.assertEqual(partial_pub["failure_stage"], "final-publication")
            self.assertLessEqual(sum(p.stat().st_size for p in out_pub.rglob("*") if p.is_file()),
                                 int(tight_mib * 1024 * 1024))

            out_oversized = root / "fail_oversized_in_progress"
            fake_over, calls_over = make_fake_run(oversized_in_progress_bytes=64 * 1024)
            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_over):
                rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "authored-static",
                                   "--baseline-probe", str(bins["base"]), "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "1",
                                   "--warmup-frames", "2", "--measure-frames", "2",
                                   "--max-output-mib", "0.03", "--output", str(out_oversized)])
            self.assertEqual(rc, 2)
            self.assertEqual(len(calls_over), 1)
            self.assertFalse((out_oversized / "report.json").exists())
            self.assertEqual(list(out_oversized.rglob("*.frames.jsonl")), [])
            partial_over = json.loads((out_oversized / "partial-report.json").read_text())
            self.assertFalse(partial_over["complete"])
            self.assertLessEqual(sum(p.stat().st_size for p in out_oversized.rglob("*") if p.is_file()),
                                 int(0.03 * 1024 * 1024))

            # 10. T062: Dense 1-frame rejection before launch & candidate missing draw_order_prepare_ns rejection.
            out_one_frame = root / "fail_dense_one_frame"
            fake_1f, calls_1f = make_fake_run()
            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_1f):
                rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "dense-z-edit",
                                   "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "1",
                                   "--warmup-frames", "2", "--measure-frames", "1", "--output", str(out_one_frame)])
            self.assertEqual(rc, 2)
            self.assertFalse(out_one_frame.exists())
            self.assertEqual(calls_1f, [])

            out_missing_col = root / "fail_missing_prep_col"
            fake_nocol, _ = make_fake_run(omit_candidate_prepare_col=True)
            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_nocol):
                rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "dense-z-edit",
                                   "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "1",
                                   "--warmup-frames", "2", "--measure-frames", "4", "--output", str(out_missing_col)])
            self.assertEqual(rc, 2)
            self.assertFalse((out_missing_col / "report.json").exists())
            partial_nocol = json.loads((out_missing_col / "partial-report.json").read_text())
            self.assertIn("draw_order_prepare_ns", partial_nocol["failure_reason"])

            # 11. T063: Negative control-resource driver tests (omitted rejected edit, misdirected edit, stationary crossing).
            for label, override, expected_arm in (
                ("omitted_rejected", {("rejected-z-edit", "cached-linear"): {"actual_changed_z_total": 399, "actual_changed_sprite_total": 399}},
                 "rejected-z-edit-cached-linear"),
                ("misdirected_pose", {("rejected-z-edit", "cached-linear"): {"actual_changed_pose_total": 1, "actual_changed_sprite_total": 401}},
                 "rejected-z-edit-cached-linear"),
                ("stationary_crossing", {("crossing", "cached-linear"): {"actual_changed_pose_total": 0, "actual_changed_sprite_total": 0}},
                 "fully-moving-crossing-cached-linear"),
            ):
                out_ctrl = root / f"fail_ctrl_{label}"
                fake_ctrl, _ = make_fake_run(control_override=override)
                with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                        mock.patch.object(capture, "_run", side_effect=fake_ctrl):
                    rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "authored-static",
                                       "--baseline-probe", str(bins["base"]), "--candidate-probe", str(bins["cand"]),
                                       "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                       "--workers", "1", "--multiworker-count", "4", "--repetitions", "1",
                                       "--warmup-frames", "2", "--measure-frames", "4", "--output", str(out_ctrl)])
                self.assertEqual(rc, 2, label)
                self.assertFalse((out_ctrl / "report.json").exists(), label)
                partial_ctrl = json.loads((out_ctrl / "partial-report.json").read_text())
                self.assertNotIn(expected_arm, [item["arm"] for item in partial_ctrl["completed_arms"]], label)

            # 12. T069/T075: validate_render_order_control_resource requires identity and surviving work counters
            #     while remaining compatible with historical legacy proofs containing queries/index_relinks.
            ctrl_rej = {"control_name": "rejected-z-edit", "profile": "rejected-z-edit", "sprite_count": 10000}
            ctrl_args = types.SimpleNamespace(workers=1, seed=0, dt=1 / 120, warmup_frames=2, measure_frames=4)
            valid_rej = {
                "profile": "rejected-z-edit", "sprite_count": 10000, "mode": "cached-linear",
                "workers": 1, "seed": 0, "dt": 1 / 120, "warmup_frames": 2, "measure_frames": 4,
                "allocation_calls": 0, "allocated_bytes": 0,
                "actual_changed_sprite_total": 400, "actual_changed_pose_total": 0,
                "actual_changed_z_total": 400, "actual_changed_sprites_per_step_max": 100,
                "actual_changed_fraction": 400 / 40000,
                "render_view_preparations": 4, "render_eligibility_checks": 4,
                "render_visibility_checks": 0, "render_submissions": 256,
                "cold_observation": {}, "capacity_growth_observation": {}, "reset_observation": {},
                "culling_work": {
                    "bounds_refreshes": 0,
                    "sprite_order_preparations": 0, "sprite_order_sorts": 0,
                    "candidate_visits": 0, "full_population_visits": 0,
                    "dirty_enqueues": 0, "visible_membership_checks": 0,
                    "visible_membership_changes": 0,
                },
            }
            capture.validate_render_order_control_resource(ctrl_rej, "cached-linear", valid_rej, ctrl_args)
            legacy_rej = json.loads(json.dumps(valid_rej))
            legacy_rej["mode"] = "indexed"
            legacy_rej["culling_work"].update({"queries": 0, "index_relinks": 0})
            capture.validate_render_order_control_resource(ctrl_rej, "indexed", legacy_rej, ctrl_args)
            for id_field in ("profile", "sprite_count", "mode", "workers", "seed", "warmup_frames", "measure_frames", "dt"):
                bad_id = json.loads(json.dumps(valid_rej))
                bad_id.pop(id_field)
                with self.subTest(missing_identity=id_field), self.assertRaises(capture.CaptureError):
                    capture.validate_render_order_control_resource(ctrl_rej, "cached-linear", bad_id, ctrl_args)
            no_work = json.loads(json.dumps(valid_rej))
            no_work.pop("culling_work")
            with self.assertRaises(capture.CaptureError):
                capture.validate_render_order_control_resource(ctrl_rej, "cached-linear", no_work, ctrl_args)
            for wk in tuple(valid_rej["culling_work"].keys()):
                for bad_val in (None, -1, True):
                    bad_w = json.loads(json.dumps(valid_rej))
                    bad_w["culling_work"][wk] = bad_val
                    with self.subTest(work_key=wk, bad_val=bad_val), self.assertRaises(capture.CaptureError):
                        capture.validate_render_order_control_resource(ctrl_rej, "cached-linear", bad_w, ctrl_args)

            # 13. T070: Final deadline race produces either valid success report or compact failure report.
            out_race_fail = root / "race_fail"
            fake_race, race_calls = make_fake_run()
            orig_check = capture.check_budget
            state = {"final_calls": 0}

            def expire_on_final_check(reserve_bytes=0):
                if reserve_bytes == 0:
                    state["final_calls"] += 1
                    if state["final_calls"] >= 1:
                        raise capture.BudgetExceeded("Capture exceeded deadline at final check")
                return orig_check(reserve_bytes=reserve_bytes)

            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_race), \
                    mock.patch.object(capture, "check_budget", side_effect=expire_on_final_check):
                rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "authored-static",
                                   "--baseline-probe", str(bins["base"]), "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "1",
                                   "--warmup-frames", "2", "--measure-frames", "2", "--output", str(out_race_fail)])
            self.assertEqual(rc, 2)
            self.assertFalse((out_race_fail / "report.json").exists())
            self.assertFalse((out_race_fail / "report.md").exists())
            partial_race = json.loads((out_race_fail / "partial-report.json").read_text())
            self.assertFalse(partial_race["complete"])
            self.assertEqual(partial_race["failure_stage"], "final-publication")

            out_race_ok = root / "race_ok"
            fake_race_ok, _ = make_fake_run()
            orig_run_roc = capture.run_render_order_culling
            clock = {"now": 100.0}

            def run_and_expire(a):
                res = orig_run_roc(a)
                # Advance monotonic past deadline right as run_render_order_culling returns;
                # with check_on_exit=False, context exit must not fail or replace the finished report.
                clock["now"] = 1000.0
                return res

            with mock.patch("capture_budget.time.monotonic", side_effect=lambda: clock["now"]), \
                    mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_race_ok), \
                    mock.patch.object(capture, "run_render_order_culling", side_effect=run_and_expire):
                rc = capture.main(["--comparison", "render-order-culling", "--focused-cases", "authored-static",
                                   "--baseline-probe", str(bins["base"]), "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]), "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "1",
                                   "--warmup-frames", "2", "--measure-frames", "2", "--max-seconds", "10",
                                   "--output", str(out_race_ok)])
            self.assertEqual(rc, 0)
            self.assertTrue((out_race_ok / "report.json").is_file())
            self.assertFalse((out_race_ok / "partial-report.json").exists())

            # Explicitly test CaptureBudget context exit enforcement: default checks on exit,
            # check_on_exit=False skips post-yield check while preserving entry check.
            b_root = root / "budget_ctx"; b_root.mkdir()
            times = iter([100.0, 100.0, 200.0])
            with mock.patch("capture_budget.time.monotonic", side_effect=lambda: next(times)):
                b = capture.CaptureBudget([b_root], max_seconds=10.0)
                with self.assertRaises(capture.BudgetExceeded):
                    with capture.capture_budget(b):
                        pass
            times2 = iter([100.0, 100.0, 200.0])
            with mock.patch("capture_budget.time.monotonic", side_effect=lambda: next(times2)):
                b2 = capture.CaptureBudget([b_root], max_seconds=10.0)
                with capture.capture_budget(b2, check_on_exit=False):
                    pass

            # 14. T081: Optional frozen endpoint for authored-static and sparse-camera-pan.
            out_cand_only = root / "cand_only_all_zero"
            fake_cand_only, _ = make_fake_run()
            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_cand_only):
                rc = capture.main(["--comparison", "render-order-culling",
                                   "--focused-cases", "authored-static,sparse-camera-pan",
                                   "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]),
                                   "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "2",
                                   "--warmup-frames", "2", "--measure-frames", "4", "--output", str(out_cand_only)])
            self.assertEqual(rc, 0)
            rep_cand_only = json.loads((out_cand_only / "report.json").read_text())
            self.assertIsNone(rep_cand_only["scope"]["baseline_build_identity"])
            self.assertEqual(rep_cand_only["comparisons"]["endpoint_implementation_and_policy"], {})
            for cname in ("authored-static", "sparse-camera-pan"):
                self.assertEqual(list(rep_cand_only["aggregates_by_case"][cname].keys()),
                                 ["candidate-cached-linear"])
                self.assertTrue(rep_cand_only["aggregates_by_case"][cname]["candidate-cached-linear"]["combined"]["visible_selection_ms"]["available"])
            self.assertIn("candidate-only", (out_cand_only / "report.md").read_text())

            out_with_base = root / "with_base_all_zero"
            fake_with_base, _ = make_fake_run()
            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_with_base):
                rc = capture.main(["--comparison", "render-order-culling",
                                   "--focused-cases", "authored-static,sparse-camera-pan",
                                   "--baseline-probe", str(bins["base"]),
                                   "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]),
                                   "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "2",
                                   "--warmup-frames", "2", "--measure-frames", "4", "--output", str(out_with_base)])
            self.assertEqual(rc, 0)
            rep_with_base = json.loads((out_with_base / "report.json").read_text())
            self.assertIsNotNone(rep_with_base["scope"]["baseline_build_identity"])
            for cname in ("authored-static", "sparse-camera-pan"):
                self.assertEqual(set(rep_with_base["aggregates_by_case"][cname].keys()),
                                 {"baseline-indexed", "candidate-cached-linear"})
                self.assertIn("combined implementation + policy",
                              rep_with_base["comparisons"]["endpoint_implementation_and_policy"][cname]["comparison_scope"])
                self.assertFalse(rep_with_base["aggregates_by_case"][cname]["baseline-indexed"]["combined"]["visible_selection_ms"]["available"])
                self.assertTrue(rep_with_base["aggregates_by_case"][cname]["candidate-cached-linear"]["combined"]["visible_selection_ms"]["available"])

            # 15. T082: Candidate paired CSV missing visible_selection_ns must fail with bounded partial report.
            out_no_sel = root / "fail_missing_selection_col"
            fake_no_sel, _ = make_fake_run(omit_candidate_selection_col=True)
            with mock.patch.object(capture, "_read_probe_identity", side_effect=make_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_no_sel):
                rc = capture.main(["--comparison", "render-order-culling",
                                   "--focused-cases", "authored-static",
                                   "--baseline-probe", str(bins["base"]),
                                   "--candidate-probe", str(bins["cand"]),
                                   "--candidate-validation-probe", str(bins["val"]),
                                   "--asset-root", str(assets),
                                   "--workers", "1", "--multiworker-count", "4", "--repetitions", "1",
                                   "--warmup-frames", "2", "--measure-frames", "4", "--output", str(out_no_sel)])
            self.assertEqual(rc, 2)
            self.assertFalse((out_no_sel / "report.json").exists())
            partial_no_sel = json.loads((out_no_sel / "partial-report.json").read_text())
            self.assertFalse(partial_no_sel["complete"])
            self.assertIn("visible_selection_ns", partial_no_sel["failure_reason"])

    def test_incremental_pilot_cli_strategy_dispatch_and_current_resource_schema(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            assets = root / "assets"; assets.mkdir(); (assets / "scene.json").write_text("{}")
            cand_tim = root / "cand_timing"
            cand_val = root / "cand_val"
            for p in (cand_tim, cand_val):
                p.write_text("#!/bin/sh\nexit 0\n")
                p.chmod(0o755)
            tim_id = {"configuration": "Release", "sanitizer": "none", "work_probes": False, "debug_tools": False,
                      "commit": "a" * 40, "dirty": False, "compiler": "g++", "flags": "-O3",
                      "source_sha256": "c" * 64, "fixture_sha256": "f" * 64, "harness_sha256": "d" * 64}
            val_id = {**tim_id, "work_probes": True}

            def fake_identity(binary, log_path, env):
                return dict(val_id if binary == cand_val else tim_id)

            def make_pilot_runner(nonzero_reset_cap=False):
                calls = []
                def fake_run(cmd, log_path, env, **kwargs):
                    calls.append(list(cmd))
                    Path(log_path).write_text("ok\n")
                    profile = cmd[cmd.index("--profile") + 1]
                    count = int(cmd[cmd.index("--sprite-count") + 1])
                    workers = int(cmd[cmd.index("--workers") + 1])
                    meas = int(cmd[cmd.index("--measure-frames") + 1])
                    warm = int(cmd[cmd.index("--warmup-frames") + 1])
                    rep = int(cmd[cmd.index("--repetitions") + 1])
                    run_idx = int(cmd[cmd.index("--run-index") + 1])
                    layout = cmd[cmd.index("--layout") + 1]
                    mode = "disabled" if "--no-render-culling" in cmd else cmd[cmd.index("--render-strategy") + 1]
                    traj = capture.expected_trajectory(
                        {"profile": profile, "sprite_count": count, "layout": layout, "changed_percent": 0},
                        types.SimpleNamespace(seed=0))
                    if "--validate-resources" in cmd:
                        ev = Path(cmd[cmd.index("--evidence-output") + 1])
                        res_path = Path(str(ev) + ".resources.json")
                        payload = {
                            "profile": profile, "sprite_count": count, "workers": workers,
                            "seed": 0, "dt": 0.008333333333333333, "warmup_frames": warm,
                            "measure_frames": meas, "mode": mode, "trajectory_parameters": traj,
                            "fixture_sha256": val_id["fixture_sha256"], "harness_sha256": val_id["harness_sha256"],
                            "source_sha256": val_id["source_sha256"], "renderer_mapping": self.mapping,
                            "headless": False, "timing_eligible": False, "validation_mode": "per-step",
                            "allocation_calls": 0, "allocated_bytes": 0,
                            "allocation_calls_per_frame": 0.0, "allocated_bytes_per_frame": 0.0,
                            "allocation_probe": "C++ operator new/new[]; SDL allocations not attributed",
                            "process_rss_before_kib": 1000, "process_rss_after_kib": 1000,
                            "process_rss_after_reset_kib": 1000,
                            "render_view_preparations": meas,
                            "render_eligibility_checks": 0 if mode == "disabled" else meas,
                            "render_visibility_checks": 0 if mode == "disabled" else count * meas,
                            "render_submissions": count * meas if mode == "disabled" else 4 * meas,
                            "actual_changed_sprite_total": 0, "actual_changed_pose_total": 0,
                            "actual_changed_z_total": 0, "actual_changed_sprites_per_step_max": 0,
                            "actual_changed_fraction": 0.0,
                            "cold_observation": {
                                "first_warmup_step_elapsed_ns": 5000,
                                "fixture_construction_and_registration_ns": 10000,
                                "fixture_construction_allocation_calls": 10,
                                "fixture_construction_allocated_bytes": 1024,
                            },
                            "capacity_growth_observation": {"attempted": False, "reason": "within capacity"},
                            "reset_observation": {
                                "records_registered_after_clear": 0,
                                "owned_bytes_after_clear": 0,
                                "record_capacity_after_clear": 16 if nonzero_reset_cap else 0,
                                "elapsed_ns": 1000, "allocation_calls": 0, "allocated_bytes": 0,
                            },
                            "culling_initial": {
                                "state_creations": 0 if mode == "disabled" else 1,
                                "records_registered": 0 if mode == "disabled" else count,
                            },
                            "culling_work": {"peak_owned_bytes": 0 if mode == "disabled" else 1024},
                        }
                        res_path.write_text(json.dumps(payload))
                    else:
                        csv_path = Path(cmd[cmd.index("--csv-output") + 1])
                        rp = Path(cmd[cmd.index("--resource-proof") + 1])
                        manifest = {
                            "schema_version": 3, "build": tim_id, "scenario": "linear_render_culling",
                            "workers": workers, "seed": 0, "dt": 0.008333333333333333,
                            "warmup_steps": warm, "measured_steps": meas, "requested_measured_steps": meas,
                            "repetitions": rep, "repetition_index": run_idx,
                            "timing_eligible": True, "validation_mode": "boundary-only",
                            "stopped_on_quit": False, "complete_capture": True, "total_steps": meas,
                            "linear_render_culling": {"mode": mode, "requested_mode": mode,
                                                       "effective_policy": mode, "enabled": True},
                            "requested_render_strategy": mode, "rendering": True,
                            "render_samples": meas, "total_render_samples": meas,
                            "resolved_parameters": traj,
                            "fixture_sha256": tim_id["fixture_sha256"], "harness_sha256": tim_id["harness_sha256"],
                            "renderer_mapping": self.mapping, "machine": {"cpu": "x"}, "affinity": [0],
                            "dependencies": {"SDL": "3"},
                            "boundaries": {s: "scope" for s in capture.INCREMENTAL_BENCHMARK_SCOPES},
                            "physical_invariants": {"finite": True, "populations_valid": True, "supports_valid": True},
                            "resource_observation": {"sha256": capture.sha256(rp)},
                        }
                        csv_path.with_suffix(".manifest.json").write_text(json.dumps(manifest))
                        with csv_path.open("w", newline="") as f:
                            w = csv.writer(f)
                            w.writerow(["schema_version", "category", "name", "samples", "avg", "median", "p95", "p99", "min", "max", "unit"])
                            for sc in capture.INCREMENTAL_BENCHMARK_SCOPES:
                                val_ms = 0.5 if sc == "render" else 1.0
                                w.writerow([3, "scope", sc, meas, val_ms, val_ms, val_ms + 0.1, val_ms + 0.1, 0.4, val_ms + 0.1, "ms"])
                            for ct in capture.INCREMENTAL_BENCHMARK_COUNTERS:
                                w.writerow([3, "counter", ct, meas, 0, 0, 0, 0, 0, 0, "count"])
                        with csv_path.with_suffix(".samples.csv").open("w", newline="") as f:
                            w = csv.writer(f)
                            w.writerow(["schema_version", "sample_kind", "index",
                                        *[s + "_ns" for s in capture.INCREMENTAL_BENCHMARK_SCOPES],
                                        *capture.INCREMENTAL_BENCHMARK_COUNTERS])
                            for i in range(meas):
                                w.writerow([3, "step", i, *[0 if s == "render" else 1_000_000 for s in capture.INCREMENTAL_BENCHMARK_SCOPES],
                                            *([0] * len(capture.INCREMENTAL_BENCHMARK_COUNTERS))])
                            for i in range(meas):
                                w.writerow([3, "render", i, *[500_000 if s == "render" else 0 for s in capture.INCREMENTAL_BENCHMARK_SCOPES],
                                            *([0] * len(capture.INCREMENTAL_BENCHMARK_COUNTERS))])
                        with csv_path.with_suffix(".paired.csv").open("w", newline="") as f:
                            w = csv.writer(f)
                            w.writerow(["step", "paired_step_render_ns", "draw_order_prepare_ns", "visible_selection_ns"])
                            for i in range(meas):
                                w.writerow([i, 1500, 100, 200])
                return fake_run, calls

            # README flags with explicit --strategy cached-linear, omitted --strategy, and --strategy all.
            for label, extra_flags in (
                ("explicit_cached", ["--strategy", "cached-linear"]),
                ("omitted_strategy", []),
                ("all_shorthand", ["--strategy", "all"]),
            ):
                out_dir = root / f"pilot_{label}"
                fake_run, _ = make_pilot_runner()
                with mock.patch.object(capture, "_read_probe_identity", side_effect=fake_identity), \
                        mock.patch.object(capture, "_run", side_effect=fake_run):
                    rc = capture.main([
                        "--comparison", "incremental", "--phase", "pilot", *extra_flags, "--exploratory",
                        "--focused-cases", "mostly-offscreen",
                        "--candidate-validation-probe", str(cand_val),
                        "--candidate-probe", str(cand_tim),
                        "--asset-root", str(assets), "--workers", "1", "--multiworker-count", "6",
                        "--seed", "0", "--repetitions", "2", "--warmup-frames", "1", "--measure-frames", "2",
                        "--dt", "0.008333333333333333", "--output", str(out_dir),
                    ])
                self.assertEqual(rc, 0, label)
                rep = json.loads((out_dir / "report.json").read_text())
                self.assertEqual(rep["scope"]["strategies"], ["cached-linear"])

            # Nonzero common capacity after clear must fail the incremental pilot resource stage.
            out_bad_reset = root / "pilot_bad_reset"
            fake_bad_reset, _ = make_pilot_runner(nonzero_reset_cap=True)
            with mock.patch.object(capture, "_read_probe_identity", side_effect=fake_identity), \
                    mock.patch.object(capture, "_run", side_effect=fake_bad_reset):
                rc = capture.main([
                    "--comparison", "incremental", "--phase", "pilot", "--strategy", "cached-linear",
                    "--focused-cases", "mostly-offscreen",
                    "--candidate-validation-probe", str(cand_val),
                    "--candidate-probe", str(cand_tim),
                    "--asset-root", str(assets), "--workers", "1", "--multiworker-count", "6",
                    "--repetitions", "2", "--warmup-frames", "1", "--measure-frames", "2",
                    "--output", str(out_bad_reset),
                ])
            self.assertEqual(rc, 2)
            self.assertFalse((out_bad_reset / "report.json").exists())

            # Retired strategies must fail before capture on CLI and option validation.
            for retired in ("indexed", "current-linear"):
                out_ret = root / f"pilot_retired_{retired}"
                fake_ret, calls_ret = make_pilot_runner()
                with mock.patch.object(capture, "_run", side_effect=fake_ret):
                    with self.assertRaises(SystemExit) as exc:
                        capture.main(["--comparison", "incremental", "--phase", "pilot",
                                      "--strategy", retired, "--output", str(out_ret),
                                      "--multiworker-count", "6"])
                self.assertEqual(exc.exception.code, 2)
                self.assertEqual(calls_ret, [])
                with self.assertRaises(capture.CaptureError):
                    capture.validate_incremental_options("incremental", "pilot", retired)


if __name__ == "__main__":
    unittest.main()
