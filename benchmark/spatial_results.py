"""Shared schema-4 spatial metric names, applicability rules and capture reader."""

import csv
import hashlib
import json
import math
import statistics
from pathlib import Path

LEGACY_SCOPES = (
    "complete_step",
    "physics",
    "cleanup",
    "scene_transition",
    "render",
    "broad_phase",
    "narrow_phase",
    "resolve_collisions",
)
LEGACY_COUNTERS = (
    "object_count",
    "occupied_cell_count",
    "candidate_pair_count",
    "pending_narrow_phase_pair_count",
    "active_collision_count",
    "awake_dynamic_body_count",
    "queued_add_count",
    "queued_sync_count",
    "queued_remove_count",
    "collision_enter_event_count",
    "collision_exit_event_count",
)
ADDITIONAL_SCOPES = (
    "spatial_update",
    "render_culling",
    "render_submit",
    "spatial_replay",
    "bounds_prepare",
    "index_update",
    "index_maintenance",
    "index_query",
    "result_reduce",
)
ADDITIONAL_COUNTERS = (
    "spatial_proxy_count",
    "changed_bounds_count",
    "filter_change_count",
    "proxy_insert_count",
    "proxy_remove_count",
    "proxy_move_count",
    "envelope_reuse_count",
    "query_count",
    "raw_discovery_count",
    "exact_overlap_count",
    "pair_add_count",
    "pair_remove_count",
    "rebuild_call_count",
    "rebuild_item_count",
    "tree_height",
    "tree_node_count",
    "sprite_count",
    "visible_candidate_count",
    "submitted_sprite_count",
    "uncullable_sprite_count",
    "culling_bypass_count",
    "visibility_cache_hit_count",
    "visibility_cell_count",
)
SCOPES = LEGACY_SCOPES + ADDITIONAL_SCOPES
COUNTERS = LEGACY_COUNTERS + ADDITIONAL_COUNTERS

STEP = "step"
RENDER = "render"
COLLISION_REPLAY = "collision_replay"
VISIBILITY_REPLAY = "visibility_replay"
REPLAY = "replay"
_REPLAY = {COLLISION_REPLAY, VISIBILITY_REPLAY}
_PHYSICS_INDEXED = {"segmented", "bvh"}
_VISIBILITY_INDEXED = {"linear", "bvh", "segmented"}


def _selected_method(sample_kind, broad_phase, render_culling, replay_domain=None):
    collision = sample_kind in {STEP, COLLISION_REPLAY} or (sample_kind == REPLAY and replay_domain == "collision")
    return broad_phase if collision else render_culling


def metric_applicability(
    metric,
    sample_kind,
    *,
    broad_phase="segmented",
    render_culling="none",
    rendering=True,
    bypass=False,
    replay_domain=None,
):
    """Return applicable, inapplicable, or unavailable for one schema-4 field.

    Unavailable is reserved for a visibility result that a bypass did not compute.
    Callers retain the reason separately in the manifest.
    """
    if sample_kind not in {STEP, RENDER, COLLISION_REPLAY, VISIBILITY_REPLAY, REPLAY}:
        raise ValueError(f"Unknown sample kind: {sample_kind}")
    if sample_kind == REPLAY and replay_domain not in {"collision", "visibility"}:
        raise ValueError("replay_domain must be collision or visibility for replay samples")
    if metric not in set(SCOPES) | set(COUNTERS):
        raise ValueError(f"Unknown schema-4 metric: {metric}")

    is_step = sample_kind == STEP
    is_render = sample_kind == RENDER and rendering
    is_collision = sample_kind == COLLISION_REPLAY or (sample_kind == REPLAY and replay_domain == "collision")
    is_visibility = sample_kind == VISIBILITY_REPLAY or (sample_kind == REPLAY and replay_domain == "visibility")
    is_replay = is_collision or is_visibility
    method = _selected_method(sample_kind, broad_phase, render_culling, replay_domain)
    physics_method = broad_phase in _PHYSICS_INDEXED
    visibility_method = render_culling in _VISIBILITY_INDEXED
    physics_domain = is_step or is_collision
    visibility_domain = is_render or is_visibility

    if metric in LEGACY_SCOPES:
        if metric == "render":
            return "applicable" if is_render else "inapplicable"
        return "applicable" if is_step else "inapplicable"
    if metric == "spatial_update":
        return "applicable" if is_step else "inapplicable"
    if metric in {"render_culling", "render_submit"}:
        return "applicable" if is_render else "inapplicable"
    if metric == "spatial_replay":
        return "applicable" if is_replay else "inapplicable"
    if metric == "bounds_prepare":
        applicable = physics_method if is_step else visibility_method if is_render or is_visibility else False
        return "applicable" if applicable else "inapplicable"
    if metric == "index_query":
        # Segmented transition discovery is charged to index_update.
        query_method = broad_phase == "bvh" if is_step or is_collision else render_culling in {"linear", "bvh"}
        return "applicable" if (is_step or is_collision or is_render or is_visibility) and query_method else "inapplicable"
    if metric in {"index_update", "index_maintenance", "result_reduce"}:
        applicable = physics_method if physics_domain else visibility_method if visibility_domain else False
        return "applicable" if applicable else "inapplicable"

    if metric in LEGACY_COUNTERS:
        if metric == "occupied_cell_count":
            return "applicable" if is_step and broad_phase == "segmented" else "inapplicable"
        if metric == "candidate_pair_count":
            return "applicable" if is_step or is_collision else "inapplicable"
        return "applicable" if is_step else "inapplicable"

    if metric == "sprite_count":
        return "applicable" if is_render or is_visibility else "inapplicable"
    if metric == "submitted_sprite_count":
        return "applicable" if is_render else "inapplicable"
    if metric == "visible_candidate_count":
        if is_render and render_culling == "none":
            return "inapplicable"
        if bypass and (is_render or is_visibility):
            return "unavailable"
        return "applicable" if (is_render and visibility_method) or is_visibility else "inapplicable"
    if metric in {"uncullable_sprite_count", "culling_bypass_count", "visibility_cache_hit_count"}:
        return "applicable" if (is_render or is_visibility) and visibility_method else "inapplicable"
    if metric == "visibility_cell_count":
        return "applicable" if (is_render or is_visibility) and render_culling == "segmented" else "inapplicable"

    physics_counters = {
        "filter_change_count",
        "pair_add_count",
        "pair_remove_count",
    }
    if metric in physics_counters:
        return "applicable" if is_step and physics_method or is_collision else "inapplicable"
    if metric == "envelope_reuse_count":
        return "applicable" if physics_domain and physics_method else "inapplicable"
    if metric in {"rebuild_call_count", "rebuild_item_count", "tree_height", "tree_node_count"}:
        return "applicable" if ((is_step or is_collision) and broad_phase == "bvh") or (
            (is_render or is_visibility) and render_culling == "bvh"
        ) else "inapplicable"
    if metric in {
        "spatial_proxy_count",
        "changed_bounds_count",
        "proxy_insert_count",
        "proxy_remove_count",
        "proxy_move_count",
        "query_count",
        "raw_discovery_count",
        "exact_overlap_count",
    }:
        applicable = physics_method if physics_domain else visibility_method if visibility_domain else False
        return "applicable" if applicable else "inapplicable"
    raise AssertionError(metric)


def pure_render_eligible(render_culling, *, bypass_count, uncullable_count):
    """A culling run is pure only if every indexed result stayed fully indexed."""
    return render_culling != "none" and bypass_count == 0 and uncullable_count == 0


def spatial_process_summary(capture):
    """Flatten one validated schema-4 process capture for sweep aggregation."""
    manifest = capture["manifest"]
    result = {}
    if manifest.get("capture_mode") != "resource":
        for (category, name), stats in capture["summary"].items():
            if stats is None:
                continue
            for field, value in stats.items():
                result[f"{category}.{name}.{field}"] = value
        for name, value in manifest.get("initialization", {}).items():
            if name.endswith("_ns") and isinstance(value, (int, float)):
                result[f"initialization.{name[:-3]}_ms"] = float(value) / 1e6
    resources = manifest.get("resources", {})
    if resources.get("status") == "complete":
        for domain, measures in resources.get("domains", {}).items():
            for name, value in measures.items():
                if isinstance(value, (int, float)) and not isinstance(value, bool):
                    result[f"resources.{domain}.{name}"] = value
        for name, value in resources.items():
            if name.endswith("_bytes") and isinstance(value, (int, float)):
                result[f"resources.{name}"] = value
    return result


def aggregate_spatial_processes(processes):
    """Aggregate independent process captures without pooling their samples.

    Scope/counter summaries are per-process values. Returned ``*_run_*``
    columns describe spread between fresh processes, while ``*_mean_run_*``
    records the mean of that statistic across processes.
    """
    if not processes:
        raise ValueError("Cannot aggregate an empty spatial process group")
    keys = sorted(set().union(*(process.keys() for process in processes)))
    result = {"process_count": len(processes)}
    for key in keys:
        values = [float(process[key]) for process in processes if key in process]
        if not values:
            continue
        result[f"{key}.across_process_mean"] = statistics.mean(values)
        result[f"{key}.across_process_stdev"] = statistics.stdev(values) if len(values) > 1 else 0.0
        result[f"{key}.across_process_min"] = min(values)
        result[f"{key}.across_process_max"] = max(values)
    return result


def _finite_nonnegative(value, field):
    try:
        number = float(value)
    except (TypeError, ValueError) as error:
        raise ValueError(f"Invalid numeric value for {field}") from error
    if not math.isfinite(number) or number < 0:
        raise ValueError(f"Invalid finite nonnegative value for {field}: {value}")
    return number


def _percentile(values, proportion):
    ordered = sorted(values)
    return ordered[math.ceil(proportion * len(ordered)) - 1]


def read_schema4_capture(csv_path):
    """Strictly validate one schema-4 CSV/manifest/raw-samples bundle.

    Returns the parsed manifest and metric summary values. CSV blanks are
    accepted only when the normative applicability matrix says inapplicable or
    unavailable; applicable zeroes remain numeric zeroes.
    """
    path = Path(csv_path)
    manifest_path = path.with_suffix(".manifest.json")
    raw_path = path.with_suffix(".samples.csv")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    required_manifest = {"schema_version", "capture_mode", "timing_eligible", "complete_capture", "status",
                         "run", "input", "selection", "sample_counts", "applicability", "boundaries",
                         "initialization", "validation", "resources", "build", "machine", "dependencies"}
    if not isinstance(manifest, dict) or set(manifest) != required_manifest or manifest.get("schema_version") != 4:
        raise ValueError("Missing, unknown, or extra top-level schema-4 manifest fields")
    if manifest["capture_mode"] not in {"performance", "validate", "resource"}:
        raise ValueError("Unknown schema-4 capture mode")
    if manifest["timing_eligible"] is not (manifest["capture_mode"] == "performance"):
        raise ValueError("Capture mode and timing eligibility contradict")
    if manifest["status"] != "complete" or manifest["complete_capture"] is not True:
        raise ValueError("Schema-4 capture is incomplete")
    run = manifest["run"]
    if not {"warmup_steps", "requested_measured_steps", "measured_steps", "seed", "dt",
            "workers_requested", "workers_effective"} <= run.keys():
        raise ValueError("Incomplete schema-4 run identity")
    if run["requested_measured_steps"] != run["measured_steps"] or run["measured_steps"] <= 0:
        raise ValueError("Schema-4 measured count is incomplete")
    if run["workers_requested"] <= 0 or run["workers_effective"] <= 0 or run["seed"] < 0:
        raise ValueError("Invalid schema-4 worker/seed values")
    if not math.isfinite(run["dt"]) or run["dt"] <= 0:
        raise ValueError("Invalid schema-4 dt")
    input_data = manifest["input"]
    domain = input_data.get("domain")
    replay_domain = {"collision-replay": "collision", "visibility-replay": "visibility"}.get(domain)
    if domain not in {"engine", "collision-replay", "visibility-replay"}:
        raise ValueError("Unknown schema-4 input domain")
    if not isinstance(input_data.get("case_sha256"), str) or len(input_data["case_sha256"]) != 64 or any(
        character not in "0123456789abcdef" for character in input_data["case_sha256"]
    ):
        raise ValueError("Missing or invalid schema-4 input SHA-256")
    trace_path = input_data.get("trace_path")
    if replay_domain and (not isinstance(trace_path, str) or not Path(trace_path).is_file() or
                          hashlib.sha256(Path(trace_path).read_bytes()).hexdigest() != input_data["case_sha256"]):
        raise ValueError("Replay trace path/hash is missing or mismatched")
    build = manifest["build"]
    fingerprint = build.get("source_sha256")
    if not isinstance(fingerprint, str) or len(fingerprint) != 64 or any(c not in "0123456789abcdef" for c in fingerprint):
        raise ValueError("Missing or invalid schema-4 source fingerprint")
    selection = manifest["selection"].get("effective", {})
    broad_phase = selection.get("broad_phase", "segmented")
    render_culling = selection.get("render_culling", "none")
    if broad_phase not in {"segmented", "bvh"} or render_culling not in {"none", "linear", "bvh", "segmented"}:
        raise ValueError("Invalid schema-4 effective spatial selection")
    applicability = manifest["applicability"]
    if set(applicability) != {"sample_kind", "replay_domain", "metrics", "units"}:
        raise ValueError("Unknown schema-4 applicability fields")
    if applicability["sample_kind"] != "replay" or applicability["replay_domain"] != replay_domain:
        if replay_domain is not None:
            raise ValueError("Manifest replay domain contradicts input domain")
    declared = applicability["metrics"]
    if set(declared) != set(SCOPES) | set(COUNTERS):
        raise ValueError("Schema-4 applicability map must name every metric exactly")
    expected_kind = "replay" if replay_domain else None
    rendering = manifest["sample_counts"].get("render", 0) > 0
    for metric in SCOPES + COUNTERS:
        if expected_kind:
            state = metric_applicability(metric, expected_kind, broad_phase=broad_phase,
                                         render_culling=render_culling, replay_domain=replay_domain)
        else:
            # The engine manifest must declare applicability per sample kind;
            # its collapsed declaration is checked against whichever domains exist.
            kinds = []
            if manifest["sample_counts"].get("step", 0):
                kinds.append(metric_applicability(metric, STEP, broad_phase=broad_phase,
                                                  render_culling=render_culling, rendering=rendering))
            if manifest["sample_counts"].get("render", 0):
                kinds.append(metric_applicability(metric, RENDER, broad_phase=broad_phase,
                                                  render_culling=render_culling, rendering=rendering))
            state = "applicable" if "applicable" in kinds else "inapplicable"
        if declared[metric] != state:
            raise ValueError(f"Producer applicability contradicts the normative matrix for {metric}")

    allowed_raw = {"schema_version", "sample_kind", "index"} | {name + "_ns" for name in SCOPES} | set(COUNTERS)
    with raw_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if set(reader.fieldnames or ()) != allowed_raw or len(reader.fieldnames or ()) != len(allowed_raw):
            raise ValueError("Unknown/missing schema-4 raw sample columns")
        raw_rows = list(reader)
    expected_counts = manifest["sample_counts"]
    kind_counts = {STEP: 0, RENDER: 0, REPLAY: 0}
    summary_values = {("scope", metric): [] for metric in SCOPES}
    summary_values.update({("counter", metric): [] for metric in COUNTERS})
    for row in raw_rows:
        kind = row["sample_kind"]
        if kind not in kind_counts or row["schema_version"] != "4" or int(row["index"]) != kind_counts[kind]:
            raise ValueError("Invalid schema-4 sample kind, version, or sequence index")
        if (kind == REPLAY) != (replay_domain is not None):
            raise ValueError("Replay and engine sample domains may not be mixed")
        kind_counts[kind] += 1
        bypass = False
        bypass_text = row["culling_bypass_count"]
        if bypass_text:
            bypass = _finite_nonnegative(bypass_text, "culling_bypass_count") > 0
        for metric in SCOPES + COUNTERS:
            status = metric_applicability(metric, kind, broad_phase=broad_phase, render_culling=render_culling,
                                          rendering=rendering, bypass=bypass, replay_domain=replay_domain)
            field = metric + "_ns" if metric in SCOPES else metric
            raw_value = row[field]
            if status == "applicable":
                value = _finite_nonnegative(raw_value, field)
                summary_values[("scope" if metric in SCOPES else "counter", metric)].append(value)
            elif raw_value != "":
                raise ValueError(f"Inapplicable/unavailable metric must be blank: {field}")
    expected = {STEP: expected_counts.get("step", 0), RENDER: expected_counts.get("render", 0),
                REPLAY: expected_counts.get("measured_replay", 0)}
    if kind_counts != expected or sum(kind_counts.values()) != len(raw_rows):
        raise ValueError("Schema-4 raw sample counts disagree with manifest")

    summary = {}
    seen = set()
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        required_columns = {"schema_version", "category", "name", "samples", "avg", "median", "p95", "p99", "min", "max", "unit"}
        if set(reader.fieldnames or ()) != required_columns:
            raise ValueError("Unknown/missing schema-4 summary columns")
        for row in reader:
            category, name = row["category"], row["name"]
            key = (category, name)
            allowed = SCOPES if category == "scope" else COUNTERS if category == "counter" else ()
            if row["schema_version"] != "4" or name not in allowed or key in seen:
                raise ValueError(f"Unknown/duplicate schema-4 metric: {key}")
            seen.add(key)
            values = summary_values[key]
            if int(row["samples"]) != len(values):
                raise ValueError(f"Schema-4 summary sample count mismatch: {key}")
            unit = "ms" if category == "scope" else "count"
            if row["unit"] != unit:
                raise ValueError(f"Schema-4 unit mismatch: {key}")
            fields = ("avg", "median", "p95", "p99", "min", "max")
            if not values:
                if any(row[field] for field in fields):
                    raise ValueError(f"Inapplicable summary must be blank: {key}")
                summary[key] = None
                continue
            parsed = {field: _finite_nonnegative(row[field], f"{key}.{field}") for field in fields}
            scale = 1e6 if category == "scope" else 1.0
            expected_stats = {"avg": sum(values) / len(values) / scale,
                              "median": sorted(values)[len(values) // 2] / scale,
                              "p95": _percentile(values, .95) / scale,
                              "p99": _percentile(values, .99) / scale,
                              "min": min(values) / scale, "max": max(values) / scale}
            for field in fields:
                if not math.isclose(parsed[field], expected_stats[field], rel_tol=1e-6, abs_tol=1e-9):
                    raise ValueError(f"Schema-4 summary/raw mismatch for {key}.{field}")
            summary[key] = parsed
    required_summary = {("scope", metric) for metric in SCOPES} | {("counter", metric) for metric in COUNTERS}
    if seen != required_summary:
        raise ValueError(f"Missing schema-4 summary metrics: {required_summary - seen}")
    return {"manifest": manifest, "summary": summary, "raw_samples": raw_rows}
