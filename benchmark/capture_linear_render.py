#!/usr/bin/env python3
"""Capture the bounded linear-render comparison using schema-v3 artifacts."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import random
import statistics
import subprocess
import sys
import time

from sweep_scaling import parse_benchmark_csv


MODES = ("pre-feature", "disabled", "linear")
PROFILES = ("visible", "offscreen", "sparse", "clustered", "crossing", "camera-pan", "churn", "rotated")
EXPECTED_HARNESS_LENGTH = 64


class CaptureError(RuntimeError):
    pass


class ImageMismatch(CaptureError):
    def __init__(self, frame: int, recovery_error: str | None = None):
        message = f"Image mismatch at frame {frame}"
        if recovery_error:
            message += f"; mismatch image recovery failed: {recovery_error}"
        super().__init__(message)
        self.frame = frame


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def case_key(case: dict) -> str:
    key = f"{case['profile']}-n{case['sprite_count']}-w{case['workers']}"
    return key + ("-headless" if case.get("headless") else "")


def matrix(multiworker_count: int, singleworker_count: int = 1) -> list[dict]:
    cases = []
    def add(profile, count, workers=1, headless=False):
        cases.append({"profile": profile, "sprite_count": count, "workers": workers, "headless": headless})
    for count in (1, 1000): add("visible", count, singleworker_count)
    for count in (100, 1000, 10000): add("sparse", count, singleworker_count)
    for profile in ("offscreen", "clustered", "camera-pan", "churn", "rotated"):
        add(profile, 1000, singleworker_count)
    for count in (1000, 10000): add("crossing", count, singleworker_count)
    for profile, count in (("visible", 1000), ("sparse", 10000), ("crossing", 10000)):
        add(profile, count, multiworker_count)
    for workers in sorted(set((singleworker_count, multiworker_count))):
        add("crossing", 1000, workers, True)
    return cases


def validate_repetitions(repetitions: int) -> None:
    if repetitions < 5:
        raise CaptureError("Acceptance timing requires at least five independent process repetitions")


def expected_trajectory(case: dict, args) -> dict:
    rotated = case["profile"] == "rotated"
    churn = case["profile"] == "churn"
    return {"profile": case["profile"], "sprite_count": case["sprite_count"], "seed": args.seed,
            "trajectory_period_steps": 240 if case["profile"] in ("crossing", "camera-pan") else 0,
            "camera_width": 640.0 if rotated else 320.0,
            "camera_height": 160.0 if rotated else 240.0,
            "sprite_width": 64 if rotated else 16, "sprite_height": 12 if rotated else 16,
            "layout": "clustered-groups" if case["profile"] == "clustered" else
                      "uniform-sparse" if case["profile"] == "sparse" else "profile-defined",
            "rotation_distribution": "seeded-uniform-pi" if rotated else "zero",
            "churn_replacements_per_step": max(1, case["sprite_count"] // 100) if churn else 0,
            "churn_minimum_replacements": 1 if churn else 0}


def _check_common_identity(proof: dict, case: dict, args, mode: str, kind: str) -> None:
    expected = {
        "profile": case["profile"],
        "sprite_count": case["sprite_count"],
        "workers": case["workers"],
        "seed": args.seed,
        "dt": args.dt,
        "warmup_frames": args.warmup_frames,
        "measure_frames": args.measure_frames,
        "mode": mode,
    }
    for field, value in expected.items():
        if proof.get(field) != value:
            raise CaptureError(f"{kind} proof has wrong {field}: expected {value!r}, got {proof.get(field)!r}")
    if proof.get("trajectory_parameters") != expected_trajectory(case, args):
        raise CaptureError(f"{kind} proof has wrong resolved trajectory parameters")
    if proof.get("timing_eligible") is not False or proof.get("validation_mode") != "per-step":
        raise CaptureError(f"{kind} proof is not a per-step validation capture")
    for name in ("fixture_sha256", "harness_sha256", "source_sha256"):
        value = proof.get(name)
        if (not isinstance(value, str) or len(value) != EXPECTED_HARNESS_LENGTH or
                any(c not in "0123456789abcdef" for c in value)):
            raise CaptureError(f"{kind} proof has missing or malformed {name}")
    mapping = proof.get("renderer_mapping")
    if not isinstance(mapping, dict):
        raise CaptureError(f"{kind} proof has no renderer/mapping identity")
    if case.get("headless"):
        if mapping.get("renderer") != "none":
            raise CaptureError("Headless proof unexpectedly created a renderer")
    else:
        if (not isinstance(mapping.get("renderer"), str) or mapping.get("renderer") in ("", "none") or
                mapping.get("mapping_eligible") is not True or mapping.get("vsync_known") is not True):
            raise CaptureError(f"{kind} proof did not record a compatible renderer mapping and VSync policy")
        expected_dimensions = {"output_width": 320, "output_height": 240,
                               "actual_output_width": 320, "actual_output_height": 240,
                               "current_output_width": 320, "current_output_height": 240}
        if any(mapping.get(field) != value for field, value in expected_dimensions.items()):
            raise CaptureError(f"{kind} proof has malformed renderer output dimensions")
        if (mapping.get("scale") != [1.0, 1.0] or mapping.get("viewport") != [0, 0, 320, 240] or
                mapping.get("viewport_set") is not False or mapping.get("target_active") is not False or
                mapping.get("logical_presentation") != [0, 0, 0]):
            raise CaptureError(f"{kind} proof has an unsupported renderer mapping")


def _valid_sprite_identity(identity) -> bool:
    return (isinstance(identity, list) and len(identity) == 2 and
            all(isinstance(part, int) and not isinstance(part, bool) and part >= 0 for part in identity))


def validate_render_proof(root: Path, case: dict, args, mode: str) -> dict:
    prefix = root / f"{case_key(case)}-{mode}"
    evidence_path = Path(str(prefix) + ".evidence.json")
    frames_path = Path(str(prefix) + ".frames.jsonl")
    if not evidence_path.is_file() or not frames_path.is_file():
        raise CaptureError(f"Missing rendered validation proof for {case_key(case)} {mode}")
    proof = json.loads(evidence_path.read_text(encoding="utf-8"))
    _check_common_identity(proof, case, args, mode, "render")
    if proof.get("headless") is not bool(case.get("headless")):
        raise CaptureError("Rendered proof has the wrong headless identity")
    if sha256(frames_path) != proof.get("frames_sha256"):
        raise CaptureError("Rendered proof frame checksum is stale or corrupt")
    frames = []
    with frames_path.open(encoding="utf-8") as stream:
        for index, line in enumerate(stream):
            frame = json.loads(line)
            if not isinstance(frame, dict):
                raise CaptureError("Rendered proof frame is not a JSON object")
            if not isinstance(frame.get("frame"), int) or isinstance(frame.get("frame"), bool) or frame["frame"] != index:
                raise CaptureError("Rendered proof frame sequence is incomplete or reordered")
            if not isinstance(frame.get("submissions"), list) or not isinstance(frame.get("oracle_visible"), list):
                raise CaptureError("Rendered proof omits submission/oracle membership")
            for field in ("submissions", "oracle_visible"):
                identities = frame[field]
                if (len(identities) > case["sprite_count"] or
                        any(not _valid_sprite_identity(identity) for identity in identities) or
                        len({tuple(identity) for identity in identities}) != len(identities)):
                    raise CaptureError(f"Rendered proof has malformed or duplicate {field} identities")
            if not case.get("headless") and (not isinstance(frame.get("image_fnv64"), str) or len(frame["image_fnv64"]) != 16):
                raise CaptureError("Rendered proof has no normalized image hash")
            if not case.get("headless") and any(character not in "0123456789abcdef" for character in frame["image_fnv64"]):
                raise CaptureError("Rendered proof has a malformed normalized image hash")
            if case.get("headless") and frame.get("image_fnv64") is not None:
                raise CaptureError("Headless proof unexpectedly claims an image hash")
            for field in ("unresolved", "overflow"):
                if not case.get("headless") and field not in frame:
                    raise CaptureError(f"Rendered proof omits the {field} observation")
                value = frame.get(field, 0)
                if not isinstance(value, int) or isinstance(value, bool) or value != 0:
                    raise CaptureError(f"Rendered proof has unresolved or overflowed {field} observations")
            if case.get("headless") and frame.get("headless_work") != {"views": 0, "eligibility_checks": 0, "visibility_checks": 0, "submissions": 0}:
                raise CaptureError("Headless validation reports rendering work")
            frames.append(frame)
    if len(frames) != args.measure_frames:
        raise CaptureError("Rendered proof frame count does not match the requested sample count")
    if case.get("headless"):
        if proof.get("representative_sha256") is not None:
            raise CaptureError("Headless proof unexpectedly claims a renderer image")
    else:
        image_path = Path(str(prefix) + ".representative.ppm")
        if not image_path.is_file() or sha256(image_path) != proof.get("representative_sha256"):
            raise CaptureError("Representative image is missing or its checksum is stale")
    proof["_frames"] = frames
    proof["_path"] = str(evidence_path)
    return proof


def validate_resource_proof(root: Path, case: dict, args, mode: str) -> dict:
    path = root / f"{case_key(case)}-{mode}.resources.json"
    if not path.is_file():
        raise CaptureError(f"Missing resource proof for {case_key(case)} {mode}")
    proof = json.loads(path.read_text(encoding="utf-8"))
    _check_common_identity(proof, case, args, mode, "resource")
    if proof.get("headless") is not bool(case.get("headless")):
        raise CaptureError("Resource proof has the wrong headless identity")
    required = ("allocation_calls", "allocated_bytes", "process_rss_before_kib", "process_rss_after_kib",
                "render_view_preparations", "render_eligibility_checks", "render_visibility_checks",
                "render_submissions")
    if any(name not in proof for name in required):
        raise CaptureError("Resource proof lacks allocation or retained-memory observations")
    work_counts = ("allocation_calls", "allocated_bytes", "render_view_preparations", "render_eligibility_checks",
                   "render_visibility_checks", "render_submissions")
    for name in work_counts:
        value = proof[name]
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            raise CaptureError(f"Resource proof has a negative, fractional, or invalid {name}")
    for name in ("process_rss_before_kib", "process_rss_after_kib"):
        value = proof[name]
        if not isinstance(value, int) or isinstance(value, bool) or value < -1:
            raise CaptureError(f"Resource proof has an invalid {name}")
    expected_submissions = case["sprite_count"] * args.measure_frames
    if proof["render_submissions"] > expected_submissions:
        raise CaptureError("Resource proof reports more submissions than sprites times measured frames")
    if case.get("headless") and any(proof[name] != 0 for name in
                                     ("render_view_preparations", "render_eligibility_checks",
                                      "render_visibility_checks", "render_submissions")):
        raise CaptureError("Headless resource proof reports rendering work")
    if not case.get("headless") and proof["render_visibility_checks"] > expected_submissions:
        raise CaptureError("Resource proof reports more visibility checks than sprites times measured frames")
    if proof.get("allocation_calls_per_frame") != proof["allocation_calls"] / args.measure_frames:
        raise CaptureError("Resource proof allocation-call rate is inconsistent")
    if proof.get("allocated_bytes_per_frame") != proof["allocated_bytes"] / args.measure_frames:
        raise CaptureError("Resource proof allocation-byte rate is inconsistent")
    if not isinstance(proof.get("allocation_probe"), str) or "not attributed" not in proof["allocation_probe"]:
        raise CaptureError("Resource proof hides allocator coverage limits")
    return {**proof, "_path": str(path), "_sha256": sha256(path)}


def validate_mode_evidence(render_proofs: dict, case: dict) -> None:
    baseline = render_proofs["pre-feature"]["_frames"]
    disabled = render_proofs["disabled"]["_frames"]
    linear = render_proofs["linear"]["_frames"]
    if len(baseline) != len(disabled) or len(baseline) != len(linear):
        raise CaptureError("Mode validation frame counts differ")
    renderer_ids = {json.dumps(render_proofs[mode]["renderer_mapping"], sort_keys=True) for mode in MODES}
    if len(renderer_ids) != 1:
        raise CaptureError("Validation modes used different renderer/output/mapping settings")
    for field in ("fixture_sha256", "harness_sha256"):
        if len({render_proofs[mode].get(field) for mode in MODES}) != 1:
            raise CaptureError(f"Validation modes use different {field} values")
    candidate_sources = {render_proofs[mode].get("source_sha256") for mode in ("disabled", "linear")}
    baseline_source = render_proofs["pre-feature"].get("source_sha256")
    if len(candidate_sources) != 1 or not next(iter(candidate_sources)) or baseline_source in candidate_sources:
        raise CaptureError("Validation modes do not identify one candidate source and a distinct pre-feature source")
    for index, (reference, off, on) in enumerate(zip(baseline, disabled, linear)):
        if not case.get("headless"):
            hashes = {reference["image_fnv64"], off["image_fnv64"], on["image_fnv64"]}
            if len(hashes) != 1:
                raise ImageMismatch(index)
        if reference["oracle_visible"] != off["oracle_visible"] or reference["oracle_visible"] != on["oracle_visible"]:
            raise CaptureError(f"Independent visibility oracle differs by mode at frame {index}")
        reference_trace = [tuple(identity) for identity in reference["submissions"]]
        disabled_trace = [tuple(identity) for identity in off["submissions"]]
        linear_trace = [tuple(identity) for identity in on["submissions"]]
        if case.get("headless"):
            if reference_trace or disabled_trace or linear_trace:
                raise CaptureError("Headless modes must have empty renderer traces")
            continue
        if disabled_trace != reference_trace:
            raise CaptureError(f"Candidate-disabled trace differs from pre-feature at frame {index}")
        if len(set(linear_trace)) != len(linear_trace):
            raise CaptureError(f"Linear trace has duplicate sprite identities at frame {index}")
        reference_positions = {identity: position for position, identity in enumerate(reference_trace)}
        previous_position = -1
        for identity in linear_trace:
            position = reference_positions.get(identity)
            if position is None or position <= previous_position:
                raise CaptureError(f"Linear trace has an order inversion or foreign identity at frame {index}")
            previous_position = position
        if not set(tuple(identity) for identity in on["oracle_visible"]) <= set(linear_trace):
            raise CaptureError(f"Linear trace omitted an oracle-visible sprite at frame {index}")
    if case["profile"] in ("crossing", "camera-pan") and not case.get("headless"):
        counts = render_proofs["linear"].get("crossing_counts", {})
        if counts.get("entries", 0) == 0 or counts.get("exits", 0) == 0:
            raise CaptureError("Dynamic proof did not observe both boundary entries and exits")


def validate_resource_mode_evidence(resource_proofs: dict, case: dict, measure_frames: int,
                                    render_proofs: dict | None = None) -> None:
    if len({resource_proofs[mode].get("fixture_sha256") for mode in MODES}) != 1 or \
            len({resource_proofs[mode].get("harness_sha256") for mode in MODES}) != 1:
        raise CaptureError("Resource modes use different fixture or harness fingerprints")
    candidate_sources = {resource_proofs[mode].get("source_sha256") for mode in ("disabled", "linear")}
    if len(candidate_sources) != 1 or resource_proofs["pre-feature"].get("source_sha256") in candidate_sources:
        raise CaptureError("Resource modes do not identify one candidate source and a distinct pre-feature source")
    if (resource_proofs["disabled"].get("allocation_calls") != resource_proofs["linear"].get("allocation_calls") or
            resource_proofs["disabled"].get("allocated_bytes") != resource_proofs["linear"].get("allocated_bytes")):
        raise CaptureError("Linear culling added observed C++ allocations over candidate submit-all")
    expected_submissions = case["sprite_count"] * measure_frames
    for mode in MODES:
        proof = resource_proofs[mode]
        views = proof["render_view_preparations"]
        eligibility = proof["render_eligibility_checks"]
        visibility = proof["render_visibility_checks"]
        submissions = proof["render_submissions"]
        if case.get("headless"):
            if any((views, eligibility, visibility, submissions)):
                raise CaptureError("Headless resource proof reports rendering work")
        elif mode == "pre-feature":
            if any((views, eligibility, visibility)) or submissions != expected_submissions:
                raise CaptureError(f"{mode} resource work counters do not match submit-all semantics")
        elif mode == "disabled":
            if views != measure_frames or eligibility != 0 or visibility != 0 or submissions != expected_submissions:
                raise CaptureError("Disabled resource work counters do not match submit-all semantics")
        elif (views != measure_frames or eligibility != measure_frames or
              visibility != expected_submissions or submissions > expected_submissions):
            raise CaptureError("Linear resource work counters do not match one ordered visibility pass per frame")
        if render_proofs is not None:
            observed_submissions = sum(len(frame["submissions"]) for frame in render_proofs[mode]["_frames"])
            if submissions != observed_submissions:
                raise CaptureError(f"{mode} resource submission count differs from the checked membership trace")
            oracle_visible = sum(len(frame["oracle_visible"]) for frame in render_proofs[mode]["_frames"])
            if submissions < oracle_visible:
                raise CaptureError(f"{mode} resource submissions omit independently visible sprites")


def validate_timing_manifest(csv_path: Path, case: dict, args, mode: str,
                              render_proof: dict, resource_proof: dict, source_identity: dict,
                              run_index: int) -> dict:
    metrics = parse_benchmark_csv(csv_path)
    manifest = json.loads(csv_path.with_suffix(".manifest.json").read_text(encoding="utf-8"))
    build = manifest["build"]
    if build.get("configuration", "").lower() != "release" or build.get("sanitizer") not in ("none", ""):
        raise CaptureError("Timing binary must be an uninstrumented Release build")
    if build.get("work_probes") is not False or build.get("debug_tools") is not False:
        raise CaptureError("Timing binary contains debug or work-probe instrumentation")
    if manifest.get("timing_eligible") is not True or manifest.get("validation_mode") != "boundary-only" or manifest.get("stopped_on_quit") is not False:
        raise CaptureError("Per-step validation/resource runs cannot be accepted as timing data")
    if manifest.get("scenario") != "linear_render_culling" or manifest.get("workers") != case["workers"]:
        raise CaptureError("Timing workload or worker count differs from the requested case")
    if manifest.get("seed") != args.seed or manifest.get("dt") != args.dt or manifest.get("warmup_steps") != args.warmup_frames or manifest.get("measured_steps") != args.measure_frames or manifest.get("requested_measured_steps") != args.measure_frames:
        raise CaptureError("Timing seed/dt/warmup/measurement parameters do not match")
    if manifest.get("repetitions") != args.repetitions or manifest.get("repetition_index") != run_index:
        raise CaptureError("Timing repetition identity does not match the requested independent run")
    if manifest.get("resolved_parameters", {}).get("profile") != case["profile"] or manifest.get("resolved_parameters", {}).get("sprite_count") != case["sprite_count"]:
        raise CaptureError("Timing profile or population does not match")
    if manifest.get("resolved_parameters") != render_proof.get("trajectory_parameters"):
        raise CaptureError("Timing trajectory parameters differ from the checked validation proof")
    culling = manifest.get("linear_render_culling", {})
    expected_policy = "headless-no-rendering" if case.get("headless") else mode
    if (culling.get("mode") != mode or culling.get("requested_mode") != mode or
            culling.get("effective_policy") != expected_policy or
            culling.get("enabled") != (mode == "linear" and not case.get("headless"))):
        raise CaptureError("Timing requested/effective culling policy does not match the comparison arm")
    expected_render_samples = 0 if case.get("headless") else args.measure_frames
    if (manifest.get("rendering") is not (not case.get("headless")) or
            manifest.get("render_samples") != expected_render_samples or
            manifest.get("total_render_samples") != expected_render_samples or
            manifest.get("total_steps") != args.measure_frames or
            manifest.get("complete_capture") is not True):
        raise CaptureError("Timing capture is incomplete or has the wrong render sample count")
    if not isinstance(manifest.get("fixture_sha256"), str) or not isinstance(manifest.get("harness_sha256"), str):
        raise CaptureError("Timing output lacks fixture/harness provenance")
    if manifest["fixture_sha256"] != render_proof.get("fixture_sha256") or manifest["harness_sha256"] != render_proof.get("harness_sha256"):
        raise CaptureError("Timing and correctness harness fingerprints differ")
    if manifest.get("build", {}).get("source_sha256") != render_proof.get("source_sha256") or manifest.get("build", {}).get("source_sha256") != resource_proof.get("source_sha256"):
        raise CaptureError("Timing binary source fingerprint differs from correctness/resource proof")
    if manifest.get("validation_proof", {}).get("sha256") != sha256(Path(render_proof["_path"])):
        raise CaptureError("Timing output does not name the checked rendered proof checksum")
    recorded_resource = manifest.get("resource_observation", {})
    if recorded_resource.get("sha256") != resource_proof["_sha256"]:
        raise CaptureError("Timing output does not name the checked resource proof checksum")
    mapping = manifest.get("renderer_mapping", {})
    if mapping != render_proof.get("renderer_mapping"):
        raise CaptureError("Timing renderer/output/mapping differs from the validation renderer")
    fingerprint = build.get("source_sha256")
    if mode in ("linear", "disabled"):
        if source_identity.setdefault("candidate", fingerprint) != fingerprint:
            raise CaptureError("Candidate modes use mixed source fingerprints")
    elif source_identity.setdefault("baseline", fingerprint) != fingerprint:
        raise CaptureError("Baseline repetitions use mixed source fingerprints")
    return {"metrics": metrics, "manifest": manifest}


def _probe_command(binary: Path, case: dict, args, mode: str, csv_path: Path,
                   validation_path: Path, resource_path: Path, run_index: int) -> list[str]:
    command = [str(binary), "--profile", case["profile"], "--sprite-count", str(case["sprite_count"]),
               "--workers", str(case["workers"]), "--seed", str(args.seed), "--warmup-frames", str(args.warmup_frames),
               "--measure-frames", str(args.measure_frames), "--dt", repr(args.dt), "--repetitions", str(args.repetitions),
               "--run-index", str(run_index), "--csv-output", str(csv_path),
               "--validation-proof", str(validation_path), "--resource-proof", str(resource_path)]
    if mode == "disabled": command.append("--no-render-culling")
    if case.get("headless"): command.append("--headless")
    if args.asset_root: command += ["--asset-root", str(args.asset_root)]
    return command


def _validation_probe_identity(binary: Path, expected_proof: dict, log_path: Path, env: dict) -> dict:
    command = [str(binary), "--print-build-identity"]
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, env=env, check=False)
    with log_path.open("x", encoding="utf-8") as log:
        log.write("COMMAND: " + json.dumps(command) + "\n")
        log.write("STDOUT:\n" + completed.stdout)
        log.write("STDERR:\n" + completed.stderr)
    if completed.returncode != 0:
        raise CaptureError(f"Could not inspect validation probe build identity; see {log_path}")
    try:
        identity = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise CaptureError(f"Validation probe did not return a build identity; see {log_path}") from error
    if not isinstance(identity, dict) or identity.get("work_probes") is not True:
        raise CaptureError(f"Validation recovery executable is not work-probe capable: {binary}")
    for field in ("source_sha256", "fixture_sha256", "harness_sha256"):
        if identity.get(field) != expected_proof.get(field):
            raise CaptureError(f"Validation recovery executable has incompatible {field}: {binary}")
    return identity


def capture_mismatch_images(args, case: dict, frame: int, output: Path, logs: Path, env: dict,
                            expected_proofs: dict) -> list[str]:
    mismatch_dir = output / "mismatches" / case_key(case)
    mismatch_dir.mkdir(parents=True, exist_ok=False)
    expected_candidate = expected_proofs["linear"]
    if (expected_proofs["disabled"].get("source_sha256") != expected_candidate.get("source_sha256") or
            expected_proofs["disabled"].get("fixture_sha256") != expected_candidate.get("fixture_sha256") or
            expected_proofs["disabled"].get("harness_sha256") != expected_candidate.get("harness_sha256")):
        raise CaptureError("Checked disabled/linear validation proofs disagree on candidate build identity")
    recovery_identities = {
        "pre-feature": _validation_probe_identity(
            args.baseline_validation_probe, expected_proofs["pre-feature"],
            logs / f"mismatch-{case_key(case)}-baseline-validation-identity.log", env),
        "candidate": _validation_probe_identity(
            args.candidate_validation_probe, expected_candidate,
            logs / f"mismatch-{case_key(case)}-candidate-validation-identity.log", env),
    }
    saved = []
    for mode in MODES:
        binary = args.baseline_validation_probe if mode == "pre-feature" else args.candidate_validation_probe
        prefix = mismatch_dir / mode
        command = [str(binary), "--profile", case["profile"], "--sprite-count", str(case["sprite_count"]),
                   "--workers", str(case["workers"]), "--seed", str(args.seed),
                   "--warmup-frames", str(args.warmup_frames), "--measure-frames", str(args.measure_frames),
                   "--dt", repr(args.dt), "--repetitions", str(args.repetitions), "--run-index", "0",
                   "--validate-rendering", "--capture-frame-index", str(frame),
                   "--evidence-output", str(prefix), "--csv-output", str(prefix) + ".csv"]
        if mode == "disabled": command.append("--no-render-culling")
        if args.asset_root: command += ["--asset-root", str(args.asset_root)]
        log_path = logs / f"mismatch-{case_key(case)}-{mode}.log"
        _run(command, log_path, env)
        image_path = Path(str(prefix) + ".mismatch.ppm")
        if not image_path.is_file():
            raise CaptureError(f"Mismatch image was not written for {mode}")
        saved.append(str(image_path))
    (mismatch_dir / "mismatch.json").write_text(
        json.dumps({"frame": frame, "images": saved, "validation_probe_identities": recovery_identities},
                   indent=2) + "\n", encoding="utf-8")
    return saved


def preserve_image_mismatch(mismatch: ImageMismatch, recovery) -> None:
    try:
        recovery()
    except Exception as recovery_error:
        raise ImageMismatch(mismatch.frame, str(recovery_error)) from mismatch
    raise mismatch


def _run(command: list[str], log_path: Path, env: dict) -> None:
    with log_path.open("x", encoding="utf-8") as log:
        log.write("COMMAND: " + json.dumps(command) + "\n")
        log.flush()
        completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, env=env, check=False)
    if completed.returncode != 0:
        raise CaptureError(f"Capture process failed ({completed.returncode}); see {log_path}")


def aggregate(rows: list[dict]) -> dict:
    result = {}
    for mode in MODES:
        selected = [row for row in rows if row["mode"] == mode]
        render_means = [row["render_mean_ms"] for row in selected]
        step_means = [row["step_mean_ms"] for row in selected]
        result[mode] = {
            "independent_runs": len(selected),
            "render_run_means_ms": render_means,
            "render_mean_ms": statistics.fmean(render_means),
            "render_median_of_run_means_ms": statistics.median(render_means),
            "render_run_mean_stdev_ms": statistics.stdev(render_means) if len(render_means) > 1 else 0.0,
            "render_run_mean_min_ms": min(render_means),
            "render_run_mean_max_ms": max(render_means),
            "render_per_run_p95_ms": [row["render_p95_ms"] for row in selected],
            "render_per_run_p99_ms": [row["render_p99_ms"] for row in selected],
            "render_per_run_max_ms": [row["render_max_ms"] for row in selected],
            "complete_step_mean_ms": statistics.fmean(step_means),
            "complete_step_median_of_run_means_ms": statistics.median(step_means),
            "complete_step_run_mean_stdev_ms": statistics.stdev(step_means) if len(step_means) > 1 else 0.0,
            "complete_step_per_run_p95_ms": [row["step_p95_ms"] for row in selected],
            "complete_step_per_run_p99_ms": [row["step_p99_ms"] for row in selected],
            "complete_step_per_run_max_ms": [row["step_max_ms"] for row in selected],
            "validated_submitted_fractions": [row["submitted_fraction"] for row in selected],
        }
    return result


def engine_control_matrix(multiworker_count: int) -> list[dict]:
    if multiworker_count <= 0:
        raise CaptureError("Engine-control worker count must be positive")
    scenarios = [
        ("persistent_contacts", False, ["--contact-pair-count", "100", "--scripted-endpoints", "2"],
         {"contact_pair_count": 100, "scripted_endpoints": 2}),
        ("broad_phase_stress", False, ["--broad-count", "300"], {"body_count": 300}),
        ("broad_phase", True, ["--broad-count", "128"], {"body_count": 128}),
        ("moving_add_and_remove", True, [], {"steady_count": 1000, "add_count": 10,
                                               "remove_count": 10, "effective_churn": 10}),
    ]
    jobs = []
    for scenario, rendered, extra, resolved_parameters in scenarios:
        workers = (1, multiworker_count) if not rendered else (1,)
        for worker_count in sorted(set(workers)):
            jobs.append({"scenario": scenario, "rendered": rendered,
                         "extra": extra, "workers": worker_count,
                         "resolved_parameters": resolved_parameters})
    return jobs


def validate_engine_control_manifest(manifest: dict, control: dict, args, mode: str,
                                     run_index: int, expected_source: str,
                                     candidate_source: str) -> str:
    if mode not in ("pre-feature", "linear"):
        raise CaptureError(f"Unknown engine-control mode: {mode}")
    build = manifest.get("build", {})
    if (build.get("configuration", "").lower() != "release" or build.get("work_probes") is not False or
            build.get("debug_tools") is not False or build.get("sanitizer") not in ("none", "")):
        raise CaptureError("Engine control used an instrumented or non-Release binary")
    fingerprint = build.get("source_sha256")
    if (not isinstance(fingerprint, str) or len(fingerprint) != EXPECTED_HARNESS_LENGTH or
            any(character not in "0123456789abcdef" for character in fingerprint)):
        raise CaptureError("Engine control has a missing or malformed source fingerprint")
    if fingerprint != expected_source:
        raise CaptureError(f"Engine {mode} control source does not match its checked focused validation binary")
    if mode == "pre-feature" and fingerprint == candidate_source:
        raise CaptureError("Pre-feature engine control unexpectedly uses the candidate source")
    expected_identity = {
        "scenario": control["scenario"], "workers": control["workers"], "seed": args.seed,
        "dt": args.dt, "warmup_steps": args.warmup_frames, "measured_steps": args.measure_frames,
        "requested_measured_steps": args.measure_frames, "repetitions": args.repetitions,
        "repetition_index": run_index,
    }
    for field, value in expected_identity.items():
        if manifest.get(field) != value:
            raise CaptureError(f"Engine control has wrong {field}: expected {value!r}, got {manifest.get(field)!r}")
    rendered = control["rendered"]
    expected_render_samples = args.measure_frames if rendered else 0
    if (manifest.get("rendering") is not rendered or manifest.get("render_samples") != expected_render_samples or
            manifest.get("total_render_samples") != expected_render_samples or
            manifest.get("total_steps") != args.measure_frames or manifest.get("complete_capture") is not True or
            manifest.get("timing_eligible") is not True or manifest.get("validation_mode") != "boundary-only" or
            manifest.get("stopped_on_quit") is not False):
        raise CaptureError("Engine control timing eligibility, render policy, or sample count is invalid")
    resolved = manifest.get("resolved_parameters")
    if not isinstance(resolved, dict):
        raise CaptureError("Engine control has no resolved workload identity")
    for field, value in control["resolved_parameters"].items():
        if resolved.get(field) != value:
            raise CaptureError(f"Engine control has wrong resolved {field}")
    culling = manifest.get("linear_render_culling")
    if mode == "linear":
        expected_policy = "linear-filtering" if rendered else "headless-no-rendering"
        if (not isinstance(culling, dict) or culling.get("requested_mode") != "linear" or
                culling.get("requested_enabled") is not True or culling.get("effective_enabled") is not rendered or
                culling.get("effective_policy") != expected_policy):
            raise CaptureError("Candidate engine control did not request and apply the expected culling policy")
    elif culling is not None:
        raise CaptureError("Pre-feature engine control unexpectedly reports candidate culling metadata")

    physical = manifest.get("physical_invariants")
    if not isinstance(physical, dict) or any(physical.get(name) is not True
                                             for name in ("finite", "populations_valid", "supports_valid")):
        raise CaptureError("Engine control did not preserve its recorded physical invariants")
    return fingerprint


def run_engine_controls(args, logs: Path, raw: Path, env: dict, rng: random.Random,
                        expected_sources: dict[str, str]) -> list[dict]:
    controls = engine_control_matrix(args.multiworker_count)
    rows = []
    fingerprints = {}
    for run_index in range(args.repetitions):
        scheduled = [(control, mode) for control in controls for mode in ("pre-feature", "linear")]
        rng.shuffle(scheduled)
        for control, mode in scheduled:
            scenario = control["scenario"]
            rendered = control["rendered"]
            extra = control["extra"]
            workers = control["workers"]
            binary = args.baseline_runner if mode == "pre-feature" else args.candidate_runner
            stem = f"engine-{scenario}-w{workers}-{mode}-r{run_index}"
            csv_path = raw / f"{stem}.csv"
            command = [str(binary), "--scenario", scenario, "--workers", str(workers), "--seed", str(args.seed),
                       "--warmup-frames", str(args.warmup_frames), "--measure-frames", str(args.measure_frames),
                       "--dt", repr(args.dt), "--repetitions", str(args.repetitions), "--run-index", str(run_index),
                       "--csv-output", str(csv_path), *extra]
            if rendered: command.append("--render")
            if args.asset_root: command += ["--asset-root", str(args.asset_root)]
            log_path = logs / f"{stem}.log"
            _run(command, log_path, env)
            metrics = parse_benchmark_csv(csv_path)
            manifest = json.loads(csv_path.with_suffix(".manifest.json").read_text(encoding="utf-8"))
            fingerprint = validate_engine_control_manifest(
                manifest, control, args, mode, run_index, expected_sources[mode], expected_sources["linear"])
            fingerprints.setdefault(mode, fingerprint)
            if fingerprints[mode] != fingerprint:
                raise CaptureError(f"Engine {mode} controls use mixed source fingerprints")
            renderer_mapping = manifest.get("renderer_mapping")
            renderer_status = "verified" if isinstance(renderer_mapping, dict) and renderer_mapping.get("renderer") else "unverified: engine-control manifest omits renderer identity"
            rows.append({"scenario": scenario, "workers": workers, "mode": mode, "run_index": run_index,
                         "rendered": rendered, "render_mean_ms": metrics["scope.render.avg"],
                         "render_p95_ms": metrics["scope.render.p95"], "render_p99_ms": metrics["scope.render.p99"],
                         "render_max_ms": metrics["scope.render.max"],
                         "step_mean_ms": metrics["scope.complete_step.avg"],
                         "step_p95_ms": metrics["scope.complete_step.p95"],
                         "step_p99_ms": metrics["scope.complete_step.p99"],
                         "step_max_ms": metrics["scope.complete_step.max"],
                         "physics_mean_ms": metrics["scope.physics.avg"],
                         "broad_phase_mean_ms": metrics["scope.broad_phase.avg"],
                         "broad_phase_p95_ms": metrics["scope.broad_phase.p95"],
                         "broad_phase_p99_ms": metrics["scope.broad_phase.p99"],
                         "broad_phase_max_ms": metrics["scope.broad_phase.max"],
                         "renderer_identity_status": renderer_status,
                         "source_sha256": fingerprint, "csv": str(csv_path),
                         "manifest_sha256": sha256(csv_path.with_suffix(".manifest.json")),
                         "samples_sha256": sha256(csv_path.with_suffix(".samples.csv")), "log": str(log_path)})
    return rows


def parser() -> argparse.ArgumentParser:
    command = argparse.ArgumentParser(description=__doc__)
    for name in ("baseline-probe", "candidate-probe", "baseline-runner", "candidate-runner",
                 "baseline-validation-probe", "candidate-validation-probe", "validation-root", "resource-root", "output"):
        command.add_argument("--" + name, type=Path, required=True)
    command.add_argument("--workers", type=int, default=1)
    command.add_argument("--multiworker-count", type=int, required=True)
    command.add_argument("--seed", type=int, default=0)
    command.add_argument("--warmup-frames", type=int, default=120)
    command.add_argument("--measure-frames", type=int, default=600)
    command.add_argument("--repetitions", type=int, default=5)
    command.add_argument("--dt", type=float, default=0.008333333333333333)
    command.add_argument("--asset-root", type=Path)
    return command


def run_capture(args) -> dict:
    validate_repetitions(args.repetitions)
    if args.multiworker_count <= 0 or args.workers <= 0 or args.seed < 0 or args.warmup_frames < 0 or args.measure_frames <= 0 or not math.isfinite(args.dt) or args.dt <= 0:
        raise CaptureError("Invalid worker, seed, frame, or timestep arguments")
    binaries = (args.baseline_probe, args.candidate_probe, args.baseline_runner, args.candidate_runner,
                args.baseline_validation_probe, args.candidate_validation_probe)
    if any(not path.is_file() or not os.access(path, os.X_OK) for path in binaries):
        raise CaptureError("Every explicit probe/stock-runner path must be an executable file")
    for root in (args.validation_root, args.resource_root):
        if not root.is_dir(): raise CaptureError(f"Evidence root does not exist: {root}")
    if args.output.exists(): raise CaptureError(f"Refusing to reuse an existing output path: {args.output}")
    args.output.mkdir(parents=True)
    logs = args.output / "logs"; logs.mkdir()
    raw = args.output / "raw"; raw.mkdir()
    env = os.environ.copy()
    env.update({"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy", "SDL_RENDER_DRIVER": "software"})
    try:
        cases = matrix(args.multiworker_count, args.workers)
        proofs = {}
        resources = {}
        for case in cases:
            key = case_key(case)
            proofs[key] = {mode: validate_render_proof(args.validation_root, case, args, mode) for mode in MODES}
            resources[key] = {mode: validate_resource_proof(args.resource_root, case, args, mode) for mode in MODES}
            for mode in MODES:
                if proofs[key][mode].get("source_sha256") != resources[key][mode].get("source_sha256"):
                    raise CaptureError(f"Rendered/resource source fingerprints differ for {key} {mode}")
                if proofs[key][mode].get("renderer_mapping") != resources[key][mode].get("renderer_mapping"):
                    raise CaptureError(f"Rendered/resource renderer mapping differs for {key} {mode}")
                if proofs[key][mode].get("fixture_sha256") != resources[key][mode].get("fixture_sha256") or proofs[key][mode].get("harness_sha256") != resources[key][mode].get("harness_sha256"):
                    raise CaptureError(f"Rendered/resource harness fingerprints differ for {key} {mode}")
            try:
                validate_mode_evidence(proofs[key], case)
                validate_resource_mode_evidence(resources[key], case, args.measure_frames, proofs[key])
            except ImageMismatch as mismatch:
                preserve_image_mismatch(
                    mismatch,
                    lambda: capture_mismatch_images(args, case, mismatch.frame, args.output, logs, env, proofs[key]))
        source_identity = {}
        raw_rows = []
        rng = random.Random(args.seed)
        for run_index in range(args.repetitions):
            for case in cases:
                modes = list(MODES)
                rng.shuffle(modes)
                for mode in modes:
                    key = case_key(case)
                    binary = args.baseline_probe if mode == "pre-feature" else args.candidate_probe
                    csv_path = raw / f"{key}-{mode}-r{run_index}.csv"
                    csv_path.touch(exist_ok=False)
                    csv_path.unlink()
                    validation_path = Path(proofs[key][mode]["_path"])
                    resource_path = Path(resources[key][mode]["_path"])
                    command = _probe_command(binary, case, args, mode, csv_path, validation_path, resource_path, run_index)
                    log_path = logs / (csv_path.stem + ".log")
                    _run(command, log_path, env)
                    proof = proofs[key][mode]
                    checked = validate_timing_manifest(csv_path, case, args, mode, proof, resources[key][mode], source_identity, run_index)
                    metrics = checked["metrics"]
                    raw_rows.append({"case": key, "mode": mode, "run_index": run_index,
                                     "render_mean_ms": metrics["scope.render.avg"],
                                     "render_p95_ms": metrics["scope.render.p95"],
                                     "render_p99_ms": metrics["scope.render.p99"],
                                     "render_max_ms": metrics["scope.render.max"],
                                     "step_mean_ms": metrics["scope.complete_step.avg"],
                                     "step_p95_ms": metrics["scope.complete_step.p95"],
                                     "step_p99_ms": metrics["scope.complete_step.p99"],
                                     "step_max_ms": metrics["scope.complete_step.max"],
                                     "submitted_fraction": _submission_fraction(proofs[key]),
                                     "csv": str(csv_path),
                                     "manifest_sha256": sha256(csv_path.with_suffix(".manifest.json")),
                                     "samples_sha256": sha256(csv_path.with_suffix(".samples.csv")),
                                     "log": str(log_path)})
        checked_sources = {
            "pre-feature": {proofs[key]["pre-feature"]["source_sha256"] for key in proofs},
            "linear": {proofs[key]["linear"]["source_sha256"] for key in proofs},
        }
        if any(len(values) != 1 for values in checked_sources.values()):
            raise CaptureError("Focused validation binaries use mixed baseline or candidate source fingerprints")
        expected_sources = {mode: next(iter(values)) for mode, values in checked_sources.items()}
        if expected_sources["pre-feature"] == expected_sources["linear"]:
            raise CaptureError("Focused baseline and candidate validation binaries do not identify distinct sources")
        controls = run_engine_controls(args, logs, raw, env, rng, expected_sources)
        with (args.output / "raw-index.csv").open("x", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=raw_rows[0].keys())
            writer.writeheader(); writer.writerows(raw_rows)
        aggregates = {case_key(case): aggregate([row for row in raw_rows if row["case"] == case_key(case)]) for case in cases}
        differences = {}
        for case in cases:
            key = case_key(case)
            summary = aggregates[key]
            linear = summary["linear"]["render_mean_ms"]
            disabled = summary["disabled"]["render_mean_ms"]
            reference = summary["pre-feature"]["render_mean_ms"]
            differences[key] = {
                "linear_vs_pre_feature_render_ms": linear - reference,
                "disabled_vs_pre_feature_render_ms": disabled - reference,
                "linear_vs_disabled_render_ms": linear - disabled,
                "linear_vs_pre_feature_complete_step_ms": summary["linear"]["complete_step_mean_ms"] - summary["pre-feature"]["complete_step_mean_ms"],
                "linear_vs_disabled_complete_step_ms": summary["linear"]["complete_step_mean_ms"] - summary["disabled"]["complete_step_mean_ms"],
            }
        membership_visibility = {}
        for case in cases:
            key = case_key(case)
            mode_summary = {}
            for mode in MODES:
                frames = proofs[key][mode]["_frames"]
                mode_summary[mode] = {
                    "frames": len(frames),
                    "submissions": sum(len(frame["submissions"]) for frame in frames),
                    "oracle_visible_memberships": sum(len(frame["oracle_visible"]) for frame in frames),
                    "maximum_oracle_visible_per_frame": max((len(frame["oracle_visible"]) for frame in frames), default=0),
                    "proof": proofs[key][mode]["_path"],
                    "proof_sha256": sha256(Path(proofs[key][mode]["_path"])),
                }
            membership_visibility[key] = mode_summary
        report = {"schema_version": 1, "cases": cases, "repetitions": args.repetitions,
                  "parameters": {"seed": args.seed, "warmup_frames": args.warmup_frames,
                                 "measure_frames": args.measure_frames, "dt": args.dt,
                                 "multiworker_count": args.multiworker_count,
                                 "affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None},
                  "binary_paths": [str(path.resolve()) for path in binaries],
                  "source_fingerprints": source_identity, "runs": raw_rows,
                  "engine_controls": controls,
                  "membership_visibility_evidence": membership_visibility,
                  "engine_control_rendering_evidence": {
                      "renderer_identity": "unverified where the engine-control manifest omits renderer_mapping",
                      "sprite_membership_and_independent_visibility": "not collected by engine-control scenarios; checked by the focused rendered validation proofs listed above",
                  },
                  "aggregates_by_case": aggregates, "differences_by_case": differences,
                  "resource_observations": {key: {mode: {"allocation_calls": resources[key][mode].get("allocation_calls"),
                                                          "allocated_bytes": resources[key][mode].get("allocated_bytes"),
                                                          "process_rss_before_kib": resources[key][mode].get("process_rss_before_kib"),
                                                          "process_rss_after_kib": resources[key][mode].get("process_rss_after_kib"),
                                                          "proof": resources[key][mode]["_path"],
                                                          "sha256": resources[key][mode]["_sha256"]}
                                                    for mode in MODES} for key in resources},
                  "acceptance": {"sc005": "not accepted by the capture tool; compare the mostly-offscreen benefit with independent-run spread and investigate all-visible/headless regressions before signoff"}}
        (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        return report
    except Exception:
        # Keep partial logs/raw captures for diagnosis. A failed run never emits
        # a report that could be mistaken for a complete acceptance matrix.
        raise


def _submission_fraction(proofs: dict) -> float:
    reference = proofs["pre-feature"]["_frames"]
    linear = proofs["linear"]["_frames"]
    denominator = sum(len(frame.get("submissions", ())) for frame in reference)
    numerator = sum(len(frame.get("submissions", ())) for frame in linear)
    return numerator / denominator if denominator else 0.0


def main(argv=None) -> int:
    args = parser().parse_args(argv)
    try:
        run_capture(args)
        return 0
    except (CaptureError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"capture_linear_render: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
