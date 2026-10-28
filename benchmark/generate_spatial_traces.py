#!/usr/bin/env python3
"""Generate deterministic spatial replay traces and their immutable case manifest."""

import argparse
import copy
import hashlib
import json
import math
import random
import sys
from pathlib import Path


REVISION = "spatial-trace-generator-v2"
DEFAULT_DT = 1.0 / 120.0
CAMERA_ID = {"slot": "18446744073709551000", "generation": "1"}


def rounded_count(population, percent):
    return int(math.floor(population * percent / 100.0 + 0.5))


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True, allow_nan=False)


def identity(slot, generation=1):
    return {"slot": str(slot), "generation": str(generation)}


def stable_sample(rng, population, count):
    return sorted(rng.sample(range(population), count)) if count else []


OPERATION_COUNT_SEMANTICS = {
    "step_scope": "Per-step entries follow trace step index and include warmup steps.",
    "changed_endpoint_count": "Unique slots whose final state or generation differs from the start of the step.",
    "changed_geometry_endpoint_count": "Unique slots whose final geometry differs, including endpoint replacement/removal/addition.",
    "geometry_write_count": "Explicit update_bounds/update_sprite operations, including no-op writes.",
    "churn_replacement_count": "Slots removed and re-added in one step with a newer generation.",
    "filter_write_count": "set_filter operations, including writes that keep the existing value.",
    "filter_value_change_count": "Unique same-generation endpoints whose final filter differs from step start.",
    "coalesced_operation_count": "Writes beyond the first write to the same endpoint property in one step; a coalesced write can be a no-op.",
}


def _state_record(domain, value):
    if domain == "collision":
        return {"id": dict(value["id"]), "bounds": list(value["bounds"]), "group": value["group"],
                "mask": value["mask"], "active": value["active"]}
    return {"id": dict(value["id"]), "center": list(value["center"]),
            "dimensions": list(value["dimensions"]), "scale": list(value["scale"]),
            "angle_degrees": value["angle_degrees"], "active": value["active"]}


def derive_operation_counts(domain, initial_record, steps):
    """Derive actual per-step mutation counts from the generated trace operations."""
    if domain not in {"collision", "visibility"}:
        raise ValueError(f"Unsupported operation-count domain: {domain}")
    collection = "colliders" if domain == "collision" else "sprites"
    states = {int(item["id"]["slot"]): _state_record(domain, item) for item in initial_record[collection]}
    per_step = []
    geometry_fields = {"update_bounds": ("bounds",),
                       "update_sprite": ("center", "dimensions", "scale", "angle_degrees")}

    for step in steps:
        before = {}
        writes = {}
        removed_generations = {}
        added_generations = {}
        geometry_write_count = 0
        filter_write_count = 0

        def remember(slot):
            if slot not in before:
                before[slot] = copy.deepcopy(states.get(slot))

        def count_write(slot, generation, field):
            key = (slot, generation, field)
            writes[key] = writes.get(key, 0) + 1

        for operation in step["ops"]:
            name = operation["op"]
            if name == "set_view":
                # Camera/view changes are workload context, not endpoint writes.
                continue
            if name == "add":
                value = operation["collider"] if domain == "collision" else operation["sprite"]
                slot = int(value["id"]["slot"])
                generation = int(value["id"]["generation"])
                remember(slot)
                added_generations.setdefault(slot, []).append(generation)
                states[slot] = _state_record(domain, value)
                continue

            identity_value = operation["id"]
            slot = int(identity_value["slot"])
            generation = int(identity_value["generation"])
            remember(slot)
            current = states[slot]
            if int(current["id"]["generation"]) != generation:
                raise ValueError(f"Operation count derivation saw stale identity at step {step['index']}")
            if name == "remove":
                removed_generations.setdefault(slot, []).append(generation)
                del states[slot]
            elif name == "update_bounds":
                geometry_write_count += 1
                count_write(slot, generation, "bounds")
                current["bounds"] = list(operation["bounds"])
            elif name == "update_sprite":
                geometry_write_count += 1
                for field in geometry_fields[name]:
                    if field in operation:
                        count_write(slot, generation, field)
                        current[field] = copy.deepcopy(operation[field])
            elif name == "set_filter":
                filter_write_count += 1
                for field in ("group", "mask"):
                    count_write(slot, generation, field)
                    current[field] = operation[field]
            elif name == "set_active":
                count_write(slot, generation, "active")
                current["active"] = operation["active"]

        replacement_count = sum(
            min(len(removed_generations.get(slot, [])), len(added_generations.get(slot, [])))
            for slot in set(removed_generations) & set(added_generations)
            if any(new > old for new in added_generations[slot] for old in removed_generations[slot])
        )
        changed_slots = {slot for slot, old in before.items() if old != states.get(slot)}
        changed_geometry_slots = set()
        filter_changed_slots = set()
        geometry_keys = ("bounds",) if domain == "collision" else ("center", "dimensions", "scale", "angle_degrees")
        for slot, old in before.items():
            new = states.get(slot)
            if old != new and (old is None or new is None or old["id"] != new["id"] or
                               any(old.get(field) != new.get(field) for field in geometry_keys)):
                changed_geometry_slots.add(slot)
            if (domain == "collision" and old is not None and new is not None and old["id"] == new["id"] and
                    (old["group"], old["mask"]) != (new["group"], new["mask"])):
                filter_changed_slots.add(slot)
        per_step.append({
            "changed_endpoint_count": len(changed_slots),
            "changed_geometry_endpoint_count": len(changed_geometry_slots),
            "geometry_write_count": geometry_write_count,
            "churn_replacement_count": replacement_count,
            "filter_write_count": filter_write_count,
            "filter_value_change_count": len(filter_changed_slots),
            "coalesced_operation_count": sum(count - 1 for count in writes.values() if count > 1),
        })
    return {"semantics": OPERATION_COUNT_SEMANTICS, "per_step": per_step}


def base_case(case_id, domain, parameters, seed, warmup, measure, dt):
    return {
        "type": "header",
        "format": "platformator-spatial-trace",
        "version": 1,
        "domain": domain,
        "case_id": case_id,
        "seed": seed,
        "units": "world",
        "dt": dt,
        "warmup_steps": warmup,
        "measure_steps": measure,
        "generator": {},
        "parameters": parameters,
    }


def collision_initial(population, variant):
    colliders = []
    for slot in range(population):
        if variant == "dense":
            x = float((slot % 8) * 0.5)
            y = float((slot // 8) * 0.5)
            width = height = 16.0
        elif variant == "skewed":
            x = float((slot % 16) * 4)
            y = float((slot % 16) * 3)
            width = height = 12.0
        elif variant == "mixed-sizes":
            x = float(slot * 80)
            y = float((slot % 7) * 70)
            width = height = 12.0 if slot % 10 else 120.0
        else:
            x = float(slot * 80)
            y = float((slot % 13) * 80)
            width = height = 12.0
        colliders.append(
            {
                "id": identity(slot + 1),
                "bounds": [x, y, x + width, y + height],
                "group": str(1),
                "mask": str((1 << 64) - 1),
                "active": True,
            }
        )
    return colliders


def collision_trace(case_id, population, change_percent, variant, seed, warmup, measure, dt, generator):
    changed = rounded_count(population, change_percent)
    parameters = {
        "population": population,
        "distribution": "uniform-sparse" if variant == "sparse" else variant,
        "variant": variant,
        "changed_percent": change_percent,
        "changed_percent_semantics": "requested workload knob; not observed operation count",
        "requested_changed_endpoint_count": changed,
        "bounds_source": "provided-aabb",
    }
    header = base_case(case_id, "collision", parameters, seed, warmup, measure, dt)
    header["generator"] = generator
    initial = collision_initial(population, variant)
    initial_state = [dict(endpoint, id=dict(endpoint["id"]), bounds=list(endpoint["bounds"])) for endpoint in initial]
    steps = []
    total = warmup + measure
    rng = random.Random(seed ^ 0xC0111DE)
    generations = [1] * population
    for index in range(total):
        operations = []
        if variant == "churn" and population:
            count = min(10, population)
            selected = stable_sample(rng, population, count)
            for slot in selected:
                old = initial[slot]
                operations.append({"op": "remove", "id": old["id"]})
                generations[slot] += 1
                x = float((slot + index + 1) * 80)
                endpoint = {
                    "id": identity(slot + 1, generations[slot]),
                    "bounds": [x, 0.0, x + 12.0, 12.0],
                    "group": "1",
                    "mask": str((1 << 64) - 1),
                    "active": True,
                }
                operations.append({"op": "add", "collider": endpoint})
                initial[slot] = endpoint
        else:
            selected = stable_sample(rng, population, changed)
            for slot in selected:
                endpoint = initial[slot]
                bounds = endpoint["bounds"]
                if variant in {"migration", "teleport"}:
                    shift = 260.0 if (index + slot) % 2 == 0 else -260.0
                elif variant == "dense":
                    shift = 0.125 if (index + slot) % 2 == 0 else -0.125
                else:
                    shift = 2.0 if (index + slot) % 2 == 0 else -2.0
                next_bounds = [bounds[0] + shift, bounds[1], bounds[2] + shift, bounds[3]]
                operation = {"op": "update_bounds", "id": endpoint["id"], "bounds": next_bounds}
                operations.append(operation)
                if variant == "coalesced":
                    operations.append({"op": "update_bounds", "id": endpoint["id"], "bounds": next_bounds})
                endpoint["bounds"] = next_bounds
            if variant == "filter-changes" and population:
                slot = index % population
                operations.append(
                    {"op": "set_filter", "id": initial[slot]["id"], "group": str(1 << (index % 4)),
                     "mask": str((1 << 64) - 1)}
                )
        steps.append({"type": "step", "index": index, "ops": operations})
    initial_record = {"type": "initial", "colliders": initial_state}
    parameters["operation_counts"] = derive_operation_counts("collision", initial_record, steps)
    return [header, initial_record, *steps]


def visibility_initial(population, visible_count):
    sprites = []
    columns = 160
    for slot in range(population):
        if slot < visible_count:
            x = 8.0 + (slot % columns) * 9.0
            y = 8.0 + (slot // columns) * 8.0
        else:
            x = 100000.0 + (slot - visible_count) * 12.0
            y = 100000.0
        sprites.append(
            {
                "id": identity(slot + 1),
                "center": [x, y],
                "dimensions": [6.0, 6.0],
                "scale": [1.0, 1.0],
                "angle_degrees": 0.0,
                "active": True,
            }
        )
    view = {"id": CAMERA_ID, "position": [0.0, 0.0], "size": [1600.0, 900.0], "output": [1600, 900]}
    return sprites, view


def visibility_trace(case_id, population, visible_percent, change_percent, variant, seed, warmup, measure, dt,
                     generator):
    visible = rounded_count(population, visible_percent)
    changed = rounded_count(population, change_percent)
    parameters = {
        "population": population,
        "distribution": "viewport-sparse",
        "variant": variant,
        "visible_percent": visible_percent,
        "visible_count": visible,
        "changed_percent": change_percent,
        "changed_percent_semantics": "requested workload knob; not observed operation count",
        "requested_changed_endpoint_count": changed,
        "bounds_source": "sprite-transform",
        "renderer": "none-replay",
        "raster_guard_output_pixels": 1,
    }
    header = base_case(case_id, "visibility", parameters, seed, warmup, measure, dt)
    header["generator"] = generator
    sprites, view = visibility_initial(population, visible)
    initial = {"type": "initial", "sprites": [dict(sprite) for sprite in sprites], "view": dict(view)}
    steps = []
    total = warmup + measure
    rng = random.Random(seed ^ 0x7151B111)
    for index in range(total):
        operations = []
        if variant in {"pan", "jump", "resize", "aspect"}:
            if variant == "pan":
                view["position"] = [float(index % 17), float(index % 11)]
            elif variant == "jump":
                view["position"] = [0.0 if index % 2 else 1600.0, 0.0]
            elif variant == "resize":
                view["size"] = [1600.0 + float(index % 5) * 20.0, 900.0 + float(index % 3) * 12.0]
            else:
                view["size"] = [1600.0, 900.0 + float(index % 4) * 150.0]
            operations.append({"op": "set_view", "view": dict(view)})

        selected = stable_sample(rng, population, changed)
        for slot in selected:
            sprite = sprites[slot]
            x, y = sprite["center"]
            delta = 2.0 if (slot + index) % 2 == 0 else -2.0
            next_center = [x + delta, y]
            operations.append({"op": "update_sprite", "id": sprite["id"], "center": next_center})
            sprite["center"] = next_center

        if variant == "churn" and population:
            slot = index % population
            old = sprites[slot]
            operations.append({"op": "remove", "id": old["id"]})
            generation = int(old["id"]["generation"]) + 1
            replacement = dict(old)
            replacement["id"] = identity(slot + 1, generation)
            replacement["center"] = [float(8 + slot % 160 * 9), float(8 + slot // 160 * 8)]
            operations.append({"op": "add", "sprite": replacement})
            sprites[slot] = replacement
        steps.append({"type": "step", "index": index, "ops": operations})
    parameters["operation_counts"] = derive_operation_counts("visibility", initial, steps)
    return [header, initial, *steps]


def percent_cases(prefix, population_values, percentages):
    return [(f"{prefix}-n{population}-p{str(percent).replace('.', 'p')}", population, percent)
            for population in population_values for percent in percentages]


def trace_specs(preset, seed):
    if preset == "smoke":
        return [
            ("collision", "smoke-collision", 1000, 0.0, 1.0, "sparse", seed),
            ("visibility", "smoke-visibility", 1000, 10.0, 1.0, "static", seed),
        ]
    if preset == "collision":
        specs = [("collision", name, n, 0.0, percent, "sparse", seed)
                 for name, n, percent in percent_cases("collision", (100, 1000, 10000), (0, 1, 10, 100))]
        specs.extend(("collision", f"collision-n1000-{variant}", 1000, 10.0, 10.0, variant, seed + offset)
                     for offset, variant in enumerate(("churn", "migration", "teleport", "skewed", "mixed-sizes",
                                                       "filter-changes", "coalesced"), start=1))
        specs.append(("collision", "collision-dense-n128", 128, 100.0, 100.0, "dense", seed + 20))
        return specs
    if preset == "visibility":
        specs = [("visibility", name, n, percent, 0.0, "static", seed)
                 for name, n, percent in percent_cases("visibility", (100, 1000, 10000), (0, 1, 10, 100))]
        specs.extend(("visibility", f"visibility-n1000-change{change}-{variant}", 1000, 10.0, change, variant,
                      seed + offset)
                     for offset, (change, variant) in enumerate(
                         ( (0, "static"), (1, "moving"), (10, "moving"), (100, "moving"), (10, "pan"),
                           (10, "jump"), (10, "resize"), (10, "aspect"), (10, "churn") ), start=1))
        return specs
    if preset == "calibration":
        return [
            ("collision", "calibration-collision-1pct", 1000, 0.0, 1.0, "sparse", seed + 10000),
            ("collision", "calibration-collision-10pct", 1000, 0.0, 10.0, "sparse", seed + 10001),
            ("visibility", "calibration-visibility-1pct", 1000, 1.0, 1.0, "moving", seed + 10002),
            ("visibility", "calibration-visibility-10pct", 1000, 10.0, 10.0, "moving", seed + 10003),
        ]
    if preset == "evaluation":
        return trace_specs("collision", seed) + trace_specs("visibility", seed)
    raise ValueError(f"Unknown preset: {preset}")


def engine_case(case_id, scenario, parameters, render, seed, warmup, measure, dt):
    case = {"case_id": case_id, "domain": "engine", "scenario": scenario,
            "parameters": parameters, "render": render, "warmup_steps": warmup,
            "measure_steps": measure, "dt": dt, "seed": seed}
    case["sha256"] = hashlib.sha256(canonical_json(case).encode("ascii")).hexdigest()
    return case


def engine_cases(seed, warmup, measure, dt):
    """Return bounded, typed engine fixtures with per-entry canonical hashes."""
    cases = []
    for count in (100, 500, 1000):
        cases.append(engine_case(f"engine-broad-n{count}", "broad_phase", {"body_count": count}, False,
                                 seed, warmup, measure, dt))
    for count in (50, 150, 300):
        cases.append(engine_case(f"engine-broad-stress-n{count}", "broad_phase_stress", {"body_count": count}, False,
                                 seed + 1, warmup, measure, dt))
    for count in (10, 100, 200):
        cases.append(engine_case(f"engine-narrow-n{count}", "narrow_phase", {"element_count": count}, False,
                                 seed + 2, warmup, measure, dt))
    for count in (100, 500, 1000):
        boxes = count // 2
        cases.append(engine_case(f"engine-container-n{count}", "rigid_body_container",
                                 {"box_count": boxes, "circle_count": count - boxes}, False,
                                 seed + 3, warmup, measure, dt))
    for scenario in ("add_and_remove", "moving_add_and_remove"):
        for count in (10, 100, 1000):
            cases.append(engine_case(f"engine-{scenario}-churn{count}", scenario,
                                     {"steady_count": 1000, "churn_count": count}, False,
                                     seed + 4, warmup, measure, dt))
    for endpoints in (0, 2):
        cases.append(engine_case(f"engine-persistent-pairs100-scripted{endpoints}", "persistent_contacts",
                                 {"contact_pair_count": 100, "scripted_endpoints": endpoints}, False,
                                 seed + 5, warmup, measure, dt))
    for percent in (0, 1, 10, 100):
        cases.append(engine_case(f"engine-sprite-visible{percent}", "sprite_visibility",
                                 {"sprite_count": 1000, "visible_percent": float(percent),
                                  "changed_percent": 0.0, "variant": "static"}, True,
                                 seed + 6, warmup, measure, dt))
    for offset, variant in enumerate(("moving", "pan", "jump", "resize", "animation", "churn"), start=7):
        cases.append(engine_case(f"engine-sprite-{variant}", "sprite_visibility",
                                 {"sprite_count": 1000, "visible_percent": 1.0,
                                  "changed_percent": 10.0, "variant": variant}, True,
                                 seed + offset, warmup, measure, dt))
    cases.append(engine_case("engine-spatial-combined", "spatial_combined",
                             {"sprite_count": 1000, "visible_percent": 1.0,
                              "changed_percent": 10.0, "variant": "moving"}, True,
                             seed + 13, warmup, measure, dt))
    return cases


def write_bundle(args):
    output = Path(args.output_dir).resolve()
    if output.exists() and any(output.iterdir()):
        raise FileExistsError(f"Refusing to write into nonempty output directory: {output}")
    output.mkdir(parents=True, exist_ok=True)
    source_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    generator = {"name": "generate_spatial_traces.py", "revision": REVISION, "source_hash": source_hash}
    if args.preset == "engine":
        manifest = {"format": "platformator-spatial-cases", "version": 1,
                    "generator": generator,
                    "cases": engine_cases(args.seed, args.warmup_frames, args.measure_frames, args.dt)}
        (output / "cases.json").write_text(json.dumps(manifest, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        print(f"Wrote {len(manifest['cases'])} typed engine cases to {output}")
        return
    cases = []
    for domain, case_id, population, percent_a, percent_b, variant, case_seed in trace_specs(args.preset, args.seed):
        if domain == "collision":
            records = collision_trace(case_id, population, percent_b, variant, case_seed,
                                      args.warmup_frames, args.measure_frames, args.dt, generator)
            trace_name = case_id.replace("-", ".") + ".jsonl"
            parameters = dict(records[0]["parameters"])
            bounds_source = "provided-aabb"
        else:
            records = visibility_trace(case_id, population, percent_a, percent_b, variant, case_seed,
                                       args.warmup_frames, args.measure_frames, args.dt, generator)
            trace_name = case_id.replace("-", ".") + ".jsonl"
            parameters = dict(records[0]["parameters"])
            bounds_source = "sprite-transform"
        trace_path = output / trace_name
        payload = "".join(canonical_json(record) + "\n" for record in records).encode("utf-8")
        trace_path.write_bytes(payload)
        cases.append(
            {
                "case_id": case_id,
                "domain": domain,
                "trace": trace_name,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "parameters": parameters,
                "warmup_steps": args.warmup_frames,
                "measure_steps": args.measure_frames,
                "dt": args.dt,
                "seed": case_seed,
                "bounds_source": bounds_source,
            }
        )
    manifest = {
        "format": "platformator-spatial-cases",
        "version": 1,
        "generator": generator,
        "cases": cases,
    }
    (output / "cases.json").write_text(json.dumps(manifest, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {len(cases)} spatial trace cases to {output}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", required=True, choices=("smoke", "collision", "visibility", "evaluation", "calibration", "engine"))
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--warmup-frames", type=int, default=120)
    parser.add_argument("--measure-frames", type=int, default=600)
    parser.add_argument("--dt", type=float, default=DEFAULT_DT)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args(argv)
    if args.seed < 0 or args.warmup_frames < 0 or args.measure_frames <= 0:
        parser.error("seed/warmup must be nonnegative and measure-frames must be positive")
    if not math.isfinite(args.dt) or args.dt <= 0:
        parser.error("dt must be finite and positive")
    try:
        write_bundle(args)
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
