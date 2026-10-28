# Spatial Replay Contract, Version 1

Status: planned deterministic input format, separate from scene serialization and benchmark output schema. Replay exercises actual selected spatial code without SDL submission or a solver-driven feedback loop. Collision and visibility traces are different domains, never interchangeable measurements.

## Files and invocation

UTF-8 JSONL: exactly one header, one initial-state record, then exactly `warmup_steps + measure_steps` step records. Each line is a complete JSON object. No comments, implicit randomness, trailing records or nonfinite numbers. A file's SHA-256 identifies its bytes; a case manifest records generator version/source hash, resolved parameters and that hash. Generate/load/parse/validate the complete trace before timed initialization. No trace JSON parsing, file I/O or oracle runs in measured scopes.

The benchmark runner accepts `--spatial-trace FILE`, mutually exclusive with `--scene`, scene positional arguments and `--scenario`. Header warmup/measurement/dt/seed are authoritative; explicit contradictory CLI values fail. Existing worker and spatial selection flags remain controls. Reject `--render` for replay: it has no SDL renderer. Both domain choices are recorded, but only the applicable domain advances. Headless visibility replay is explicitly labeled visibility replay, not a rendered frame.

New generator interface:

```sh
python3 benchmark/generate_spatial_traces.py --preset smoke --seed 0 \
  --warmup-frames 120 --measure-frames 600 --output-dir /tmp/spatial-traces
```

Presets `smoke`, `collision`, `visibility`, `evaluation`, and `calibration` produce immutable files plus `cases.json`; refuse a nonempty destination. Evaluation is the bounded matrix in [benchmark-results.md](benchmark-results.md). Calibration uses distinct case IDs/seeds, cannot be silently merged with evaluation, and freezes chosen settings before evaluation. Smoke contains one dynamic collision and one dynamic visibility trace with 1,000 endpoints, named `smoke.collision.jsonl` and `smoke.visibility.jsonl`.

`cases.json` has `format: "platformator-spatial-cases"`, `version: 1`, generator provenance, and a `cases` array. Trace entries require `case_id`, domain `collision` or `visibility`, relative `trace` path, `sha256`, resolved `parameters`, `warmup_steps`, `measure_steps`, dt and seed; metadata must agree with the referenced header. The top-level format also accepts the typed engine entries defined by the benchmark contract. Reject duplicate case IDs or hash/header mismatches.

## Header and identity

Required header fields: `type: "header"`, `format: "platformator-spatial-trace"`, `version: 1`, `domain: "collision" | "visibility"`, nonempty `case_id`, integer `seed`, `units: "world"`, positive finite `dt`, nonnegative integer `warmup_steps`, positive integer `measure_steps`, `generator` (name, revision and source hash), and `parameters` containing resolved population/distribution/change/visibility/churn controls. Reject unknown version/domain, duplicate IDs and unsupported operation fields rather than silently ignoring them.

Endpoint identity is `{"slot":"<uint64 decimal>","generation":"<uint64 decimal>"}`. Strings preserve precision in JSON consumers; slot and generation are both required. Within a trace, runtime context is implicit and isolated from other traces. Slots may be reused only after removal, with strictly increased generation. Component replacement uses a new identity. Camera IDs obey the same rule. Tree node IDs are never serialized.

Step records have `type: "step"`, contiguous zero-based `index`, and an ordered `ops` array. The first warmup_steps records are warmup; the rest are measured. Apply/coalesce all operations before one publish/query/consume boundary. Repeated valid writes coalesce according to engine behavior; updates to an already removed generation, duplicate live adds and out-of-order indexes are malformed input. Initialization builds the selected context separately from warmup and is measured as initialization, including initial pair/result construction.

## Collision payload

Initial state is `{"type":"initial","colliders":[...]}`. Each collider has `id`, `bounds:[minX,minY,maxX,maxY]`, `group` and `mask` as uint64 decimal strings, and boolean `active`. Eligibility means both endpoints active, inclusive exact bounds overlap, and both directional group/mask tests pass. Trace bounds are precomputed exact AABBs; metadata must say `bounds_source: "provided-aabb"`. This replay excludes shape/transform-to-AABB construction and cannot replace full-engine timing.

| op | Required payload | Effect |
| --- | --- | --- |
| `add` | `collider` with all fields above | Create a new endpoint; append desired membership if active |
| `remove` | `id` | Remove endpoint and all relationships before reclaim |
| `update_bounds` | `id`, `bounds` | Publish exact current geometry, including updates inside an unchanged padded envelope |
| `set_filter` | `id`, `group`, `mask` | Reconcile both mask directions even without movement |
| `set_active` | `id`, `active` | Change membership without destroying identity |

Consume persistent candidate deltas and changed-pair notifications through the shared pair-state path. Do not force a full-pair scan or query all unchanged colliders every step just to serialize output. In validation mode only, compare the complete candidate set and per-step transitions with an independent O(N²) reference outside the measured boundary. A digest/snapshot may be extracted outside timing. Narrow phase, solver and callbacks are not simulated by this domain; actual-engine fixtures separately validate those consumers.

Minimal example (each object below is one line in the file):

```jsonl
{"type":"header","format":"platformator-spatial-trace","version":1,"domain":"collision","case_id":"example","seed":0,"units":"world","dt":0.008333333333333333,"warmup_steps":0,"measure_steps":1,"generator":{"name":"example","revision":"1","source_hash":"example-only"},"parameters":{"population":2,"bounds_source":"provided-aabb"}}
{"type":"initial","colliders":[{"id":{"slot":"1","generation":"1"},"bounds":[0,0,1,1],"group":"1","mask":"1","active":true},{"id":{"slot":"2","generation":"1"},"bounds":[2,0,3,1],"group":"1","mask":"1","active":true}]}
{"type":"step","index":0,"ops":[{"op":"update_bounds","id":{"slot":"2","generation":"1"},"bounds":[1,0,2,1]}]}
```

Expected final candidate: the one touching pair, exactly once. Production generator source_hash must be an actual SHA-256; the example is illustrative, not an eligible evidence bundle.

## Visibility payload

Initial state has `type: "initial"`, ordered `sprites`, and `view`. Each sprite has `id`, `center:[x,y]` in current world coordinates, `dimensions:[w,h]` before object scale, `scale:[x,y]`, `angle_degrees`, and boolean `active`. View has camera `id`, top-left `position:[x,y]`, positive `size:[w,h]`, and positive integer `output:[width,height]`. This domain supplies transforms, not final bounds: forming current conservative destination-quad bounds is inside the measured boundary. Metadata says `bounds_source: "sprite-transform"` and `renderer: "none-replay"`.

| op | Required payload | Effect |
| --- | --- | --- |
| `add` | `sprite` with all fields above | Retain identity; append to submission order if active |
| `remove` | `id` | Remove using engine swap-with-last order semantics |
| `update_sprite` | `id`, one or more of center/dimensions/scale/angle_degrees | Coalesced geometry update |
| `set_active` | `id`, `active` | Deactivation removes by swap-with-last; reactivation appends without changing identity; repeated identical values do not reorder |
| `set_view` | Complete `view` | Pan/jump/resize or replace camera; trigger required aspect invalidation |

Initial active-array order and membership operations define the engine-equivalent current draw order. Output is the unique ordered conservative candidate list. None produces all current renderable sprites; culled methods may include conservative extras but cannot miss a sprite that can affect the viewport. Validation independently projects the actual quad into output space, tests viewport intersection with the declared raster guard, and checks order/deduplication; it does not call the implementation's bounds helper as its reference. A linear result alone is not an independent oracle. Replay checks numeric/order contracts; same-renderer image tests require full-engine fixtures.

This format covers supported finite view state. Actual invalid camera, malformed sprite and external SDL state handling is exercised by engine tests, not accepted as an eligible normal trace. No-camera/paused/parent mutation tests also belong to runtime integration coverage.

## Timing and validation

Each step's timed `spatial_replay` covers applying preloaded operations, bounds formation where applicable, selected index mutation/maintenance, queries, reconciliation/deduplication and ordered consumer results. Supplied collision AABB loading/copying is included; excluded geometry construction is disclosed. Current persistent outputs may be reused on zero-change steps without an artificial full enumeration.

Validation mode checks every small/randomized step; large correctness cases use an independent sweep reference where quadratic work is impractical. Matching backends use identical trace hashes. Validation failures, unsupported bounds and trace/header mismatches make the run ineligible and return nonzero. Capture modes, scopes and resources follow [benchmark-results.md](benchmark-results.md).
