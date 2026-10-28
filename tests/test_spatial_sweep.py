#!/usr/bin/env python3
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "benchmark" / "generate_spatial_traces.py"
TRACE = ROOT / "tests" / "fixtures" / "spatial_trace_cli.jsonl"
sys.path.insert(0, str(ROOT / "benchmark"))
from generate_spatial_traces import (collision_trace, derive_operation_counts, engine_cases,
                                     trace_specs, visibility_trace)  # noqa: E402
from spatial_results import aggregate_spatial_processes  # noqa: E402
from sweep_spatial import (case_method_profiles, load_cases, parse_choices, parse_workers,
                           validation_key, _group_key, _engine_parameters_match, _render_eligibility_reasons,
                           _check_manifest, image_qualification_matches)  # noqa: E402
from plot_scaling import load_rows as load_plot_rows  # noqa: E402


class SpatialTraceGeneratorTest(unittest.TestCase):
    def test_image_qualification_requires_actual_matching_renderer_and_identity(self):
        signature = {"status": "available", "backend": "software", "output_size": [1600, 900], "sdl_version": "3.4.18"}
        manifest = {"input": {"case_sha256": "case", "renderer_signature": signature},
                    "build": {"source_sha256": "source"}, "validation": {"status": "passed"}}
        self.assertFalse(image_qualification_matches(manifest))
        proof = {"status": "passed", "order_status": "passed", "method": "same-frame-submit-all-rgba32",
                 "frames": 1, "tolerance": 0, "reference_hash": "123", "result_hash": "123",
                 "renderer_signature": dict(signature)}
        manifest["validation"].update(image=proof, image_case_sha256="case", image_source_sha256="source")
        self.assertTrue(image_qualification_matches(manifest))
        for field, replacement in (("backend", "opengl"), ("output_size", [128,96]), ("sdl_version", "other")):
            proof["renderer_signature"] = dict(signature, **{field: replacement})
            self.assertFalse(image_qualification_matches(manifest))
        proof["renderer_signature"] = signature
        proof["result_hash"] = "different"
        self.assertFalse(image_qualification_matches(manifest))

    def test_smoke_traces_are_valid_hashed_and_repeatable(self):
        with tempfile.TemporaryDirectory(prefix="platformator-spatial-gen-") as directory:
            first = Path(directory) / "first"
            second = Path(directory) / "second"
            for output in (first, second):
                subprocess.run(
                    [sys.executable, str(GENERATOR), "--preset", "smoke", "--seed", "17",
                     "--warmup-frames", "1", "--measure-frames", "2", "--output-dir", str(output)],
                    check=True,
                )
            one = json.loads((first / "cases.json").read_text())
            two = json.loads((second / "cases.json").read_text())
            self.assertEqual(one, two)
            self.assertEqual({case["case_id"] for case in one["cases"]}, {"smoke-collision", "smoke-visibility"})
            for case in one["cases"]:
                trace_path = first / case["trace"]
                raw = trace_path.read_bytes()
                self.assertEqual(hashlib.sha256(raw).hexdigest(), case["sha256"])
                records = [json.loads(line) for line in raw.decode("utf-8").splitlines()]
                self.assertEqual(records[0]["case_id"], case["case_id"])
                self.assertEqual(len(records), case["warmup_steps"] + case["measure_steps"] + 2)
                self.assertEqual(records[0]["parameters"]["population"], 1000)
                self.assertEqual(records[0]["generator"]["source_hash"], hashlib.sha256(GENERATOR.read_bytes()).hexdigest())
                self.assertTrue(any(record["ops"] for record in records[2:]))

    def test_generator_refuses_nonempty_output(self):
        with tempfile.TemporaryDirectory(prefix="platformator-spatial-gen-refuse-") as directory:
            output = Path(directory) / "output"
            output.mkdir()
            (output / "keep.txt").write_text("existing")
            result = subprocess.run(
                [sys.executable, str(GENERATOR), "--preset", "smoke", "--warmup-frames", "0",
                 "--measure-frames", "1", "--output-dir", str(output)],
                text=True,
                capture_output=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("nonempty", result.stderr.lower())
            self.assertEqual((output / "keep.txt").read_text(), "existing")

    def test_evaluation_has_exact_sparse_scale_and_percentage_matrix(self):
        specs = trace_specs("evaluation", 23)
        def is_percentage_case(name):
            suffix = name.rsplit("-", 1)[-1]
            return suffix.startswith("p") and suffix[1:].isdigit()
        sparse_collision = [case for case in specs if case[0] == "collision" and is_percentage_case(case[1])]
        sparse_visibility = [case for case in specs if case[0] == "visibility" and is_percentage_case(case[1])]
        expected = {(population, percent) for population in (100, 1000, 10000) for percent in (0, 1, 10, 100)}
        self.assertEqual({(case[2], case[4]) for case in sparse_collision}, expected)
        self.assertEqual({(case[2], case[3]) for case in sparse_visibility}, expected)
        self.assertEqual(len(sparse_collision), 12)
        self.assertEqual(len(sparse_visibility), 12)
        evaluation_ids = {case[1] for case in specs}
        self.assertIn("collision-dense-n128", evaluation_ids)
        self.assertIn("collision-n1000-churn", evaluation_ids)
        self.assertIn("collision-n1000-filter-changes", evaluation_ids)
        self.assertIn("visibility-n1000-change10-aspect", evaluation_ids)
        self.assertIn("visibility-n1000-change10-churn", evaluation_ids)
        calibration = trace_specs("calibration", 23)
        self.assertFalse({case[1] for case in calibration} & {case[1] for case in specs})
        self.assertTrue(all(case[-1] not in {23, *[item[-1] for item in specs]} for case in calibration))

    def test_typed_engine_case_matrix_hashes_and_domain_expansion(self):
        cases = engine_cases(7, 1, 2, 1 / 120)
        self.assertTrue(cases)
        for case in cases:
            canonical = dict(case)
            declared_hash = canonical.pop("sha256")
            self.assertEqual(hashlib.sha256(json.dumps(canonical, sort_keys=True, separators=(",", ":"),
                                                           ensure_ascii=True, allow_nan=False).encode("ascii")).hexdigest(),
                             declared_hash)
        physics = next(case for case in cases if case["scenario"] == "broad_phase")
        visibility = next(case for case in cases if case["scenario"] == "sprite_visibility")
        combined = next(case for case in cases if case["scenario"] == "spatial_combined")
        choices = (["segmented", "bvh"], ["none", "linear", "bvh", "segmented"],
                   ["native-partial"], [0.0], ["parallel"])
        self.assertEqual(len(case_method_profiles(physics, None, *choices)), 2)
        self.assertEqual(len(case_method_profiles(visibility, None, *choices)), 4)
        self.assertEqual(len(case_method_profiles(combined, None, *choices)), 8)

    def test_engine_render_contract_matches_fixture_domain(self):
        cases = engine_cases(3, 1, 2, 1 / 120)
        sprite = next(case for case in cases if case["scenario"] == "sprite_visibility")
        combined = next(case for case in cases if case["scenario"] == "spatial_combined")
        physics = next(case for case in cases if case["scenario"] == "broad_phase")
        from sweep_spatial import _validate_engine_case
        invalid = dict(sprite, render=False)
        canonical = dict(invalid)
        canonical.pop("sha256")
        invalid["sha256"] = hashlib.sha256(json.dumps(canonical, sort_keys=True, separators=(",", ":"),
                                                  ensure_ascii=True, allow_nan=False).encode("ascii")).hexdigest()
        with self.assertRaisesRegex(ValueError, "requires actual rendering"):
            _validate_engine_case(invalid)
        _validate_engine_case(sprite)
        _validate_engine_case(combined)
        invalid_physics = dict(physics, render=True)
        canonical = dict(invalid_physics)
        canonical.pop("sha256")
        invalid_physics["sha256"] = hashlib.sha256(json.dumps(canonical, sort_keys=True, separators=(",", ":"),
                                                               ensure_ascii=True, allow_nan=False).encode("ascii")).hexdigest()
        with self.assertRaisesRegex(ValueError, "must keep rendering disabled"):
            _validate_engine_case(invalid_physics)

    def test_validation_pairing_requires_identical_renderer_output_signature(self):
        base = {"input": {"case_sha256": "a" * 64, "domain": "engine"},
                "selection": {"requested": {"bvh_maintenance": "native-partial", "bvh_padding": 0,
                                                "bvh_query_execution": "parallel"},
                              "effective": {"broad_phase": "segmented", "render_culling": "bvh"}},
                "run": {"workers_effective": 1}, "build": {"source_sha256": "b" * 64},
                "renderer_signature": {"status": "available", "backend": "software",
                                       "output_size": [1600, 900], "viewport": [0, 0, 1600, 900]}}
        hardware = json.loads(json.dumps(base))
        hardware["renderer_signature"]["backend"] = "hardware"
        self.assertNotEqual(validation_key(base), validation_key(hardware))
        resized = json.loads(json.dumps(base))
        resized["renderer_signature"]["output_size"] = [1920, 1080]
        self.assertNotEqual(validation_key(base), validation_key(resized))

    def test_qualification_key_requires_same_build_run_and_resolved_fixture(self):
        base = {"input": {"case_sha256": "a" * 64, "domain": "engine", "case_id": "fixture",
                          "parameters": {"body_count": 100}},
                "selection": {"requested": {"bvh_maintenance": "native-partial", "bvh_padding": 0,
                                                "bvh_query_execution": "parallel"},
                              "effective": {"broad_phase": "segmented", "render_culling": "none"}},
                "run": {"workers_requested": 1, "workers_effective": 1, "seed": 0, "dt": 1 / 120,
                        "warmup_steps": 10, "requested_measured_steps": 30, "measured_steps": 30},
                "build": {"commit": "commit", "dirty": True, "source_sha256": "b" * 64,
                          "compiler": "GNU 16.2.1", "flags": "-O3", "configuration": "Release",
                          "sanitizer": "none", "work_probes": False, "debug_tools": False},
                "dependencies": {"SDL": "3.4.18"},
                "machine": {"architecture": "x86_64", "cpu": "cpu", "kernel": "kernel",
                            "logical_cpus": 8, "os": "Linux"}}
        variants = []
        for field, value in (("compiler", "Clang 20"), ("flags", "-O2")):
            changed = json.loads(json.dumps(base))
            changed["build"][field] = value
            variants.append(changed)
        for field, value in (("seed", 1), ("dt", 1 / 60), ("warmup_steps", 11),
                             ("requested_measured_steps", 31), ("measured_steps", 31)):
            changed = json.loads(json.dumps(base))
            changed["run"][field] = value
            variants.append(changed)
        changed_fixture = json.loads(json.dumps(base))
        changed_fixture["input"]["parameters"]["body_count"] = 200
        variants.append(changed_fixture)
        self.assertTrue(all(validation_key(base) != validation_key(variant) for variant in variants))
        first = {"case_id": "fixture", "domain": "engine", "case_sha256": "a" * 64,
                 "broad_phase": "segmented", "render_culling": "none", "workers": 1,
                 "bvh_maintenance": "native-partial", "bvh_padding": 0,
                 "bvh_query_execution": "parallel", "source_sha256": "b" * 64,
                 "renderer_signature": "", "qualification_key": json.dumps(list(validation_key(base)))}
        second = dict(first, qualification_key=json.dumps(list(validation_key(changed_fixture))))
        self.assertNotEqual(_group_key(first), _group_key(second))

    def test_typed_churn_is_checked_against_resolved_add_and_remove_counts(self):
        expected = {"steady_count": 1000, "churn_count": 100}
        actual = {"steady_count": 1000, "add_count": 100, "remove_count": 100,
                  "effective_churn": 100}
        self.assertEqual(_engine_parameters_match(expected, actual), (True, []))
        actual["remove_count"] = 99
        self.assertEqual(_engine_parameters_match(expected, actual), (False, ["churn_count"]))

    def test_one_fallback_frame_excludes_whole_run_without_filtering_samples(self):
        rows = [{"sample_kind": "render", "index": 0, "culling_bypass_count": "0",
                 "uncullable_sprite_count": "0"},
                {"sample_kind": "render", "index": 1, "culling_bypass_count": "1",
                 "uncullable_sprite_count": "0"},
                {"sample_kind": "render", "index": 2, "culling_bypass_count": "0",
                 "uncullable_sprite_count": "0"}]
        count_before = len(rows)
        reasons = _render_eligibility_reasons(rows)
        self.assertEqual(len(rows), count_before)
        self.assertEqual(len(reasons), 1)
        self.assertIn("entire run", reasons[0])
        self.assertEqual(_render_eligibility_reasons(rows[:1]), [])

    def test_capture_rejections_include_validation_and_build_instrumentation_reasons(self):
        trace_hash = hashlib.sha256(TRACE.read_bytes()).hexdigest()
        case = {"case_id": "fixture", "domain": "collision", "sha256": trace_hash, "seed": 0,
                "warmup_steps": 0, "measure_steps": 1, "dt": 1 / 120}
        manifest = {
            "input": {"domain": "collision-replay", "case_sha256": trace_hash, "case_id": "fixture",
                      "parameters": {},
                      "trace_path": str(TRACE.resolve())},
            "selection": {"requested": {"bvh_maintenance": "native-partial", "bvh_padding": 0.0,
                                          "bvh_query_execution": "parallel"},
                          "effective": {"broad_phase": "bvh", "render_culling": "none"}},
            "run": {"workers_requested": 1, "workers_effective": 1, "seed": 0,
                    "warmup_steps": 0, "requested_measured_steps": 1, "measured_steps": 1, "dt": 1 / 120},
            "capture_mode": "performance", "timing_eligible": True,
            "build": {"configuration": "Release", "sanitizer": "none",
                      "commit": "commit", "dirty": True, "source_sha256": "b" * 64,
                      "compiler": "GNU 16.2.1", "flags": "-O3 -DNDEBUG",
                      "resource_hooks": False, "work_probes": False, "debug_tools": False},
            "dependencies": {"SDL": "3.4.18"},
            "machine": {"architecture": "x86_64", "cpu": "cpu", "kernel": "kernel",
                        "logical_cpus": 8, "os": "Linux"},
            "validation": {"status": "not_run"},
        }
        capture = {"manifest": manifest, "raw_samples": []}
        common = (case, capture, TRACE, "bvh", "none", 1, 0, "performance",
                  ("native-partial", 0.0, "parallel"))
        self.assertEqual(_check_manifest(*common), [])

        manifest["build"]["work_probes"] = True
        self.assertIn("build is not an uninstrumented Release configuration", _check_manifest(*common))
        manifest["build"]["work_probes"] = False
        manifest["timing_eligible"] = False
        self.assertIn("capture is marked timing-ineligible", _check_manifest(*common))
        manifest["timing_eligible"] = True
        manifest["capture_mode"] = "validate"
        manifest["validation"]["status"] = "failed"
        validation = list(common)
        validation[7] = "validate"
        self.assertIn("independent validation did not pass", _check_manifest(*validation))

    def test_capture_rejects_resolved_engine_fixture_mismatch(self):
        case = next(item for item in engine_cases(0, 0, 1, 1 / 120) if item["scenario"] == "broad_phase")
        manifest = {"input": {"domain": "engine", "scenario": case["scenario"],
                              "case_sha256": case["sha256"], "case_id": case["case_id"],
                              "parameters": {"body_count": case["parameters"]["body_count"] + 1}},
                    "selection": {"requested": {"bvh_maintenance": "native-partial", "bvh_padding": 0.0,
                                                   "bvh_query_execution": "parallel"},
                                  "effective": {"broad_phase": "segmented", "render_culling": "none"}},
                    "run": {"workers_requested": 1, "workers_effective": 1, "seed": case["seed"],
                            "warmup_steps": 0, "requested_measured_steps": 1, "measured_steps": 1,
                            "dt": case["dt"]},
                    "capture_mode": "validate", "timing_eligible": False,
                    "build": {"commit": "commit", "dirty": False, "source_sha256": "b" * 64,
                              "compiler": "GNU", "flags": "-O3", "configuration": "Release",
                              "sanitizer": "none", "resource_hooks": False,
                              "work_probes": False, "debug_tools": False},
                    "dependencies": {},
                    "machine": {"architecture": "x86_64", "cpu": "cpu", "kernel": "kernel",
                                "logical_cpus": 1, "os": "Linux"},
                    "validation": {"status": "passed"}}
        capture = {"manifest": manifest, "raw_samples": []}
        reasons = _check_manifest(case, capture, None, "segmented", "none", 1, 0, "validate",
                                  ("native-partial", 0.0, "parallel"))
        self.assertIn("resolved engine fixture parameters mismatch: body_count", reasons)

    def test_engine_preset_loads_as_typed_manifest(self):
        with tempfile.TemporaryDirectory(prefix="platformator-spatial-engine-gen-") as directory:
            output = Path(directory) / "bundle"
            subprocess.run([sys.executable, str(GENERATOR), "--preset", "engine", "--seed", "9",
                            "--warmup-frames", "0", "--measure-frames", "1", "--output-dir", str(output)],
                           check=True, stdout=subprocess.DEVNULL)
            manifest, cases = load_cases(output / "cases.json")
            self.assertEqual(manifest["version"], 1)
            self.assertGreater(len(cases), 20)
            self.assertTrue(all(trace is None for _, trace in cases))

    def test_sweep_method_expansion_omits_irrelevant_choices(self):
        collision = {"case_id": "c", "domain": "collision"}
        visibility = {"case_id": "v", "domain": "visibility"}
        collision_runs = case_method_profiles(collision, None, ["segmented", "bvh"], ["none"],
                                               ["native-partial", "full-rebuild"], [0.0, 2.0],
                                               ["serial", "parallel"])
        self.assertEqual(len(collision_runs), 1 + 8)
        self.assertEqual(collision_runs[0], ("collision", "segmented", "none", ("native-partial", 0.0, "parallel")))
        self.assertEqual({item[1] for item in collision_runs}, {"segmented", "bvh"})
        visibility_runs = case_method_profiles(visibility, None, ["segmented"], ["linear", "bvh"],
                                               ["native-partial", "full-rebuild"], [0.0, 2.0],
                                               ["serial", "parallel"])
        self.assertEqual(len(visibility_runs), 1 + 8)
        self.assertTrue(all(item[2] == "bvh" for item in visibility_runs[1:]))

    def test_case_manifest_trace_integrity(self):
        with tempfile.TemporaryDirectory(prefix="platformator-spatial-cases-") as directory:
            bundle = Path(directory) / "cases"
            subprocess.run([sys.executable, str(GENERATOR), "--preset", "smoke", "--seed", "3",
                            "--warmup-frames", "0", "--measure-frames", "1", "--output-dir", str(bundle)],
                           check=True, stdout=subprocess.DEVNULL)
            manifest, cases = load_cases(bundle / "cases.json")
            self.assertEqual(manifest["version"], 1)
            self.assertEqual([case["case_id"] for case, _ in cases], ["smoke-collision", "smoke-visibility"])
            trace = cases[0][1]
            self.assertEqual(hashlib.sha256(trace.read_bytes()).hexdigest(), cases[0][0]["sha256"])
            malformed = json.loads((bundle / "cases.json").read_text())
            malformed["cases"][0]["warmup_steps"] = True
            (bundle / "cases.json").write_text(json.dumps(malformed), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "warmup"):
                load_cases(bundle / "cases.json")
            (bundle / "cases.json").write_text(json.dumps(manifest), encoding="utf-8")
            trace.write_text(trace.read_text() + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "hash"):
                load_cases(bundle / "cases.json")

    def test_operation_metadata_matches_generated_operations(self):
        generator = {"name": "test", "revision": "2", "source_hash": "a" * 64}
        collision_cases = {
            variant: collision_trace(f"collision-{variant}", 1000, 10.0, variant, 9, 0, 3, 1 / 120, generator)
            for variant in ("churn", "filter-changes", "coalesced")
        }
        for variant, records in collision_cases.items():
            parameters = records[0]["parameters"]
            operation_counts = derive_operation_counts("collision", records[1], records[2:])
            self.assertEqual(parameters["operation_counts"], operation_counts)
            self.assertEqual(parameters["requested_changed_endpoint_count"], 100)
            self.assertEqual(len(operation_counts["per_step"]), 3)
            if variant == "churn":
                self.assertEqual(operation_counts["per_step"][0]["churn_replacement_count"], 10)
                self.assertEqual(operation_counts["per_step"][0]["changed_endpoint_count"], 10)
            elif variant == "filter-changes":
                self.assertEqual(operation_counts["per_step"][0]["filter_write_count"], 1)
                self.assertEqual(operation_counts["per_step"][0]["filter_value_change_count"], 0)
                self.assertEqual(operation_counts["per_step"][1]["filter_value_change_count"], 1)
            else:
                self.assertEqual(operation_counts["per_step"][0]["geometry_write_count"], 200)
                self.assertEqual(operation_counts["per_step"][0]["coalesced_operation_count"], 100)

        visibility = visibility_trace("visibility-churn", 1000, 10.0, 10.0, "churn", 21, 0, 2,
                                      1 / 120, generator)
        visibility_counts = derive_operation_counts("visibility", visibility[1], visibility[2:])
        self.assertEqual(visibility[0]["parameters"]["operation_counts"], visibility_counts)
        self.assertEqual(visibility_counts["per_step"][0]["churn_replacement_count"], 1)
        self.assertEqual(visibility_counts["per_step"][0]["geometry_write_count"], 100)
        visibility[2]["ops"].append({"op": "set_view", "view": {"position": [12, 0]}})
        self.assertEqual(derive_operation_counts("visibility", visibility[1], visibility[2:]),
                         visibility_counts)
        self.assertTrue(any("including no-op writes" in item
                            for item in visibility[0]["parameters"]["operation_counts"]["semantics"].values()))

    def test_trace_case_manifest_requires_header_parameters_and_operation_counts(self):
        with tempfile.TemporaryDirectory(prefix="platformator-spatial-metadata-") as directory:
            output = Path(directory) / "bundle"
            subprocess.run([sys.executable, str(GENERATOR), "--preset", "collision", "--seed", "4",
                            "--warmup-frames", "0", "--measure-frames", "1", "--output-dir", str(output)],
                           check=True, stdout=subprocess.DEVNULL)
            manifest_path = output / "cases.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            load_cases(manifest_path)
            manifest["cases"][0]["parameters"]["operation_counts"]["per_step"][0]["changed_endpoint_count"] += 1
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "header parameters contradict"):
                load_cases(manifest_path)

    def test_worker_and_method_lists_reject_duplicates_or_unavailable_values(self):
        self.assertEqual(parse_choices("bvh, segmented", {"bvh", "segmented"}, "methods"), ["bvh", "segmented"])
        self.assertEqual(parse_workers("1,2", 2), [1, 2])
        with self.assertRaisesRegex(ValueError, "unique"):
            parse_choices("bvh,bvh", {"bvh"}, "methods")
        with self.assertRaisesRegex(ValueError, "Unsupported"):
            parse_workers("1,3", 2)

    def test_process_aggregation_reports_between_process_spread(self):
        result = aggregate_spatial_processes([
            {"scope.spatial_replay.avg": 2.0, "scope.spatial_replay.p95": 3.0},
            {"scope.spatial_replay.avg": 4.0, "scope.spatial_replay.p95": 7.0},
        ])
        self.assertEqual(result["process_count"], 2)
        self.assertEqual(result["scope.spatial_replay.avg.across_process_mean"], 3.0)
        self.assertEqual(result["scope.spatial_replay.p95.across_process_mean"], 5.0)
        self.assertAlmostEqual(result["scope.spatial_replay.avg.across_process_stdev"], 2 ** 0.5)

    def test_resource_mode_requires_timing_bundle(self):
        with tempfile.TemporaryDirectory(prefix="platformator-spatial-resource-") as directory:
            result = subprocess.run(
                [sys.executable, str(ROOT / "benchmark" / "sweep_spatial.py"), "--runner", "missing",
                 "--cases", "missing", "--capture-mode", "resource", "--output-dir", directory],
                text=True, capture_output=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("--timing-dir", result.stderr)

    def test_resource_summaries_exclude_timing_metrics(self):
        capture = {"manifest": {"capture_mode": "resource", "initialization": {"total_ns": 123}},
                   "summary": {("scope", "complete_step"): {"avg": 4.0}},
                   "raw_samples": []}
        from spatial_results import spatial_process_summary
        result = spatial_process_summary(capture)
        self.assertNotIn("scope.complete_step.avg", result)
        self.assertNotIn("initialization.total_ms", result)

    def test_sweep_refuses_nonempty_output_directory(self):
        with tempfile.TemporaryDirectory(prefix="platformator-spatial-sweep-refuse-") as directory:
            root = Path(directory)
            bundle = root / "cases"
            subprocess.run([sys.executable, str(GENERATOR), "--preset", "smoke", "--warmup-frames", "0",
                            "--measure-frames", "1", "--output-dir", str(bundle)], check=True,
                           stdout=subprocess.DEVNULL)
            output = root / "output"
            output.mkdir()
            (output / "keep.txt").write_text("existing", encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(ROOT / "benchmark" / "sweep_spatial.py"), "--runner", sys.executable,
                 "--cases", str(bundle / "cases.json"), "--worker-counts", "1", "--repetitions", "1",
                 "--capture-mode", "validate", "--output-dir", str(output)],
                text=True, capture_output=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("nonempty", result.stderr.lower())
            self.assertEqual((output / "keep.txt").read_text(encoding="utf-8"), "existing")

    def test_schema4_sweep_summary_plot_dispatch_and_filtering(self):
        with tempfile.TemporaryDirectory(prefix="platformator-spatial-plot-") as directory:
            output = Path(directory) / "summary.csv"
            columns = ["schema_version", "capture_mode", "timing_eligible", "case_id", "domain", "parameters",
                       "population", "case_sha256", "broad_phase", "render_culling", "workers",
                       "bvh_maintenance", "bvh_padding", "bvh_query_execution", "source_sha256",
                       "requested_process_count", "eligible_process_count",
                       "scope.spatial_replay.avg.across_process_mean", "scope.spatial_replay.p95.across_process_mean",
                       "scope.complete_step.avg.across_process_mean", "scope.complete_step.p95.across_process_mean"]
            rows = []
            for population in (100, 200):
                rows.append({"schema_version": 4, "capture_mode": "performance", "timing_eligible": True,
                             "case_id": f"collision-n{population}", "domain": "collision",
                             "parameters": json.dumps({"population": population, "changed_percent": 10,
                                                        "variant": "sparse"}),
                             "population": population, "case_sha256": "a" * 64, "broad_phase": "bvh",
                             "render_culling": "none", "workers": 1, "bvh_maintenance": "native-partial",
                             "bvh_padding": 0, "bvh_query_execution": "parallel", "source_sha256": "b" * 64,
                             "requested_process_count": 5, "eligible_process_count": 5,
                             "scope.spatial_replay.avg.across_process_mean": 1.5,
                             "scope.spatial_replay.p95.across_process_mean": 2.0,
                             "scope.complete_step.avg.across_process_mean": "",
                             "scope.complete_step.p95.across_process_mean": ""})
            with output.open("w", newline="", encoding="utf-8") as handle:
                writer = __import__("csv").DictWriter(handle, fieldnames=columns)
                writer.writeheader()
                writer.writerows(rows)
            plotted = load_plot_rows(output, domain="collision", broad_phase="bvh", render_culling="none",
                                     workers=1, changed_percent=10, variant="sparse")
            self.assertEqual([row["swept_value"] for row in plotted], [100.0, 200.0])
            self.assertEqual(plotted[0]["complete_step_avg_ms"], 1.5)


if __name__ == "__main__":
    unittest.main(verbosity=2)
