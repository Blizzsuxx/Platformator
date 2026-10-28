# Spatial Benchmark and Evidence Contract

Status: planned **output schema 4**. Input trace version 1 is separate. No performance result is asserted by these planning documents.

## Modes, commands and compatibility

The normal benchmark runner adds `--capture-mode performance|validate` (default performance). Existing `--validate-invariants` maps to validate; an explicit conflicting mode fails. Validation executes independent oracles/readback outside timed scopes and sets `timing_eligible=false`. Normal performance captures do not run shadow indexes, full overlap references or image comparisons. Lightweight existing boundary invariants remain outside measured scopes.

Add a separate `platformator_spatial_resource_runner` with the same workload/selection parser and `--capture-mode resource`; it defaults to resource and rejects performance. Build it only when benchmarks are enabled. The normal executable rejects resource with a message naming this executable. Allocation interceptors are linked only into the resource executable. All resource captures are timing-ineligible, even if durations are emitted.

New orchestration interface:

```sh
python3 benchmark/sweep_spatial.py --runner bin/cleanup-benchmark/platformator_benchmark_runner \
  --cases /tmp/spatial-traces/cases.json --broad-phases segmented,bvh \
  --render-cullings none,linear,bvh,segmented --worker-counts 1,6 \
  --repetitions 5 --capture-mode performance --validation-dir /tmp/spatial-validation \
  --output-dir /tmp/spatial-evaluation
```

Comma-separated method/worker lists must be nonempty and unique. Collision traces vary broad phase only; visibility traces vary render culling only. Record the inactive domain's fixed default. An explicit engine-case manifest can request all eight combinations. No silently multiplying irrelevant settings into duplicate replay cases. Validate the declared available worker allowance; do not assume six workers on every machine. The sweep passes BVH maintenance/padding/query flags from the [runtime contract](runtime-selection.md), defaults as there, and refuses existing output files. Validation/resource sweeps may use one repetition; reported performance comparisons require at least five fresh processes per case/method/worker/profile.

Performance sweeps require `--validation-dir` pointing to a completed matching validation bundle; match case hash, effective profile, worker allowance and source fingerprint. A standalone runner may produce an otherwise timing-capable capture before that pairing, but aggregate eligibility remains pending until a matching validation is linked. The sweep produces an eligibility ledger, retained per-run artifacts, checksums, and `summary.csv`/`report.md`; rejected captures remain in the bundle with reasons. Resource bundles may be linked later with the same keys.

Engine-case manifests use `format: "platformator-spatial-cases"`, `version: 1`, generator provenance and a `cases` array. Each entry has unique `case_id`, domain `engine`, resolved `scenario`, typed `parameters`, `render` boolean, `warmup_steps`, `measure_steps`, dt/seed and `sha256`. The engine hash is SHA-256 of UTF-8 JSON for that entry excluding sha256, with sorted object keys, no whitespace, finite numbers and ASCII escaping; emit it using the generator's canonical serializer. Trace entries instead reference the trace path/hash as defined by the replay contract. The sweep translates typed parameters to documented flags, never executes arbitrary shell text from a manifest.

New engine scenarios `sprite_visibility` and `spatial_combined` accept `--sprite-count N`, `--visible-percent P` and `--changed-percent P` (positive count, finite percentages in [0,100]); record exact rounded integer counts. Defaults are 1,000 sprites, 10% visible and 10% changed; combined additionally uses 128 moving colliders. Sprite-only cases contain no gameplay colliders. With `--render`, use the existing 1,600x900 runner output and record actual output/view state. Headless use is allowed for combined physics checks but provides no rendering evidence. `--spatial-variant static|moving|pan|jump|resize|animation|churn` selects a versioned fixture variant (default static); record it in case parameters. These are bounded fixtures, not an open-ended scripting interface.

Extend existing sweep/plot tooling with explicit schema-3 and schema-4 dispatch. Preserve exact schema-3 field sets, meanings and historical files; reject mixed-schema aggregates and unknown versions. Schema-4 validators require its complete defined fields. Legacy scenarios and CLI remain supported. A separate comparison report may align common equivalent boundaries across versions, naming each source/schema, but must never relabel historical rows as schema 4.

## Per-process artifacts

`--csv-output DIR/run.csv` writes:

- `run.csv`: summaries with columns `schema_version,category,name,samples,avg,median,p95,p99,min,max,unit`. Category is scope or counter; exact allowed metric sets follow below. Empty inapplicable metrics have zero samples, with applicability declared in the manifest.
- `run.samples.csv`: raw records with `schema_version,sample_kind,index`, every scope suffixed `_ns`, and every counter. sample_kind is `step`, `render`, or `replay`; each sequence has contiguous zero-based indexes. Fields outside the sample kind's domain are empty, not fabricated zero-duration samples. Applicable skipped work is zero. No actual render rows in headless/replay captures.
- `run.manifest.json`: required provenance, applicability, configuration, initialization, validation and resource records below.
- `run.log`: retained stdout/stderr and exit status supplied by the sweep.

Write failures are fatal. A requested measured count must match actual samples for its domain. Warmup records are not measurement rows. Record quit/early termination as incomplete, never an eligible shorter run.

### Scope names and boundaries

Retain the eight schema-3 names and meanings: `complete_step`, `physics`, `cleanup`, `scene_transition`, `render`, `broad_phase`, `narrow_phase`, `resolve_collisions`. Physics/subsystem scopes are nested in complete_step; render is independent. Existing broad_phase remains its named PhysicsManager boundary so before/after comparisons retain meaning.

Add these names:

| Scope | Boundary |
| --- | --- |
| `spatial_update` | All selected physics reconciliation flushes, including those initiated by cleanup/lifecycle outside the named broad_phase scope |
| `render_culling` | Selected visibility dirty-bound preparation, index work, query/refinement, deduplication and draw-order restoration/cache lookup |
| `render_submit` | Actual ordered sprite submissions; renderer presentation remains inside the existing complete render scope |
| `spatial_replay` | Preloaded step operations through selected-domain persistent consumer output, no engine step or SDL draw |
| `bounds_prepare` | Dirty geometry -> conservative bounds, within its physics/render/replay domain |
| `index_update` | Insert/remove/category/bounds writes, including segmented membership and local interval work |
| `index_maintenance` | Separately scheduled tree rebuild work; segmented repairs remain in index_update unless explicitly split without overlap |
| `index_query` | Read-only tree/linear queries; segmented transition discovery stays in index_update |
| `result_reduce` | Delta merging, exact postfilter, deduplication, common pair updates or visible-list ordering; the linear visibility ordered scan and exact refinement are charged here as one fused O(N) pass |

Domain-specific samples avoid conflating physics and visibility subscopes. Timers use non-overlapping sibling boundaries where possible; nested scope relations are declared in the manifest and are never summed into a total. Segmented parallel phase durations are wall time, not a sum of worker times. Full step includes all required real-engine physics bounds and cleanup; visibility work is charged to the rendered frame. Collision replay explicitly excludes precomputed geometry construction. Draw/present cost is included in render even if the selected renderer synchronizes or queues work differently.

### Counter names

Retain existing counters with unchanged meanings: `object_count`, `occupied_cell_count`, `candidate_pair_count`, `pending_narrow_phase_pair_count`, `active_collision_count`, `awake_dynamic_body_count`, `queued_add_count`, `queued_sync_count`, `queued_remove_count`, `collision_enter_event_count`, `collision_exit_event_count`. Candidate count means current canonical physics pairs, not raw tree discoveries. Grid cell count is zero for physics BVH, with its applicability documented.

Add exactly: `spatial_proxy_count`, `changed_bounds_count`, `filter_change_count`, `proxy_insert_count`, `proxy_remove_count`, `proxy_move_count`, `envelope_reuse_count`, `query_count`, `raw_discovery_count`, `exact_overlap_count`, `pair_add_count`, `pair_remove_count`, `rebuild_call_count`, `rebuild_item_count`, `tree_height`, `tree_node_count`, `sprite_count`, `visible_candidate_count`, `submitted_sprite_count`, `uncullable_sprite_count`, `culling_bypass_count`, `visibility_cache_hit_count`, `visibility_cell_count`.

Counts describe work **per sample**, except populations/tree diagnostics/current candidate counts, which are end-of-sample gauges. `pair_add_count` and `pair_remove_count` count logical net transitions published after the complete dirty batch reconciles. A retained relationship must not incur a remove/add merely because its endpoints or cell witnesses were visited sequentially. Independent validation requires exact equality with the before/after candidate-set differences. Internal witness discoveries and duplicates remain measured separately in `raw_discovery_count`. `exact_overlap_count` counts unique exact eligible relationships examined in that sample, not the persistent pair population. `raw_discovery_count` includes duplicates/envelope false positives. Changed bounds count uses actual dirty exact geometry, not only tree structural mutations. Tree metrics refer to that sample's domain; non-BVH metrics are inapplicable. Rebuild item count follows Box2D's returned build-item semantics, including retained subtrees, rather than being mislabeled dirty leaves. Visible candidates count conservative unique retained sprites after refinement; submitted count is actual SDL submissions and is inapplicable in replay. Bypass count is 0 or 1 per rendered frame; reason counts are also retained in the manifest.

### Normative metric applicability matrix

The writer, reader, tests and sweep eligibility checks use this matrix. `E` means an engine step sample, `R` an actual rendered frame, `C` a collision replay step and `V` a visibility replay step. `all` includes both selected algorithms in the named domain; `seg` means segmented; `bvh` means dynamic BVH; `linear` means linear culling. A blank CSV field means inapplicable. An applicable operation that did no work is numeric zero. A metric whose value cannot be established because a view was bypassed is blank and has an explicit `unavailable` reason in the manifest; it must never be encoded as zero.

| Metrics | E | R | C | V | Additional condition |
| --- | --- | --- | --- | --- | --- |
| Legacy scopes `complete_step`, `physics`, `cleanup`, `scene_transition`, `broad_phase`, `narrow_phase`, `resolve_collisions` | applicable | blank | blank | blank | Existing schema-3 meanings retained |
| Legacy scope `render` | blank | applicable | blank | blank | Actual renderer call only |
| `spatial_update` | applicable | blank | blank | blank | Zero when no reconciliation flush occurred |
| `render_culling`, `render_submit` | blank | applicable | blank | blank | Actual renderer frames only |
| `spatial_replay` | blank | blank | applicable | applicable | One row per trace step |
| `bounds_prepare` | applicable for segmented/BVH physics | applicable for linear/BVH/segmented culling | blank | applicable for linear/BVH/segmented culling | Collision replay supplies AABBs, so construction is excluded |
| `index_update`, `index_maintenance`, `result_reduce` | applicable for segmented/BVH physics | applicable for linear/BVH/segmented culling | applicable for segmented/BVH collision | applicable for linear/BVH/segmented visibility | For an active method, skipped work is zero; none has these phases inapplicable |
| `index_query` | applicable for BVH physics | applicable for linear/BVH culling | applicable for BVH collision | applicable for linear/BVH visibility | Segmented interval discovery is charged to `index_update`; none and segmented have no separate query scope |
| Legacy `object_count`, `pending_narrow_phase_pair_count`, `active_collision_count`, `awake_dynamic_body_count`, `queued_add_count`, `queued_sync_count`, `queued_remove_count`, `collision_enter_event_count`, `collision_exit_event_count` | applicable | blank | blank | blank | Existing schema-3 meanings retained |
| Legacy `occupied_cell_count` | applicable | blank | blank | blank | Applicable only to segmented physics; blank for BVH |
| Legacy `candidate_pair_count` | applicable | blank | applicable | blank | Current persistent canonical pair gauge |
| `spatial_proxy_count`, `changed_bounds_count`, `proxy_insert_count`, `proxy_remove_count`, `proxy_move_count` | applicable for segmented/BVH physics | applicable for linear/BVH/segmented culling | applicable for segmented/BVH collision | applicable for linear/BVH/segmented visibility | End gauge for proxy population, per-sample counts for the rest; none is blank |
| `filter_change_count`, `envelope_reuse_count`, `pair_add_count`, `pair_remove_count` | applicable for segmented/BVH physics | blank | applicable for segmented/BVH collision | blank | Envelope reuse is zero for segmented; physics pair metrics are never visibility metrics |
| `query_count`, `raw_discovery_count`, `exact_overlap_count` | applicable for segmented/BVH physics | applicable for linear/BVH/segmented culling | applicable for segmented/BVH collision | applicable for linear/BVH/segmented visibility | Counts selected-domain queries/discoveries/exact refinements; zero if active method skipped that phase |
| `rebuild_call_count`, `rebuild_item_count`, `tree_height`, `tree_node_count` | applicable for BVH physics | applicable for BVH culling | applicable for BVH collision | applicable for BVH visibility | Tree gauges use end-of-sample state; blank for non-BVH methods |
| `sprite_count` | blank | applicable | blank | applicable | Active ordered sprite population gauge |
| `visible_candidate_count` | blank | applicable for linear/BVH/segmented culling | blank | applicable for all methods | Actual none rendering does not compute visibility; none replay returns all active sprites. Candidate count is blank when visibility is unknown after bypass |
| `uncullable_sprite_count`, `culling_bypass_count`, `visibility_cache_hit_count` | blank | applicable for linear/BVH/segmented culling | blank | applicable for linear/BVH/segmented culling | These describe culling work and are blank for none |
| `submitted_sprite_count` | blank | applicable | blank | blank | Actual SDL submissions only; never synthesized for visibility replay |
| `visibility_cell_count` | blank | applicable for segmented culling | blank | applicable for segmented visibility | Segmented visibility context only; blank for other methods |

For `render_culling` and `V`, a valid cached result still has a complete set of applicable counters: query and index-work counters are zero and `visibility_cache_hit_count` is one. A bypass preserves submitted-sprite counts and records its reason, but leaves visibility candidate count blank because no indexed visibility result was established. An uncullable sprite increments `uncullable_sprite_count`; that frame and its entire run are classified as mixed/fallback for pure-index eligibility, even if the conservative submission is correct. A selected `none` control records submissions for actual `R`, and returns all live sprites as candidates for replay `V`; index-specific metric families are inapplicable. No-camera/headless operation has no `R` sample and does not manufacture visibility work rows.

On an invalidated linear visibility frame, `query_count` remains one and the
authoritative unique draw-order vector is scanned once. The scan performs exact
refinement and emits output in order directly, so it has no candidate sort or
deduplication. `index_query` is zero for this fused path; its scan/refinement
duration is recorded once in `result_reduce`. An unchanged cached frame retains
the cache-hit path and reports no scan. Indexed sparse paths continue to sort
and deduplicate their discovered candidates before refinement.

### Required manifest groups

Preserve existing schema-3 provenance groups and add:

- `schema_version: 4`, capture mode, timing_eligible, completion/error status, requested/actual warmup and measured counts for each sample domain, run/repetition identity.
- Input: domain (`engine`, `collision-replay`, `visibility-replay`), case ID/hash, generator revision/hash and resolved geometry/distribution/change/visibility/churn parameters, dt/seed, collision bounds_source and excluded preparation.
- Requested/effective broad phase, render mode/status, maintenance policy, padding/units, query execution, cell/chunk constants, engine scheduling policy, adapter revision and Box2D source/checksum reference. No-camera/unsupported-view/hybrid reasons and affected counts are explicit.
- Source commit, dirty state plus source fingerprint, compiler/version/flags, configuration, benchmark/work/sanitizer/debug instrumentation, dependencies, OS/CPU/topology/affinity/governor and contention notes. Workers record requested allowance and effective availability; renderer records SDL/version, driver, output size, target/viewport/scale/logical state, presentation/vsync and software/hardware identity.
- `boundaries`, metric units, sample-kind applicability and nesting. `initialization` records bounds preparation, index construction/initial maintenance, initial consumer-result construction and total ns, plus initial counts; excluded scene asset loading is disclosed. Steady-state results never hide this initialization cost.
- Validation: fixture/reference revision, tolerances, coverage/snapshot hashes, invariant results and paired validation bundle. Candidate and fixed-input logical transition sets are exact; numerical full-simulation fixture tolerances must be committed before measurement, not widened after observing differences.
- Resources: coverage/method, initial/final/peak capacities, allocation counts/bytes and peak live bytes by tagged domain where supported, exclusions, and process peak RSS if available. Unavailable measurements carry explicit status/reason, not a zero.

Resource runner hooks cover C++ new/delete families (including array/aligned/sized forms) and Box2D allocator callbacks, tagged by physics-index, physics-common-pairs, visibility-index and scratch phase. Report untagged engine allocations separately. Independently report owned-capacity accounting for arrays/chunks, projection bindings, maps/sets, pairs, stable records and reusable scratch; tree bytes use the tree API. Capacity excludes allocator metadata; direct C/SDL/TBB allocations outside hooks are excluded and disclosed. Whole-process RSS is not index-only memory. These instrumentation limits are part of eligibility for resource claims, not a reason to add hook overhead to timing binaries.

Replay resources sample the selected replay owner after initial index construction and after every complete step, including warmup, outside the replay timer. Collision replay reports its selected broad-phase owner, persistent pair state and pair scratch; visibility replay reports its selected culling owner. The `owned_capacity_snapshots.domains` manifest list identifies applicable measurements; zero placeholders for other resource tags are not interpreted as measured owner capacity. The replay fixture's identity/order storage and allocator metadata are outside owner-capacity estimates and remain covered by allocation totals/RSS where those mechanisms apply.

## Required case matrix

Use warmup 120, measure 600, dt 1/120 and seed 0 as initial reproducible presets. Trace percentage fields such as `changed_percent` are requested workload knobs; their rounded `requested_changed_endpoint_count` label is not an observed count. Trace headers and case manifests include matching per-step `operation_counts` derived from recorded operations, including warmup steps, for changed geometry/endpoints, writes, churn replacement, filter writes/value changes and coalesced writes. Visibility populations remain exact integer fixture counts. Generator validity and boundary placement take precedence over a guessed percentage.

| Family | Bounded evaluation cases |
| --- | --- |
| Collision sparse replay | N = 100/1,000/10,000, each with 0/1/10/100% changed bounds, uniform sparse geometry; both methods |
| Collision companions | N=1,000 with insertion/removal churn, cell migration/teleports, clustered/skewed occupancy and mixed sizes; dense overlaps bounded at N=128; symmetric filter changes and repeated coalesced operations |
| Visibility sparse replay | N=100/1,000/10,000, each with 0/1/10/100% visible; all four choices |
| Visibility dynamic companions | N=1,000, 10% visible, 0/1/10/100% changed bounds; pan, jump, resize/aspect and lifecycle/order cases |
| Full physics engine | Existing broad_phase (100/500/1,000), broad_phase_stress (50/150/300), narrow_phase (10/100/200 elements), rigid_body_container (100/500/1,000), add_and_remove and moving_add_and_remove (10/100/1,000 churn with 1,000 steady), persistent_contacts (fixed documented pairs/script endpoints); selected renderer none |
| Actual rendering | New sprite_visibility fixture with N=1,000 and exact 0/1/10/100% visible, plus animated/moving/camera companions; all four render choices, fixed segmented physics |
| Combined engine | New spatial_combined fixture with moving colliders and sprites; all eight combinations for correctness, one bounded matched case for comparative engine timing |

Run one worker and at least one available multiple-worker allowance for reported cases. Hardware unavailable means explicitly unverified, never inferred from a serial or software-renderer result. Record multithread query controls and assess small-batch overhead, skew and serial write/reduce limits. Primary evaluation uses frozen native-partial/zero-padding settings; full-rebuild and padded profiles are separately keyed controls, calibrated independently. Do not expand every companion case into a needless Cartesian product.

For each reported comparison retain >=5 independent fresh-process runs, randomize execution order with a recorded sweep seed, retain every raw capture/log/checksum and aggregate per-process means and tails. Report mean, median, p95, p99 and max per run; across-run mean/stdev and mean of per-run p95/p99 are labeled accordingly. Do not call pooled frame samples independent process repetitions. Report initialization separately; resource runs link to matching case hashes/configuration.

## Validation and eligibility

Before timing, validate matched trace hashes and independent exact references; candidate sets and fixed-input transitions must match. Full-engine fixtures exercise actual narrow phase/contact/support/solver/sleep/lifecycle contracts at one and multiple workers. Closed-loop trajectories may diverge numerically: use predeclared fixture tolerances and logical invariants rather than equating identical seeds with identical subsequent geometry. Retain actual inputs/snapshots when diagnosing differences.

Image fixtures compare each culling mode with none using the same renderer/state. Software fixtures require identical pixels; accelerated comparisons use a predeclared renderer-specific tolerance only if necessary, recorded before capture. Include transparent order, swap removal, edge-touching/rotated/scaled quads, animation, source/flip, sprite-only objects, hierarchy changes, paused/zero-step frames and output resize. The 1,000-sprite fixture places exactly 10 sprites safely inside and all others beyond conservative guards; every culled choice must submit those 10 once in order.

Reject invalid/incomplete runs, failed or missing paired validation, mixed input hashes/configuration/boundaries, inappropriate instrumentation, unsupported physics inputs, or incorrectly labeled render fallback. Exclude bypass/hybrid frames from pure indexed aggregate claims and retain them in explicitly labeled fallback reports. Do not discard slow valid outliers. Output a machine-readable eligibility reason for every excluded run.

Freeze a baseline at `2ad8caa466b1c0217b7911d3bca1a53160150456` before shared-core changes. Capture existing segmented/none full-engine workloads with its original schema-3 runner, then repeat identical legacy commands after refactoring with the same build/toolchain/hardware and comparable common scopes (especially complete_step). Use explicit version-aware report alignment, not mixed raw aggregation. Investigate unexpected changes before attributing ratios to BVH. Keep baseline source/build/raw files intact; no new trace flags are presumed to exist on that revision.

Deliver a comparison report identifying improvements, regressions, initialization/memory tradeoffs, refactor overhead, worker scaling and unmeasured cases. Box2D is an evaluated implementation/policy, not a universal BVH optimum. Negative results are useful evidence; no mandatory speedup or default adoption gate exists.

### Convergence qualification and accounting details

Actual rendered validation retains `validation.image`: the `same-frame-submit-all-rgba32` comparison method, passed pixel/order status, compared frame count, zero pixel tolerance, reference/result hashes with their declared algorithm, and complete SDL/backend/output/presentation signature. The engine validation also binds this proof to `image_case_sha256` and `image_source_sha256`. Physical invariants alone cannot qualify rendering. The sweep requires matching image proof for rendered validation and an equivalent qualification identity before admitting timing or resource results. That identity includes case hash/domain/id, scenario and resolved fixture parameters, effective methods and requested BVH profile, requested/effective workers, seed, dt, warmup and requested/actual measured boundaries, source commit/dirty state/fingerprint, compiler/version/flags, configuration, sanitizer/work-probe/debug-tool state, dependencies, complete recorded machine fields, and renderer signature where applicable. Typed engine cases are checked against the runtime's recorded scenario and resolved parameters; a caller-supplied case hash alone is insufficient. Resource captures must match timing on this identity, with only `resource_hooks` changing from false to true for the resource-only executable. Capture mode and that explicitly checked hook setting are excluded from the shared key; arbitrary build differences are not. Replay geometry validation remains renderer-independent. Reference redraws and readback occur only in validation, so those captures remain timing-ineligible.

Trace metadata separates requested workload knobs from observed operations.
`changed_percent` is a requested percentage label, and a derived requested
endpoint count is not an observation. Each trace header and case manifest
records the same per-step operation counts: final changed endpoints and changed
geometry endpoints, explicit geometry writes, remove/re-add generation
replacements, filter writes including no-op writes, effective filter changes,
and repeated endpoint/property writes coalesced after the first. Per-step
entries include warmup. Reports preserve immutable older traces and label their
historical requested metadata separately from counts derived from their
recorded operations.

Owned-capacity estimates include selected root/wrapper objects, allocated proxy/render records, tree storage, projection axes, cell/binding payloads, interval chunks and checkpoints, membership/hash payloads, retained result/query-hit buffers and common pair/scratch capacity. Hash-node estimates include two pointer-sized link/hash words per value. Owner-recursive accounting avoids counting embedded cells/chunks twice and keeps common pairs separate from index payload. Private container segment/control tables beyond the declared estimate and allocator headers remain explicit exclusions; allocated records and chunks are not classified as allocator overhead. Allocation-hook totals, owner capacity estimates and whole-process RSS are separate views and must not be added together.

Per-method `exact_overlap_count` records unique relationships reaching that method's exact publication/refinement gate: reduced positive witness keys for segmented collision, filter-eligible affected keys for BVH collision, and refined visibility candidates for culling. It is not a count of every endpoint comparison in an interval repair, nor a persistent population gauge. Visibility retirement operations execute at lifecycle cleanup; their counts are carried to the next visibility publication, while their elapsed time stays in the enclosing cleanup/step scope rather than being charged again to render preparation.

For actual-engine startup comparisons, distinguish runtime/fixture setup (`initialization.total_ns`) from the first simulation/render publication: registration can defer index construction until that first frame. Retain zero-warmup, one-frame variants of the same fixtures with separate matching validation and five fresh processes. Report setup and first-frame spatial subscopes separately; setup + complete_step + actual render are disjoint and provide a cold operational startup total. Do not add nested children again, pool first-frame data into steady-state samples, or treat a one-frame percentile as a steady-state tail estimate. Replay initialization reports endpoint preparation, populated-index build and initial-result preparation; private visibility-context constructor bootstrap precedes that replay scope, so use the actual-engine cold pass for complete operational startup claims.
