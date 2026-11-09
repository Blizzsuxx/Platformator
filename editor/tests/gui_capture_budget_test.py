#!/usr/bin/env python3
"""Focused guard tests for the GUI capture pilot; these tests never run a capture."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

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


class GuiCaptureBudgetTest(unittest.TestCase):
    def make_session(self, root: Path):
        session_path = root / "pilot.session.json"
        capture_root = root / "pilot.capture"
        capture_root.mkdir()
        session = capture_editor._new_session(session_path, capture_root, dict(IDENTITY), 1.0, 60.0)
        session["session_path"] = str(session_path)
        return session_path, capture_root, session

    def timing_output(self):
        samples = []
        workloads = ("tiny", "objects_1000", "objects_10000", "deep_parent_128")
        for repetition in range(capture_editor.REPETITIONS):
            for frame in range(capture_editor.MEASURED_FRAMES):
                for workload in workloads:
                    samples.append({"workload": workload, "repetition": repetition,
                                    "frame": frame, "select_edit_ns": 1})
        return {"schema_version": 1, "mode": "timing",
                "repetitions": capture_editor.REPETITIONS,
                "warmup_frames": capture_editor.WARMUP_FRAMES,
                "measured_frames": capture_editor.MEASURED_FRAMES,
                "samples": samples}

    def test_defaults_and_ceiling_remain_the_small_pilot(self):
        self.assertEqual(capture_editor.DEFAULT_MAX_OUTPUT_MIB, 50.0)
        self.assertEqual(capture_editor.DEFAULT_MAX_SECONDS, 600.0)
        self.assertEqual(capture_editor.REPETITIONS, 3)
        self.assertEqual(capture_editor.WARMUP_FRAMES, 5)
        self.assertEqual(capture_editor.MEASURED_FRAMES, 30)

    def test_byte_and_time_exhaustion_stop_without_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "partial.raw"
            output.write_bytes(b"x" * 2048)
            with self.assertRaises(BudgetExceeded):
                CaptureBudget([root], max_mib=0.001, max_seconds=60).check()
            expired = CaptureBudget([root], max_mib=1, max_seconds=60)
            expired.deadline = 0
            with self.assertRaises(BudgetExceeded):
                expired.check()

    def test_partial_and_malformed_samples_are_rejected(self):
        output = self.timing_output()
        capture_editor._validate_timing_output(output)
        output["samples"].pop()
        with self.assertRaises(capture_editor.CaptureError):
            capture_editor._validate_timing_output(output)
        output = self.timing_output()
        output["samples"][0]["select_edit_ns"] = -1
        with self.assertRaises(capture_editor.CaptureError):
            capture_editor._validate_timing_output(output)

    def test_identity_mismatch_and_repeated_phase_cannot_retry(self):
        with self.assertRaises(capture_editor.CaptureError):
            capture_editor.validate_identity(IDENTITY, dict(IDENTITY, source_sha256="changed"))
        with tempfile.TemporaryDirectory() as temporary:
            _, _, session = self.make_session(Path(temporary))
            with self.assertRaises(capture_editor.CaptureError):
                capture_editor.validate_phase_transition(session, "timing")
            session["phases"]["resources"]["status"] = "complete"
            capture_editor.validate_phase_transition(session, "timing")
            session["phases"]["timing"]["status"] = "complete"
            with self.assertRaises(capture_editor.CaptureError):
                capture_editor.validate_phase_transition(session, "timing")

    def test_expired_budget_terminates_the_owned_child_and_keeps_partial_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session_path, _, session = self.make_session(root)
            budget = CaptureBudget([root], max_mib=1, max_seconds=0.05)
            original_popen = subprocess.Popen
            children = []

            def record_child(*args, **kwargs):
                child = original_popen(*args, **kwargs)
                children.append(child)
                return child

            with mock.patch.object(capture_editor.subprocess, "Popen", side_effect=record_child):
                with self.assertRaises(BudgetExceeded):
                    capture_editor._run_command(
                        [sys.executable, "-c", "import time; print('partial', flush=True); time.sleep(10)"],
                        root / "partial.stdout", root / "partial.stderr", budget, session)
            self.assertEqual(len(children), 1)
            self.assertIsNotNone(children[0].poll())
            self.assertTrue((root / "partial.stdout").is_file())
            self.assertIsNone(session["commands"][0]["exit_code"])
            self.assertTrue(session_path.is_file())


if __name__ == "__main__":
    unittest.main(verbosity=2)
