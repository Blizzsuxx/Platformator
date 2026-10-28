#!/usr/bin/env python3
"""Run reproducible spatial replay sweeps and retain every process capture."""

import argparse
import csv
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import random
from collections import Counter
import subprocess
import sys
import time

from spatial_results import (aggregate_spatial_processes, read_schema4_capture,
                             spatial_process_summary)
from generate_spatial_traces import derive_operation_counts


DEFAULT_PROFILE = ("native-partial", 0.0, "parallel")


def _canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True,
                      allow_nan=False)


def _sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def parse_choices(value, allowed, field):
    choices = [item.strip() for item in value.split(",") if item.strip()]
    if not choices or len(choices) != len(set(choices)):
        raise ValueError(f"{field} must be a nonempty comma-separated list of unique values")
    invalid = set(choices) - set(allowed)
    if invalid:
        raise ValueError(f"Unsupported {field}: {', '.join(sorted(invalid))}")
    return choices


def parse_workers(value, available):
    choices = parse_choices(value, {str(value) for value in range(1, available + 1)},
                            "worker counts")
    return [int(item) for item in choices]


def _validate_trace_case(case, manifest_path):
    required = {"case_id", "domain", "trace", "sha256", "parameters", "warmup_steps",
                "measure_steps", "dt", "seed", "bounds_source"}
    if set(case) != required:
        raise ValueError(f"Trace case has unknown/missing fields: {case.get('case_id', '<unknown>')}")
    if case["domain"] not in {"collision", "visibility"}:
        raise ValueError(f"Unsupported replay domain: {case['domain']}")
    if not isinstance(case["case_id"], str) or not case["case_id"]:
        raise ValueError("Case id must be a nonempty string")
    if (not isinstance(case["parameters"], dict) or isinstance(case["seed"], bool) or
            not isinstance(case["seed"], int) or case["seed"] < 0):
        raise ValueError(f"Invalid parameters/seed in case {case['case_id']}")
    if isinstance(case["warmup_steps"], bool) or not isinstance(case["warmup_steps"], int) or case["warmup_steps"] < 0:
        raise ValueError(f"Invalid warmup count in case {case['case_id']}")
    if isinstance(case["measure_steps"], bool) or not isinstance(case["measure_steps"], int) or case["measure_steps"] <= 0:
        raise ValueError(f"Invalid measure count in case {case['case_id']}")
    if isinstance(case["dt"], bool) or not isinstance(case["dt"], (int, float)) or not math.isfinite(case["dt"]) or case["dt"] <= 0:
        raise ValueError(f"Invalid dt in case {case['case_id']}")
    trace = (manifest_path.parent / case["trace"]).resolve()
    if not trace.is_file() or _sha256(trace) != case["sha256"]:
        raise ValueError(f"Missing or mismatched trace hash for case {case['case_id']}")
    with trace.open(encoding="utf-8") as handle:
        try:
            records = [json.loads(line) for line in handle if line.strip()]
        except json.JSONDecodeError as error:
            raise ValueError(f"Invalid trace record for case {case['case_id']}") from error
    if not records:
        raise ValueError(f"Invalid trace header for case {case['case_id']}")
    header = records[0]
    if (header.get("type") != "header" or header.get("case_id") != case["case_id"] or
            header.get("domain") != case["domain"] or header.get("seed") != case["seed"] or
            header.get("warmup_steps") != case["warmup_steps"] or
            header.get("measure_steps") != case["measure_steps"] or
            not math.isclose(header.get("dt", float("nan")), case["dt"], rel_tol=0, abs_tol=0)):
        raise ValueError(f"Trace header contradicts case manifest for {case['case_id']}")
    if _canonical_json(header.get("parameters")) != _canonical_json(case["parameters"]):
        raise ValueError(f"Trace header parameters contradict case manifest for {case['case_id']}")
    if len(records) < 2:
        raise ValueError(f"Trace is missing initial data for case {case['case_id']}")
    try:
        actual_counts = derive_operation_counts(case["domain"], records[1], records[2:])
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError(f"Cannot derive operation metadata for case {case['case_id']}: {error}") from error
    if case["parameters"].get("operation_counts") != actual_counts:
        raise ValueError(f"Operation metadata disagrees with trace operations for case {case['case_id']}")
    return trace


def _validate_engine_case(case):
    allowed = {"case_id", "domain", "scenario", "parameters", "render", "warmup_steps",
               "measure_steps", "dt", "seed", "sha256"}
    if not isinstance(case, dict) or set(case) != allowed or case.get("domain") != "engine":
        raise ValueError("Engine case entries must use the exact typed engine-case fields")
    if not isinstance(case.get("case_id"), str) or not case["case_id"]:
        raise ValueError("Engine case id must be a nonempty string")
    scenario_parameters = {
        "broad_phase": {"body_count"},
        "broad_phase_stress": {"body_count"},
        "narrow_phase": {"element_count"},
        "rigid_body_container": {"box_count", "circle_count"},
        "add_and_remove": {"steady_count", "churn_count"},
        "moving_add_and_remove": {"steady_count", "churn_count"},
        "persistent_contacts": {"contact_pair_count", "scripted_endpoints"},
        "sprite_visibility": {"sprite_count", "visible_percent", "changed_percent", "variant"},
        "spatial_combined": {"sprite_count", "visible_percent", "changed_percent", "variant"},
    }
    if case.get("scenario") not in scenario_parameters:
        raise ValueError(f"Unsupported engine scenario: {case.get('scenario')}")
    params = case.get("parameters")
    if not isinstance(params, dict) or set(params) != scenario_parameters[case["scenario"]] or not isinstance(case.get("render"), bool):
        raise ValueError(f"Engine case {case['case_id']} must contain typed parameters/render")
    if case["scenario"] in {"sprite_visibility", "spatial_combined"}:
        if params["variant"] not in {"static", "moving", "pan", "jump", "resize", "animation", "churn"}:
            raise ValueError(f"Invalid spatial variant for engine case {case['case_id']}")
        if case["render"] is not True:
            raise ValueError(f"Spatial engine case {case['case_id']} requires actual rendering")
        _engine_arguments(case)
    elif case["scenario"] == "persistent_contacts":
        if (isinstance(params["contact_pair_count"], bool) or not isinstance(params["contact_pair_count"], int) or
                params["contact_pair_count"] <= 0 or isinstance(params["scripted_endpoints"], bool) or
                params["scripted_endpoints"] not in (0, 1, 2)):
            raise ValueError(f"Invalid persistent_contacts parameters in {case['case_id']}")
    elif any(isinstance(value, bool) or not isinstance(value, int) or value <= 0 for value in params.values()):
        raise ValueError(f"Engine case {case['case_id']} parameters must be positive integers")
    elif case["render"] is not False:
        raise ValueError(f"Physics engine case {case['case_id']} must keep rendering disabled")
    if isinstance(case.get("warmup_steps"), bool) or not isinstance(case.get("warmup_steps"), int) or case["warmup_steps"] < 0:
        raise ValueError(f"Invalid engine warmup count in {case['case_id']}")
    if isinstance(case.get("measure_steps"), bool) or not isinstance(case.get("measure_steps"), int) or case["measure_steps"] <= 0:
        raise ValueError(f"Invalid engine measure count in {case['case_id']}")
    if isinstance(case.get("seed"), bool) or not isinstance(case.get("seed"), int) or case["seed"] < 0:
        raise ValueError(f"Invalid engine seed in {case['case_id']}")
    if isinstance(case.get("dt"), bool) or not isinstance(case.get("dt"), (int, float)) or not math.isfinite(case["dt"]) or case["dt"] <= 0:
        raise ValueError(f"Invalid engine dt in {case['case_id']}")
    canonical = dict(case)
    declared = canonical.pop("sha256", None)
    expected = hashlib.sha256(_canonical_json(canonical).encode("ascii")).hexdigest()
    if declared != expected:
        raise ValueError(f"Missing or mismatched canonical engine-case hash for {case['case_id']}")
    return None


def load_cases(path):
    manifest_path = Path(path).resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (not isinstance(manifest, dict) or manifest.get("format") != "platformator-spatial-cases" or
            manifest.get("version") != 1 or not isinstance(manifest.get("generator"), dict) or
            not isinstance(manifest.get("cases"), list) or not manifest["cases"]):
        raise ValueError("Expected a nonempty platformator-spatial-cases version 1 manifest")
    seen = set()
    loaded = []
    for case in manifest["cases"]:
        if not isinstance(case, dict) or case.get("case_id") in seen:
            raise ValueError("Case ids must be present and unique")
        seen.add(case["case_id"])
        trace = _validate_trace_case(case, manifest_path) if case.get("domain") in {"collision", "visibility"} else None
        if case.get("domain") == "engine":
            _validate_engine_case(case)
        loaded.append((case, trace))
    return manifest, loaded


def _read_capture(csv_path):
    capture = read_schema4_capture(csv_path)
    manifest = capture["manifest"]
    return capture, manifest


def image_qualification_matches(manifest):
    """Only a renderer-matched submit-all comparison qualifies actual rendered timings."""
    signature = manifest.get("renderer_signature", manifest.get("input", {}).get("renderer_signature"))
    if signature is None or signature.get("status") == "unavailable":
        return False
    validation = manifest.get("validation", {})
    proof = validation.get("image", {})
    return (signature.get("status") == "available" and proof.get("status") == "passed" and proof.get("order_status") == "passed" and
            proof.get("method") == "same-frame-submit-all-rgba32" and proof.get("frames", 0) > 0 and
            proof.get("tolerance") == 0 and proof.get("reference_hash") == proof.get("result_hash") and
            bool(proof.get("reference_hash")) and proof.get("renderer_signature") == signature and
            validation.get("image_source_sha256") == manifest.get("build", {}).get("source_sha256") and
            validation.get("image_case_sha256") == manifest.get("input", {}).get("case_sha256"))


def validation_index(validation_dir):
    root = Path(validation_dir).resolve()
    if not root.is_dir():
        raise FileNotFoundError(f"Validation bundle directory does not exist: {root}")
    candidates = list(root.glob("run-*.csv")) + list(root.glob("**/run-*.csv"))
    if (root / "validation.csv").is_file():
        candidates.append(root / "validation.csv")
    index = {}
    for path in sorted(set(candidates)):
        try:
            capture, manifest = _read_capture(path)
        except (OSError, ValueError, json.JSONDecodeError):
            continue
        if (manifest.get("capture_mode") != "validate" or manifest.get("validation", {}).get("status") != "passed" or
                manifest.get("timing_eligible") is not False):
            continue
        if not _qualification_fields_present(manifest):
            continue
        if manifest.get("input", {}).get("domain") == "engine" and manifest.get("sample_counts", {}).get("actual_render", 0) > 0 and not image_qualification_matches(manifest):
            continue
        key = validation_key(manifest)
        index.setdefault(key, []).append(str(path))
    return index


def performance_index(timing_dir):
    root = Path(timing_dir).resolve()
    if not root.is_dir():
        raise FileNotFoundError(f"Timing bundle directory does not exist: {root}")
    candidates = list(root.glob("run-*.csv")) + list(root.glob("**/run-*.csv"))
    index = {}
    for path in sorted(set(candidates)):
        try:
            capture, manifest = _read_capture(path)
        except (OSError, ValueError, json.JSONDecodeError):
            continue
        if manifest.get("capture_mode") != "performance" or manifest.get("timing_eligible") is not True:
            continue
        if not _qualification_fields_present(manifest):
            continue
        index.setdefault(validation_key(manifest), []).append(str(path))
    return index


def _qualification_fields_present(manifest):
    """Reject captures without enough recorded provenance to prove an equivalent run."""
    input_data = manifest.get("input", {})
    run = manifest.get("run", {})
    build = manifest.get("build", {})
    required_build = ("commit", "dirty", "source_sha256", "compiler", "flags", "configuration",
                      "sanitizer", "work_probes", "debug_tools")
    required_run = ("workers_requested", "workers_effective", "seed", "dt", "warmup_steps",
                    "requested_measured_steps", "measured_steps")
    # The replay writer records worker-visible hardware concurrency; the full engine writer also
    # includes OS/CPU identity. Compare every recorded machine field without rejecting the replay
    # schema's intentionally smaller machine record.
    required_machine = ("logical_cpus",)
    return (isinstance(input_data.get("parameters"), dict) and bool(input_data.get("case_id")) and
            all(field in build and build[field] is not None for field in required_build) and
            all(field in run and run[field] is not None for field in required_run) and
            isinstance(manifest.get("dependencies"), dict) and
            all(field in manifest.get("machine", {}) for field in required_machine))


def validation_key(manifest):
    """Identity shared by validation, timing, and resources when they ran equivalent work.

    Capture mode is intentionally excluded: validation and timing are separate invocations.
    The resource-only allocator hooks are also excluded from the build key, but each capture
    mode enforces its expected hook setting in `_check_manifest`.
    """
    selection = manifest.get("selection", {})
    requested = selection.get("requested", {})
    effective = selection.get("effective", {})
    run = manifest.get("run", {})
    build = manifest.get("build", {})
    input_data = manifest.get("input", {})
    signature = manifest.get("renderer_signature", manifest.get("input", {}).get("renderer_signature"))
    build_identity = {field: build.get(field) for field in
                      ("commit", "dirty", "source_sha256", "compiler", "flags", "configuration",
                       "sanitizer", "work_probes", "debug_tools")}
    run_identity = {field: run.get(field) for field in
                    ("workers_requested", "workers_effective", "seed", "dt", "warmup_steps",
                     "requested_measured_steps", "measured_steps")}
    return (input_data.get("case_sha256"),
            input_data.get("domain"), input_data.get("case_id"), input_data.get("scenario"),
            _canonical_json(input_data.get("parameters", {})),
            effective.get("broad_phase"), effective.get("render_culling"),
            requested.get("bvh_maintenance"), requested.get("bvh_padding"),
            requested.get("bvh_query_execution"),
            _canonical_json(run_identity), _canonical_json(build_identity),
            _canonical_json(manifest.get("dependencies", {})),
            _canonical_json(manifest.get("machine", {})),
            _canonical_json(signature) if signature is not None else None)


def _same_value(expected, actual):
    if isinstance(expected, bool) or isinstance(actual, bool):
        return type(expected) is type(actual) and expected == actual
    if isinstance(expected, (int, float)) and isinstance(actual, (int, float)):
        return math.isfinite(float(expected)) and math.isfinite(float(actual)) and math.isclose(
            float(expected), float(actual), rel_tol=1e-12, abs_tol=1e-12)
    if isinstance(expected, dict):
        return isinstance(actual, dict) and all(key in actual and _same_value(value, actual[key])
                                                for key, value in expected.items())
    if isinstance(expected, list):
        return isinstance(actual, list) and len(expected) == len(actual) and all(
            _same_value(left, right) for left, right in zip(expected, actual))
    return expected == actual


def _engine_parameters_match(expected, actual):
    if not isinstance(actual, dict):
        return False, list(expected)
    mismatched = []
    for field, value in expected.items():
        if field == "churn_count":
            # The runtime records resolved add/remove populations separately; all must match
            # the typed per-step churn requested by the case.
            aliases = ("add_count", "remove_count", "effective_churn")
            if any(alias not in actual or not _same_value(value, actual[alias]) for alias in aliases):
                mismatched.append(field)
        elif field not in actual or not _same_value(value, actual[field]):
            mismatched.append(field)
    return not mismatched, mismatched


def _group_key(record):
    return (record["case_id"], record["domain"], record["case_sha256"], record["broad_phase"],
            record["render_culling"], record["workers"], record["bvh_maintenance"], record["bvh_padding"],
            record["bvh_query_execution"], record.get("source_sha256", ""),
            record.get("renderer_signature", ""), record.get("qualification_key", ""))


def case_method_profiles(case, trace, broad_phases, render_cullings, maintenances, paddings, executions):
    profiles = [DEFAULT_PROFILE]
    if "bvh" in broad_phases or "bvh" in render_cullings:
        profiles = list(itertools.product(maintenances, paddings, executions))
    else:
        profiles = [DEFAULT_PROFILE]
    if case["domain"] == "collision":
        return [("collision", broad, "none", profile) for broad in broad_phases for profile in
                (profiles if broad == "bvh" else [DEFAULT_PROFILE])]
    if case["domain"] == "visibility":
        return [("visibility", "segmented", culling, profile) for culling in render_cullings for profile in
                (profiles if culling == "bvh" else [DEFAULT_PROFILE])]
    scenario = case["scenario"]
    if scenario in {"broad_phase", "broad_phase_stress", "narrow_phase", "rigid_body_container",
                    "add_and_remove", "moving_add_and_remove", "persistent_contacts"}:
        return [("engine", broad, "none", profile) for broad in broad_phases for profile in
                (profiles if broad == "bvh" else [DEFAULT_PROFILE])]
    if scenario == "sprite_visibility":
        return [("engine", "segmented", culling, profile) for culling in render_cullings for profile in
                (profiles if culling == "bvh" else [DEFAULT_PROFILE])]
    return [("engine", broad, culling, profile) for broad in broad_phases for culling in render_cullings
            for profile in (profiles if broad == "bvh" or culling == "bvh" else [DEFAULT_PROFILE])]


def _engine_arguments(case):
    params = case["parameters"]
    scenario = case["scenario"]
    command = ["--scenario", scenario]
    if scenario in {"sprite_visibility", "spatial_combined"}:
        for field in ("sprite_count",):
            if isinstance(params[field], bool) or not isinstance(params[field], int) or params[field] <= 0:
                raise ValueError(f"Invalid {field} for {case['case_id']}")
        for field in ("visible_percent", "changed_percent"):
            value = params[field]
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not 0 <= value <= 100:
                raise ValueError(f"Invalid {field} for {case['case_id']}")
        return command + ["--sprite-count", str(params["sprite_count"]),
                          "--visible-percent", str(params["visible_percent"]),
                          "--changed-percent", str(params["changed_percent"]),
                          "--spatial-variant", str(params["variant"])]
    if scenario in {"broad_phase", "broad_phase_stress"}:
        return command + ["--broad-count", str(params["body_count"])]
    if scenario == "narrow_phase":
        return command + ["--narrow-count", str(params["element_count"])]
    if scenario == "rigid_body_container":
        return command + ["--box-count", str(params["box_count"]), "--circle-count", str(params["circle_count"])]
    if scenario in {"add_and_remove", "moving_add_and_remove"}:
        return command + ["--steady-count", str(params["steady_count"]), "--churn-count", str(params["churn_count"])]
    return command + ["--contact-pair-count", str(params["contact_pair_count"]),
                      "--scripted-endpoints", str(params["scripted_endpoints"])]


def _profile_key(method, profile):
    maintenance, padding, execution = profile
    if method != "bvh":
        return DEFAULT_PROFILE
    return maintenance, float(padding), execution


def _render_eligibility_reasons(render_rows):
    """Classify a complete rendered run without filtering any of its samples."""
    if not render_rows:
        return ["no actual rendered frames; headless output cannot support a pure culling comparison"]
    if any(float(row.get("culling_bypass_count") or 0) > 0 or
           float(row.get("uncullable_sprite_count") or 0) > 0 for row in render_rows):
        return ["mixed/fallback rendering excludes the entire run from pure indexed comparisons"]
    return []


def _build_command(runner, case, trace, broad, culling, workers, repetition, repetitions,
                   capture_mode, profile, output_csv):
    maintenance, padding, execution = profile
    common = [str(runner), "--broad-phase", broad, "--render-culling", culling,
              "--bvh-maintenance", maintenance, "--bvh-padding", str(padding),
              "--bvh-query-execution", execution, "--workers", str(workers),
              "--seed", str(case["seed"]), "--warmup-frames", str(case["warmup_steps"]),
              "--measure-frames", str(case["measure_steps"]), "--dt", str(case["dt"]),
              "--repetitions", str(repetitions), "--run-index", str(repetition),
              "--capture-mode", capture_mode]
    if trace is not None:
        common += ["--spatial-trace", os.path.relpath(trace, Path.cwd())]
    else:
        common += _engine_arguments(case)
        common += ["--case-id", case["case_id"], "--case-sha256", case["sha256"]]
        if case["render"]:
            common.append("--render")
    return common + ["--csv-output", str(output_csv)]


def _check_manifest(case, capture, trace, broad, culling, workers, repetition, capture_mode, profile):
    manifest = capture["manifest"]
    problems = []
    input_data = manifest.get("input", {})
    expected_domain = {"collision": "collision-replay", "visibility": "visibility-replay"}.get(case["domain"], "engine")
    expected_hash = case["sha256"]
    if input_data.get("domain") != expected_domain or input_data.get("case_sha256") != expected_hash:
        problems.append("case hash or domain mismatch")
    if input_data.get("case_id") != case.get("case_id"):
        problems.append("recorded case id mismatch")
    if trace is not None and Path(input_data.get("trace_path", "")).resolve() != trace.resolve():
        problems.append("runner recorded a different trace path")
    recorded_parameters = input_data.get("parameters")
    expected_parameters = case.get("parameters", {})
    if case.get("domain") in {"collision", "visibility"}:
        if not _same_value(expected_parameters, recorded_parameters):
            problems.append("recorded trace fixture parameters differ from the case manifest")
    else:
        if input_data.get("scenario") != case.get("scenario"):
            problems.append("recorded engine scenario mismatch")
        parameters_match, mismatched = _engine_parameters_match(expected_parameters, recorded_parameters)
        if not parameters_match:
            problems.append("resolved engine fixture parameters mismatch: " + ", ".join(mismatched or ["parameters"]))
    selection = manifest.get("selection", {})
    effective = selection.get("effective", {})
    requested = selection.get("requested", {})
    if effective.get("broad_phase") != broad or effective.get("render_culling") != culling:
        problems.append("effective method selection mismatch")
    if (requested.get("bvh_maintenance"), requested.get("bvh_padding"), requested.get("bvh_query_execution")) != profile:
        problems.append("BVH profile mismatch")
    run = manifest.get("run", {})
    if run.get("workers_requested") != workers or run.get("workers_effective") != workers:
        problems.append("worker allowance mismatch")
    if run.get("seed") != case["seed"] or run.get("warmup_steps") != case["warmup_steps"] or run.get("requested_measured_steps") != case["measure_steps"]:
        problems.append("trace run parameters mismatch")
    if run.get("measured_steps") != case["measure_steps"]:
        problems.append("recorded measurement boundary mismatch")
    if not math.isclose(run.get("dt", float("nan")), case["dt"], rel_tol=1e-12):
        problems.append("dt mismatch")
    if manifest.get("capture_mode") != capture_mode:
        problems.append("capture mode mismatch")
    if repetition < 0:
        problems.append("invalid repetition")
    if capture_mode == "validate" and manifest.get("validation", {}).get("status") != "passed":
        problems.append("independent validation did not pass")
    if capture_mode == "performance":
        build = manifest.get("build", {})
        if manifest.get("timing_eligible") is not True:
            problems.append("capture is marked timing-ineligible")
        if build.get("configuration") != "Release" or build.get("sanitizer") != "none" or build.get("work_probes") or build.get("debug_tools"):
            problems.append("build is not an uninstrumented Release configuration")
        if build.get("resource_hooks") is not False:
            problems.append("timing capture has unexpected resource-only hooks")
    if capture_mode == "resource" and manifest.get("timing_eligible") is not False:
        problems.append("resource capture is not marked timing-ineligible")
    if capture_mode == "resource":
        build = manifest.get("build", {})
        if build.get("configuration") != "Release" or build.get("sanitizer") != "none" or build.get("work_probes") or build.get("debug_tools"):
            problems.append("resource capture is not from an uninstrumented Release configuration")
        if manifest.get("resources", {}).get("status") != "complete":
            problems.append("resource capture has no complete allocation/capacity record")
        if build.get("resource_hooks") is not True:
            problems.append("resource runner did not link allocation hooks")
    if capture_mode == "validate" and manifest.get("build", {}).get("resource_hooks") is not False:
        problems.append("validation capture has unexpected resource-only hooks")
    if not _qualification_fields_present(manifest):
        problems.append("capture is missing required build, run, dependency, or machine provenance")
    if case.get("domain") == "engine" and case.get("render", False):
        signature = manifest.get("renderer_signature", input_data.get("renderer_signature"))
        if not isinstance(signature, dict) or signature.get("status") != "available":
            problems.append("actual-render capture is missing a valid renderer/output-view signature")
        if capture_mode == "validate" and not image_qualification_matches(manifest):
            problems.append("actual-render validation lacks matching submit-all image/order proof")
        render_rows = [row for row in capture["raw_samples"] if row["sample_kind"] == "render"]
        problems.extend(_render_eligibility_reasons(render_rows))
    return problems


def _write_csv(path, rows):
    if not rows:
        path.write_text("\n", encoding="utf-8")
        return
    columns = list(dict.fromkeys(key for row in rows for key in row))
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns, extrasaction="raise")
        writer.writeheader()
        writer.writerows(rows)


def _write_report(path, records, summary_rows):
    complete = sum(record["capture_status"] == "complete" for record in records)
    eligible = sum(record["eligible"] for record in records)
    lines = ["# Spatial sweep report", "", f"Process captures: {len(records)} ({complete} complete).",
             f"Eligible process captures: {eligible}.", "",
             "Each timing summary aggregates independent processes; frame samples are not pooled. `avg.across_process_mean/stdev` describe process means, while `median`, `p95`, `p99`, and `max` are means of per-process tail statistics. Validation and resource captures are not timing results.", "",
             "## Configurations", "", "| Case | Domain | Broad phase | Render culling | Workers | Profile | Processes | Eligible |", "|---|---|---|---|---:|---|---:|---:|"]
    for row in summary_rows:
        profile = f"{row['bvh_maintenance']}/{row['bvh_padding']}/{row['bvh_query_execution']}"
        lines.append(f"| {row['case_id']} | {row['domain']} | {row['broad_phase']} | {row['render_culling']} | {row['workers']} | {profile} | {row['process_count']} | {row['eligible_process_count']} |")
    mixed = [record for record in records if record.get("classification") == "mixed_fallback"]
    lines += ["", "## Mixed/fallback captures reported separately", ""]
    if not mixed:
        lines.append("None.")
    else:
        lines.append(f"{len(mixed)} complete process capture(s) were retained in `mixed-fallback.csv`; each whole run is excluded from pure indexed aggregates.")
        for record in mixed:
            lines.append(f"- `{record['capture']}`: {'; '.join(record['reasons'])}")
    lines += ["", "## Rejected captures", ""]
    rejected = [record for record in records if not record["eligible"]]
    if not rejected:
        lines.append("None.")
    else:
        for record in rejected:
            lines.append(f"- `{record['capture']}`: {'; '.join(record['reasons']) or 'capture not eligible for this mode'}")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def run_sweep(args):
    if args.capture_mode == "resource" and not args.timing_dir:
        raise ValueError("Resource sweeps require --timing-dir with matching performance captures")
    manifest, cases = load_cases(args.cases)
    broad_phases = parse_choices(args.broad_phases, {"segmented", "bvh"}, "broad phases")
    render_cullings = parse_choices(args.render_cullings, {"none", "linear", "bvh", "segmented"}, "render cullings")
    maintenances = parse_choices(args.bvh_maintenances, {"native-partial", "full-rebuild"}, "BVH maintenances")
    executions = parse_choices(args.bvh_query_executions, {"serial", "parallel"}, "BVH query executions")
    paddings = []
    for value in parse_choices(args.bvh_paddings, set(args.bvh_paddings.split(",")), "BVH paddings"):
        padding = float(value)
        if not math.isfinite(padding) or padding < 0:
            raise ValueError("BVH paddings must be finite and nonnegative")
        if padding in paddings:
            raise ValueError("BVH paddings must be unique numeric values")
        paddings.append(padding)
    workers = parse_workers(args.worker_counts, args.available_workers)
    if args.repetitions <= 0 or (args.capture_mode == "performance" and args.repetitions < 5):
        raise ValueError("Performance sweeps require at least five independent process repetitions")
    if args.capture_mode not in {"validate", "performance", "resource"}:
        raise ValueError("Unknown capture mode")
    if args.capture_mode == "performance" and not args.validation_dir:
        raise ValueError("Performance sweeps require --validation-dir with matching passed validation captures")
    runner = Path(args.runner).resolve()
    if not runner.is_file():
        raise FileNotFoundError(runner)
    output = Path(args.output_dir).resolve()
    if output.exists() and any(output.iterdir()):
        raise FileExistsError(f"Refusing to write into nonempty output directory: {output}")
    output.mkdir(parents=True, exist_ok=True)
    paired = validation_index(args.validation_dir) if args.validation_dir else {}
    timed = performance_index(args.timing_dir) if args.timing_dir else {}
    profiles = list(itertools.product(maintenances, paddings, executions))
    jobs = []
    for case, trace in cases:
        for domain, broad, culling, profile in case_method_profiles(case, trace, broad_phases, render_cullings,
                                                                    maintenances, paddings, executions):
            for worker in workers:
                for repetition in range(args.repetitions):
                    jobs.append((case, trace, domain, broad, culling, profile, worker, repetition))
    random.Random(args.sweep_seed).shuffle(jobs)
    records = []
    for case, trace, domain, broad, culling, profile, worker, repetition in jobs:
        profile = _profile_key("bvh" if broad == "bvh" or culling == "bvh" else "default", profile)
        case_dir = output / case["case_id"] / f"{domain}-{broad}-{culling}-w{worker}-{profile[0]}-p{profile[1]:g}-{profile[2]}"
        case_dir.mkdir(parents=True, exist_ok=True)
        csv_path = case_dir / f"run-{repetition:02d}.csv"
        log_path = csv_path.with_suffix(".log")
        command = _build_command(runner, case, trace, broad, culling, worker, repetition, args.repetitions,
                                 args.capture_mode, profile, csv_path)
        print(f"[Spatial sweep] {case['case_id']} {broad}/{culling} workers={worker} run={repetition + 1}/{args.repetitions}", flush=True)
        started = time.time()
        with log_path.open("w", encoding="utf-8") as log:
            process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
        record = {"case_id": case["case_id"], "domain": domain, "case_sha256": case["sha256"],
                  "case_parameters": case.get("parameters", {}), "scenario": case.get("scenario", "replay"),
                  "broad_phase": broad, "render_culling": culling, "workers": worker,
                  "bvh_maintenance": profile[0], "bvh_padding": profile[1],
                  "bvh_query_execution": profile[2], "repetition": repetition,
                  "command": command, "exit_code": process.returncode,
                  "capture": str(csv_path.relative_to(output)), "log": str(log_path.relative_to(output)),
                  "elapsed_wall_seconds": time.time() - started, "capture_status": "failed",
                  "eligible": False, "reasons": []}
        if process.returncode != 0:
            record["reasons"].append(f"runner exited with status {process.returncode}")
        elif not csv_path.exists():
            record["reasons"].append("runner exited without writing a CSV capture")
        else:
            try:
                capture, run_manifest = _read_capture(csv_path)
                record["capture_status"] = "complete"
                record["source_sha256"] = run_manifest["build"].get("source_sha256")
                signature = run_manifest.get("renderer_signature", run_manifest.get("input", {}).get("renderer_signature"))
                record["renderer_signature"] = _canonical_json(signature) if signature is not None else ""
                record["qualification_key"] = _canonical_json(list(validation_key(run_manifest)))
                record["build_signature"] = _canonical_json({field: run_manifest.get("build", {}).get(field)
                    for field in ("compiler", "flags", "configuration", "sanitizer", "work_probes", "debug_tools")})
                record["run_signature"] = _canonical_json({field: run_manifest.get("run", {}).get(field)
                    for field in ("seed", "dt", "warmup_steps", "requested_measured_steps", "measured_steps",
                                  "workers_requested", "workers_effective")})
                record["fixture_signature"] = _canonical_json(run_manifest.get("input", {}).get("parameters", {}))
                record["manifest"] = str(csv_path.with_suffix(".manifest.json").relative_to(output))
                record["reasons"].extend(_check_manifest(case, capture, trace, broad, culling, worker,
                                                         repetition, args.capture_mode, profile))
                if args.capture_mode == "performance":
                    match_key = validation_key(run_manifest)
                    matches = paired.get(match_key, [])
                    if not matches:
                        record["reasons"].append("no matching passed validation capture (case/build/workload/selection/renderer)")
                    else:
                        record["validation_capture"] = matches[repetition % len(matches)]
                elif args.capture_mode == "resource":
                    match_key = validation_key(run_manifest)
                    matches = timed.get(match_key, [])
                    if not matches:
                        record["reasons"].append("no matching timing capture (case/build/workload/selection/renderer)")
                    else:
                        record["timing_capture"] = matches[repetition % len(matches)]
                record["eligible"] = not record["reasons"]
                record["classification"] = "mixed_fallback" if any("mixed/fallback" in reason for reason in record["reasons"]) else (
                    "eligible" if record["eligible"] else "rejected")
                record["process_summary"] = spatial_process_summary(capture)
            except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
                record["capture_status"] = "invalid"
                record["reasons"].append(f"capture validation failed: {error}")
        records.append(record)

    (output / "sweep.json").write_text(json.dumps({"format": "platformator-spatial-sweep", "version": 1,
        "cases_manifest": str(Path(args.cases).resolve()), "cases_manifest_sha256": _sha256(args.cases),
        "generator": manifest["generator"], "capture_mode": args.capture_mode,
        "sweep_seed": args.sweep_seed, "available_workers": args.available_workers,
        "requested_workers": workers, "broad_phases": broad_phases, "render_cullings": render_cullings,
        "bvh_profiles": [{"maintenance": item[0], "padding": item[1], "query_execution": item[2]} for item in profiles],
        "requested_repetitions": args.repetitions, "records": records}, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    (output / "eligibility.json").write_text(json.dumps(records, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    reason_counts = Counter(reason for record in records for reason in record["reasons"])
    (output / "reason_counts.json").write_text(json.dumps({"reasons": dict(sorted(reason_counts.items())),
        "classifications": dict(sorted(Counter(record.get("classification", "rejected") for record in records).items()))},
        sort_keys=True, indent=2) + "\n", encoding="utf-8")

    groups = {}
    for record in records:
        key = _group_key(record)
        groups.setdefault(key, []).append(record)
    summary_rows = []
    for key, group in groups.items():
        case_id, domain, case_hash, broad, culling, worker, maintenance, padding, execution, source_hash, renderer_signature, qualification_key = key
        valid = [record["process_summary"] for record in group if record.get("eligible") and "process_summary" in record]
        aggregate = aggregate_spatial_processes(valid) if valid else {}
        sample = group[0]
        parameters = sample["case_parameters"]
        summary_rows.append({"schema_version": 4, "capture_mode": args.capture_mode,
                             "timing_eligible": args.capture_mode == "performance",
                             "case_id": case_id, "domain": domain, "scenario": sample["scenario"],
                             "parameters": _canonical_json(parameters),
                             "population": parameters.get("population", parameters.get("sprite_count", "")),
                             "changed_percent": parameters.get("changed_percent", ""),
                             "visible_percent": parameters.get("visible_percent", ""),
                             "case_sha256": case_hash,
                             "broad_phase": broad, "render_culling": culling, "workers": worker,
                             "bvh_maintenance": maintenance, "bvh_padding": padding,
                             "bvh_query_execution": execution, "source_sha256": source_hash,
                             "renderer_signature": renderer_signature,
                             "qualification_key": qualification_key,
                             "build_signature": sample.get("build_signature", ""),
                             "run_signature": sample.get("run_signature", ""),
                             "fixture_signature": sample.get("fixture_signature", ""),
                             "requested_process_count": args.repetitions,
                             "process_count": len(group), "eligible_process_count": len(valid),
                             "paired_validation_count": sum(bool(record.get("validation_capture")) for record in group), **aggregate})
    _write_csv(output / "summary.csv", summary_rows)
    mixed_groups = {}
    for record in records:
        if record.get("classification") != "mixed_fallback" or "process_summary" not in record:
            continue
        key = _group_key(record)
        mixed_groups.setdefault(key, []).append(record)
    mixed_rows = []
    for key, group in mixed_groups.items():
        case_id, domain, case_hash, broad, culling, worker, maintenance, padding, execution, source_hash, renderer_signature, qualification_key = key
        aggregate = aggregate_spatial_processes([record["process_summary"] for record in group])
        mixed_rows.append({"classification": "mixed_fallback", "case_id": case_id, "domain": domain,
                           "case_sha256": case_hash, "broad_phase": broad, "render_culling": culling,
                           "workers": worker, "bvh_maintenance": maintenance, "bvh_padding": padding,
                           "bvh_query_execution": execution, "source_sha256": source_hash,
                           "renderer_signature": renderer_signature,
                           "qualification_key": qualification_key,
                           "process_count": len(group), **aggregate})
    if mixed_rows:
        _write_csv(output / "mixed-fallback.csv", mixed_rows)
    _write_report(output / "report.md", records, summary_rows)
    checksum_lines = []
    for path in sorted(item for item in output.rglob("*") if item.is_file() and item.name != "checksums.sha256"):
        checksum_lines.append(f"{_sha256(path)}  {path.relative_to(output)}")
    (output / "checksums.sha256").write_text("\n".join(checksum_lines) + "\n", encoding="utf-8")
    eligible_count = sum(item["eligible"] for item in records)
    print(f"[Spatial sweep] retained {len(records)} processes in {output}; eligible={eligible_count}")
    return 0 if all(record["eligible"] for record in records) else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True)
    parser.add_argument("--cases", required=True)
    parser.add_argument("--broad-phases", default="segmented,bvh")
    parser.add_argument("--render-cullings", default="none,linear,bvh,segmented")
    parser.add_argument("--worker-counts", default="1")
    parser.add_argument("--available-workers", type=int, default=max(1, os.cpu_count() or 1))
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--capture-mode", choices=("performance", "validate", "resource"), default="performance")
    parser.add_argument("--validation-dir")
    parser.add_argument("--timing-dir")
    parser.add_argument("--bvh-maintenances", default="native-partial")
    parser.add_argument("--bvh-paddings", default="0")
    parser.add_argument("--bvh-query-executions", default="parallel")
    parser.add_argument("--sweep-seed", type=int, default=0)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args(argv)
    if args.available_workers <= 0 or args.repetitions <= 0 or args.sweep_seed < 0:
        parser.error("available workers/repetitions must be positive and sweep seed nonnegative")
    try:
        return run_sweep(args)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
