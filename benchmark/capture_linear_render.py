#!/usr/bin/env python3
"""Capture the bounded linear-render comparison using schema-v3 artifacts."""

from __future__ import annotations

import argparse
import csv
from contextlib import nullcontext
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import random
import signal
import statistics
import subprocess
import sys
import time

from capture_budget import BudgetExceeded, CaptureBudget, capture_budget, check_budget, wait_for_process

from sweep_scaling import COUNTERS as INCREMENTAL_BENCHMARK_COUNTERS
from sweep_scaling import SCOPES as INCREMENTAL_BENCHMARK_SCOPES
from sweep_scaling import parse_benchmark_csv


MODES = ("pre-feature", "disabled", "linear")
PROFILES = ("visible", "offscreen", "sparse", "clustered", "crossing", "camera-pan", "churn", "rotated")
INCREMENTAL_PROFILES = ("visible", "offscreen", "crossing", "camera-pan", "churn", "sparse", "clustered",
                        "rotated", "visible-motion", "changed", "spread", "giant", "large-offset",
                        "output-resize", "scene-default", "scene-stacking")
TRAJECTORY_PERIOD_STEPS = {profile: (240 if profile in ("crossing", "camera-pan", "visible-motion", "changed",
                                                           "giant", "large-offset", "rotated", "clustered") else 0)
                           for profile in INCREMENTAL_PROFILES}
EXPECTED_HARNESS_LENGTH = 64
INCREMENTAL_STRATEGIES = ("current-linear", "cached-linear", "indexed")
INCREMENTAL_PHASES = ("validate", "resources", "time", "pilot")
FOCUSED_CASE_NAMES = ("mostly-offscreen", "camera-motion", "small-moving-subset",
                      "fully-moving-crossing", "all-visible", "crossing-multiworker")


class CaptureError(RuntimeError):
    pass


class ImageMismatch(CaptureError):
    def __init__(self, frame: int, recovery_error: str | None = None):
        message = f"Image mismatch at frame {frame}"
        if recovery_error:
            message += f"; mismatch image recovery failed: {recovery_error}"
        super().__init__(message)
        self.frame = frame


def validate_incremental_options(comparison: str, phase: str | None, strategy: str | None) -> tuple[str, str | None, str | None]:
    if comparison not in ("legacy", "incremental"):
        raise CaptureError(f"Unknown comparison: {comparison}")
    if comparison == "incremental":
        if phase not in INCREMENTAL_PHASES:
            raise CaptureError(f"Unknown incremental phase: {phase}")
        if strategy not in (*INCREMENTAL_STRATEGIES, "all"):
            raise CaptureError(f"Unknown render strategy: {strategy}")
    elif phase is not None or strategy is not None:
        raise CaptureError("--phase and --strategy require --comparison incremental")
    return comparison, phase, strategy


def validate_incremental_case_identity(proof: dict, expected_case: dict, expected_identity: dict) -> None:
    if proof.get("proof_kind") != "platformator.incremental-render-culling" or proof.get("proof_version") != 1:
        raise CaptureError("Incremental proof kind or version is unsupported")
    if proof.get("comparison") != "incremental":
        raise CaptureError("Incremental proof has the wrong comparison identity")
    if proof.get("case") != expected_case:
        raise CaptureError("Incremental proof has a stale or mismatched full case key")
    if proof.get("identity") != expected_identity:
        raise CaptureError("Incremental proof has substituted or mismatched source/build/binary identities")
    if not isinstance(expected_case.get("population"), int) or expected_case["population"] < 0:
        raise CaptureError("Incremental case population is missing or negative")
    fraction = expected_case.get("changed_fraction")
    if not isinstance(fraction, (int, float)) or isinstance(fraction, bool) or not 0 <= fraction <= 1:
        raise CaptureError("Incremental case changed fraction is missing or outside [0, 1]")
    for field in ("strategy", "profile", "layout", "camera_schedule", "output_schedule"):
        if not isinstance(expected_case.get(field), str) or not expected_case[field]:
            raise CaptureError(f"Incremental case is missing {field}")
    for field, value in expected_identity.items():
        if not isinstance(value, str) or not value:
            raise CaptureError(f"Incremental case identity is missing {field}")


def validate_incremental_timing_samples(raw_samples: list[dict], paired_samples: list[float],
                                        expected_count: int, build: dict) -> None:
    if expected_count <= 0 or len(raw_samples) != expected_count or len(paired_samples) != expected_count:
        raise CaptureError("Incremental raw or paired timing samples are truncated")
    if (build.get("configuration", "").lower() != "release" or
            build.get("sanitizer") not in ("none", "") or build.get("work_probes") is not False or
            build.get("debug_tools") is not False):
        raise CaptureError("Incremental timing requires an uninstrumented Release binary")
    keys = {row.get("case_key") for row in raw_samples}
    if len(keys) != 1 or None in keys:
        raise CaptureError("Incremental timing rows have mixed or missing case keys")
    for index, row in enumerate(raw_samples):
        if row.get("run_index") != index:
            raise CaptureError("Incremental raw timing samples are reordered or duplicated")
        for field in ("render_ms", "complete_step_ms", "paired_step_render_ms"):
            value = row.get(field)
            if not isinstance(value, (int, float)) or isinstance(value, bool) or not math.isfinite(value) or value < 0:
                raise CaptureError(f"Incremental timing sample has invalid {field}")
        if paired_samples[index] != row["paired_step_render_ms"]:
            raise CaptureError("Incremental paired host intervals do not match raw sample rows")


def read_incremental_timing_samples(samples_path: Path, paired_path: Path, expected_steps: int,
                                    expected_renders: int, headless: bool) -> tuple[list[dict], list[float]]:
    expected_columns = {"schema_version", "sample_kind", "index"} | {
        name + "_ns" for name in INCREMENTAL_BENCHMARK_SCOPES} | set(INCREMENTAL_BENCHMARK_COUNTERS)
    step_rows: list[dict] = []
    render_rows: list[dict] = []
    with samples_path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if set(reader.fieldnames or ()) != expected_columns or len(reader.fieldnames or ()) != len(expected_columns):
            raise CaptureError("Incremental timing raw columns differ from schema-v3")
        for row in reader:
            kind = row["sample_kind"]
            destination = step_rows if kind == "step" else render_rows if kind == "render" else None
            if row["schema_version"] != "3" or destination is None:
                raise CaptureError("Incremental timing raw sample has an unknown schema or kind")
            expected_index = len(destination)
            try:
                index = int(row["index"])
            except (TypeError, ValueError) as error:
                raise CaptureError("Incremental timing raw sample index is invalid") from error
            if index != expected_index:
                raise CaptureError("Incremental timing raw samples are truncated, reordered, or duplicated")
            parsed = {}
            for field in INCREMENTAL_BENCHMARK_SCOPES:
                try:
                    value = int(row[field + "_ns"])
                except (TypeError, ValueError) as error:
                    raise CaptureError(f"Incremental timing raw {field} duration is invalid") from error
                if value < 0:
                    raise CaptureError(f"Incremental timing raw {field} duration is negative")
                parsed[field] = value / 1_000_000
            for field in INCREMENTAL_BENCHMARK_COUNTERS:
                try:
                    value = int(row[field])
                except (TypeError, ValueError) as error:
                    raise CaptureError(f"Incremental timing raw {field} counter is invalid") from error
                if value < 0:
                    raise CaptureError(f"Incremental timing raw {field} counter is negative")
            destination.append({"sample_index": index, **parsed})
    if len(step_rows) != expected_steps or len(render_rows) != expected_renders:
        raise CaptureError("Incremental timing raw sample counts differ from the requested run")
    if headless and expected_renders != 0:
        raise CaptureError("Headless incremental timing unexpectedly has render samples")
    by_render_index = {row["sample_index"]: row for row in render_rows}
    for row in step_rows:
        if row["render"] != 0.0:
            raise CaptureError("Incremental step samples contain duration attributed to the render scope")
    paired_values = []
    with paired_path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["step", "paired_step_render_ns"]:
            raise CaptureError("Incremental paired timing columns are invalid")
        for index, row in enumerate(reader):
            try:
                sample_index = int(row["step"])
                nanoseconds = int(row["paired_step_render_ns"])
            except (TypeError, ValueError) as error:
                raise CaptureError("Incremental paired timing sample is invalid") from error
            if sample_index != index or nanoseconds < 0:
                raise CaptureError("Incremental paired timing samples are negative, truncated, or reordered")
            paired_values.append(nanoseconds / 1_000_000)
    if len(paired_values) != expected_steps:
        raise CaptureError("Incremental paired timing sample count differs from the requested run")
    for row in render_rows:
        if any(row[name] != 0.0 for name in INCREMENTAL_BENCHMARK_SCOPES if name != "render"):
            raise CaptureError("Incremental render samples contain duration attributed to a non-render scope")
    rows = []
    for index, row in enumerate(step_rows):
        rows.append({"sample_index": index, "complete_step_ms": row["complete_step"],
                     "render_ms": by_render_index[index]["render"] if index in by_render_index else 0.0,
                     "paired_step_render_ms": paired_values[index]})
    return rows, paired_values


def _nearest_rank(values: list[float], percentile: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[max(0, math.ceil(percentile * len(ordered)) - 1)]


def _timing_summary(values: list[float]) -> dict:
    if not values:
        return {"mean_ms": 0.0, "median_ms": 0.0, "p95_ms": 0.0, "p99_ms": 0.0, "worst_ms": 0.0}
    return {"mean_ms": statistics.fmean(values), "median_ms": statistics.median(values),
            "p95_ms": _nearest_rank(values, 0.95), "p99_ms": _nearest_rank(values, 0.99),
            "worst_ms": max(values)}


def validate_incremental_phase_roots(phase: str, validation_root: Path | None,
                                     resource_root: Path | None, output: Path) -> None:
    if phase not in INCREMENTAL_PHASES:
        raise CaptureError(f"Unknown incremental phase: {phase}")
    if output.exists():
        raise CaptureError(f"Refusing to reuse an existing output path: {output}")
    if phase == "pilot":
        return
    if phase == "validate":
        if validation_root is None or validation_root.exists():
            raise CaptureError("Validation phase requires a fresh validation root")
        return

    if validation_root is None or resource_root is None:
        raise CaptureError(f"{phase} phase requires a validation root and a resource root")
    if phase == "resources" and resource_root.exists():
        raise CaptureError("Resource phase requires a fresh resource root")

    roots = (validation_root,) if phase == "resources" else (validation_root, resource_root)
    phase_metadata = []
    for root in roots:
        marker = root / "phase.json"
        if not root.is_dir() or not marker.is_file():
            raise CaptureError(f"{phase} phase prerequisite is missing: {root}")
        metadata = json.loads(marker.read_text(encoding="utf-8"))
        expected_phase = ("validate" if root == validation_root else "resources") if phase == "time" else \
                         "validate" if phase == "resources" else None
        if metadata.get("comparison") != "incremental" or (expected_phase and metadata.get("phase") != expected_phase) or \
                not isinstance(metadata.get("case_keys"), list):
            raise CaptureError(f"{phase} phase prerequisite is incomplete or incompatible: {root}")
        if phase == "time" and metadata.get("complete") is not True:
            raise CaptureError(f"Time phase prerequisite is incomplete: {root}")
        phase_metadata.append(metadata)
    if phase == "time":
        phase_keys = [metadata.get("case_keys") for metadata in phase_metadata]
        if phase_keys[0] != phase_keys[1] or not phase_keys[0]:
            raise CaptureError("Validation and resource phase inventories do not match")
        phase_strategies = [metadata.get("strategies") for metadata in phase_metadata]
        if phase_strategies[0] != phase_strategies[1] or phase_strategies[0] != list(INCREMENTAL_STRATEGIES):
            raise CaptureError("Validation and resource strategy inventories do not match")
        frozen_keys = [metadata.get("frozen_control_keys") for metadata in phase_metadata]
        if frozen_keys[0] != frozen_keys[1] or not isinstance(frozen_keys[0], list) or not frozen_keys[0]:
            raise CaptureError("Validation and resource frozen-control inventories do not match")


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


def _scene_sprite_population(asset_root: Path, relative_scene: str) -> int:
    scene_path = asset_root / relative_scene.removeprefix("assets/")
    document = json.loads(scene_path.read_text(encoding="utf-8"))
    population = 0

    def visit(objects):
        nonlocal population
        for object_data in objects:
            population += sum(component.get("type") == 6 for component in object_data.get("components", ()))
            visit(object_data.get("children", ()))

    visit(document)
    return population


def incremental_matrix(multiworker_count: int, singleworker_count: int = 1,
                       asset_root: Path | None = None) -> list[dict]:
    if multiworker_count <= 0 or singleworker_count <= 0:
        raise CaptureError("Incremental worker counts must be positive")
    cases = []

    def add(profile, count, *, changed=0.0, layout="profile-defined", size="standard",
            camera="fixed", output="320x240", workers=singleworker_count, headless=False):
        cases.append({"profile": profile, "sprite_count": count, "population": count,
                      "workers": workers, "headless": headless,
                      "changed_fraction": changed, "changed_percent": round(changed * 100),
                      "layout": layout, "size_distribution": size,
                      "camera_schedule": camera, "output_schedule": output})

    for count in (0, 1, 1000, 10000): add("visible", count, layout="fixed-area")
    for count in (1, 1000, 10000): add("visible-motion", count, changed=1.0, layout="fixed-area")
    for count in (1000, 10000, 50000): add("offscreen", count, layout="fixed-area")
    for count in (1000, 10000, 50000): add("spread", count, layout="constant-density-spread")
    add("sparse", 10000, layout="uniform-sparse")
    for count in (1000, 10000, 50000):
        for fraction in (0.01, 0.10, 1.0):
            add("changed", count, changed=fraction, layout="constant-density-spread")
    for count in (1000, 10000, 50000):
        add("camera-pan", count, camera="continuous-pan-240", layout="constant-density-spread")
        add("crossing", count, changed=1.0, layout="continuous-crossing")
    for count in (10000, 50000): add("clustered", count, changed=0.10, layout="fixed-area-skew")
    for count in (1000, 10000): add("churn", count, changed=0.01, layout="stable-slot-churn")
    for count in (1000, 10000): add("rotated", count, changed=0.10, size="seeded-rotated")
    add("giant", 1000, changed=0.10, size="giant-side-list")
    add("output-resize", 1000, output="320x240-then-400x300-at-240")
    add("large-offset", 1000, changed=0.01, layout="large-common-offset", camera="large-offset-pan-240")
    for count, profile in ((1000, "visible"), (10000, "changed"), (50000, "camera-pan"),
                           (50000, "crossing"), (10000, "churn")):
        add(profile, count, changed=0.01 if profile in ("changed", "churn") else 1.0 if profile == "crossing" else 0.0,
            workers=multiworker_count,
            layout={"visible": "fixed-area-visible", "changed": "constant-density-spread",
                    "camera-pan": "constant-density-spread", "crossing": "continuous-crossing",
                    "churn": "stable-slot-churn"}[profile],
            camera="continuous-pan-240" if profile == "camera-pan" else "fixed")
    for workers in sorted(set((singleworker_count, multiworker_count))):
        add("crossing", 1000, changed=1.0, workers=workers, headless=True, layout="continuous-crossing")
    scene_assets = asset_root or (Path.cwd() / "assets")
    for profile, scene in (("scene-default", "assets/scenes/default.scene"),
                           ("scene-stacking", "assets/scenes/stacking.scene")):
        population = _scene_sprite_population(scene_assets, scene)
        for workers in sorted(set((singleworker_count, multiworker_count))):
            add(profile, population, changed=0.0, layout="scene-natural", workers=workers)
    return cases


def focused_incremental_cases(selection: str | list[str], multiworker_count: int) -> list[dict]:
    names = [item.strip() for item in selection.split(",")] if isinstance(selection, str) else list(selection)
    if not names or any(not name for name in names):
        raise CaptureError("Focused comparison requires one or more named cases")
    if len(names) != len(set(names)):
        raise CaptureError("Focused case selection contains duplicates")
    unknown = [name for name in names if name not in FOCUSED_CASE_NAMES]
    if unknown:
        raise CaptureError("Unknown focused case name: " + ", ".join(unknown))
    if multiworker_count <= 1 and "crossing-multiworker" in names:
        raise CaptureError("The crossing-multiworker case requires --multiworker-count greater than one")

    specs = {
        "mostly-offscreen": ("sparse", 10000, 0.0, "uniform-sparse", "fixed", 1),
        "camera-motion": ("camera-pan", 10000, 0.0, "constant-density-spread", "continuous-pan-240", 1),
        "small-moving-subset": ("changed", 10000, 0.01, "constant-density-spread", "fixed", 1),
        "fully-moving-crossing": ("crossing", 10000, 1.0, "continuous-crossing", "fixed", 1),
        "all-visible": ("visible", 1000, 0.0, "fixed-area", "fixed", 1),
        "crossing-multiworker": ("crossing", 10000, 1.0, "continuous-crossing", "fixed", multiworker_count),
    }
    cases = []
    for name in names:
        profile, population, changed, layout, camera, workers = specs[name]
        cases.append({"pilot_name": name, "profile": profile, "sprite_count": population,
                      "population": population, "workers": workers, "headless": False,
                      "changed_fraction": changed, "changed_percent": round(changed * 100),
                      "layout": layout, "size_distribution": "standard",
                      "camera_schedule": camera, "output_schedule": "320x240"})
    return cases


def incremental_case_key(case: dict) -> str:
    encoded = json.dumps(case, sort_keys=True, separators=(",", ":")).encode("utf-8")
    digest = hashlib.sha256(encoded).hexdigest()[:12]
    return f"{case['profile']}-n{case['population']}-c{round(case['changed_fraction'] * 100)}-w{case['workers']}-{digest}"


def frozen_control_case_ids(cases: list[dict]) -> list[str]:
    required = (("visible", 1000, 1, False), ("sparse", 10000, 1, False),
                ("crossing", 10000, 1, False), ("crossing", 1000, 1, True))
    ids = []
    for profile, count, workers, headless in required:
        match = next((case for case in cases if case["profile"] == profile and
                      case["sprite_count"] == count and case["workers"] == workers and
                      case["headless"] is headless), None)
        if match is not None:
            ids.append(incremental_case_key(match))
    return ids


def validate_repetitions(repetitions: int) -> None:
    if repetitions < 5:
        raise CaptureError("Acceptance timing requires at least five independent process repetitions")


def expected_trajectory(case: dict, args) -> dict:
    profile = case["profile"]
    if profile not in TRAJECTORY_PERIOD_STEPS:
        raise CaptureError(f"Unknown incremental trajectory profile: {profile}")
    rotated = profile == "rotated"
    giant = profile == "giant"
    churn = profile == "churn"
    changed_percent = (100 if profile in ("visible-motion", "crossing") else
                       max(1, case["sprite_count"] // 100) * 100 // case["sprite_count"]
                       if profile == "churn" and case["sprite_count"] else
                       case.get("changed_percent", 0))
    scene_profile = profile in ("scene-default", "scene-stacking")
    return {"profile": case["profile"], "sprite_count": case["sprite_count"], "seed": args.seed,
            "trajectory_period_steps": TRAJECTORY_PERIOD_STEPS[profile],
            "camera_width": 640.0 if rotated or scene_profile else 320.0,
            "camera_height": 160.0 if rotated else 480.0 if scene_profile else 240.0,
            "sprite_width": None if scene_profile else 64 if rotated else 2048 if giant else 16,
            "sprite_height": None if scene_profile else 12 if rotated else 1024 if giant else 16,
            "layout": case.get("layout", "clustered-groups" if profile == "clustered" else
                               "uniform-sparse" if profile == "sparse" else
                               "constant-density-spread" if profile == "spread" else
                               "scene-natural" if scene_profile else "profile-defined"),
            "size_distribution": case.get("size_distribution", "giant-side-list" if giant else
                                          "seeded-rotated" if rotated else "standard"),
            "rotation_distribution": "scene-resolved" if scene_profile else
                                     "seeded-uniform-pi" if rotated or giant else "zero",
            "changed_percent": changed_percent,
            "output_resize_step": 240 if profile == "output-resize" else 0,
            "camera_schedule": "continuous-pan-240" if profile == "camera-pan" else
                               "large-offset-pan-240" if profile == "large-offset" else "fixed",
            "output_schedule": "320x240-then-400x300-at-240" if profile == "output-resize" else "320x240",
            "scene_asset": "assets/scenes/default.scene" if profile == "scene-default" else
                           "assets/scenes/stacking.scene" if profile == "scene-stacking" else "",
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


def _iter_render_proof_frames(proof: dict):
    frames_path = proof.get("_frames_path")
    if frames_path is None:
        # Small in-memory proofs are useful to focused unit tests.
        yield from proof.get("_frames", ())
        return
    with Path(frames_path).open(encoding="utf-8") as stream:
        for line in stream:
            yield json.loads(line)


def _frame_total(proof: dict, field: str) -> int:
    summary_key = "_submission_total" if field == "submissions" else "_oracle_visible_total"
    if summary_key in proof:
        return proof[summary_key]
    return sum(len(frame.get(field, ())) for frame in _iter_render_proof_frames(proof))


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
    frame_count = 0
    submission_total = 0
    oracle_visible_total = 0
    maximum_oracle_visible = 0
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
            frame_count += 1
            submission_total += len(frame["submissions"])
            oracle_visible_total += len(frame["oracle_visible"])
            maximum_oracle_visible = max(maximum_oracle_visible, len(frame["oracle_visible"]))
    if frame_count != args.measure_frames:
        raise CaptureError("Rendered proof frame count does not match the requested sample count")
    if case.get("headless"):
        if proof.get("representative_sha256") is not None:
            raise CaptureError("Headless proof unexpectedly claims a renderer image")
    else:
        image_path = Path(str(prefix) + ".representative.ppm")
        if not image_path.is_file() or sha256(image_path) != proof.get("representative_sha256"):
            raise CaptureError("Representative image is missing or its checksum is stale")
    proof["_frames_path"] = str(frames_path)
    proof["_frame_count"] = frame_count
    proof["_submission_total"] = submission_total
    proof["_oracle_visible_total"] = oracle_visible_total
    proof["_maximum_oracle_visible"] = maximum_oracle_visible
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
                "process_rss_after_reset_kib", "render_view_preparations", "render_eligibility_checks",
                "render_visibility_checks", "render_submissions", "actual_changed_sprite_total",
                "actual_changed_sprites_per_step_max", "actual_changed_fraction", "cold_observation",
                "capacity_growth_observation",
                "reset_observation")
    if any(name not in proof for name in required):
        raise CaptureError("Resource proof lacks allocation or retained-memory observations")
    work_counts = ("allocation_calls", "allocated_bytes", "render_view_preparations", "render_eligibility_checks",
                   "render_visibility_checks", "render_submissions")
    for name in work_counts:
        value = proof[name]
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            raise CaptureError(f"Resource proof has a negative, fractional, or invalid {name}")
    for name in ("process_rss_before_kib", "process_rss_after_kib", "process_rss_after_reset_kib"):
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
    changed_total = proof["actual_changed_sprite_total"]
    changed_per_step = proof["actual_changed_sprites_per_step_max"]
    if (not isinstance(changed_total, int) or isinstance(changed_total, bool) or changed_total < 0 or
            changed_total > case["sprite_count"] * args.measure_frames or
            not isinstance(changed_per_step, int) or isinstance(changed_per_step, bool) or
            changed_per_step < 0 or changed_per_step > case["sprite_count"]):
        raise CaptureError("Resource proof has an invalid actual changed-Sprite observation")
    changed_fraction = proof["actual_changed_fraction"]
    if (not isinstance(changed_fraction, (int, float)) or isinstance(changed_fraction, bool) or
            not math.isfinite(changed_fraction) or not 0 <= changed_fraction <= 1 or
            changed_fraction != (changed_total / (case["sprite_count"] * args.measure_frames)
                                 if case["sprite_count"] else 0.0)):
        raise CaptureError("Resource proof changed fraction is inconsistent with actual per-step observations")
    cold = proof["cold_observation"]
    reset = proof["reset_observation"]
    if (not isinstance(cold, dict) or not isinstance(reset, dict) or
            not isinstance(cold.get("first_warmup_step_elapsed_ns"), int) or
            cold["first_warmup_step_elapsed_ns"] <= 0 or
            not isinstance(cold.get("fixture_construction_and_registration_ns"), int) or
            cold["fixture_construction_and_registration_ns"] <= 0 or
            not isinstance(cold.get("fixture_construction_allocation_calls"), int) or
            cold["fixture_construction_allocation_calls"] < 0 or
            not isinstance(cold.get("fixture_construction_allocated_bytes"), int) or
            cold["fixture_construction_allocated_bytes"] < 0 or
            reset.get("records_registered_after_clear") != 0 or
            reset.get("owned_bytes_after_clear") != 0 or
            reset.get("record_capacity_after_clear") != 0 or
            reset.get("cell_capacity_after_clear") != 0 or
            not isinstance(reset.get("elapsed_ns"), int) or reset["elapsed_ns"] <= 0 or
            not isinstance(reset.get("allocation_calls"), int) or reset["allocation_calls"] < 0 or
            not isinstance(reset.get("allocated_bytes"), int) or reset["allocated_bytes"] < 0):
        raise CaptureError("Resource proof lacks separate setup/cold or full-storage-reclamation evidence")
    growth = proof["capacity_growth_observation"]
    if not isinstance(growth, dict) or not isinstance(growth.get("attempted"), bool):
        raise CaptureError("Resource proof lacks a capacity-growth observation")
    if growth["attempted"]:
        if (not isinstance(growth.get("records_added"), int) or growth["records_added"] <= 0 or
                not isinstance(growth.get("elapsed_ns"), int) or growth["elapsed_ns"] <= 0 or
                not isinstance(growth.get("allocation_calls"), int) or growth["allocation_calls"] < 0 or
                not isinstance(growth.get("allocated_bytes"), int) or growth["allocated_bytes"] < 0 or
                not isinstance(growth.get("record_capacity_after"), int) or
                growth["record_capacity_after"] <= growth.get("record_capacity_before", -1) or
                not isinstance(growth.get("owned_bytes_after"), int) or growth["owned_bytes_after"] <= 0):
            raise CaptureError("Resource proof has incomplete or inconsistent capacity-growth attribution")
    elif not isinstance(growth.get("reason"), str) or not growth["reason"]:
        raise CaptureError("Skipped capacity growth lacks an explicit reason")
    return {**proof, "_path": str(path), "_sha256": sha256(path)}


def validate_mode_evidence(render_proofs: dict, case: dict) -> None:
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
    frame_streams = [_iter_render_proof_frames(render_proofs[mode]) for mode in MODES]
    for index, group in enumerate(itertools.zip_longest(*frame_streams)):
        if any(frame is None for frame in group):
            raise CaptureError("Mode validation frame counts differ")
        reference, off, on = group
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
            observed_submissions = _frame_total(render_proofs[mode], "submissions")
            if submissions != observed_submissions:
                raise CaptureError(f"{mode} resource submission count differs from the checked membership trace")
            oracle_visible = _frame_total(render_proofs[mode], "oracle_visible")
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


def _directory_sha256(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        digest.update(b"\0")
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    return digest.hexdigest()


def _read_probe_identity(binary: Path, log_path: Path, env: dict) -> dict:
    command = [str(binary), "--print-build-identity"]
    stdout_path = log_path.with_suffix(".identity.json")
    _run(command, log_path, env, stdout_path=stdout_path)
    try:
        identity = json.loads(stdout_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise CaptureError(f"Probe did not return valid build identity JSON; see {log_path}") from error
    if not isinstance(identity, dict):
        raise CaptureError("Probe build identity must be a JSON object")
    return identity


def _incremental_case_identity(case: dict, strategy: str, args) -> dict:
    identity = {"strategy": strategy, "profile": case["profile"], "population": case["population"],
            "changed_fraction": case["changed_fraction"], "layout": case["layout"],
            "size_distribution": case["size_distribution"], "camera_schedule": case["camera_schedule"],
            "output_schedule": case["output_schedule"], "seed": args.seed, "dt": args.dt,
            "warmup_steps": args.warmup_frames, "measured_steps": args.measure_frames,
            "workers": case["workers"], "headless": case["headless"]}
    if case["profile"] in ("scene-default", "scene-stacking"):
        identity["scene_asset"] = "assets/scenes/default.scene" if case["profile"] == "scene-default" else \
                                   "assets/scenes/stacking.scene"
    return identity


def _write_incremental_identity(path: Path, case: dict, strategy: str, args,
                                render_proof: dict, proof_path: Path, binary: Path,
                                build_identity: dict, assets_hash: str) -> dict:
    identity = {"renderer": str(render_proof.get("renderer_mapping", {}).get("renderer", "none")),
                "fixture": render_proof["fixture_sha256"], "harness": render_proof["harness_sha256"],
                "source": render_proof["source_sha256"], "assets": assets_hash,
                "build": hashlib.sha256(json.dumps(build_identity, sort_keys=True).encode("utf-8")).hexdigest(),
                "binary": sha256(binary), "proof": sha256(proof_path)}
    wrapper = {"proof_kind": "platformator.incremental-render-culling", "proof_version": 1,
               "comparison": "incremental", "case": _incremental_case_identity(case, strategy, args),
               "identity": identity, "render_proof": proof_path.name, "render_proof_sha256": identity["proof"]}
    validate_incremental_case_identity(wrapper, wrapper["case"], identity)
    path.write_text(json.dumps(wrapper, indent=2) + "\n", encoding="utf-8")
    return wrapper


def _incremental_probe_command(binary: Path, case: dict, args, mode: str, csv_path: Path,
                               evidence_path: Path, *, phase: str, run_index: int = 0,
                               validation_path: Path | None = None,
                               resource_path: Path | None = None) -> list[str]:
    command = [str(binary), "--profile", case["profile"], "--sprite-count", str(case["sprite_count"]),
               "--workers", str(case["workers"]), "--seed", str(args.seed),
               "--warmup-frames", str(args.warmup_frames), "--measure-frames", str(args.measure_frames),
               "--dt", repr(args.dt), "--repetitions", str(args.repetitions), "--run-index", str(run_index),
               "--csv-output", str(csv_path), "--evidence-output", str(evidence_path),
               "--changed-percent", str(case["changed_percent"]), "--layout", case["layout"],
               "--size-distribution", case["size_distribution"]]
    if mode in INCREMENTAL_STRATEGIES:
        command += ["--render-strategy", mode]
    elif mode == "disabled":
        command.append("--no-render-culling")
    else:
        raise CaptureError(f"Unknown incremental probe strategy: {mode}")
    if phase == "validate":
        command.append("--validate-rendering")
    elif phase == "resources":
        command.append("--validate-resources")
    elif phase == "time":
        if validation_path is None or resource_path is None:
            raise CaptureError("Timing probe requires matching validation and resource proofs")
        command += ["--validation-proof", str(validation_path), "--resource-proof", str(resource_path)]
    else:
        raise CaptureError(f"Unknown incremental phase: {phase}")
    if case.get("headless"):
        command.append("--headless")
    if args.asset_root:
        command += ["--asset-root", str(args.asset_root)]
    return command


def validate_incremental_render_group(proofs: dict, case: dict, args) -> None:
    required_modes = set(INCREMENTAL_STRATEGIES) | {"disabled"}
    if set(proofs) != required_modes:
        raise CaptureError("Incremental rendered group is missing one or more strategies or submit-all reference")
    candidate_modes = tuple(mode for mode in INCREMENTAL_STRATEGIES)
    frame_modes = ("disabled", *candidate_modes)
    frame_streams = [_iter_render_proof_frames(proofs[mode]) for mode in frame_modes]
    for frame_index, group in enumerate(itertools.zip_longest(*frame_streams)):
        if any(frame is None for frame in group):
            raise CaptureError("Incremental rendered frame count differs from submit-all")
        all_sprites, *actual_frames = group
        position = None
        for mode, actual in zip(candidate_modes, actual_frames):
            if not case.get("headless") and actual.get("image_fnv64") != all_sprites.get("image_fnv64"):
                raise ImageMismatch(frame_index)
            selected = [tuple(identity) for identity in actual["submissions"]]
            if selected:
                if position is None:
                    position = {tuple(identity): index for index, identity in enumerate(all_sprites["submissions"])}
                last = -1
                for identity in selected:
                    current = position.get(identity)
                    if current is None or current <= last:
                        raise CaptureError(f"{mode} has an identity/order mismatch at frame {frame_index}")
                    last = current
            if not {tuple(identity) for identity in actual["oracle_visible"]} <= set(selected):
                raise CaptureError(f"{mode} omitted an independent oracle-visible identity at frame {frame_index}")
            if case["output_schedule"] != "320x240" and not case.get("headless"):
                expected_size = (400, 300) if case["output_schedule"] == "320x240-then-400x300-at-240" and \
                    args.warmup_frames + frame_index >= 240 else (320, 240)
                if (actual.get("output_width"), actual.get("output_height")) != expected_size:
                    raise CaptureError(f"{mode} output trajectory differs from its declared schedule")
    if case["profile"] in ("crossing", "camera-pan") and not case.get("headless"):
        for mode in INCREMENTAL_STRATEGIES:
            counts = proofs[mode].get("crossing_counts", {})
            if counts.get("entries", 0) == 0 or counts.get("exits", 0) == 0:
                raise CaptureError(f"{mode} dynamic proof did not observe both boundary entries and exits")


def validate_frozen_control_group(frozen: dict, candidate: dict, case: dict) -> None:
    if set(frozen) != {"current-linear", "disabled"}:
        raise CaptureError("Frozen feature-008 controls are missing a strategy")
    for mode in ("current-linear", "disabled"):
        baseline = frozen[mode]
        current = candidate[mode]
        if baseline.get("fixture_sha256") != current.get("fixture_sha256") or \
                baseline.get("harness_sha256") != current.get("harness_sha256"):
            raise CaptureError("Frozen and candidate controls do not share the audited capture harness")
        if baseline.get("source_sha256") == current.get("source_sha256"):
            raise CaptureError("Frozen feature-008 control does not identify a distinct engine source")
        if baseline.get("renderer_mapping") != current.get("renderer_mapping") or \
                baseline.get("trajectory_parameters") != current.get("trajectory_parameters"):
            raise CaptureError("Frozen and candidate controls do not share the same rendered case identity")
        frame_streams = (_iter_render_proof_frames(baseline), _iter_render_proof_frames(current))
        for index, (frozen_frame, candidate_frame) in enumerate(itertools.zip_longest(*frame_streams)):
            if frozen_frame is None or candidate_frame is None:
                raise CaptureError("Frozen and candidate control frame counts differ")
            if not case.get("headless") and frozen_frame.get("image_fnv64") != candidate_frame.get("image_fnv64"):
                raise ImageMismatch(index)
            if (frozen_frame.get("submissions") != candidate_frame.get("submissions") or
                    frozen_frame.get("oracle_visible") != candidate_frame.get("oracle_visible")):
                raise CaptureError(f"Frozen and candidate {mode} controls differ at frame {index}")


def validate_incremental_resource_group(resources: dict, case: dict, render_proofs: dict | None = None) -> None:
    required_modes = set(INCREMENTAL_STRATEGIES) | {"disabled"}
    if set(resources) != required_modes:
        raise CaptureError("Incremental resource group is missing a strategy or submit-all reference")
    disabled = resources["disabled"]
    observed_changes = {resources[mode]["actual_changed_sprite_total"] for mode in required_modes}
    if len(observed_changes) != 1:
        raise CaptureError("Incremental strategies observed different fixture geometry changes")
    if case["profile"] not in ("scene-default", "scene-stacking"):
        expected_per_step = (max(1, case["sprite_count"] // 100) if case["profile"] == "churn" and case["sprite_count"]
                             else round(case["sprite_count"] * case["changed_fraction"]))
        expected_total = expected_per_step * resources["disabled"]["measure_frames"]
        if next(iter(observed_changes)) != expected_total:
            raise CaptureError(f"Fixture changed fraction differs from actual Sprite observations for {case['profile']}")
    for mode in INCREMENTAL_STRATEGIES:
        proof = resources[mode]
        if proof["allocation_calls"] != disabled["allocation_calls"] or \
                proof["allocated_bytes"] != disabled["allocated_bytes"]:
            raise CaptureError(f"{mode} added warmed C++ allocations over submit-all")
        initial = proof.get("culling_initial", {})
        work = proof.get("culling_work", {})
        if case.get("headless") or mode == "current-linear":
            if any(initial.get(name, 0) for name in ("state_creations", "records_registered")) or any(
                    work.get(name, 0) for name in ("dirty_enqueues", "bounds_refreshes", "queries",
                                                  "index_insertions", "peak_owned_bytes")):
                raise CaptureError(f"{mode} allocated or performed inactive/cached culling bookkeeping")
        elif case["sprite_count"] == 0:
            if initial.get("records_registered") != 0 or initial.get("state_creations", 0) not in (0, 1):
                raise CaptureError(f"{mode} empty case unexpectedly registered cache records")
        elif initial.get("state_creations") != 1 or initial.get("records_registered") != case["sprite_count"]:
            raise CaptureError(f"{mode} cache registration counts do not match its live Sprite population")
        if render_proofs is not None:
            observed = _frame_total(render_proofs[mode], "submissions")
            if proof["render_submissions"] != observed:
                raise CaptureError(f"{mode} resource submission count differs from its image/order proof")


def _validation_probe_identity(binary: Path, expected_proof: dict, log_path: Path, env: dict) -> dict:
    identity = _read_probe_identity(binary, log_path, env)
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


def capture_incremental_mismatch_images(args, case: dict, frame: int, output: Path, logs: Path, env: dict,
                                        candidate_proofs: dict, frozen_proofs: dict | None = None) -> list[str]:
    if case.get("headless"):
        raise CaptureError("Headless incremental validation has no mismatch image to recover")
    mismatch_dir = output / "mismatches" / incremental_case_key(case)
    mismatch_dir.mkdir(parents=True, exist_ok=False)
    candidate_modes = [mode for mode in (*INCREMENTAL_STRATEGIES, "disabled") if mode in candidate_proofs]
    if "disabled" not in candidate_modes or not set(INCREMENTAL_STRATEGIES) <= set(candidate_modes):
        raise CaptureError("Incremental mismatch recovery lacks all candidate strategy proofs")
    expected_candidate = candidate_proofs["current-linear"]
    for mode in candidate_modes:
        proof = candidate_proofs[mode]
        if any(proof.get(field) != expected_candidate.get(field)
               for field in ("fixture_sha256", "harness_sha256", "source_sha256")):
            raise CaptureError("Incremental mismatch recovery candidate proofs use mixed identities")
    candidate_identity = _validation_probe_identity(
        args.candidate_validation_probe, expected_candidate,
        logs / f"mismatch-{incremental_case_key(case)}-candidate-validation-identity.log", env)
    jobs = [(f"candidate-{mode}", args.candidate_validation_probe, mode, candidate_proofs[mode])
            for mode in candidate_modes]
    recovery_identities = {"candidate": candidate_identity}
    if frozen_proofs:
        for mode in ("current-linear", "disabled"):
            if mode not in frozen_proofs:
                continue
            proof = frozen_proofs[mode]
            if (proof.get("fixture_sha256") != expected_candidate.get("fixture_sha256") or
                    proof.get("harness_sha256") != expected_candidate.get("harness_sha256") or
                    proof.get("source_sha256") == expected_candidate.get("source_sha256")):
                raise CaptureError("Incremental mismatch recovery frozen proof has an incompatible identity")
            jobs.append((f"frozen-{mode}", args.baseline_validation_probe, mode, proof))
        if any(mode not in frozen_proofs for mode in ("current-linear", "disabled")):
            raise CaptureError("Incremental mismatch recovery has an incomplete frozen-control proof set")
        recovery_identities["frozen-feature008"] = _validation_probe_identity(
            args.baseline_validation_probe, frozen_proofs["current-linear"],
            logs / f"mismatch-{incremental_case_key(case)}-frozen-validation-identity.log", env)

    saved = []
    for label, binary, mode, proof in jobs:
        prefix = mismatch_dir / label
        csv_path = Path(str(prefix) + ".csv")
        command = _incremental_probe_command(
            binary, case, args, mode, csv_path, prefix, phase="validate", run_index=0)
        command += ["--capture-frame-index", str(frame)]
        log_path = logs / f"mismatch-{incremental_case_key(case)}-{label}.log"
        _run(command, log_path, env)
        image_path = Path(str(prefix) + ".mismatch.ppm")
        if not image_path.is_file():
            raise CaptureError(f"Incremental mismatch image was not written for {label}")
        saved.append({"mode": label, "image": str(image_path), "sha256": sha256(image_path),
                      "log": str(log_path), "source_sha256": proof["source_sha256"]})
    (mismatch_dir / "mismatch.json").write_text(
        json.dumps({"case": _incremental_case_identity(case, "mismatch", args), "frame": frame,
                    "images": saved, "validation_probe_identities": recovery_identities},
                   indent=2) + "\n", encoding="utf-8")
    return [item["image"] for item in saved]


def preserve_image_mismatch(mismatch: ImageMismatch, recovery) -> None:
    try:
        recovery()
    except Exception as recovery_error:
        raise ImageMismatch(mismatch.frame, str(recovery_error)) from mismatch
    raise mismatch


def _run(command: list[str], log_path: Path, env: dict, *, stdout_path: Path | None = None) -> None:
    check_budget()
    with log_path.open("x", encoding="utf-8") as log:
        log.write("COMMAND: " + json.dumps(command) + "\n")
        log.flush()
        # Identity JSON stays separate from stderr diagnostics; both files count toward the budget.
        with stdout_path.open("x", encoding="utf-8") if stdout_path else nullcontext(log) as output:
            process = subprocess.Popen(command, stdout=output,
                                       stderr=log if stdout_path else subprocess.STDOUT, env=env,
                                       start_new_session=(os.name == "posix"))
            try:
                return_code = wait_for_process(process)
            except BaseException:
                _stop_owned_process(process)
                raise
    if return_code != 0:
        raise CaptureError(f"Capture process failed ({return_code}); see {log_path}")


def _stop_owned_process(process: subprocess.Popen) -> None:
    try:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGINT)
        elif process.poll() is None:
            process.send_signal(signal.SIGINT)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass
    try:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGTERM)
        elif process.poll() is None:
            process.terminate()
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass
    if os.name == "posix":
        time.sleep(0.1)
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    elif process.poll() is None:
        try:
            process.kill()
        except ProcessLookupError:
            pass
    process.wait()


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


def engine_control_matrix(multiworker_count: int, include_rendered_multiworker: bool = False) -> list[dict]:
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
        workers = (1, multiworker_count) if not rendered or include_rendered_multiworker else (1,)
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
                        expected_sources: dict[str, str],
                        include_rendered_multiworker: bool = False) -> list[dict]:
    controls = engine_control_matrix(args.multiworker_count, include_rendered_multiworker)
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
                         "csv_sha256": sha256(csv_path),
                         "samples_csv": str(csv_path.with_suffix(".samples.csv")),
                         "manifest_sha256": sha256(csv_path.with_suffix(".manifest.json")),
                         "samples_sha256": sha256(csv_path.with_suffix(".samples.csv")), "log": str(log_path)})
    return rows


def parser() -> argparse.ArgumentParser:
    command = argparse.ArgumentParser(description=__doc__)
    for name in ("baseline-probe", "candidate-probe", "baseline-runner", "candidate-runner",
                 "baseline-validation-probe", "candidate-validation-probe", "validation-root", "resource-root"):
        command.add_argument("--" + name, type=Path)
    command.add_argument("--output", type=Path, required=True)
    command.add_argument("--workers", type=int, default=1)
    command.add_argument("--multiworker-count", type=int, required=True)
    command.add_argument("--seed", type=int, default=0)
    command.add_argument("--warmup-frames", type=int, default=5)
    command.add_argument("--measure-frames", type=int, default=30)
    command.add_argument("--repetitions", type=int, default=3)
    command.add_argument("--dt", type=float, default=0.008333333333333333)
    command.add_argument("--asset-root", type=Path)
    command.add_argument("--comparison", choices=("legacy", "incremental"), default="incremental")
    command.add_argument("--phase", choices=INCREMENTAL_PHASES,
                         help="Defaults to compact pilot for incremental comparisons")
    command.add_argument("--strategy", choices=(*INCREMENTAL_STRATEGIES, "all"))
    command.add_argument("--focused-cases", default=",".join(FOCUSED_CASE_NAMES[:5]),
                         help="Comma-separated exploratory pilot cases")
    command.add_argument("--exploratory", action="store_true",
                         help="Compatibility flag; pilots are always labeled exploratory, not acceptance evidence")
    command.add_argument("--allow-full-campaign", action="store_true",
                         help="Explicitly enable historical non-pilot campaigns and potentially large traces")
    command.add_argument("--max-output-mib", type=float, default=50,
                         help="Combined new output budget in MiB (default: 50); always positive")
    command.add_argument("--max-seconds", type=float, default=600,
                         help="Whole capture time budget in seconds (default: 600); always positive")
    command.add_argument("--case-limit", type=int,
                         help="Run only the first N incremental cases; partial roots cannot be used for acceptance timing")
    return command


def _validate_incremental_timing_manifest(csv_path: Path, case: dict, args, strategy: str,
                                          render_proof: dict, resource_proof: dict,
                                          binary_identity: dict, binary: Path, run_index: int,
                                          metrics: dict) -> dict:
    manifest_path = csv_path.with_suffix(".manifest.json")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    build = manifest.get("build", {})
    if (build.get("configuration", "").lower() != "release" or
            build.get("sanitizer") not in ("none", "") or build.get("work_probes") is not False or
            build.get("debug_tools") is not False):
        raise CaptureError("Incremental timing requires an uninstrumented Release binary")
    expected_fields = {"scenario": "linear_render_culling", "workers": case["workers"],
                       "seed": args.seed, "dt": args.dt, "warmup_steps": args.warmup_frames,
                       "measured_steps": args.measure_frames, "requested_measured_steps": args.measure_frames,
                       "repetitions": args.repetitions, "repetition_index": run_index}
    for field, expected in expected_fields.items():
        if manifest.get(field) != expected:
            raise CaptureError(f"Incremental timing manifest has wrong {field}")
    if (manifest.get("timing_eligible") is not True or manifest.get("validation_mode") != "boundary-only" or
            manifest.get("stopped_on_quit") is not False or manifest.get("complete_capture") is not True or
            manifest.get("total_steps") != args.measure_frames):
        raise CaptureError("Incremental timing manifest is incomplete or ineligible")
    if manifest.get("resolved_parameters") != render_proof.get("trajectory_parameters"):
        raise CaptureError("Incremental timing trajectory differs from the matching rendering proof")
    culling = manifest.get("linear_render_culling", {})
    expected_policy = "headless-no-rendering" if case.get("headless") else strategy
    expected_enabled = strategy in INCREMENTAL_STRATEGIES and not case.get("headless")
    if (culling.get("mode") != strategy or culling.get("requested_mode") != strategy or
            culling.get("effective_policy") != expected_policy or culling.get("enabled") is not expected_enabled or
            manifest.get("requested_render_strategy") != strategy):
        raise CaptureError("Incremental timing requested/effective strategy differs from the case")
    expected_renders = 0 if case.get("headless") else args.measure_frames
    if (manifest.get("rendering") is not (not case.get("headless")) or
            manifest.get("render_samples") != expected_renders or
            manifest.get("total_render_samples") != expected_renders):
        raise CaptureError("Incremental timing render sample count is wrong")
    if build.get("source_sha256") != binary_identity.get("source_sha256"):
        raise CaptureError("Incremental timing binary identity differs for source_sha256")
    for field in ("fixture_sha256", "harness_sha256"):
        if manifest.get(field) != binary_identity.get(field):
            raise CaptureError(f"Incremental timing binary identity differs for {field}")
    if build.get("source_sha256") != render_proof.get("source_sha256") or \
            build.get("source_sha256") != resource_proof.get("source_sha256"):
        raise CaptureError("Incremental timing source differs from its validation/resource proofs")
    if manifest.get("fixture_sha256") != render_proof.get("fixture_sha256") or \
            manifest.get("harness_sha256") != render_proof.get("harness_sha256") or \
            manifest.get("fixture_sha256") != resource_proof.get("fixture_sha256") or \
            manifest.get("harness_sha256") != resource_proof.get("harness_sha256"):
        raise CaptureError("Incremental timing fixture/harness differs from its proofs")
    if manifest.get("renderer_mapping") != render_proof.get("renderer_mapping") or \
            manifest.get("renderer_mapping") != resource_proof.get("renderer_mapping"):
        raise CaptureError("Incremental timing output/backend differs from its validation/resource proofs")
    validation_observation = manifest.get("validation_proof", {})
    resource_observation = manifest.get("resource_observation", {})
    if validation_observation.get("sha256") != sha256(Path(render_proof["_path"])):
        raise CaptureError("Incremental timing does not name its matching rendering proof")
    if resource_observation.get("sha256") != resource_proof.get("_sha256"):
        raise CaptureError("Incremental timing does not name its matching resource proof")
    if build.get("source_sha256") != binary_identity.get("source_sha256"):
        raise CaptureError("Incremental timing source identity differs from the requested timing binary")
    render_samples = manifest.get("render_samples")
    if metrics["scope.render.avg"] < 0 or (render_samples == 0 and metrics["scope.render.avg"] != 0):
        raise CaptureError("Incremental timing render summary is invalid")
    return manifest


def _validate_focused_timing_manifest(csv_path: Path, case: dict, args, strategy: str,
                                      resource_proof: dict, binary_identity: dict,
                                      metrics: dict) -> dict:
    manifest = json.loads(csv_path.with_suffix(".manifest.json").read_text(encoding="utf-8"))
    build = manifest.get("build", {})
    if (build.get("configuration", "").lower() != "release" or
            build.get("sanitizer") not in ("none", "") or build.get("work_probes") is not False or
            build.get("debug_tools") is not False):
        raise CaptureError("Focused timing requires an uninstrumented Release binary")
    expected = {"scenario": "linear_render_culling", "workers": case["workers"],
                "seed": args.seed, "dt": args.dt, "warmup_steps": args.warmup_frames,
                "measured_steps": args.measure_frames,
                "requested_measured_steps": args.measure_frames,
                "repetitions": args.repetitions, "repetition_index": args._pilot_run_index}
    for field, value in expected.items():
        if manifest.get(field) != value:
            raise CaptureError(f"Focused timing manifest has wrong {field}")
    if (manifest.get("timing_eligible") is not True or manifest.get("validation_mode") != "boundary-only" or
            manifest.get("stopped_on_quit") is not False or manifest.get("complete_capture") is not True or
            manifest.get("total_steps") != args.measure_frames):
        raise CaptureError("Focused timing manifest is incomplete or ineligible")
    if manifest.get("resolved_parameters") != resource_proof.get("trajectory_parameters"):
        raise CaptureError("Focused timing trajectory differs from its resource observation")
    culling = manifest.get("linear_render_culling", {})
    if (culling.get("mode") != strategy or culling.get("requested_mode") != strategy or
            culling.get("effective_policy") != strategy or culling.get("enabled") is not True or
            manifest.get("requested_render_strategy") != strategy):
        raise CaptureError("Focused timing requested/effective strategy differs from the case")
    if (manifest.get("rendering") is not True or manifest.get("render_samples") != args.measure_frames or
            manifest.get("total_render_samples") != args.measure_frames):
        raise CaptureError("Focused timing render sample count is wrong")
    if (build.get("source_sha256") != binary_identity.get("source_sha256") or
            build.get("source_sha256") != resource_proof.get("source_sha256")):
        raise CaptureError("Focused timing binary identity differs for source_sha256")
    for field in ("fixture_sha256", "harness_sha256"):
        if (manifest.get(field) != binary_identity.get(field) or
                manifest.get(field) != resource_proof.get(field)):
            raise CaptureError(f"Focused timing binary identity differs for {field}")
    if (manifest.get("fixture_sha256") != resource_proof.get("fixture_sha256") or
            manifest.get("harness_sha256") != resource_proof.get("harness_sha256") or
            manifest.get("renderer_mapping") != resource_proof.get("renderer_mapping")):
        raise CaptureError("Focused timing backend or harness differs from its resource proof")
    observation = manifest.get("resource_observation", {})
    if observation.get("sha256") != resource_proof.get("_sha256"):
        raise CaptureError("Focused timing does not name its checked resource proof")
    if metrics["scope.render.avg"] < 0 or metrics["scope.complete_step.avg"] < 0:
        raise CaptureError("Focused timing summary contains a negative duration")
    return manifest


def _run_incremental_pilot(args, selection: str) -> dict:
    if args.case_limit is not None:
        raise CaptureError("The pilot phase uses --focused-cases, not --case-limit")
    if args.repetitions <= 0 or args.warmup_frames <= 0:
        raise CaptureError("Exploratory pilot requires positive repetitions and at least one warmup step")
    if (args.multiworker_count <= 0 or args.workers <= 0 or args.seed < 0 or args.measure_frames <= 0 or
            not math.isfinite(args.dt) or args.dt <= 0):
        raise CaptureError("Invalid worker, seed, frame, or timestep arguments")
    if not args.candidate_probe or not args.candidate_validation_probe:
        raise CaptureError("Pilot requires --candidate-probe and --candidate-validation-probe")
    binaries = (args.candidate_probe, args.candidate_validation_probe)
    if any(not path.is_file() or not os.access(path, os.X_OK) for path in binaries):
        raise CaptureError("Pilot timing and validation probe paths must be executable files")
    asset_root = args.asset_root or (Path.cwd() / "assets")
    if not asset_root.is_dir():
        raise CaptureError(f"Asset root does not exist: {asset_root}")
    cases = focused_incremental_cases(args.focused_cases or selection, args.multiworker_count)
    validate_incremental_phase_roots("pilot", None, None, args.output)
    args.output.mkdir(parents=True)
    logs = args.output / "logs"
    logs.mkdir()
    resources_root = args.output / "resources"
    resources_root.mkdir()
    raw_root = args.output / "raw"
    raw_root.mkdir()
    env = os.environ.copy()
    env.update({"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy", "SDL_RENDER_DRIVER": "software"})

    def terminate_as_interrupt(signum, _frame):
        raise KeyboardInterrupt(f"received signal {signum}")

    old_sigterm = signal.signal(signal.SIGTERM, terminate_as_interrupt)
    try:
        validation_identity = _read_probe_identity(
            args.candidate_validation_probe, logs / "candidate-validation-identity.log", env)
        timing_identity = _read_probe_identity(args.candidate_probe, logs / "candidate-timing-identity.log", env)
        if (validation_identity.get("work_probes") is not True or
                validation_identity.get("configuration", "").lower() != "release" or
                validation_identity.get("sanitizer") not in ("none", "")):
            raise CaptureError("Pilot resource probe must be a work-probe Release build without sanitizers")
        if (timing_identity.get("work_probes") is not False or
                timing_identity.get("configuration", "").lower() != "release" or
                timing_identity.get("sanitizer") not in ("none", "") or
                timing_identity.get("debug_tools") is not False):
            raise CaptureError("Pilot timing probe must be an uninstrumented Release build")
        for field in ("source_sha256", "fixture_sha256", "harness_sha256"):
            if validation_identity.get(field) != timing_identity.get(field):
                raise CaptureError(f"Pilot timing and validation builds differ for {field}")
        assets_hash = _directory_sha256(asset_root)
        resource_inventory = []
        resource_by_case = {}
        for case in cases:
            case_id = incremental_case_key(case)
            case_root = resources_root / case_id
            case_root.mkdir()
            mode_proofs = {}
            for mode in (*INCREMENTAL_STRATEGIES, "disabled"):
                prefix = case_root / f"{case_key(case)}-{mode}"
                command = _incremental_probe_command(
                    args.candidate_validation_probe, case, args, mode,
                    Path(str(prefix) + ".csv"), prefix, phase="resources", run_index=0)
                _run(command, logs / f"resource-{case_id}-{mode}.log", env)
                proof = validate_resource_proof(case_root, case, args, mode)
                if (proof.get("source_sha256") != validation_identity.get("source_sha256") or
                        proof.get("fixture_sha256") != validation_identity.get("fixture_sha256") or
                        proof.get("harness_sha256") != validation_identity.get("harness_sha256")):
                    raise CaptureError("Pilot resource proof differs from its validation build identity")
                mode_proofs[mode] = proof
            validate_incremental_resource_group(mode_proofs, case)
            if len({json.dumps(mode_proofs[mode]["renderer_mapping"], sort_keys=True)
                    for mode in mode_proofs}) != 1:
                raise CaptureError("Pilot resource modes used different renderer mappings")
            resource_by_case[case_id] = mode_proofs
            resource_inventory.append({
                "case_id": case_id, "case": case,
                "modes": {mode: {"path": proof["_path"], "sha256": proof["_sha256"],
                                 "allocation_calls": proof["allocation_calls"],
                                 "allocated_bytes": proof["allocated_bytes"],
                                 "cold_observation": proof["cold_observation"],
                                 "capacity_growth_observation": proof["capacity_growth_observation"],
                                 "reset_observation": proof["reset_observation"],
                                 "culling_initial": proof["culling_initial"],
                                 "culling_work": proof["culling_work"]}
                          for mode, proof in mode_proofs.items()}})

        rng = random.Random(args.seed ^ 0x9E3779B9)
        run_rows = []
        raw_index = []
        host_environment = None
        jobs = [(case, strategy) for case in cases for strategy in INCREMENTAL_STRATEGIES]
        for run_index in range(args.repetitions):
            schedule = list(jobs)
            rng.shuffle(schedule)
            for case, strategy in schedule:
                case_id = incremental_case_key(case)
                resource = resource_by_case[case_id][strategy]
                prefix_name = f"{case_id}-{strategy}-r{run_index}"
                csv_path = raw_root / f"{prefix_name}.csv"
                prefix = raw_root / prefix_name
                command = [str(args.candidate_probe), "--profile", case["profile"],
                           "--sprite-count", str(case["sprite_count"]),
                           "--workers", str(case["workers"]), "--seed", str(args.seed),
                           "--warmup-frames", str(args.warmup_frames),
                           "--measure-frames", str(args.measure_frames), "--dt", repr(args.dt),
                           "--repetitions", str(args.repetitions), "--run-index", str(run_index),
                           "--csv-output", str(csv_path), "--render-strategy", strategy,
                           "--layout", case["layout"], "--changed-percent", str(case["changed_percent"]),
                           "--size-distribution", case["size_distribution"],
                           "--resource-proof", resource["_path"]]
                if args.asset_root:
                    command += ["--asset-root", str(args.asset_root)]
                _run(command, logs / f"time-{prefix_name}.log", env)
                metrics = parse_benchmark_csv(csv_path)
                args._pilot_run_index = run_index
                manifest = _validate_focused_timing_manifest(
                    csv_path, case, args, strategy, resource, timing_identity, metrics)
                observed_environment = {"machine": manifest.get("machine"),
                                        "cpu_affinity": manifest.get("affinity")}
                if host_environment is None:
                    host_environment = observed_environment
                elif observed_environment != host_environment:
                    raise CaptureError("Pilot timing runs used different host or CPU affinity identities")
                samples, paired = read_incremental_timing_samples(
                    csv_path.with_suffix(".samples.csv"), csv_path.with_suffix(".paired.csv"),
                    args.measure_frames, args.measure_frames, False)
                if len(samples) != args.measure_frames or len(paired) != args.measure_frames:
                    raise CaptureError("Pilot timing sample files are truncated")
                row = {"case_id": case_id, "pilot_name": case["pilot_name"],
                       "strategy": strategy, "run_index": run_index,
                       "render_mean_ms": metrics["scope.render.avg"],
                       "render_p95_ms": metrics["scope.render.p95"],
                       "render_p99_ms": metrics["scope.render.p99"],
                       "render_worst_ms": metrics["scope.render.max"],
                       "complete_step_mean_ms": metrics["scope.complete_step.avg"],
                       "complete_step_p95_ms": metrics["scope.complete_step.p95"],
                       "complete_step_p99_ms": metrics["scope.complete_step.p99"],
                       "complete_step_worst_ms": metrics["scope.complete_step.max"],
                       "paired_step_render_mean_ms": statistics.fmean(paired),
                       "paired_p95_ms": _nearest_rank(paired, .95),
                       "paired_p99_ms": _nearest_rank(paired, .99),
                       "paired_worst_ms": max(paired),
                       "samples": len(samples), "samples_sha256": sha256(csv_path.with_suffix(".samples.csv")),
                       "paired_sha256": sha256(csv_path.with_suffix(".paired.csv")),
                       "manifest": str(csv_path.with_suffix(".manifest.json")),
                       "manifest_sha256": sha256(csv_path.with_suffix(".manifest.json")),
                       "resource_proof": resource["_path"], "resource_proof_sha256": resource["_sha256"],
                       "source_sha256": manifest["build"]["source_sha256"],
                       "binary_sha256": sha256(args.candidate_probe),
                       "renderer_mapping": manifest["renderer_mapping"],
                       "csv": str(csv_path), "log": str(logs / f"time-{prefix_name}.log")}
                run_rows.append(row)
                raw_index.append({"case_id": case_id, "strategy": strategy, "run_index": run_index,
                                  "csv": str(csv_path), "csv_sha256": sha256(csv_path),
                                  "samples": str(csv_path.with_suffix(".samples.csv")),
                                  "samples_sha256": row["samples_sha256"],
                                  "paired": str(csv_path.with_suffix(".paired.csv")),
                                  "paired_sha256": row["paired_sha256"],
                                  "manifest": row["manifest"], "manifest_sha256": row["manifest_sha256"],
                                  "log": row["log"]})
        for case in cases:
            case_id = incremental_case_key(case)
            for strategy in INCREMENTAL_STRATEGIES:
                selected = sorted((row for row in run_rows
                                   if row["case_id"] == case_id and row["strategy"] == strategy),
                                  key=lambda row: row["run_index"])
                if len(selected) != args.repetitions or [row["run_index"] for row in selected] != list(range(args.repetitions)):
                    raise CaptureError(f"Pilot timing is missing runs for {case_id} {strategy}")
                validate_incremental_timing_samples(
                    [{"case_key": case_id, "run_index": index, "render_ms": row["render_mean_ms"],
                      "complete_step_ms": row["complete_step_mean_ms"],
                      "paired_step_render_ms": row["paired_step_render_mean_ms"]}
                     for index, row in enumerate(selected)],
                    [row["paired_step_render_mean_ms"] for row in selected], args.repetitions,
                    timing_identity)
        aggregates = {}
        differences = {}
        for case in cases:
            case_id = incremental_case_key(case)
            aggregates[case_id] = {}
            for strategy in INCREMENTAL_STRATEGIES:
                selected = [row for row in run_rows if row["case_id"] == case_id and row["strategy"] == strategy]
                aggregates[case_id][strategy] = {
                    field: _timing_summary([row[source] for row in selected])
                    for field, source in (("render_process_mean_ms", "render_mean_ms"),
                                          ("complete_step_process_mean_ms", "complete_step_mean_ms"),
                                          ("paired_step_render_process_mean_ms", "paired_step_render_mean_ms"))}
                aggregates[case_id][strategy]["render_frame_tails_ms"] = {
                    "p95_by_process": [row["render_p95_ms"] for row in selected],
                    "p99_by_process": [row["render_p99_ms"] for row in selected],
                    "worst_by_process": [row["render_worst_ms"] for row in selected]}
            differences[case_id] = {}
            for left, right in (("cached-linear", "current-linear"), ("indexed", "cached-linear"),
                                ("indexed", "current-linear")):
                left_rows = {row["run_index"]: row for row in run_rows
                             if row["case_id"] == case_id and row["strategy"] == left}
                right_rows = {row["run_index"]: row for row in run_rows
                              if row["case_id"] == case_id and row["strategy"] == right}
                differences[case_id][f"{left}_vs_{right}"] = {
                    field: [left_rows[index][field] - right_rows[index][field]
                            for index in range(args.repetitions)]
                    for field in ("render_mean_ms", "complete_step_mean_ms", "paired_step_render_mean_ms")}
        (raw_root / "raw-index.json").write_text(json.dumps(raw_index, indent=2) + "\n", encoding="utf-8")
        report = {"proof_kind": "platformator.incremental-pilot", "proof_version": 1,
                  "comparison": "incremental", "phase": "pilot", "complete": True,
                  "exploratory": True,
                  "acceptance": "No feature acceptance claim; no framewise parity/frozen-control proof; SC-006 remains open until evidence supports the indexed incremental benefit.",
                  "scope": {"focused_cases": [case["pilot_name"] for case in cases],
                            "strategies": list(INCREMENTAL_STRATEGIES), "resource_modes": [*INCREMENTAL_STRATEGIES, "disabled"],
                            "repetitions": args.repetitions, "warmup_steps": args.warmup_frames,
                            "measured_steps": args.measure_frames, "seed": args.seed, "dt": args.dt,
                            "workers": args.workers, "multiworker_count": args.multiworker_count,
                            "per_frame_identity_traces": False, "frozen_feature008_controls": False,
                            "assets_sha256": assets_hash,
                            "candidate_source_sha256": timing_identity["source_sha256"],
                            "candidate_fixture_sha256": timing_identity["fixture_sha256"],
                            "candidate_harness_sha256": timing_identity["harness_sha256"],
                            "candidate_timing_binary": str(args.candidate_probe.resolve()),
                            "candidate_timing_binary_sha256": sha256(args.candidate_probe),
                            "candidate_validation_binary": str(args.candidate_validation_probe.resolve()),
                            "candidate_validation_binary_sha256": sha256(args.candidate_validation_probe),
                            "validation_build_identity": validation_identity,
                            "timing_build_identity": timing_identity,
                            "capture_driver": str(Path(__file__).resolve()),
                            "capture_driver_sha256": sha256(Path(__file__)),
                            "host_environment": host_environment,
                            "renderer_mapping_by_case": {incremental_case_key(case):
                                resource_by_case[incremental_case_key(case)]["current-linear"]["renderer_mapping"]
                                for case in cases}},
                  "resources": resource_inventory, "timing_runs": run_rows,
                  "aggregates_by_case": aggregates, "process_differences_by_case": differences,
                  "raw_index": str(raw_root / "raw-index.json"),
                  "raw_index_sha256": sha256(raw_root / "raw-index.json")}
        (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        return report
    finally:
        signal.signal(signal.SIGTERM, old_sigterm)


def _incremental_phase_inventory(root: Path, marker: dict, phase: str, cases: list[dict],
                                 strategies: list[str], args) -> list[dict]:
    if marker.get("phase") != phase:
        raise CaptureError(f"{phase} phase marker has the wrong phase identity")
    expected_parameters = {"seed": args.seed, "dt": args.dt,
                           "warmup_steps": args.warmup_frames, "measured_steps": args.measure_frames}
    for field, expected in expected_parameters.items():
        if marker.get(field) != expected:
            raise CaptureError(f"{phase} phase was captured with a different {field}")
    if marker.get("complete") is not True or marker.get("strategies") != strategies:
        raise CaptureError(f"{phase} phase is not complete for the requested strategies")
    if marker.get("frozen_control_keys") != frozen_control_case_ids(cases):
        raise CaptureError(f"{phase} phase is missing required frozen feature-008 controls")
    index_path = root / "case-index.json"
    inventory = json.loads(index_path.read_text(encoding="utf-8"))
    expected_ids = [incremental_case_key(case) for case in cases]
    if marker.get("case_keys") != expected_ids or [entry.get("case_id") for entry in inventory] != expected_ids:
        raise CaptureError(f"{phase} phase case inventory differs from this capture request")
    if len(inventory) != len(cases) or any(entry.get("case") != case for entry, case in zip(inventory, cases)):
        raise CaptureError(f"{phase} phase resolved case parameters differ")
    for entry in inventory:
        if entry["case_id"] in marker["frozen_control_keys"]:
            field = "frozen_proofs" if phase == "validate" else "frozen_resources"
            if set(entry.get(field, {})) != {"current-linear", "disabled"}:
                raise CaptureError(f"{phase} phase frozen-control artifacts are incomplete for {entry['case_id']}")
    return inventory


def _run_incremental_timing(args, strategies: list[str], cases: list[dict], logs: Path,
                            env: dict, assets_hash: str, timing_identity: dict,
                            baseline_timing_identity: dict) -> dict:
    if strategies != list(INCREMENTAL_STRATEGIES):
        raise CaptureError("Acceptance timing requires all three candidate strategies")
    validation_marker = json.loads((args.validation_root / "phase.json").read_text(encoding="utf-8"))
    resource_marker = json.loads((args.resource_root / "phase.json").read_text(encoding="utf-8"))
    validation_cases = _incremental_phase_inventory(
        args.validation_root, validation_marker, "validate", cases, strategies, args)
    resource_cases = _incremental_phase_inventory(
        args.resource_root, resource_marker, "resources", cases, strategies, args)
    if validation_marker.get("candidate_source_sha256") != resource_marker.get("candidate_source_sha256") or \
            validation_marker.get("candidate_source_sha256") != timing_identity.get("source_sha256"):
        raise CaptureError("Validation, resource, and timing candidate source identities differ")
    if validation_marker.get("assets_sha256") != assets_hash or resource_marker.get("assets_sha256") != assets_hash:
        raise CaptureError("Timing loaded-asset identity differs from its prerequisite proofs")

    raw_root = args.output / "raw"
    raw_root.mkdir()
    rng = random.Random(args.seed)
    run_rows: list[dict] = []
    frozen_rows: list[dict] = []
    raw_index: list[dict] = []
    source_fingerprints = set()
    frozen_source_fingerprints = set()
    evidence_by_case = []
    for case_index, case in enumerate(cases):
        validation_entry = validation_cases[case_index]
        resource_entry = resource_cases[case_index]
        case_id = validation_entry["case_id"]
        validation_case_root = args.validation_root / case_id
        resource_case_root = args.resource_root / case_id
        proofs = {mode: validate_render_proof(validation_case_root, case, args, mode)
                  for mode in (*strategies, "disabled")}
        resources = {mode: validate_resource_proof(resource_case_root, case, args, mode)
                     for mode in (*strategies, "disabled")}
        validate_incremental_render_group(proofs, case, args)
        validate_incremental_resource_group(resources, case, proofs)
        frozen_proofs = {}
        frozen_resources = {}
        if case_id in frozen_control_case_ids(cases):
            frozen_validation_root = validation_case_root / "frozen"
            frozen_resource_root = resource_case_root / "frozen"
            frozen_proofs = {mode: validate_render_proof(frozen_validation_root, case, args, mode)
                             for mode in ("current-linear", "disabled")}
            frozen_resources = {mode: validate_resource_proof(frozen_resource_root, case, args, mode)
                                for mode in ("current-linear", "disabled")}
            validate_frozen_control_group(frozen_proofs, proofs, case)
            for mode in ("current-linear", "disabled"):
                if (frozen_proofs[mode].get("source_sha256") != baseline_timing_identity.get("source_sha256") or
                        frozen_resources[mode].get("source_sha256") != baseline_timing_identity.get("source_sha256")):
                    raise CaptureError("Frozen timing identity differs from its feature-008 validation/resource proofs")
                if (frozen_resources[mode].get("renderer_mapping") != frozen_proofs[mode].get("renderer_mapping") or
                        frozen_resources[mode].get("fixture_sha256") != frozen_proofs[mode].get("fixture_sha256") or
                        frozen_resources[mode].get("harness_sha256") != frozen_proofs[mode].get("harness_sha256")):
                    raise CaptureError("Frozen timing control has mismatched rendering/resource identities")
        evidence_by_case.append({"case": case, "case_id": case_id, "proofs": proofs,
                                 "resources": resources, "frozen_proofs": frozen_proofs,
                                 "frozen_resources": frozen_resources})

    scheduled_jobs = [(case_index, "candidate", strategy)
                      for case_index in range(len(cases)) for strategy in (*strategies, "disabled")]
    scheduled_jobs += [(case_index, "frozen-feature008", strategy)
                       for case_index, item in enumerate(evidence_by_case) if item["frozen_proofs"]
                       for strategy in ("current-linear", "disabled")]
    for run_index in range(args.repetitions):
        scheduled = list(scheduled_jobs)
        rng.shuffle(scheduled)
        for case_index, role, strategy in scheduled:
            item = evidence_by_case[case_index]
            case = item["case"]
            case_id = item["case_id"]
            frozen = role == "frozen-feature008"
            proof = (item["frozen_proofs"] if frozen else item["proofs"])[strategy]
            resource = (item["frozen_resources"] if frozen else item["resources"])[strategy]
            binary = args.baseline_probe if frozen else args.candidate_probe
            identity = baseline_timing_identity if frozen else timing_identity
            prefix_name = f"{'frozen-' if frozen else ''}{case_id}-{strategy}-r{run_index}"
            csv_path = raw_root / f"{prefix_name}.csv"
            evidence_prefix = raw_root / f"{prefix_name}"
            command = _incremental_probe_command(
                binary, case, args, strategy, csv_path, evidence_prefix,
                phase="time", run_index=run_index,
                validation_path=Path(proof["_path"]), resource_path=Path(resource["_path"]))
            log_path = logs / f"time-{prefix_name}.log"
            _run(command, log_path, env)
            metrics = parse_benchmark_csv(csv_path)
            manifest = _validate_incremental_timing_manifest(
                csv_path, case, args, strategy, proof, resource, identity,
                binary, run_index, metrics)
            sample_rows, paired_values = read_incremental_timing_samples(
                csv_path.with_suffix(".samples.csv"), csv_path.with_suffix(".paired.csv"),
                args.measure_frames, 0 if case.get("headless") else args.measure_frames,
                bool(case.get("headless")))
            process_summary = {"case_key": case_id, "role": role, "strategy": strategy, "run_index": run_index,
                               "render_ms": metrics["scope.render.avg"],
                               "complete_step_ms": metrics["scope.complete_step.avg"],
                               "paired_step_render_ms": statistics.fmean(paired_values)}
            (frozen_source_fingerprints if frozen else source_fingerprints).add(manifest["build"]["source_sha256"])
            row = {**process_summary,
                   "render_p95_ms": metrics["scope.render.p95"],
                   "render_p99_ms": metrics["scope.render.p99"],
                   "render_worst_ms": metrics["scope.render.max"],
                   "complete_step_p95_ms": metrics["scope.complete_step.p95"],
                   "complete_step_p99_ms": metrics["scope.complete_step.p99"],
                   "complete_step_worst_ms": metrics["scope.complete_step.max"],
                   "paired_p95_ms": _nearest_rank(paired_values, 0.95),
                   "paired_p99_ms": _nearest_rank(paired_values, 0.99),
                   "paired_worst_ms": max(paired_values),
                   "paired_samples_sha256": sha256(csv_path.with_suffix(".paired.csv")),
                   "samples_sha256": sha256(csv_path.with_suffix(".samples.csv")),
                   "manifest_sha256": sha256(csv_path.with_suffix(".manifest.json")),
                   "binary_sha256": sha256(binary),
                   "fixture_sha256": proof["fixture_sha256"], "harness_sha256": proof["harness_sha256"],
                   "source_sha256": proof["source_sha256"], "asset_sha256": assets_hash,
                   "validation_proof": proof["_path"], "validation_proof_sha256": sha256(Path(proof["_path"])),
                   "resource_proof": resource["_path"], "resource_proof_sha256": resource["_sha256"],
                   "renderer_mapping": proof["renderer_mapping"],
                   "csv": str(csv_path), "paired_csv": str(csv_path.with_suffix(".paired.csv")),
                   "samples_csv": str(csv_path.with_suffix(".samples.csv")),
                   "manifest": str(csv_path.with_suffix(".manifest.json")), "log": str(log_path)}
            (frozen_rows if frozen else run_rows).append(row)
            raw_index.append({"case_key": case_id, "role": role, "strategy": strategy, "run_index": run_index,
                              "csv": row["csv"], "paired_csv": row["paired_csv"],
                              "samples_csv": row["samples_csv"], "manifest": row["manifest"],
                              "log": row["log"], "samples": len(sample_rows),
                              "csv_sha256": sha256(csv_path), "paired_samples_sha256": row["paired_samples_sha256"],
                              "samples_sha256": row["samples_sha256"], "manifest_sha256": row["manifest_sha256"]})

    if len(source_fingerprints) != 1:
        raise CaptureError("Incremental timing runs use mixed candidate source fingerprints")
    aggregates = {}
    differences = {}
    for case in cases:
        case_id = incremental_case_key(case)
        by_strategy = {}
        for strategy in (*strategies, "disabled"):
            selected = [row for row in run_rows if row["case_key"] == case_id and row["strategy"] == strategy]
            if len(selected) != args.repetitions or {row["run_index"] for row in selected} != set(range(args.repetitions)):
                raise CaptureError(f"Timing matrix is missing a repetition for {case_id} {strategy}")
            ordered = sorted(selected, key=lambda row: row["run_index"])
            validate_incremental_timing_samples(
                [{"case_key": case_id, "run_index": index, "render_ms": row["render_ms"],
                  "complete_step_ms": row["complete_step_ms"],
                  "paired_step_render_ms": row["paired_step_render_ms"]}
                 for index, row in enumerate(ordered)],
                [row["paired_step_render_ms"] for row in ordered], args.repetitions, timing_identity)
            by_strategy[strategy] = {
                "render_process_means_ms": [row["render_ms"] for row in selected],
                "complete_step_process_means_ms": [row["complete_step_ms"] for row in selected],
                "paired_process_means_ms": [row["paired_step_render_ms"] for row in selected],
                "render": _timing_summary([row["render_ms"] for row in selected]),
                "complete_step": _timing_summary([row["complete_step_ms"] for row in selected]),
                "paired_step_render": _timing_summary([row["paired_step_render_ms"] for row in selected]),
                "render_frame_p95_ms_by_process": [row["render_p95_ms"] for row in selected],
                "render_frame_p99_ms_by_process": [row["render_p99_ms"] for row in selected],
                "render_frame_worst_ms_by_process": [row["render_worst_ms"] for row in selected],
                "paired_frame_p95_ms_by_process": [row["paired_p95_ms"] for row in selected],
                "paired_frame_p99_ms_by_process": [row["paired_p99_ms"] for row in selected],
                "paired_frame_worst_ms_by_process": [row["paired_worst_ms"] for row in selected]}
        aggregates[case_id] = by_strategy
        differences[case_id] = {}
        for strategy, reference in (("cached-linear", "current-linear"), ("indexed", "cached-linear"),
                                    ("indexed", "current-linear")):
            left = {row["run_index"]: row for row in run_rows
                    if row["case_key"] == case_id and row["strategy"] == strategy}
            right = {row["run_index"]: row for row in run_rows
                     if row["case_key"] == case_id and row["strategy"] == reference}
            differences[case_id][f"{strategy}_vs_{reference}"] = {
                field: [left[index][field] - right[index][field] for index in range(args.repetitions)]
                for field in ("render_ms", "complete_step_ms", "paired_step_render_ms")}
        for strategy in (*strategies, "disabled"):
            baseline = {row["run_index"]: row for row in run_rows
                        if row["case_key"] == case_id and row["strategy"] == "current-linear"}
            candidate = {row["run_index"]: row for row in run_rows
                         if row["case_key"] == case_id and row["strategy"] == strategy}
            differences[case_id][f"{strategy}_vs_current-linear"] = {
                field: [candidate[index][field] - baseline[index][field] for index in range(args.repetitions)]
                for field in ("render_ms", "complete_step_ms", "paired_step_render_ms")}

    if len(frozen_source_fingerprints) != 1 or next(iter(frozen_source_fingerprints)) != baseline_timing_identity.get("source_sha256"):
        raise CaptureError("Frozen feature-008 timing runs use mixed or unexpected source fingerprints")

    frozen_control_analysis = {}
    for item in evidence_by_case:
        if not item["frozen_proofs"]:
            continue
        case_id = item["case_id"]
        frozen_control_analysis[case_id] = {}
        for strategy in ("current-linear", "disabled"):
            candidate = {row["run_index"]: row for row in run_rows
                         if row["case_key"] == case_id and row["strategy"] == strategy}
            frozen = {row["run_index"]: row for row in frozen_rows
                      if row["case_key"] == case_id and row["strategy"] == strategy}
            if set(candidate) != set(range(args.repetitions)) or set(frozen) != set(range(args.repetitions)):
                raise CaptureError(f"Frozen control timing is missing a repetition for {case_id} {strategy}")
            metrics = ("render_ms", "complete_step_ms", "paired_step_render_ms")
            candidate_summaries = {
                field: _timing_summary([candidate[index][field] for index in range(args.repetitions)])
                for field in metrics}
            frozen_summaries = {
                field: _timing_summary([frozen[index][field] for index in range(args.repetitions)])
                for field in metrics}
            frozen_control_analysis[case_id][strategy] = {
                "candidate_summaries": candidate_summaries,
                "feature008_summaries": frozen_summaries,
                "candidate_minus_feature008_by_process": {
                    field: [candidate[index][field] - frozen[index][field]
                            for index in range(args.repetitions)] for field in metrics},
                "candidate_runs": [candidate[index] for index in range(args.repetitions)],
                "feature008_runs": [frozen[index] for index in range(args.repetitions)]}

    stock_controls = run_engine_controls(
        args, logs, raw_root, env, rng,
        {"pre-feature": baseline_timing_identity["source_sha256"],
         "linear": timing_identity["source_sha256"]}, include_rendered_multiworker=True)
    for row in stock_controls:
        raw_index.append({"control": "stock-engine", "scenario": row["scenario"],
                          "workers": row["workers"], "mode": row["mode"], "run_index": row["run_index"],
                          "csv": row["csv"], "csv_sha256": row["csv_sha256"],
                          "samples_csv": row["samples_csv"], "samples_sha256": row["samples_sha256"],
                          "manifest": str(Path(row["csv"]).with_suffix(".manifest.json")),
                          "manifest_sha256": row["manifest_sha256"], "log": row["log"]})
    with (raw_root / "raw-index.json").open("w", encoding="utf-8") as stream:
        json.dump(raw_index, stream, indent=2); stream.write("\n")
    return {"proof_kind": "platformator.incremental-timing", "proof_version": 1,
            "comparison": "incremental", "complete": True, "cases": cases,
            "strategies": [*strategies, "disabled"], "repetitions": args.repetitions,
            "parameters": {"seed": args.seed, "warmup_steps": args.warmup_frames,
                           "measured_steps": args.measure_frames, "dt": args.dt,
                           "workers": args.workers, "multiworker_count": args.multiworker_count,
                           "affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
                           "assets_sha256": assets_hash},
            "binary": str(args.candidate_probe.resolve()), "binary_sha256": sha256(args.candidate_probe),
            "source_sha256": next(iter(source_fingerprints)), "frozen_source_sha256": next(iter(frozen_source_fingerprints)),
            "runs": run_rows, "frozen_control_runs": frozen_rows,
            "aggregates_by_case": aggregates, "process_differences_by_case": differences,
            "frozen_control_analysis": frozen_control_analysis,
            "stock_engine_controls": stock_controls,
            "raw_index": str(raw_root / "raw-index.json"),
            "raw_index_sha256": sha256(raw_root / "raw-index.json"),
            "acceptance": "timing and resource evidence collected; SC-006 requires independent analysis"}


def run_incremental_capture(args, strategy_selection: str) -> dict:
    if args.phase == "pilot":
        if strategy_selection != "all":
            raise CaptureError("The pilot phase compares all three candidate strategies")
        return _run_incremental_pilot(args, args.focused_cases)
    validate_repetitions(args.repetitions)
    if (args.multiworker_count <= 0 or args.workers <= 0 or args.seed < 0 or args.warmup_frames < 0 or
            args.measure_frames <= 0 or not math.isfinite(args.dt) or args.dt <= 0):
        raise CaptureError("Invalid worker, seed, frame, or timestep arguments")
    if args.case_limit is not None and args.case_limit <= 0:
        raise CaptureError("--case-limit must be positive")
    binaries = (args.baseline_probe, args.candidate_probe, args.baseline_runner, args.candidate_runner,
                args.baseline_validation_probe, args.candidate_validation_probe)
    if any(path is None or not path.is_file() or not os.access(path, os.X_OK) for path in binaries):
        raise CaptureError("Every explicit probe/stock-runner path must be an executable file")
    validate_incremental_phase_roots(args.phase, args.validation_root, args.resource_root, args.output)

    asset_root = args.asset_root or (Path.cwd() / "assets")
    all_cases = incremental_matrix(args.multiworker_count, args.workers, asset_root)
    cases = all_cases[:args.case_limit] if args.case_limit is not None else all_cases
    control_ids = frozen_control_case_ids(cases)
    strategies = list(INCREMENTAL_STRATEGIES) if strategy_selection == "all" else [strategy_selection]
    full_strategy_set = strategies == list(INCREMENTAL_STRATEGIES)
    full_case_set = len(cases) == len(all_cases)
    complete = full_strategy_set and full_case_set and len(control_ids) == 4 and args.case_limit is None

    args.output.mkdir(parents=True)
    logs = args.output / "logs"; logs.mkdir()
    env = os.environ.copy()
    env.update({"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy", "SDL_RENDER_DRIVER": "software"})
    baseline_validation_identity = _read_probe_identity(args.baseline_validation_probe,
                                                        logs / "baseline-validation-identity.log", env)
    validation_identity = _read_probe_identity(args.candidate_validation_probe,
                                               logs / "candidate-validation-identity.log", env)
    baseline_timing_identity = _read_probe_identity(args.baseline_probe,
                                                    logs / "baseline-timing-identity.log", env)
    timing_identity = _read_probe_identity(args.candidate_probe, logs / "candidate-timing-identity.log", env)
    if validation_identity.get("work_probes") is not True or baseline_validation_identity.get("work_probes") is not True:
        raise CaptureError("Candidate and frozen-reference validation probes must include work probes")
    if timing_identity.get("work_probes") is not False or timing_identity.get("sanitizer") not in ("none", ""):
        raise CaptureError("Candidate timing probe must be uninstrumented")
    if baseline_timing_identity.get("work_probes") is not False or baseline_timing_identity.get("sanitizer") not in ("none", ""):
        raise CaptureError("Frozen-reference timing probe must be uninstrumented")
    if baseline_validation_identity.get("source_sha256") == validation_identity.get("source_sha256"):
        raise CaptureError("Frozen feature-008 and candidate validation sources must be distinct")
    for field in ("fixture_sha256", "harness_sha256"):
        if baseline_validation_identity.get(field) != validation_identity.get(field):
            raise CaptureError(f"Frozen-reference and candidate capture harnesses differ for {field}")
    if args.phase == "time" and (baseline_timing_identity.get("source_sha256") !=
                                  baseline_validation_identity.get("source_sha256") or
                                  timing_identity.get("source_sha256") != validation_identity.get("source_sha256")):
        raise CaptureError("Validation and timing binaries do not match their source fingerprints")
    assets_hash = _directory_sha256(asset_root)

    if args.phase == "validate":
        phase_root = args.validation_root
        phase_root.mkdir(parents=True)
        modes = [*strategies, "disabled"]
        inventory = []
        for case in cases:
            case_id = incremental_case_key(case)
            inventory.append(case_id)
            case_root = phase_root / case_id
            case_root.mkdir()
            proofs = {}
            case_modes = list(modes)
            if case_id in control_ids and "current-linear" not in case_modes:
                case_modes.append("current-linear")
            for mode in case_modes:
                prefix = case_root / f"{case_key(case)}-{mode}"
                csv_path = Path(str(prefix) + ".csv")
                command = _incremental_probe_command(
                    args.candidate_validation_probe, case, args, mode, csv_path, prefix,
                    phase="validate", run_index=0)
                _run(command, logs / f"validate-{case_id}-{mode}.log", env)
                proof = validate_render_proof(case_root, case, args, mode)
                proof_path = Path(proof["_path"])
                wrapper_path = Path(str(prefix) + ".incremental-proof.json")
                wrapper = _write_incremental_identity(wrapper_path, case, mode, args, proof, proof_path,
                                                      args.candidate_validation_probe, validation_identity,
                                                      assets_hash)
                proof["_incremental_identity"] = wrapper
                proofs[mode] = proof
            mismatch = None
            if full_strategy_set:
                try:
                    validate_incremental_render_group(proofs, case, args)
                except ImageMismatch as image_mismatch:
                    mismatch = image_mismatch
            frozen_proofs = {}
            if case_id in control_ids:
                frozen_root = case_root / "frozen"
                frozen_root.mkdir()
                for mode in ("current-linear", "disabled"):
                    prefix = frozen_root / f"{case_key(case)}-{mode}"
                    command = _incremental_probe_command(
                        args.baseline_validation_probe, case, args, mode,
                        Path(str(prefix) + ".csv"), prefix, phase="validate", run_index=0)
                    _run(command, logs / f"validate-{case_id}-frozen-{mode}.log", env)
                    proof = validate_render_proof(frozen_root, case, args, mode)
                    proof_path = Path(proof["_path"])
                    wrapper = _write_incremental_identity(
                        Path(str(prefix) + ".incremental-proof.json"), case, f"frozen-{mode}",
                        args, proof, proof_path, args.baseline_validation_probe,
                        baseline_validation_identity, assets_hash)
                    proof["_incremental_identity"] = wrapper
                    frozen_proofs[mode] = proof
                try:
                    validate_frozen_control_group(frozen_proofs, proofs, case)
                except ImageMismatch as image_mismatch:
                    if mismatch is None:
                        mismatch = image_mismatch
            if mismatch is not None:
                preserve_image_mismatch(
                    mismatch,
                    lambda: capture_incremental_mismatch_images(
                        args, case, mismatch.frame, phase_root, logs, env, proofs, frozen_proofs))
            inventory[-1] = {"case_id": case_id, "case": case,
                             "proofs": {mode: proofs[mode]["_path"] for mode in proofs},
                             "frozen_proofs": {mode: proof["_path"] for mode, proof in frozen_proofs.items()}}
        marker = {"proof_kind": "platformator.incremental-phase", "proof_version": 1,
                  "comparison": "incremental", "phase": "validate", "complete": complete,
                  "case_count": len(cases), "case_keys": [item["case_id"] if isinstance(item, dict) else item for item in inventory],
                  "strategies": strategies, "frozen_control_keys": control_ids,
                  "seed": args.seed, "dt": args.dt,
                  "frozen_source_sha256": baseline_validation_identity.get("source_sha256"),
                  "warmup_steps": args.warmup_frames, "measured_steps": args.measure_frames,
                  "candidate_source_sha256": validation_identity.get("source_sha256"),
                  "candidate_binary_sha256": sha256(args.candidate_validation_probe),
                  "assets_sha256": assets_hash, "partial_case_limit": args.case_limit}
        (phase_root / "phase.json").write_text(json.dumps(marker, indent=2) + "\n", encoding="utf-8")
        (phase_root / "case-index.json").write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
        report = {"phase": "validate", "complete": complete, "cases": inventory,
                  "strategies": strategies, "assets_sha256": assets_hash}
    elif args.phase == "resources":
        phase_root = args.resource_root
        phase_root.mkdir(parents=True)
        validation_marker = json.loads((args.validation_root / "phase.json").read_text(encoding="utf-8"))
        validation_cases = json.loads((args.validation_root / "case-index.json").read_text(encoding="utf-8"))
        if [entry["case_id"] for entry in validation_cases] != validation_marker.get("case_keys"):
            raise CaptureError("Validation root case index does not match its phase marker")
        if len(validation_cases) != len(cases) or any(entry["case"] != case for entry, case in zip(validation_cases, cases)):
            raise CaptureError("Resource phase case inventory differs from validation proofs")
        if validation_marker.get("strategies") != strategies:
            raise CaptureError("Resource phase strategy selection differs from validation proofs")
        if validation_marker.get("frozen_control_keys") != control_ids:
            raise CaptureError("Resource phase frozen-control inventory differs from validation proofs")
        modes = [*strategies, "disabled"]
        inventory = []
        for case, validation_entry in zip(cases, validation_cases):
            case_id = validation_entry["case_id"]
            case_root = phase_root / case_id
            case_root.mkdir()
            resources = {}
            render_proofs = {}
            validation_case_root = args.validation_root / case_id
            case_modes = list(modes)
            if case_id in control_ids and "current-linear" not in case_modes:
                case_modes.append("current-linear")
            for mode in case_modes:
                render_proofs[mode] = validate_render_proof(validation_case_root, case, args, mode)
                prefix = case_root / f"{case_key(case)}-{mode}"
                command = _incremental_probe_command(
                    args.candidate_validation_probe, case, args, mode, Path(str(prefix) + ".csv"), prefix,
                    phase="resources", run_index=0)
                _run(command, logs / f"resources-{case_id}-{mode}.log", env)
                resource = validate_resource_proof(case_root, case, args, mode)
                resource_path = Path(resource["_path"])
                resource["_incremental_identity"] = _write_incremental_identity(
                    Path(str(prefix) + ".incremental-resource-proof.json"), case, mode, args,
                    resource, resource_path, args.candidate_validation_probe, validation_identity, assets_hash)
                resources[mode] = resource
            if full_strategy_set:
                validate_incremental_resource_group(resources, case, render_proofs)
            frozen_resources = {}
            if case_id in control_ids:
                frozen_root = case_root / "frozen"
                frozen_root.mkdir()
                validation_frozen_root = args.validation_root / case_id / "frozen"
                for mode in ("current-linear", "disabled"):
                    frozen_render_proof = validate_render_proof(validation_frozen_root, case, args, mode)
                    prefix = frozen_root / f"{case_key(case)}-{mode}"
                    command = _incremental_probe_command(
                        args.baseline_validation_probe, case, args, mode,
                        Path(str(prefix) + ".csv"), prefix, phase="resources", run_index=0)
                    _run(command, logs / f"resources-{case_id}-frozen-{mode}.log", env)
                    resource = validate_resource_proof(frozen_root, case, args, mode)
                    resource["_incremental_identity"] = _write_incremental_identity(
                        Path(str(prefix) + ".incremental-resource-proof.json"), case, f"frozen-{mode}",
                        args, resource, Path(resource["_path"]), args.baseline_validation_probe,
                        baseline_validation_identity, assets_hash)
                    if resource.get("source_sha256") != frozen_render_proof.get("source_sha256"):
                        raise CaptureError("Frozen control rendering/resource source fingerprints differ")
                    if resource.get("renderer_mapping") != frozen_render_proof.get("renderer_mapping"):
                        raise CaptureError("Frozen control rendering/resource mappings differ")
                    frozen_resources[mode] = resource
            inventory.append({"case_id": case_id, "case": case,
                              "resources": {mode: resources[mode]["_path"] for mode in resources},
                              "frozen_resources": {mode: proof["_path"] for mode, proof in frozen_resources.items()}})
        complete = bool(validation_marker.get("complete")) and full_strategy_set and \
                   set(control_ids) == set(validation_marker.get("frozen_control_keys", []))
        marker = {"proof_kind": "platformator.incremental-phase", "proof_version": 1,
                  "comparison": "incremental", "phase": "resources", "complete": complete,
                  "case_count": len(cases), "case_keys": [entry["case_id"] for entry in inventory],
                  "strategies": strategies, "frozen_control_keys": control_ids,
                  "seed": args.seed, "dt": args.dt,
                  "warmup_steps": args.warmup_frames, "measured_steps": args.measure_frames,
                  "frozen_source_sha256": baseline_validation_identity.get("source_sha256"),
                  "candidate_source_sha256": validation_identity.get("source_sha256"),
                  "candidate_binary_sha256": sha256(args.candidate_validation_probe),
                  "assets_sha256": assets_hash, "partial_case_limit": args.case_limit}
        (phase_root / "phase.json").write_text(json.dumps(marker, indent=2) + "\n", encoding="utf-8")
        (phase_root / "case-index.json").write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
        report = {"phase": "resources", "complete": complete, "cases": inventory,
                  "strategies": strategies, "assets_sha256": assets_hash}
    else:
        report = _run_incremental_timing(args, strategies, cases, logs, env, assets_hash,
                                         timing_identity, baseline_timing_identity)

    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def run_capture(args) -> dict:
    validate_repetitions(args.repetitions)
    if args.multiworker_count <= 0 or args.workers <= 0 or args.seed < 0 or args.warmup_frames < 0 or args.measure_frames <= 0 or not math.isfinite(args.dt) or args.dt <= 0:
        raise CaptureError("Invalid worker, seed, frame, or timestep arguments")
    binaries = (args.baseline_probe, args.candidate_probe, args.baseline_runner, args.candidate_runner,
                args.baseline_validation_probe, args.candidate_validation_probe)
    if any(path is None or not path.is_file() or not os.access(path, os.X_OK) for path in binaries):
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
                proof = proofs[key][mode]
                mode_summary[mode] = {
                    "frames": proof["_frame_count"],
                    "submissions": proof["_submission_total"],
                    "oracle_visible_memberships": proof["_oracle_visible_total"],
                    "maximum_oracle_visible_per_frame": proof["_maximum_oracle_visible"],
                    "proof": proof["_path"],
                    "proof_sha256": sha256(Path(proof["_path"])),
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
    denominator = _frame_total(proofs["pre-feature"], "submissions")
    numerator = _frame_total(proofs["linear"], "submissions")
    return numerator / denominator if denominator else 0.0


def main(argv=None) -> int:
    args = parser().parse_args(argv)
    try:
        comparison = args.comparison
        if comparison == "incremental" and args.phase is None:
            args.phase = "pilot"
        if (comparison != "incremental" or args.phase != "pilot") and not args.allow_full_campaign:
            raise CaptureError("Non-pilot campaigns require --allow-full-campaign; use the compact pilot by default")
        strategy = args.strategy
        if comparison == "incremental" and strategy is None:
            strategy = "all"
        validate_incremental_options(comparison, args.phase, strategy)
        if args.case_limit is not None and args.case_limit <= 0:
            raise CaptureError("--case-limit must be positive")
        roots = [args.output]
        if comparison == "incremental" and args.phase in ("validate", "resources"):
            phase_root = args.validation_root if args.phase == "validate" else args.resource_root
            if phase_root is not None:
                roots.append(phase_root)
        for root in roots:
            if root.exists():
                raise CaptureError(f"Refusing to reuse an existing output path: {root}")
        budget = CaptureBudget(roots, args.max_output_mib, args.max_seconds)
        try:
            with capture_budget(budget):
                if comparison == "incremental":
                    run_incremental_capture(args, strategy)
                else:
                    run_capture(args)
        except BudgetExceeded:
            # Completion markers cannot survive a failed final size/time check.
            for root in roots:
                for name in ("report.json", "phase.json", "case-index.json"):
                    (root / name).unlink(missing_ok=True)
            raise
        return 0
    except (BudgetExceeded, CaptureError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"capture_linear_render: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("capture_linear_render: interrupted; partial artifacts were retained without a success report", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
