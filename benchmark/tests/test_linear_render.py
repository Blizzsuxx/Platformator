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
                        "linear_render_culling": {"mode": "indexed", "requested_mode": "indexed",
                                                   "effective_policy": "indexed", "enabled": True},
                        "requested_render_strategy": "indexed", "rendering": True,
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
                csv_path, self.case, args, "indexed", resource,
                {"source_sha256": self.identity, "fixture_sha256": self.identity,
                 "harness_sha256": "b" * 64}, metrics)
            self.assertTrue(result["timing_eligible"])
            manifest["repetition_index"] = 1
            csv_path.with_suffix(".manifest.json").write_text(json.dumps(manifest))
            with self.assertRaises(capture.CaptureError):
                capture._validate_focused_timing_manifest(
                    csv_path, self.case, args, "indexed", resource,
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
            path = root / f"{capture.case_key(self.case)}-linear.resources.json"
            proof = self.proof()
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
                                                "record_capacity_after_clear": 0, "cell_capacity_after_clear": 0,
                                                "elapsed_ns": 4321, "allocation_calls": 3, "allocated_bytes": 128},
                          "headless": False,
                          "allocation_probe": "C++ new/new[] counts; SDL allocations not attributed"})
            path.write_text(json.dumps(proof))
            self.assertEqual(capture.validate_resource_proof(root, self.case, self.args, "linear")["allocation_calls"], 0)
            for change in ({"mode": "disabled"}, {"trajectory_parameters": {"profile": "visible"}},
                           {"allocation_calls": -1}, {"render_submissions": None},
                           {"render_visibility_checks": 2.5}, {"render_submissions": 21},
                           {"actual_changed_sprite_total": -1}, {"actual_changed_fraction": 0.1},
                           {"cold_observation": {}}, {"capacity_growth_observation": {}},
                           {"reset_observation": {"owned_bytes_after_clear": 1}},
                           {"source_sha256": "not-a-fingerprint"}):
                invalid = dict(proof); invalid.update(change)
                path.write_text(json.dumps(invalid))
                with self.subTest(change=change), self.assertRaises(capture.CaptureError):
                    capture.validate_resource_proof(root, self.case, self.args, "linear")
            path.write_text("corrupt JSON")
            with self.assertRaises(json.JSONDecodeError):
                capture.validate_resource_proof(root, self.case, self.args, "linear")

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
        for values in (("unknown", "validate", "indexed"),
                       ("incremental", "unknown", "indexed"),
                       ("incremental", "validate", "automatic")):
            with self.subTest(values=values), self.assertRaises(capture.CaptureError):
                capture.validate_incremental_options(*values)
        self.assertEqual(capture.validate_incremental_options("incremental", "validate", "indexed"),
                         ("incremental", "validate", "indexed"))

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
        cases = capture.incremental_matrix(6, 1, Path(__file__).resolve().parents[2] / "assets")
        scenes = {(case["profile"], case["workers"], case["sprite_count"]) for case in cases
                  if case["profile"].startswith("scene-")}
        self.assertEqual(scenes, {("scene-default", 1, 6), ("scene-default", 6, 6),
                                  ("scene-stacking", 1, 0), ("scene-stacking", 6, 0)})
        giant = next(case for case in cases if case["profile"] == "giant")
        self.assertEqual(giant["changed_percent"], 10)
        self.assertEqual(giant["size_distribution"], "giant-side-list")
        self.assertEqual(len(capture.frozen_control_case_ids(cases)), 4)

    def test_incremental_matrix_covers_required_population_fraction_and_inactive_controls(self):
        cases = capture.incremental_matrix(6, 1, Path(__file__).resolve().parents[2] / "assets")
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
        cases = capture.incremental_matrix(6, 1, Path(__file__).resolve().parents[2] / "assets")
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
            2, 1, Path(__file__).resolve().parents[2] / "assets")
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
            self.assertEqual(len(images), 6)
            self.assertEqual(len(called), 6)
            self.assertTrue(all("--capture-frame-index" in command and command[-1] == "2" for command in called))
            self.assertIn(("candidate-probe", "c" * 64), identity_calls)
            self.assertIn(("baseline-probe", "d" * 64), identity_calls)
            mismatch_record = json.loads((output / "mismatches" / capture.incremental_case_key(case) /
                                          "mismatch.json").read_text())
            self.assertEqual(mismatch_record["frame"], 2)
            self.assertEqual(len(mismatch_record["images"]), 6)


if __name__ == "__main__":
    unittest.main()
