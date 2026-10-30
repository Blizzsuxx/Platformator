#!/usr/bin/env python3
"""Standard-library contract tests for the editor pilot journal and budget."""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT / "benchmark"))

from capture_budget import BudgetExceeded, CaptureBudget  # noqa: E402
import capture_editor  # noqa: E402


IDENTITY = {
    "source_sha256": "source",
    "harness_sha256": "harness",
    "probe_binary_sha256": "binary",
    "probe_path": "/tmp/probe",
    "build_label": "test-build",
    "workload_sha256": "workload",
}


class CaptureBudgetContractTest(unittest.TestCase):
    def create_session(self, root: Path, max_mib: float = 1.0, max_seconds: float = 60.0):
        session_path = root / "pilot.session.json"
        capture_root = root / "pilot.capture"
        capture_root.mkdir()
        session = capture_editor._new_session(session_path, capture_root, dict(IDENTITY), max_mib, max_seconds)
        session["session_path"] = str(session_path)
        return session_path, capture_root, session

    def resource_output(self):
        populations = {
            "tiny": (10, 10),
            "objects_1000": (1000, 1000),
            "objects_10000": (10000, 10000),
            "deep_parent_128": (128, 1),
        }
        observations = []
        for cycle in range(capture_editor.RESOURCE_CYCLES):
            workloads = []
            for name, (objects, textured) in populations.items():
                workloads.append({
                    "workload": name,
                    "objects": objects,
                    "texture_references": textured,
                    "history_retained_bytes": 100,
                    "runtime": {
                        "objects": objects,
                        "preview_proxies": textured,
                        "textured_proxies": textured,
                        "unique_texture_wrappers": 1,
                        "texture_wrapper_references": textured,
                        "video_driver": "dummy",
                        "renderer_name": "software",
                        "render_target_allocated": True,
                        "document_dirty_after_undo": False,
                        "released_proxies": 0,
                        "released_texture_wrappers": 0,
                        "render_target_after_release": False,
                        "runtime_objects_with_preview": textured,
                        "runtime_objects_after_preview_release": 0,
                    },
                })
            observations.append({
                "cycle": cycle,
                "workloads": workloads,
                "task": {"files": 32, "completed_tasks": 1},
                "process": {"output_bytes": 1_200_000, "console_truncated": True,
                            "console_retained_bytes": 1_048_576},
                "allocation": {"calls": 1, "bytes": 2},
                "game_pipeline": {"target": "main", "executable_identity": "123:456",
                                  "source_identity": "source-identity",
                                  "build_mode": "reuse_existing_binary",
                                  "catalog_verified": True, "pipeline_cancelled": True,
                                  "pipeline_to_run_ns": 1, "stop_reap_ns": 1},
            })
        return {"schema_version": 1, "mode": "resources", "warmup_cycles": 1,
                "cycles": capture_editor.RESOURCE_CYCLES, "observations": observations}

    def test_resource_output_requires_live_runtime_references_and_run_stop(self):
        output = self.resource_output()
        capture_editor._validate_resource_output(output)
        output["observations"][0]["workloads"][0]["runtime"]["released_texture_wrappers"] = 1
        with self.assertRaises(capture_editor.CaptureError):
            capture_editor._validate_resource_output(output)

    def test_phase_continuation_reuses_same_identity_deadline_and_byte_limit(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session_path, capture_root, session = self.create_session(root)
            original_budget = dict(session["budget"])
            session["phases"]["resources"]["status"] = "complete"
            capture_editor._atomic_write_json(session_path, session)
            loaded = capture_editor._load_session(session_path)
            capture_editor.validate_phase_transition(loaded, "timing")
            self.assertEqual(loaded["budget"], original_budget)
            self.assertEqual(loaded["identity"], IDENTITY)
            budget = capture_editor._make_budget(session_path, loaded)
            self.assertEqual(budget.max_bytes, original_budget["max_output_bytes"])
            self.assertLessEqual(budget.deadline, original_budget["deadline_monotonic"])
            self.assertTrue(capture_root.is_dir())

    def test_malformed_session_and_bad_phase_are_rejected(self):
        with self.assertRaises(capture_editor.CaptureError):
            capture_editor.validate_session({"schema_version": 1})
        with tempfile.TemporaryDirectory() as temporary:
            session_path, _, session = self.create_session(Path(temporary))
            session["budget"]["deadline_epoch"] = "later"
            with self.assertRaises(capture_editor.CaptureError):
                capture_editor.validate_session(session)
            with self.assertRaises(capture_editor.CaptureError):
                capture_editor.validate_phase_transition(session, "timing")

    def test_failed_or_already_attempted_phase_cannot_continue(self):
        with tempfile.TemporaryDirectory() as temporary:
            _, _, session = self.create_session(Path(temporary))
            with self.assertRaises(capture_editor.CaptureError):
                capture_editor.validate_phase_transition(session, "timing")
            session["status"] = "failed"
            session["phases"]["resources"]["status"] = "failed"
            with self.assertRaises(capture_editor.CaptureError):
                capture_editor.validate_phase_transition(session, "timing")
            session["status"] = "active"
            session["phases"]["resources"]["status"] = "complete"
            session["phases"]["timing"]["status"] = "complete"
            with self.assertRaises(capture_editor.CaptureError):
                capture_editor.validate_phase_transition(session, "timing")

    def test_cumulative_byte_budget_counts_session_file_and_capture_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session_path, capture_root, session = self.create_session(root, max_mib=0.001)
            budget = capture_editor._make_budget(session_path, session)
            budget.check()
            (capture_root / "partial.raw").write_bytes(b"x" * 2048)
            with self.assertRaises(BudgetExceeded):
                budget.check()

    def test_expired_persistent_deadline_stops_without_restarting_it(self):
        with tempfile.TemporaryDirectory() as temporary:
            session_path, _, session = self.create_session(Path(temporary))
            session["budget"]["deadline_monotonic"] = 0.0
            session["budget"]["deadline_epoch"] = 0.0
            with self.assertRaises(BudgetExceeded):
                capture_editor._make_budget(session_path, session).check()

    def test_identity_mismatch_reopens_evidence_gate(self):
        with self.assertRaises(capture_editor.CaptureError):
            changed = dict(IDENTITY, source_sha256="new-source")
            capture_editor.validate_identity(IDENTITY, changed)
        capture_editor.validate_identity(IDENTITY, dict(IDENTITY))

    def test_frozen_baseline_identity_includes_binary_workload_host_and_harness(self):
        identity = dict(IDENTITY)
        identity.update({
            "build_label": "editor-release",
            "expected_configuration": "Release",
            "probe_path": "/repo/bin/editor-release/platformator_editor_probe",
            "host": {"system": "Linux", "machine": "x86_64", "release": "6.0",
                     "cpu_model": "CPU Model", "cpu_affinity": [0, 1]},
        })
        rows = {
            "Source SHA-256": "source",
            "Harness SHA-256": "harness",
            "Workload SHA-256": "workload",
            "Probe build": "bin/editor-release/platformator_editor_probe, Release, GNU 16, C++20",
            "Probe binary SHA-256": "binary",
            "Host": "Linux x86_64, kernel 6.0, CPU Model, CPUs 0–1",
        }
        with tempfile.TemporaryDirectory() as temporary:
            baseline = Path(temporary) / "BASELINE.md"
            baseline.write_text("\n".join(f"| {key} | {value} |" for key, value in rows.items()))
            capture_editor.validate_frozen_identity(identity, baseline)
            rows["Harness SHA-256"] = "stale-harness"
            baseline.write_text("\n".join(f"| {key} | {value} |" for key, value in rows.items()))
            with self.assertRaises(capture_editor.CaptureError):
                capture_editor.validate_frozen_identity(identity, baseline)

    def test_identity_mismatch_marks_original_timing_phase_terminal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session_path, _, session = self.create_session(root)
            session["phases"]["resources"]["status"] = "complete"
            session["session_path"] = str(session_path)
            changed = dict(IDENTITY, harness_sha256="new-harness")
            with self.assertRaises(capture_editor.CaptureError):
                capture_editor.reject_identity_mismatch(session_path, session, changed)
            stored = capture_editor._load_session(session_path)
            self.assertEqual(stored["status"], "failed")
            self.assertEqual(stored["phases"]["timing"]["status"], "failed")

    def test_fixed_session_path_rejects_replacement_journals(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaises(capture_editor.CaptureError):
                capture_editor.validate_fixed_session_path(root, root / "replacement.json")
            evidence = root / "specs/011-add-imgui-editor/evidence"
            evidence.mkdir(parents=True)
            capture_editor.validate_fixed_session_path(
                root, evidence / "editor-pilot-converged.session.json")
            with self.assertRaises(capture_editor.CaptureError):
                capture_editor.validate_fixed_session_path(
                    root, evidence / "editor-pilot-converged.json")

    def test_nonzero_subprocess_is_retained_and_reported_as_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session_path, _, session = self.create_session(root)
            session["session_path"] = str(session_path)
            budget = capture_editor._make_budget(session_path, session)
            with self.assertRaises(capture_editor.CaptureError):
                capture_editor._run_command(
                    [sys.executable, "-c", "raise SystemExit(7)"],
                    root / "failure.stdout", root / "failure.stderr", budget, session)
            self.assertEqual(session["commands"][-1]["exit_code"], 7)
            self.assertTrue((root / "failure.stderr").is_file())

    def test_budget_file_root_counts_single_file_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            file_path = Path(temporary) / "raw.json"
            file_path.write_bytes(b"z" * 2048)
            budget = CaptureBudget([file_path], max_mib=0.001, max_seconds=60)
            with self.assertRaises(BudgetExceeded):
                budget.check()


if __name__ == "__main__":
    unittest.main(verbosity=2)
