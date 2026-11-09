#!/usr/bin/env python3
"""Guard tests for the matched GUI pilot; no capture workload is run."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT / "benchmark"))

from capture_budget import BudgetExceeded, CaptureBudget  # noqa: E402
import capture_gui  # noqa: E402


class CaptureGuiTest(unittest.TestCase):
    def probe_output(self, workload: str = "hover") -> str:
        lines = [f"GUI_PROBE,version=2,workload={workload},configuration=Debug,compiler=GNU-16.2.1,"
                 f"frames={capture_gui.WARMUP_FRAMES + capture_gui.MEASURED_FRAMES},"
                 f"warmup_frames={capture_gui.WARMUP_FRAMES}", "GUI_RENDERER,dummy,software",
                 "GUI_COLD_PREP_US,120000"]
        expected_events = {0, 10, 20} if workload == "hover" else set()
        for frame in range(capture_gui.MEASURED_FRAMES):
            lines.extend((f"GUI_ACTIVE_FRAME_US,{frame},1200",
                          f"GUI_WAIT_INCLUSIVE_US,{frame},1800",
                          f"GUI_IDLE_CPU_US,{frame},20"))
            if frame in expected_events:
                lines.append(f"GUI_EVENT_PRESENT_US,{frame},80")
        return "\n".join(lines) + "\n"

    def test_small_pilot_defaults_and_hard_ceiling(self):
        parser = capture_gui._parser()
        defaults = parser.parse_args([
            "--baseline-probe", "a", "--baseline-source-root", ".", "--baseline-label", "base",
            "--candidate-probe", "b", "--candidate-source-root", ".", "--candidate-label", "cand",
            "--workload", "idle", "--project-root", ".", "--asset-root", "assets", "--output", "out",
        ])
        self.assertEqual(defaults.max_output_mib, 50.0)
        self.assertEqual(defaults.max_seconds, 600.0)
        self.assertEqual(capture_gui.REPETITIONS, 3)
        self.assertEqual(capture_gui.WARMUP_FRAMES, 5)
        self.assertEqual(capture_gui.MEASURED_FRAMES, 30)
        self.assertEqual(capture_gui.MAX_OUTPUT_MIB, 50.0)
        self.assertEqual(capture_gui.MAX_SECONDS, 600.0)

    def test_probe_schema_captures_all_metrics_and_event_latency(self):
        parsed = capture_gui._parse_probe_output(self.probe_output("hover"), "hover")
        self.assertEqual(len(parsed["frames"]), 30)
        self.assertEqual(parsed["cold_preparation_us"], 120000)
        self.assertEqual(parsed["frames"][0]["active_frame_us"], 1200)
        self.assertEqual(parsed["frames"][0]["wait_inclusive_us"], 1800)
        self.assertEqual(parsed["frames"][0]["idle_cpu_us"], 20)
        self.assertEqual(parsed["event_present"], [
            {"frame": 0, "event_present_us": 80},
            {"frame": 10, "event_present_us": 80},
            {"frame": 20, "event_present_us": 80},
        ])
        idle = capture_gui._parse_probe_output(self.probe_output("idle"), "idle")
        self.assertEqual(idle["event_present"], [])

    def test_malformed_or_mismatched_workload_samples_are_rejected(self):
        text = self.probe_output("hover")
        with self.assertRaises(capture_gui.CaptureError):
            capture_gui._parse_probe_output(text, "idle")
        with self.assertRaises(capture_gui.CaptureError):
            capture_gui._parse_probe_output(text.replace("GUI_IDLE_CPU_US,29,20\n", ""), "hover")
        with self.assertRaises(capture_gui.CaptureError):
            capture_gui._parse_probe_output(text.replace("GUI_EVENT_PRESENT_US,10,80\n", ""), "hover")
        with self.assertRaises(capture_gui.CaptureError):
            capture_gui._parse_probe_output(text.replace("GUI_COLD_PREP_US,120000", "GUI_COLD_PREP_US,-1"),
                                            "hover")

    def test_matched_identity_and_fresh_output_path_are_required(self):
        base = {"workload": {"name": "idle"}, "workload_sha256": "same"}
        candidate = dict(base)
        capture_gui.validate_matched_workload(base, candidate)
        with self.assertRaises(capture_gui.CaptureError):
            capture_gui.validate_matched_workload(base, {"workload": {"name": "hover"},
                                                          "workload_sha256": "other"})
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "existing"
            output.mkdir()
            args = type("Args", (), {"output": output})()
            with self.assertRaises(capture_gui.CaptureError):
                capture_gui.run_capture(args)

    def test_identity_includes_source_harness_build_and_workload_hashes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            probe = root / "probe"
            probe.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            probe.chmod(0o755)
            project = root / "project"
            assets = project / "assets"
            assets.mkdir(parents=True)
            workload = capture_gui._workload_identity("idle", project, assets, None)
            identity = capture_gui._identity(REPOSITORY_ROOT, probe, "candidate-debug", workload)
            self.assertEqual(len(identity["source_sha256"]), 64)
            self.assertEqual(len(identity["harness_sha256"]), 64)
            self.assertEqual(len(identity["probe_binary_sha256"]), 64)
            self.assertEqual(identity["build_label"], "candidate-debug")
            expected = hashlib.sha256(
                json.dumps(workload, sort_keys=True, separators=(",", ":")).encode("utf-8")
            ).hexdigest()
            self.assertEqual(identity["workload_sha256"], expected)

    def test_capture_failure_keeps_one_partial_run_and_never_retries(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = root / "project"
            assets = project / "assets"
            assets.mkdir(parents=True)
            baseline_source = root / "baseline-source"
            candidate_source = root / "candidate-source"
            baseline_source.mkdir()
            candidate_source.mkdir()
            workload = {"name": "idle"}
            identity = {"workload": workload, "workload_sha256": "same-workload",
                        "probe_path": "/tmp/focused-probe"}
            args = type("Args", (), {
                "output": root / "report", "max_output_mib": 50.0, "max_seconds": 600.0,
                "project_root": project, "asset_root": assets, "scene": None,
                "workload": "idle", "baseline_source_root": baseline_source,
                "baseline_probe": root / "baseline", "baseline_label": "baseline",
                "candidate_source_root": candidate_source,
                "candidate_probe": root / "candidate", "candidate_label": "candidate",
            })()
            run_calls = []

            def fail_once(command, cwd, stdout_path, stderr_path, budget):
                run_calls.append(command)
                stdout_path.write_text("partial probe output\n", encoding="utf-8")
                raise BudgetExceeded("focused output/time budget exhausted")

            with mock.patch.object(capture_gui, "_workload_identity", return_value=workload), \
                    mock.patch.object(capture_gui, "_identity", side_effect=[identity, dict(identity)]), \
                    mock.patch.object(capture_gui, "_run_probe", side_effect=fail_once):
                with self.assertRaises(BudgetExceeded):
                    capture_gui.run_capture(args)

            self.assertEqual(len(run_calls), 1)
            report_path = args.output / "gui-pilot.json"
            self.assertTrue(report_path.is_file())
            report = json.loads(report_path.read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "failed")
            self.assertEqual(len(report["runs"]), 1)
            self.assertIsNone(report["runs"][0]["exit_code"])
            self.assertTrue((args.output / report["runs"][0]["stdout"]).is_file())

    def test_budget_terminates_one_owned_child_and_preserves_partial_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stdout = root / "partial.stdout"
            stderr = root / "partial.stderr"
            children = []
            original_popen = subprocess.Popen

            def record_child(*args, **kwargs):
                child = original_popen(*args, **kwargs)
                children.append(child)
                return child

            # Leave enough startup time for the child to flush its first line on
            # loaded CI hosts; the ten-second sleep still proves budget teardown.
            budget = CaptureBudget([root], max_mib=1, max_seconds=1.0)
            with mock.patch.object(capture_gui.subprocess, "Popen", side_effect=record_child):
                with self.assertRaises(BudgetExceeded):
                    capture_gui._run_probe([sys.executable, "-c",
                                            "import time; print('partial', flush=True); time.sleep(10)"],
                                           root, stdout, stderr, budget)
            self.assertEqual(len(children), 1)
            self.assertIsNotNone(children[0].poll())
            self.assertTrue(stdout.is_file())
            self.assertIn("partial", stdout.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
