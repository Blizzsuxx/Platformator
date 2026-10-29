import csv
import json
import os
from pathlib import Path
import sys
import tempfile
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
        with self.assertRaises(capture.CaptureError):
            capture.engine_control_matrix(0)

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
            capture.validate_render_proof(root, self.case, self.args, "linear")
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
                          "render_view_preparations": 2, "render_eligibility_checks": 2,
                          "render_visibility_checks": 20, "render_submissions": 4,
                          "headless": False,
                          "allocation_probe": "C++ new/new[] counts; SDL allocations not attributed"})
            path.write_text(json.dumps(proof))
            self.assertEqual(capture.validate_resource_proof(root, self.case, self.args, "linear")["allocation_calls"], 0)
            for change in ({"mode": "disabled"}, {"trajectory_parameters": {"profile": "visible"}},
                           {"allocation_calls": -1}, {"render_submissions": None},
                           {"render_visibility_checks": 2.5}, {"render_submissions": 21},
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

            def fake_run(command, log_path, env):
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


if __name__ == "__main__":
    unittest.main()
