# Research: C++ Engine Cleanup

**24 October 2026 assets-only amendment:** At the user's request, all asset-loading APIs and serialized resource fields require assets/ paths. Scene/animation-relative and absolute asset paths, bare root-relative paths and parent traversal are unsupported. Directory-aware resolvers were removed. Host asset-root configuration and scene-opening paths retain their filesystem semantics. This supersedes the original legacy-path decisions/tasks below; see [library-benchmarks.md](contracts/library-benchmarks.md) and [validation.md](validation.md).


**24 October 2026 amendment:** At the user's request, behavior reference resolution now runs only immediately before first start, for both loaded and programmatic behaviors. Inactive behaviors wait for activation. `referencesPrepared` and the scene-preparation behavior-resolution pass were removed. Startup reference errors leave the replacement committed. Earlier staged-behavior-binding decisions/tasks below record the original implementation and are superseded by this amendment; see [runtime-lifecycle.md](contracts/runtime-lifecycle.md) and [validation.md](validation.md).


**Date**: 2026-10-21  
**Inputs**: [spec.md](spec.md), [review](../../CODEBASE_REVIEW.md), current C++ sources at `e01526b`. Three read-only research passes covered lifecycle/references, physics/transforms, and validation/packaging. Decisions below resolve the technical design choices; no external service or new dependency is required.

## R1 — Preserve the current architecture and limit scene scope

**Decision**: Keep the component model, dense indexed registrations, TBB, SDL, and segmented sweep-and-prune. Support one active scene with replacement/clear; keep the single-runtime constraint. Leave the unused vendored Flecs tree outside the implementation diff and document that it is not integrated.

**Rationale**: Existing systems already implement the diploma's core functionality. The reported failures arise from mutation/ownership contracts, not a demonstrated need for an ECS replacement. Single-scene replacement avoids inventing additive scene membership/remapping.

**Alternatives considered**: ECS migration, additive scene manager, and multiple runtimes would multiply the work and change unrelated behavior. Keeping implicit append semantics would preserve duplicate-ID ambiguity.

**Evidence**: `src/runtime.cpp`, `src/gamemanager.cpp:71`, `include/platformator/runtime.h`, `CMakeLists.txt`, review recent-change table.

## R2 — Stable dispatch and affected-item mutation queues

**Decision**: Keep system registration vectors stable through callback dispatch. Object/component retirement marks logical state immediately, but physical deletion and swap-pop registration changes happen at a synchronization barrier. Coalesce desired-state work using a queue bit/index per affected object/component. Setup outside dispatch can commit immediately. Newly created/replaced scripts become eligible on the next step.

**Rationale**: Prevents vector invalidation without copying script/animator lists each phase. Destruction is terminal, so one lifecycle-state test at dispatch prevents later callbacks. Existing swap-pop indexes already support constant-time removal.

**Alternatives considered**: Full collection snapshots allocate/copy unchanged work; tombstones requiring a full compaction pass add recurring work; pointer validity scans cannot establish ownership. A generic command/event framework is unnecessary: a few typed pending vectors and one scene request are sufficient.

**Evidence**: `src/gamemanager.cpp:516-589,676-737`, `src/scriptcomponent.cpp:26-68`, `src/gameobject.cpp:212-276`.

## R3 — Scene requests latch once, prepare only the selected replacement

**Decision**: Store one pending scene request. Ordinary requests made before the end-step barrier coalesce with last-request-wins semantics. At the barrier, latch the request, stage only that replacement, validate/resolve it, and then commit. Requests produced by retirement collision callbacks are for the following step, never recursively applied. Idle `loadScene/clearScene` use the same commit machinery synchronously. During dispatch, existing methods enqueue; explicit `requestLoadScene/requestClearScene` make that behavior clear in scripts.

**Rationale**: Avoids recursive simulation and wasted disk/asset work for requests that are superseded in the same step. On failure the old scene and current path remain intact. The existing exception model is sufficient: deferred preparation errors emerge from the host's step/run boundary, not synchronously inside the requesting script.

**Alternatives considered**: Staging each request synchronously would let scripts catch file errors locally, but performs unnecessary loads for superseded requests. Tickets/futures or a transition event bus add machinery for a single pending operation. Immediate callback-time replacement is unsafe.

**Evidence**: `src/runtime.cpp:61-73,105-115`, `src/scene.cpp:92-118`, `src/gamemanager.cpp:728-737`.

## R4 — Ownership, readable destruction, and bounded teardown

**Decision**: The active scene owns every GameObject once in flat owning storage; objects own their component slots, ScriptComponents own behaviors, and hierarchy links are non-owning. Retired components/objects remain owned until physics/event users finish. Dispatch `onDestroy` explicitly before freeing any of that object's components; destructors release storage only. For this cleanup, `onDestroy` permits inspection/resource release, not structural runtime mutation. Enforce that once at the mutation API boundary. During shutdown, no creation/scene requests are accepted.

**Rationale**: Removes two competing object ownership paths, makes shutdown order independent of allocation order, and avoids an unbounded callback-generated destruction drain. This is a deliberate documented callback restriction rather than silently accepting mutations the current implementation cannot support safely.

**Alternatives considered**: General onDestroy spawning/scene transitions need an additional ownership destination and scheduling policy during full-scene teardown. A recursively expanding teardown loop would be unsafe. Recursively owning registered children conflicts with flat runtime ownership.

**Evidence**: `src/gameobject.cpp:21-38`, `src/gamemanager.cpp:35-49,666-673`, `src/scriptcomponent.cpp:85-97`.

## R5 — Direct runtime handles; file IDs remain file IDs

**Decision**: Use a small slot registry with `{slot, generation, runtimeSerial}` handles and a free list. Store concrete object-kind tags in registry records, including separate box/circle tags. Validate type when binding a typed reference; subsequent access indexes the slot and verifies lifetime. JSON keeps integer scene-local IDs, resolved through a load-time ID map. Active-scene ID lookup uses a map to handles, never the reverse dereference of a cached pointer. Make loaded-ID assignment a staging-only operation.

**Rationale**: Direct indexing is simpler to bound and cheaper than hashing every persistent-reference access. Generation prevents same-runtime slot reuse; runtimeSerial prevents references from binding to a newly constructed runtime. File IDs can repeat across successive scenes without reviving old references. Internal physics/callback loops retain borrowed pointers while the barrier guarantees their lifetimes.

**Alternatives considered**: A cached raw pointer plus scene epoch still fails on same-scene component replacement. A map keyed by monotonic IDs could work but adds hashing to each checked access. Observer back-reference lists require per-reference registration/unregistration. Adding a polymorphic BaseObject solely for dynamic_cast is unnecessary when current component types are a closed set.

**Evidence**: `include/platformator/objectreference.h`, `include/platformator/baseobject.h`, `include/platformator/gameobject.h:137-146`, `src/gamemanager.cpp:148-166,490-513`.

## R6 — Explicit, exception-safe scene preparation

**Decision**: Introduce a concrete scene-build context and owned staging data. Parse records, validate file IDs/numeric/configuration fields, construct owned objects/components, prepare resources, and bind references against the staged ID/type map before touching active registrations. Pass an explicit resolver to behavior reference resolution; update serialization macros/examples accordingly. Reference deserialization stores IDs/paths rather than eagerly resolving through the active runtime. Reserve staged runtime handles privately; publish active ID/system registrations only at commit, and invalidate reserved handles if staging fails. Deserialization/reference preparation may throw; RAII reclaims staging and old scene stays live. Unknown behavior types reject the load.

**Rationale**: Existing eager AssetReference resolution and implicit Runtime::current lookups otherwise make a transactional load bind against the old scene. Validation before the irreversible boundary removes the need for rollback of active registrations or speculative checks in every frame.

**Alternatives considered**: Temporarily replacing the global active runtime to deserialize is fragile. Partial live insertion plus rollback needs a transaction log over physics/render/callback registrations. Silently dropping unknown behaviors can change a game and lose data on later save.

**Evidence**: `include/platformator/assetreference.h:211-229`, `include/platformator/scriptregistration.h`, `src/gameobjectserializer.cpp:87-174`, `src/scriptcomponentserializer.cpp:18-31`, `src/referencehelpers.cpp`.

## R7 — Pair-local geometry with deterministic body reduction

**Decision**: Parallel narrow phase writes only each pair's result and pair-owned state. Compute support transitions from old/new flags keyed to canonical pair A/B identities instead of unconditional decrement/reincrement; SAT reference/incident swaps must not change which body a support flag belongs to. Collect only nonzero body deltas, then apply them serially before sleeping/event dispatch. Remove hidden Rigidbody changes from Collision destructors. Reuse result/scratch buffers indexed by stable pair slots. Keep persistent indexed active contacts and canonical keys; apply membership changes in deterministic order, sorting only changed membership batches if necessary, never the whole unchanged contact set every step.

**Rationale**: Pairs sharing a Rigidbody are not independent. This preserves TBB geometric work while avoiding hot shared atomics and nondeterministic support/solver ordering. Geometry-independent bookkeeping can be restricted to actual transitions.

**Alternatives considered**: Serializing all narrow phase abandons useful parallel work. Atomic support counters do not fix paired transition semantics or solver ordering. Sorting the full solver set each frame is avoidable work.

**Evidence**: `src/physicsmanager.cpp:153-200,256-310,339-381`, `src/collision.cpp:159-238`, `src/rigidbody.cpp:361-373`.

## R8 — Local movement and changed-subtree propagation

**Decision**: Each movement task changes only its own object's local translation/rotation. Merge thread-local changed-object outputs, then update affected subtree world-position caches in parent-before-child order on the main thread. Reuse a flat hierarchy-order/depth cache rebuilt only on structural hierarchy changes; enqueue dirty subtree roots with indexed deduplication. While draining roots, use a traversal stamp so a descendant reached through multiple queued ancestors is processed once. Refresh affected collider geometry/cells before the next collision query. Physics/render hot-loop world-position reads use synchronized caches, not recursive ancestor reads in parallel. Main-thread public getters preserve immediate effective position between edits; this compatibility path must not leak into pair/solver traversal.

**Rationale**: Existing parenting is additive translation, so local integration eliminates ancestor races directly. A moving parent necessarily changes descendant geometry; update those descendants once rather than scanning all objects or recursively querying ancestry per collider.

**Alternatives considered**: Per-depth parallel barriers introduce more scheduling work than needed initially. A whole-scene world-transform pass is easy but wastes unchanged work. General inherited affine transforms would change supported semantics and broaden geometry work.

**Evidence**: `src/physicsmanager.cpp:26-29`, `src/rigidbody.cpp:264-280`, `src/gameobject.cpp:56-62,161-183,279-335`, `tests/regression_tests.cpp:1751-1786`.

## R9 — Reconcile collider state; locally rebuild filter witnesses

**Decision**: Replace conflicting ADD/SYNC/REMOVE flags with one coalesced desired-state entry and geometry/filter/body/trigger dirty bits. Compare desired/current membership once at synchronization. For filter changes, remove and reinsert the affected collider in its occupied cells so the existing filtered witness algorithm can rediscover newly allowed overlaps; net witness deltas preserve still-allowed contacts. Work is confined to affected cells/adjacency. Maintain active contacts independently of broad-phase dirty pairs and define stay cadence explicitly.

**Rationale**: Fixes disable/re-enable lost registration and both directions of filter changes without retaining every disallowed pair globally or scanning the grid. Reusing local broad-phase remove/add is less machinery than inventing a second spatial query engine.

**Alternatives considered**: Checking masks only in narrow phase misses newly enabled overlaps never stored as candidates. Retaining all geometric pairs independent of filters simplifies toggles but can greatly increase memory/pair processing in layer-heavy scenes. Whole-grid rebuilds are unnecessary.

**Evidence**: `src/grid.cpp:426-521`, `src/gridcell.cpp:78-108`, `src/collider.cpp:96-111,141-186`, `src/aabb.cpp:102-141`.

## R10 — Fixed-size shape work, sleeping indexes, explicit step semantics

**Decision**: Use specialized circle/box closest-point contact generation with a deterministic containment case and stable contact features. Use fixed-size axis storage for box/box and exact circle/circle work; remove temporary normal vectors from narrow-phase hot paths. After support bookkeeping is correct, maintain indexed awake-dynamic/kinematic lists and sleeping eligibility so sleeping/static bodies are not repeatedly traversed for integration. Support loss enqueues wake work through affected adjacency.

**Rationale**: These solve verified geometry/support bugs while eliminating existing avoidable allocations and scans. Preserve existing failing stack tests as gates; improve the solver if they still fail after correct accounting rather than widening tolerances to hide failures.

**Alternatives considered**: Adding a corner axis to generic SAT leaves specialized contact generation/containment work anyway. Full island-based sleeping is not justified without evidence after the smaller repairs.

**Evidence**: `src/circlecollider.cpp:43-60`, `src/boxcollider.cpp:117-130`, `src/physicsmanager.cpp:312-381,707-748`.

## R11 — One step pipeline and a small interactive accumulator

**Decision**: Canonical `step(dt, optionalHook)` performs one explicit simulation step and its final cleanup. Retain current host wrappers as forwarding compatibility APIs. All simulation callbacks, including update/animation/late-update, run once per simulation step; rendering is separate. Interactive `run()` accumulates steady-clock elapsed time at default 1/120 seconds, runs at most eight steps per render iteration, retains a fractional remainder, and drops excess whole steps after a stall with an aggregate diagnostic. Pause discards wall-time accumulation; manual advance runs exactly one fixed step. Explicit host stepping accepts one finite positive caller-supplied dt and never runs an accumulator.

**Rationale**: Avoids duplicated pipelines and ambiguous update-before-start/deletion ordering. A fixed tick yields comparable interactive and default benchmark simulation. Eight is an initial documented catch-up bound, not a performance claim; expose it as a run setting if later workloads require adjustment.

**Alternatives considered**: Separate variable-rate update and fixed physics phases would change behavior cadence and add more mutation boundaries. Rendering interpolation is useful later but not needed for this correctness pass. Unbounded catch-up can prevent recovery from stalls.

**Evidence**: `src/gamemanager.cpp:277-423`, `src/constants.h:12-14`, `src/benchmark_main.cpp:1518-1572`.

## R12 — Preserve the library while making assets/package behavior explicit

**Decision**: Keep `Platformator::platformator`, a static core, public headers, and source/installed consumption. Export the target and represent bundled dependencies through their supported package/export rules plus find_dependency; keep bundled/offline consumption. Resolve an explicit runtime asset root once, preserving filename case. Provide a deterministic executable-relative default for packaged hosts and an explicit example root; never require demo asset folders/default scenes. Make headless physics independent of video/audio initialization. Use exceptions for startup/I/O failure and catch them in CLI boundaries. Asset ownership uses existing reference mechanisms consistently, including animator clip retention; handles must not outlive runtime resources as unchecked pointers.

**Rationale**: Fixes observed standalone-consumer errors without rebuilding the engine as a different product. Resource roots and startup validation belong at startup/load boundaries, not repeated upward directory searches.

**Alternatives considered**: Requiring new system dependency installs would regress the bundled workflow. Keeping hand-written imported-target library searches makes relocation/configuration fragile. A new result/exception framework is unnecessary.

**Evidence**: `CMakeLists.txt`, `cmake/PlatformatorConfig.cmake.in`, `examples/mario/CMakeLists.txt:39-87`, `src/pathmanager.cpp`, `src/sdlwindow.cpp:89-138`, `src/animator.cpp:46-68`.

## R13 — Repair tests first, then use a correctness/performance gate

**Decision**: Fix dangling-pointer test helpers, disable dependency tests, preserve real failing tests, and add focused public-API lifecycle cases plus small internal algorithm/oracle tests. Keep the current lightweight C++ harness. Add project-only ASan/UBSan and an instrumented-TBB TSan validation configuration; do not hide all TBB warnings with broad suppressions. Reserve warmed-up scratch capacity; use test-only counters/allocation instrumentation to prove idle/dirty-work complexity. Tests may use brute-force reference solutions.

**Rationale**: Current sanitizer results include a helper bug, and some tests bypass the synchronization they should verify. Correctness gates must become trustworthy before performance results are accepted. Existing flawed benchmarks cannot establish a numeric regression budget; capture a corrected Release baseline before optimizing, then compare repeated pinned runs.

**Alternatives considered**: A test-framework migration is not needed to add discriminating coverage. Mandating an invented percentage improvement would misrepresent the available evidence. Disabling failing cases would hide the primary risks.

**Evidence**: `tests/queued_deletion_safety_test.cpp:31-60,298-327`, `tests/scene_script_roundtrip_test.cpp:417-476`, review verification table, `CMakeLists.txt:36,148-216`.

## R14 — Define timing boundaries and the efficiency rule precisely

**Decision**: Add a versioned `complete_step` metric through final retirement/cleanup, distinct phase subscopes, and a separate render interval. Reuse sample storage, reserve measured steps, and record run metadata outside the step loop. Sweep each population at least five independent times and preserve raw data plus spread. Bound invariants and worker configuration are part of benchmark input. Keep old files labeled historical. For any deliberate remaining runtime inefficiency, require a local four-part `EFFICIENCY:` explanation: operation/cost, why retained, efficient alternative, concrete replacement prerequisites.

**Rationale**: Add/remove currently excludes actual destruction. Fixing the metric is necessary before diploma comparisons. The user's efficiency rule should change design choices, not generate blanket comments that excuse known fixable work.

**Alternatives considered**: Renaming the old timer without including cleanup does not create a complete-step measurement. Performance checks inside every release frame would contaminate the measured runtime; expensive invariants belong in validation runs or explicitly instrumented benchmarks.

**Evidence**: `src/gamemanager.cpp:277-319,352-363`, `src/benchmark.cpp:97-115`, `benchmark/sweep_scaling.py`, `benchmark/godot/benchmark_case.gd`.

## Resolved scope and gate status

No technical clarification remains open. Constitution text is a scaffold, not adopted governance; the plan explicitly evaluates feature-local gates from the user's requirements. The design intentionally chooses replacement-only scenes, readable but structurally read-only onDestroy, direct typed handles, translation-only parenting, and step-cadence updates. These decisions are documented contracts, not claims that the current implementation already behaves this way.
