# Implementation Plan: C++ Engine Cleanup

**24 October 2026 assets-only amendment:** At the user's request, all asset-loading APIs and serialized resource fields require assets/ paths. Scene/animation-relative and absolute asset paths, bare root-relative paths and parent traversal are unsupported. Directory-aware resolvers were removed. Host asset-root configuration and scene-opening paths retain their filesystem semantics. This supersedes the original legacy-path decisions/tasks below; see [library-benchmarks.md](contracts/library-benchmarks.md) and [validation.md](validation.md).


**24 October 2026 amendment:** At the user's request, behavior reference resolution now runs only immediately before first start, for both loaded and programmatic behaviors. Inactive behaviors wait for activation. `referencesPrepared` and the scene-preparation behavior-resolution pass were removed. Startup reference errors leave the replacement committed. Earlier staged-behavior-binding decisions/tasks below record the original implementation and are superseded by this amendment; see [runtime-lifecycle.md](contracts/runtime-lifecycle.md) and [validation.md](validation.md).


**Branch**: `001-cpp-engine-cleanup` | **Date**: 2026-10-21 | **Spec**: [spec.md](spec.md)  
**Input**: [Feature specification](spec.md), [codebase review](../../CODEBASE_REVIEW.md), and the user's explicit simplicity/performance requirements.  
**Status**: All 95 tasks implemented and verified on Linux/GCC. Corrected 36-process invariant and 180-process Release references are recorded; see [validation.md](validation.md) for evidence, migrations and host/platform limitations.

## Summary

Harden the existing C++ engine around a small set of explicit invariants: one owner per allocation, stable registration arrays during callback dispatch, deferred physical retirement, scene preparation before commit, lifetime-checked references, and pair-local parallel physics. Preserve the component model, indexed vectors, segmented broad phase, bundled dependencies, and familiar library target.

Correctness work should improve the hot paths rather than surround them with guards. Use coalesced typed queues, direct registry slots, changed-subtree propagation, fixed-size geometry scratch, persistent indexed contacts, body-state reductions, and awake/eligible-body lists. Validate external data/API misuse at boundaries; rely on established invariants and debug assertions internally. Do not copy entire active lists or scan the whole scene each frame to compensate for uncertain ownership.

When an implementation appears inefficient, stop and evaluate an efficient solution. Implement it when feasible. Otherwise, add a local `EFFICIENCY:` comment identifying the cost, why it remains, the efficient alternative, and exact replacement prerequisites. This requirement applies during implementation/review; it is not satisfied by a vague “optimize later” note. Tests can use simple scans and brute-force oracles.

## Technical Context

**Language/Version**: C++20 for runtime; existing Python benchmark scripts for orchestration. No broad editor work.

**Primary Dependencies**: Bundled SDL 3.4.2, SDL_image 3.4.0, SDL_mixer 3.2.0, SDL_ttf 3.1.0, Eigen 5.0.1, oneTBB 2022.3.0, nlohmann JSON header; CMake and Ninja. No new runtime dependency. Flecs is vendored but not integrated and remains outside this change.

**Storage**: Existing JSON scene/animation files and filesystem assets; flat owning object storage; dense indexed system lists; runtime slot registry; scene-local integer IDs. CSV/manifest files for versioned benchmark output.

**Testing**: Existing five C++ CTest executables plus focused new public-API/internal cases; normal, ASan/UBSan, and correctly instrumented TSan configurations; serial/multiworker comparison; clean installed/source/relocated consumer tests. Preserve the lightweight harness. Tests and expensive invariants may prioritize clarity over runtime performance.

**Target Platform**: Linux desktop and physics-only/headless hosts are the verified targets. Keep portable C++/CMake and document unverified platforms. Physics-only construction must not require video/audio devices.

**Project Type**: Embeddable static C++ engine library, stock desktop runner, separate game consumer, and benchmark tools.

**Performance Goals**: No whole-scene mutation/reference safety sweeps or active-list snapshots; O(1) indexed lifetime lookup and registration membership; work proportional to changed entities/affected subtrees/cells/adjacency; no avoidable per-pair allocations or shared-body locks; reusable buffers in steady state. Interactive simulation defaults to 120 Hz; this is simulation cadence, not a claim that every workload fits its wall-time budget. Establish corrected repeated Release baselines before setting any numeric improvement target.

**Pre-cleanup performance reference**: [Recorded baseline](../../benchmark/baselines/pre-cleanup-e01526b/RESULTS.md) preserves the original engine at `e01526b`, with five Release repetitions for six workloads at three sizes and one-/six-thread limits. It retains the old partial `frame` metric and an independently measured complete public-step interval, raw samples, provenance, final-state snapshots, and observed support-accounting failures. Use it as the before-cleanup reference while executing tasks; it does not satisfy the future physics-correctness or corrected benchmark-schema acceptance gates.

**Constraints**: One active runtime and one active scene; replacement-only loads; translation-only parent inheritance; current valid JSON format; documented source/API migrations where old behavior was ambiguous/unsafe; readable but structurally read-only onDestroy; main-thread mutation and rendering. No ECS rewrite, additive scene manager, general thread-safe public API, new gameplay features, or blanket defensive null checks.

**Scale/Scope**: All review findings, including lifecycle, references/IDs, hierarchy, parallel physics, geometry/sleeping/timing, resources, install/package behavior, test correctness, and complete-step benchmark measurement. Validate at least 1,000 scripted scene cycles and shared-body 128-contact cases. Use actual benchmark populations for scaling; do not invent user-count or universal FPS targets.

## Constitution Check

The checked `.specify/memory/constitution.md` is the generated placeholder template. Its commented examples are not adopted policy. Do not replace it or pretend its placeholders pass a project constitution. Feature-local gates below are derived from the user's request and this specification. Constitution ratification is separate and does not block this authorized plan.

| Gate | Before research | After Phase 1 design |
| --- | --- | --- |
| G1 — Scope preserves the existing C++ engine and limits Python/ECS work | Pass: source/review establish concrete defects | Pass: R1 and explicit exclusions |
| G2 — Simple ownership/lifecycle, no callback-time physical deletion | Requires design; no code-pass claim | Pass by design: R2–R6, lifecycle contract |
| G3 — Efficient affected-item work; unavoidable inefficiency is explicit | User requirement recorded | Pass by design: R5, R7–R11, complexity table, EFFICIENCY rule |
| G4 — Boundary validation and internal invariants, not blanket guard code | User requirement recorded | Pass by design: staged validation, checked binding, scoped phases/assertions |
| G5 — Tests remain truthful and measure the public contract | Known failures documented, not hidden | Pass by design: WP0/gates and quickstart; execution still required |
| G6 — Public behavior, compatibility changes, and failures are explicit | Old behavior/source evidence captured | Pass by design: three contracts and migration table |
| G7 — Performance evidence includes actual complete-step work | Current timing defect recorded | Pass by design: versioned metric, repetitions, invariants, corrected baseline |

No unjustified gate violation or unresolved technical clarification remains. “Pass by design” does not assert that implementation tests currently pass. The review's failing baseline remains the starting point.

## Project Structure

### Documentation (this feature)

```text
specs/001-cpp-engine-cleanup/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
└── contracts/
    ├── runtime-lifecycle.md
    ├── physics-step.md
    └── library-benchmarks.md
```

`tasks.md` is intentionally reserved for the subsequent task-generation workflow.

### Source Code (repository root)

```text
include/platformator/        # Public runtime, components, references, options, serialization
src/                        # Core lifecycle, registry/scene staging, managers, physics, assets
  gamemanager.*
  runtime.*
  gameobject.*
  scriptcomponent.*
  scene.*
  physicsmanager.*
  grid* / aabb* / *intervallist* / *sortarray*
  pathmanager.* / sdlwindow.* / *wrapper* / animator.*
  benchmark.* / benchmark_main.cpp
  objectregistry.*          # Planned concrete slot/ID/type registry
  scenebuilder.*            # Planned concrete staging/resolution ownership
  mutationqueue.h           # Planned small typed buffers; omit if manager members suffice
cmake/                      # Exported package/dependency configuration
assets/                     # Deliberately packaged core/fallback assets
examples/mario/             # Source/installed consumer and independent runtime assets
tests/                      # Existing regressions plus focused lifecycle/reference/physics/package cases
benchmark/                  # Repeated-run scripts, raw results, historical outputs
.github/                    # C++ checks and refreshed guidance
CMakeLists.txt
CMakePresets.json
README.md
```

**Structure Decision**: Keep current directories and component files; no ornamental folder/service rewrite. Add only the small registry and staging units with distinct ownership responsibilities. Mutation queues can stay concrete manager members; a generic scheduler/command hierarchy is not required. Public headers expose only types clients need, not internal queue/grid machinery. Normalize old include shims incrementally after consumer checks.

## Phase 0: Research Outcome

[research.md](research.md) records 14 decisions with source evidence, rationale, and alternatives. Research covered:

- Safe scene commit, component retirement, starts/destruction, and allocation ownership.
- Direct lifetime/type-checked handles independent of serialized IDs, plus explicit staged resolution.
- Pair-local parallel work, support reductions, dirty hierarchy updates, collider reconciliation, geometry, sleeping, and step cadence.
- Correct test baselines, resource lifetimes, source/installed consumers, asset roots, backend/I/O failures, and timing/repetition contracts.

All choices have explicit defaults. Unknown performance numbers become measurement work in implementation, not guessed acceptance claims or unresolved design placeholders.

## Phase 1: Design and Contracts

[data-model.md](data-model.md) defines ownership, states, queued work, reference identity, staged preparation, transforms/pairs/resources, and intended complexity.

- [Runtime lifecycle contract](contracts/runtime-lifecycle.md): replacement/clear behavior, callback eligibility, readable destruction, ID/reference semantics, and deferred errors.
- [Physics/step contract](contracts/physics-step.md): pipeline, fixed interactive clock, hierarchy snapshots, body reductions, enter/stay/exit, and shape/numeric rules.
- [Library/benchmark contract](contracts/library-benchmarks.md): package/asset/backend boundaries, sanitizer/test configuration, benchmark CLI/schema, and efficiency review rule.
- [Validation guide](quickstart.md): normal/sanitized checks, discriminating scenarios, package relocation, and repeated Release runs. Planned flags/labels are explicitly identified as future implementation outputs.

### Compatibility decisions

| Surface | Decision / migration |
| --- | --- |
| `Platformator::platformator` and include layout | Preserve; replace handwritten installed target with proper export/dependencies |
| Existing `loadScene` overloads | Replace when idle; enqueue when dispatched; update examples/tests that implicitly relied on append |
| Script scene operations | Add explicit request methods; deferred errors arise at host step/run boundary |
| `clearScene` | Empty scene without a simulation; no default camera recreation during clear |
| JSON integer IDs/references | Preserve valid file format; reject duplicates/wrong types/unknown behavior; zero allowed |
| `BaseObject::setId` | Make loaded-ID assignment a staging concern; migrate tests or callers mutating live IDs |
| `ObjectReference<T>` | Replace cached pointer identity with typed lifetime handle; expired access is optional |
| `Behavior::resolveReferences` / macros | Accept explicit build resolver; migrate handwritten overrides and example macros |
| `onDestroy` | Readable component state; structural runtime mutation is a documented unsupported precondition |
| Callback cadence | One start/fixed/update/animation/late sequence per simulation step; interactive rendering is separate |
| Parent transforms | Preserve translation-only hierarchy; fix synchronized descendant colliders and parallel movement |
| Assets | Add explicit asset root, deterministic package default, case-preserving normalization, visible errors |
| Benchmarks | Schema v2 complete_step; old frame results remain historical, not silently relabeled |

## Implementation Sequence for Subsequent Task Generation

These are dependency-ordered work packages, not a generated tasks.md or a claim of completed implementation. Each should be split into reviewable changes with its own tests. Protect the user's existing staged files and keep source changes separate from planning artifacts.

### WP0 — Establish a trustworthy baseline

**Depends on**: None.  
Repair dangling-pointer test helpers by capturing identity while alive; fix misleading test names/diagnostics and duplicate-ID expectations. Disable unbuilt TBB dependency tests. Add supported sanitizer configuration and public-header compilation. Preserve sleeping and expected-exit failures as failures. Record current normal/sanitized results and build metadata in a reproducible location.

**Exit gate**: Project test enumeration is clean, test-helper memory errors are gone, current engine failures are reproducible, and no failing case is simply disabled. This gate need not make engine defects green before WP1.

### WP1 — Stable dispatch, ownership, retirement, and starts

**Depends on**: WP0.  
Introduce explicit execution phases, flat object ownership, stable registered dispatch vectors, coalesced mutation/retirement buffers, and reusable start buffers with exactly-once state. Keep retired component allocations through physics/events. Deliver onDestroy before component free; detach hierarchy links without consulting freed neighbors. Reject structural onDestroy/shutdown operations once at the public mutation boundary. Define exception cleanup centrally.

**Exit gate**: Growth/self-removal/destroy-another/replace-component cases pass for callback families; survivors dispatch once, inactive behaviors start correctly, both hierarchy allocation orders and runtime recreation are sanitizer-clean. Use targeted cases before broadening the suite.

### WP2 — Runtime identity, staged scene transactions, and resources

**Depends on**: WP1; resource ownership repairs are part of this package before transactional-load acceptance.  
Add direct slot/generation/runtimeSerial registry and type traits. Restrict loaded-ID assignment and separate file maps from live handles. Make ObjectReference bind/access/serialization stale-safe. Implement owned scene staging and explicit reference resolver; remove eager current-runtime reference resolution from JSON extraction. Stage all failure-prone work before commit, then route idle/scripted load and clear through the same boundary. Add deliberate resource retention for animator clips and staging while preserving existing asset ownership APIs.

**Exit gate**: 1,000 scripted transitions pass; failed preparation preserves the old scene/path; duplicate IDs/wrong subtype/unknown behavior are rejected; resource failure unwinds staging; slot reuse/new runtime cannot revive stale references. No call to simulateFrame remains in cleanup.

### WP3 — Dirty transforms and desired collider state

**Depends on**: WP1 and the relevant identity/scene interfaces from WP2.  
Separate worker-local integration from world transform propagation. Coalesce dirty roots and update affected descendants/colliders once; preserve immediate main-thread world reads. Rebuild hierarchy ordering only on structural edits. Replace contradictory collider operation flags with final desired-state reconciliation. Reinsert only changed filter colliders locally, merge net witness changes, and update active/subscribed contact indexes through affected adjacency.

**Exit gate**: Child colliders move without manual prepareSync; parent/child bodies have race-free summed displacement; off/on and mask-off/on restore unchanged overlaps; still-allowed contacts do not manufacture exit/enter. Test counters show no unrelated full-scene/cell scan in no-change/local-change paths.

### WP4 — Pair-local physics, geometry, sleeping, and deterministic membership

**Depends on**: WP3; can be researched/tested earlier but integrates after the lifecycle barriers are reliable.  
Remove shared body mutations from Collision lifetime methods. Compute pair results independently, reduce nonzero support transitions, and canonicalize solver/subscribed-event lists only when their membership changes. Keep the diagnostic contact collection as unsorted indexed membership; its iteration is not a solver or dispatch input. Add exact circle/box manifolds/fixed-size axes and correct offset normal orientation. Make stay/exit subscriptions independent of geometry dirtiness. Repair sleeping/stack behavior, then retain indexed awake/kinematic/sleep-eligible lists rather than repeated static/sleeping scans. Validate numeric constraints at input/mutation boundaries.

**Exit gate**: Existing geometry/sleeping tests plus corner, offset, containment, support-loss, shared-body and worker-count comparisons pass. TSan uses appropriately instrumented dependencies; if the environment cannot run it, record the exact limitation without claiming a clean run. No assertions are weakened merely to align with observed bugs.

### WP5 — One stepping pipeline and fixed interactive timing

**Depends on**: WP1–WP4.  
Consolidate step/render/custom wrappers, use consistent pre-step hook placement and end-step cleanup, then add the interactive accumulator and bounded catch-up/pause/manual-step behavior. Preserve explicit host stepping as one caller-supplied dt step. Stop rendered benchmarks on quit.

**Exit gate**: Equivalent accumulated time partitions produce equal simulation tick/state results within stated tolerances; pause/resume/stall/manual-step and all wrapper callback traces agree with the contract. No duplicated lifecycle implementation remains across wrappers.

### WP6 — Assets, errors, exported package, and standalone example

**Depends on**: WP2/WP5 for stable API; build/test baseline changes can happen earlier.  
Make asset-root ownership explicit, preserve filesystem case/legacy normalization, package fallbacks deliberately, and isolate physics-only backend setup. Fix SDL3 boolean initialization and propagate startup/load/save errors through existing exceptions/CLI catch boundaries. Use safe save replacement. Export the library/transitive bundled dependency packages, remove unused generated header machinery, and validate independent headers/consumers. Fix standalone example assets/prefix logic and add a finite smoke mode. Update documentation/guidance with actual APIs/build commands.

**Exit gate**: Core-only and examples-enabled builds both work; source/installed/relocated consumers compile and run outside the repository; no machine-specific paths or accidental example-asset dependencies; startup/I/O error tests pass.

### WP7 — Complete-step metrics and reproducible Release evidence

**Depends on**: All correctness/library gates above.  
Measure the canonical complete step through final cleanup; distinguish physics/cleanup/scene/render scopes, version CSV/parser behavior, reserve sample buffers, and collect run metadata once. Add worker/seed/repetition/raw-output controls, at least five runs per population, and explicit invariant-checking modes. Keep old graphs historical; document Godot equivalence or separate incomparable metrics.

**Exit gate**: Add/remove timing includes actual destructor/retirement work; expected populations/contacts and finite state are verified; raw runs/manifests reproduce aggregates. Capture a corrected baseline before optimization comparisons. Investigate regressions outside measured run-to-run variation and either fix them or document a necessary tradeoff with the required local EFFICIENCY explanation.

## Requirements Coverage

| Requirements / review findings | Work packages |
| --- | --- |
| FR-001, FR-004–006; callback invalidation, component retirement, readable destruction/shutdown | WP1, WP3, WP5 |
| FR-002–003, FR-007–009; scripted scenes, IDs, stale references, transaction/resource safety | WP2, WP6 |
| FR-010–011; descendant geometry and shared-body/ancestor races | WP3, WP4 |
| FR-012–014; filters/events, shape/sleeping correctness, fixed timestep | WP3–WP5 |
| FR-015–016; asset/backend ownership, public/installed package | WP2, WP6 |
| FR-017; helper defects, misleading tests, dependency test pollution | WP0 and each package's focused regressions |
| FR-018–019; timing/repetitions/metadata and accurate API/build guidance | WP6, WP7 |
| PR-001–007; simplicity, invariants, affected-item work, efficiency comments, clear tests | Every package; G2–G5 and per-change review |
| SC-001–008 | Normal/sanitized/scenario/package/performance checks in quickstart |

## Validation Strategy

Use focused tests appropriate to each package and stop repeating broad checks after a package passes unless subsequent changes affect it. Before feature completion, run the full project matrix in [quickstart.md](quickstart.md). Normal and ASan/UBSan are required; TSan environment failures remain explicit limitations and cannot be represented as passed.

Performance validation has two layers: deterministic test-only work/allocation counters for queue/dirty/list complexity, and repeated uninstrumented Release measurements on pinned workloads. These prove different things. Avoid mixing sanitizer/debug/oracle cost into performance claims. Tests may scan/allocate to provide independent expected results.

Introduce persistent indexes only for the concrete irrelevant traversals identified here, using the same small swap-pop/index pattern already present. Establish correct behavior first and compare work/allocation counters before and after each index change; if an index creates greater actual work, reconsider the layout instead of keeping complexity for its own sake.

Check each code change against PR-001–PR-007. If implementation discovers an inefficient approach, stop coding that approach and compare a better layout/algorithm first. Record necessary exceptions locally with the four-part comment, not in a blanket waiver. Do not add safety scans or atomics to mask a lifecycle/ownership defect.

## Complexity Tracking

No constitution violation is being waived. The only new mechanisms are justified by existing failures:

| Mechanism | Concrete need | Simpler alternatives evaluated |
| --- | --- | --- |
| Slot registry with generation/runtimeSerial and type tag | Stale-safe references across deletion, reuse, scene/runtime replacement | Cached pointers are unsafe; per-get hash lookup adds avoidable hot work; observer lists complicate reference lifetime |
| Small typed mutation/retirement buffers | Stable vectors and readable callbacks until users retire | Full snapshots/scans duplicate unchanged work; immediate deletion is unsafe |
| Owned scene staging plus explicit resolver | Validate/reclaim before changing live state; bind against new scene | Live insertion/rollback and temporarily replacing globals have wider side effects |
| Dirty hierarchy/cache and persistent contact/body indexes | Eliminate repeated ancestor queries and unchanged/sleeping scans in hot systems | Whole-scene refresh/full-contact sorts discard existing local knowledge |

No generic dependency injection container, universal command bus, new test framework, ECS, or broad manager abstraction is needed. Post-design gate check: G1–G7 are satisfied by the linked decisions/contracts; implementation exit gates remain to be executed.
