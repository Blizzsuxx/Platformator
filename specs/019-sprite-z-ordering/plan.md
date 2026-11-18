# Implementation Plan: Sprite Z Ordering and Render Culling Cleanup

**Branch**: `branch-without-bvh` | **Date**: 2026-11-17 | **Spec**: [spec.md](spec.md)

**Input**: `specs/019-sprite-z-ordering/spec.md`; user direction: add Z ordering and clean up render culling using the engine comparisons. Keep the implementation small and performance-sensitive.

**Feature identifier**: `019-sprite-z-ordering`. The setup script reports this identifier as `BRANCH`; the actual Git branch above is unchanged.

## Summary

Make `cached-linear` the sole enabled production render-culling policy and retire the sprite render spatial grid (`SpatialIndex`), cell/link pools, side list, query stamps, and runtime `indexed`/`current-linear` production dispatch. The selector resolves at window construction (`unset`, empty, or `cached-linear` -> `cached-linear`; explicit `indexed` or `current-linear` -> actionable startup error directing migration to `cached-linear`), while `--no-render-culling` / `enableRenderCulling=false` opt-out, headless zero-storage, conservative fallback/restoration, direct caller-ordered `Camera` draws, and stable `(zOrder, registrationSequence)` ties remain intact. The pre-removal 10,000-sprite sparse-pan tradeoff (`pre-removal-grid-vs-cached-20261118`, `+0.038 ms` selection vs `-9.06 MB` retained grid bytes and `-13.4 ms` cold setup) is recorded as historical context. [Culling research](research.md)

Add a zero-default signed-32 `zOrder` property and draw sprites by ascending `(zOrder, registrationSequence)`. Keep the dense sprite registry. Give culling responsibility for **which** slots are retained and give the window responsibility for **their draw order**. The proposed culling selection is an unordered dense vector with a membership position on each record. A camera pan can enumerate that vector without sorting by storage rank. The renderer sorts by the draw key once when its retained membership or a retained key changes. Editor preview keeps its own cached order because it submits directly through Camera.

Use sequential `std::sort` initially. Add no ordered tree, per-edit queue, packed key encoding, bucket/radix implementation, parallel threshold, sorting service, or new dependency. Input validation lives at JSON/editor boundaries; trusted draw-loop ranks and owner lifetimes are invariants. No blanket catches or repeated per-entry handle resolution are introduced.

## Technical Context

**Language/Version**: C++20; Python 3 standard library for existing capture tooling.

**Primary Dependencies**: Existing SDL3 3.4.18, SDL3_image 3.4.8, SDL3_mixer 3.2.4, SDL3_ttf 3.2.2, Eigen 5.0.1, oneTBB 2022.3.0, nlohmann JSON, and ImGui 1.92.9b. No added packages.

**Storage**: Existing scene/template JSON; optional `zOrder` scalar, emitted on save. Registration sequence and prepared order are runtime-only.

**Testing**: Existing CMake/CTest engine/editor integration tests, runtime work probes, deterministic software-rendering fixtures, and Python unittest capture-driver tests. Extend relevant fixtures and add one focused runtime ordering integration target.

**Target Platform**: Current desktop runtime platforms; Linux editor and deterministic software-rendering validation. Hardware-renderer/GPU performance is not inferred from software measurements.

**Project Type**: Runtime library/runner plus native editor.

**Performance Goals**: O(1) clean frame validity checks with zero sprite-order comparisons/rebuilds on unchanged ordering inputs; O(dirty slots) fixed-view selection maintenance; O(N) common-record candidate visits when camera/projection changes; up to O(V log V) draw-order preparation only when retained keys or membership change; zero temporary ordering allocations after sufficient capacity is warmed; no render grid storage or maintenance; no headless culling/ordering bookkeeping.

**Constraints**: Existing culling/lifecycle semantics, stable ties through slot moves, next-prepared-frame updates, KISS, existing mutation/worker barriers. Benchmarks use the guarded rendering driver, 3 repetitions, 5 warmup frames, 30 measured frames, 50 MiB output, 600 seconds; stop on failure/budget and report partial results.

**Scale/Scope**: Correctness from empty/tiny through the existing 1,000/10,000 sprite scales, including sparse/dense visibility, single/bulk edits, churn, activation, scene/template reuse, fallback and one/multiple worker configurations. A small focused timing selection replaces any historical workload matrix.

## Constitution Check

*Pre-research gate: PASS. Post-design review: PASS. No exceptions.*

| Principle | Before research | After design |
|---|---|---|
| I. Performance and scalability | Identify repeated sorting, N/V distinction, edit costs, allocations and retained memory. | Unchanged ordering O(1) checks; one O(V log V) draw-key sort on a changed set or retained edit, zero rebuilds for edits confined to rejected slots; O(dirty slots) fixed-view maintenance and O(N) camera-change candidate scans without spatial grid cells. Compare keys in compact entries and reuse capacity. Time the worst sparse-edit case with 10,000 retained sprites and preserve pre-removal sparse-pan measurements historically. |
| II. Multicore design | Evaluate feasibility rather than assuming every operation should parallelize. | Preparation is owner-thread work after existing worker barriers. Sequential sorting avoids unmeasured task/join overhead; oneTBB parallel_sort is a feasible alternative if profiling justifies it. No locks, extra pool, worker-time Sprite notifications, or arbitrary threshold. Correctness must agree across worker counts; one matched candidate worker timing control on the dense single-edit case checks complete update/render worker scaling within the same budget. No sorting speedup claim. |
| III. Collision architecture | Review segmented sweep and prune for the camera. | Reviewed its endpoint and checkpoint maintenance, the archived camera experiment, and sprite/collider geometry differences. Visibility uses render-owned bounds and selection. Physics ownership, scheduling and overlap events stay independent. |
| IV. KISS | Use existing registries, setter notifications, editor commands, scratch vectors and tests. | One inverse position per culling record replaces numeric rank sorting, the delta-change list and the ordered merge. Use one reusable selection scratch vector, one draw list and one order-dirty bit in the runtime. Each state field has an explicit owner/invalidation rule in data-model.md. |
| V. Developer experience | Default-zero property with predictable ordering and existing workflows. | Runtime get/set, JSON, Inspector, undo/redo, templates, clone/copy and preview are covered. Higher-in-front direction is familiar; local naming, wider range and no sorting layers are explicit deviations in research/contracts. |
| VI. Evidence | Current compact benchmark policy governs older campaign requirements. | Separate correctness/resource builds and timing builds, equivalent baseline/reference workloads, total render/update cost and raw samples. No benchmarks are executed or speedups asserted during planning. |
| VII. Correctness/lifetimes | Preserve rank invariants and frame publication. | Clear cached ranks before slot retirement; move sequence metadata with swapped slots; reset at existing successful scene commit/shutdown. Exact membership comparison accepts permutations and detects equal-count replacements. Transparency, fallback, activation and stale-reference tests are specified. |

## Project Structure

### Documentation (this feature)

```text
specs/019-sprite-z-ordering/
├── spec.md
├── checklists/requirements.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
└── contracts/
    ├── sprite-ordering.md
    └── authoring.md
```

`tasks.md` is produced by the later `$speckit-tasks` phase, not by this plan.

### Source Code (repository root)

```text
engine/include/platformator/sprite.h
docs/CPP_API_MIGRATION.md                 # runtime policy, Z order and opt-out
engine/benchmarks/README.md               # selection and capture semantics
engine/src/
├── sprite.cpp, spriteserializer.cpp
├── sdlwindow.h, sdlwindow.cpp
├── benchmark_main.cpp                  # effective culling metadata
├── renderculling.h, renderculling.cpp
└── gamemanager.cpp                    # existing rendering-storage reset call sites
engine/tests/
├── sprite_ordering_test.cpp           # new focused integration test
├── render_culling_*.cpp, runtime_work_probe.h
├── scene_io_contract_test.cpp, runtime_template_test.cpp
└── CMakeLists.txt
engine/benchmarks/
├── capture_linear_render.py, linear_render_probe.cpp
└── tests/test_linear_render.py
editor/src/
├── componenttypes.h
├── panels/inspector.cpp
├── validation.cpp
├── preview.h, preview.cpp
└── viewport.cpp                       # reuse visual order in existing pick cycle
editor/tests/
├── inspector_interaction_test.cpp, preview_test.cpp, viewport_cases.cpp
├── diagnostics_test.cpp, templates_test.cpp, component_operations_test.cpp
└── existing CMakeLists.txt
```

**Structure Decision**: Extend these owners in place. `SpriteDrawOrder` is a private state struct with a unique owner in `SDLWindow`, not a reusable framework or new public subsystem. No new source directory, renderer backend, or authoring change category is needed. Generic template-field and component-copy workflows already carry ordinary fields; extend their tests rather than special-casing their implementation.

## Phase 0 — Research outcomes

See [research.md](research.md) for source evidence and primary documentation. Resolved: rank-versus-draw separation; actual membership invalidation; stable registration metadata; sequential versus parallel sorting; property validation; preview/picking seams; and focused guarded evidence. There are no unresolved design clarifications.

## Phase 1 — Design

### Render culling cleanup

1. Resolve the enabled policy once when constructing `SDLWindow`. An unset, empty, or `cached-linear` `PLATFORMATOR_RENDER_CULLING_MODE` selects `cached-linear`. Reject retired `indexed` / `current-linear` and unknown nonempty values with an actionable startup error naming `cached-linear`, while preserving the public culling opt-out (`--no-render-culling` / `enableRenderCulling=false`). [Runtime contract](contracts/sprite-ordering.md#render-culling-policy)
2. `SDLWindow::prepareVisibleSelection` owns the sole cached selection path. On an eligible stationary view cleanly matching the cached key and empty dirty queue, it returns in O(1); with geometry edits, it refines only O(dirty slots) queued records; when projection/camera changes, it scans O(N) common records cleanly without grid cells or spatial queries. Retain conservative oversized/uncertifiable handling, raster padding, and independent drawing eligibility.
3. Retire the renderer's `SpatialIndex`, cell/link/node pools, side list, and query stamps from `renderculling.{h,cpp}` and `sdlwindow.{h,cpp}` (leaving the physics collision grid untouched). Enabled `cached-linear` allocates zero grid storage and performs zero spatial maintenance. [Ownership and lifecycle](data-model.md#render-culling-storage)
4. Preserve culling opt-out submit-all traversal on a live renderer. Temporary mapping/numeric fallback takes the all-slot ordered path and resumes `cached-linear` on the next eligible frame. A headless window owns neither culling nor draw-order state.
5. Update engine and benchmark documentation, probe manifests, and capture scripts for single-culler `cached-linear` execution. Pass retired `indexed` only to the recorded frozen pre-removal baseline when explicitly comparing against historical endpoints, labeled as a combined implementation + policy change. Keep older manifests/reports readable.

### Runtime property and boundaries

- Add `std::int32_t getZOrder() const` and `void setZOrder(std::int32_t)` following Sprite naming conventions. Default member initialization handles all constructors; clone copies the value through the setter.
- Persist `zOrder`; missing means zero, including deserializing into a previously nonzero object. Reject present non-integers, null/bool/floating values and out-of-range signed/unsigned values before narrowing. Keep this check local to serialization and editor validation; typed runtime setters do not repeat it.
- Setter equality is the no-op check. A registered sprite passes its dense slot to `SDLWindow::notifySpriteOrderChanged(slot)`. For a valid prepared subset, dirty the order only if that slot belonged to the published membership. A prepared all-sprite set includes every slot. An unprepared or structurally invalid order already needs a rebuild. A dirty bit coalesces notifications. A later admission, including fallback to all, rebuilds from current authored keys. Headless state is absent. Z Order never enters geometry notification, physics queues or culling dirty bounds.

### Draw preparation and lifecycle

- Initialize ordering state only after successful renderer creation. Reserve its two vectors geometrically alongside the dense sprite registry, before registration commit; do not allocate from a setter or warm preparation.
- Assign sequences on actual registration. Swap metadata with moved dense slots. Dirty and clear prepared entries on registration/removal so no cached rank can be consumed after identity/slot changes.
- Replace ascending rank storage in `cached-linear` culling with an unordered dense vector of unique retained ranks. Store its inverse position (or absent sentinel) in each culling record. (Earlier `indexed` culling is retired from production; pre-removal comparisons are preserved as historical evidence.)
- On a full query, collect unique refined ranks in reusable scratch. Compare their count and exact membership against the still-published set. Equal sets reuse the old publication even if traversal order differs. On change, repair inverse positions in O(previous V + new V), swap vectors, and advance the membership revision once. On a dirty-geometry update, append or swap-erase only actual membership changes, repairing the moved position. Remove the numeric sort and sorted delta merge together with their obsolete change buffer. See [publication and retirement invariants](data-model.md#culling-membership-revision).
- All selection mutation and publication finishes on the owner thread before ordering or submission. Repair the inverse mapping when a sprite slot moves and invalidate prepared entries before they can refer to a retired registration. Never infer identity from an unchanged count or a reused dense slot.
- Normalize selection to either all N slots or a proper culled subset. All is detected by submit-all/fallback or `visibleRanks().size() == N`. This uses the established unique, in-range rank invariant; preserve/assert it rather than adding defensive deduplication. Compare this status and, for subsets, culling membership revision with prepared metadata. A mode switch with an identical all-sprite set does not invalidate ordering.
- On dirty/changed selection, fill `{rank, zOrder, sequence}` entries from that selection and `std::sort` by the total key. Otherwise reuse entries directly. Continue using current sprite geometry/textures during drawing; do not cache visual content. Direct camera draws retain caller order.
- Rename the internal `releaseRenderCullingStorage` to `releaseRenderingStorage` and extend its existing scene-clear/replacement/shutdown calls to release ordering and culling state storage together. Retain capacity during ordinary churn; failed scene preparation must preserve the live scene's caches.

### Implementation sequence

1. Preserve the frozen pre-change executable identity and historical indexed/pilot evidence (`pilot-20261117-2255` and `pre-removal-grid-vs-cached-20261118`). Establish selector, visual parity, work-count and failure-atomicity fixtures for `cached-linear` production.
2. Implement the dense retained-set representation and Z-ordered submission together. Every consumer that used ascending numeric ranks moves to the new contract in the same change. Preserve registration ties across slot compaction and verify `cached-linear` alongside opt-out/fallback parity.
3. Retire renderer spatial-grid storage and runtime `indexed`/`current-linear` dispatch, make `cached-linear` the sole enabled production culler with actionable startup diagnostics for retired selectors, and wire the resolved policy through diagnostics and benchmark reporting. Exercise allocation failure during capacity growth and confirm the previous common selection remains usable.
4. Complete editor and persistence integration, then validate the bounded `cached-linear` comparison (`authored-static`, `sparse-camera-pan`, `dense-z-edit`, with `--baseline-probe` optional for all-zero cases). Preserved indexed comparisons (`pre-removal-grid-vs-cached-20261118` and `pilot-20261117-2255`) remain historical evidence rather than active production arms.

### Authoring and preview

- Add `zOrder: 0` to `default_component_record(6)`. Reuse Inspector integer editing, semantics, history, multi-edit and template field controls. The generic Sprite edit category, template field propagation and component-copy schema already suffice.
- Extend `validate_sprite` and the validation component fingerprint to include optional `zOrder`. Validate affected objects through existing paths; do not add a separate scene scan or change category.
- Preview caches `zOrder` separately from texture/animation `SpriteInputs`. Update the property and draw-order dirty state before the existing heavy-input early return, so an ordering-only edit does not reload textures/animations or refresh geometry. Existing document reconciliation invalidates the scene image.
- Keep document traversal IDs intact. A cached draw-ID list sorts on proxy membership/Z Order changes using stable preview registration sequences. Actual active-to-inactive-to-active transitions renew precedence to match runtime; view/texture loss does not. Do not rebuild order for camera motion, animation or selection overlays.
- Feed the sprite-bearing subsequence of viewport pickable IDs from this cached visual order. Preserve non-sprite target slots, existing hidden/locked filtering and per-object target priority; the current reverse traversal and repeated-click cycle remain. Preparation occurs at existing document-batch/render boundaries, never as a new per-frame sort.

### Work and storage bounds

Let N be registered sprites, V the retained candidate count, and G the geometrically grown capacity. On a changed camera or projection view, `cached-linear` examines N common records (O(N)) even if V is small (pre-removal sparse-pan tradeoff of indexed vs cached-linear is archived in `pre-removal-grid-vs-cached-20261118/report.md`). On a stationary view, clean frames check cache keys in O(1), and geometry edits refine O(dirty slots) queued records. With all sprites moving, bounds refresh is O(N) without grid link migration. If all N sprites are retained, a single Z edit can incur up to O(V log V) comparisons when retained keys or membership change.

| Operation | Added cost |
|---|---|
| Identical setter assignment | O(1), no notification |
| Real setter assignment | O(1) prepared-membership check. Rejected slots leave a valid subset clean. Retained edits coalesce. |
| Unchanged prepared frame | O(1) cache checks, then existing O(V) drawing |
| Dirty order or changed membership | O(V) entry fill plus O(V log V) comparisons |
| Full culling query with same membership and clean retained keys | Existing query/refinement plus O(V) exact membership checks, zero rank sorts and zero draw-key sorts |
| Geometry delta in a fixed view | O(1) selection maintenance per dirty record after refinement. A real membership change can trigger one draw-order rebuild. |
| Registration/removal | Amortized O(1) metadata maintenance, capacity growth when needed, deferred order rebuild |
| Headless | Authored value only; no ordering-state allocation/maintenance |

Runtime state adds approximately 8 bytes per reserved sequence slot and `sizeof(Entry)` per reserved draw slot (typically 24 bytes on 64-bit builds), plus a small state header. Measure actual capacities/sizeof rather than assume ABI padding. Reserving entries to population capacity trades O(G) retained bytes for zero allocation when visibility grows; filling/sorting remains O(V). Account separately for the inverse position per culling record, actual record padding and the removed delta buffer. Reuse the existing merge scratch for full queries. Preview has its own ID capacity and per-proxy scalar state, bounded by its current document/capacity.

One retained edit can still cost O(V log V). The benchmark will compare that rebuild with repositioning one entry in the prepared list. The latter uses O(log V) key comparisons and may move O(V) entries. Charge both detection and movement to that variant. Keep the simpler rebuild if the pilot is inconclusive. Adopt single-entry repair only if matched total times justify it and it passes the same lifetime, allocation and bulk-edit checks. Document any retained material cost with the constitution's local `EFFICIENCY:` comment.

## Validation and measurement

- One runtime integration fixture validates negative/extreme values, deterministic ties, mixed textures/transparency, repeated edits, exact membership reuse across different traversal orders, sparse candidates, slot swaps, real re-registration, camera/projection/fallback transitions and retirement. Assert against independent expected sequences/composited pixels in memory; do not retain frame lists/images.
- Extend scene/template tests for missing-field defaults, signed/unsigned invalid boundaries, clone/instantiation and save/load. Extend existing Inspector, component-copy, template, diagnostics and preview/viewport cases for editor parity and order-only edit resource behavior.
- Extend the existing culling tests for inverse-position integrity, delta swap-erase, equal-count exchanges, full queries with identical membership, structural rank relocation, cached-only gameplay, and retired `indexed`/`current-linear` startup error diagnostics. Keep independent image, transparency and submission-order oracles. Use a fixed-view rejected edit followed by pan-in and by fallback to all to verify latest-key use on the first eligible frame.
- Record validation-only counts for retained membership visits/changes, draw-key sorts, entries visited/moved, draw-key comparisons, allocations and retained bytes. On a warmed fixed subset, rejected-only edits must register zero draw preparations, comparisons, visibility queries and bounds refreshes. A changed selection permits at most one draw-key sort and zero numeric-rank sorts. A same-set query with clean retained keys permits neither sort.
- Use one bounded `render-order-culling` comparison under `cached-linear` covering authored-static (64 sprites), sparse-camera-pan (10,000 sprites), and dense-z-edit (10,000 retained mixed-Z sprites across rebuild, forced-sort, benchmark-only single-entry repair, and multiworker scaling). Keep separate `rejected-z-edit` and `fully-moving-crossing` work-only controls; when a frozen pre-removal baseline binary is supplied for authored-static/sparse-camera-pan, report it explicitly as an endpoint change combining implementation cleanup and render-grid retirement. [Validation guide](quickstart.md)
- Include scheduled mutations, step and render in the paired timing. Retain independent timings, process variation, tails, cold costs and bytes. For ordinary scenes check default initialization cost as well as warmed frame cost. Label the sparse-pan tradeoff and investigate regressions beyond run spread before accepting the change. Historical reports inform case selection. Only matched current captures support new performance claims.
- Current CMake derives instrumentation from build purpose: use Release + tests ON for resource validation and separate Release + tests OFF for timing, with benchmarks ON in both. Reuse builds when equivalent. Stop on failure/budget and retain a compact partial report in that same output directory. No automatic retry, replacement directory, full-campaign flag, enlarged budget, source/binary bundle or artifact build tree.

The selected pilot is exploratory evidence. Compare observed distributions and work/resource invariants, and report inconclusive timing honestly. Any unexplained regression beyond observed run variation keeps implementation acceptance open; a short pilot does not prove statistical equivalence. No runtime measurements have been executed in this planning phase.

## Complexity Tracking

The inverse position serves both exact membership tests and constant-time delta removal. The change retires the previous numeric sort and merge path. The selection scratch and draw entries have distinct owners and lifetimes. Single-entry repair is a benchmark-only alternative until its timing, work and allocation results justify adoption. The default policy adds no population threshold or automatic backend transition. Timing, allocation and performance acceptance remain open until implementation and measurement.
