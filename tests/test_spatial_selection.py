#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class BenchmarkSpatialSelectionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if len(sys.argv) != 2:
            raise RuntimeError("Expected the built platformator_benchmark_runner path")
        cls.runner = Path(sys.argv[1]).resolve()

    def run_capture(self, directory, name, *spatial_args):
        output = Path(directory) / f"{name}.csv"
        command = [
            str(self.runner), "--scenario", "broad_phase", "--broad-count", "8",
            "--warmup-frames", "1", "--measure-frames", "2", "--workers", "1",
            "--seed", "0", *spatial_args, "--csv-output", str(output),
        ]
        completed = subprocess.run(command, text=True, capture_output=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        manifest = output.with_suffix(".manifest.json")
        return json.loads(manifest.read_text())

    def test_requested_effective_and_disabled_render_status(self):
        with tempfile.TemporaryDirectory(prefix="platformator-spatial-selection-") as directory:
            manifest = self.run_capture(
                directory, "bvh", "--broad-phase", "bvh", "--render-culling", "bvh",
                "--bvh-maintenance", "full-rebuild", "--bvh-padding", "3",
                "--bvh-query-execution", "serial",
            )
            settings = manifest["selection"]
            self.assertEqual(settings["requested"], {
                "broad_phase": "bvh", "render_culling": "bvh", "bvh_maintenance": "full-rebuild",
                "bvh_padding": 3.0, "bvh_query_execution": "serial",
            })
            self.assertEqual(settings["effective"], {
                "broad_phase": "bvh", "render_culling": "none", "bvh_maintenance": "full-rebuild",
                "bvh_padding": 3.0, "bvh_query_execution": "serial",
                "render_culling_status": "disabled_no_render",
            })
            self.assertTrue(manifest["input"]["headless"])
            self.assertEqual(manifest["sample_counts"]["render"], 0)
            self.assertEqual(manifest["input"]["actual_render_samples"], 0)

    def test_invalid_spatial_settings_fail_in_runner(self):
        completed = subprocess.run(
            [str(self.runner), "--scenario", "broad_phase", "--bvh-padding", "nan"],
            text=True, capture_output=True,
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("finite and nonnegative", completed.stderr)

    def test_all_eight_active_render_selections(self):
        with tempfile.TemporaryDirectory(prefix="platformator-render-selection-") as directory:
            for broad_phase in ("segmented", "bvh"):
                for render_mode in ("none", "linear", "bvh", "segmented"):
                    manifest = self.run_capture(
                        directory, f"{broad_phase}-{render_mode}", "--render", "--broad-phase", broad_phase,
                        "--render-culling", render_mode,
                    )
                    settings = manifest["selection"]
                    self.assertEqual(settings["requested"]["broad_phase"], broad_phase)
                    self.assertEqual(settings["requested"]["render_culling"], render_mode)
                    self.assertEqual(settings["effective"]["render_culling"], render_mode)
                    self.assertEqual(
                        settings["effective"]["render_culling_status"],
                        "none_selected" if render_mode == "none" else "active",
                    )
                    self.assertFalse(manifest["input"]["headless"])
                    self.assertEqual(manifest["sample_counts"]["render"], 2)
                    self.assertEqual(manifest["input"]["actual_render_samples"], 2)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
