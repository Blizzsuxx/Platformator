# Feature Specification: C++ Engine Cleanup

**Feature Branch**: `001-cpp-engine-cleanup`  
**Created**: 2026-10-21  
**Status**: Implemented and validated on Linux/GCC; see [validation.md](validation.md) for evidence and limitations
**Input**: Clean up the C++ engine according to `CODEBASE_REVIEW.md`. Prefer simple code and efficient algorithms. Avoid scattered defensive checks. When an implementation appears inefficient, stop and evaluate a better design; implement it when feasible. Any retained inefficiency requires an `EFFICIENCY:` comment explaining the reason, efficient alternative, and prerequisites for replacing it. Tests may favor simplicity over speed.

## Scope

Harden the existing engine while preserving its component-based design and segmented sweep-and-prune broad phase. Establish explicit lifecycle, reference, physics, library, asset, and benchmark contracts. Verified defects and baseline results come from [CODEBASE_REVIEW.md](../../CODEBASE_REVIEW.md), reviewed at commit `e01526b`.

Include test repair, scripted scene replacement, structural mutation, identities/references, hierarchy updates, physics concurrency/geometry/sleeping/timing, build/install cleanup, startup/I/O failures, and benchmark methodology. Exclude broad Python GUI changes, an ECS/Flecs migration, multiple active runtimes, additive/multiple simultaneously loaded scenes, hot reload, and new gameplay/render features. Python changes are limited to integration compatibility if a planned CLI/path change requires them.

## User Scenarios & Testing

### User Story 1 — Change scenes from scripts (Priority: P1)

As a game author, I can request replacement or clear from start, update phases, collision handlers, or animation callbacks without re-entering simulation or freeing the executing callback.

**Independent Test**: Repeatedly alternate two small scenes through a public-API host under ASan/UBSan, including failed replacement.

**Acceptance Scenarios**:

1. A callback requests replacement, returns while its object remains physically alive, and replacement commits at the documented boundary. Each new behavior starts before its first update.
2. Several ordinary scene requests occur in a step; the last request wins deterministically. Destruction callbacks follow a separately documented non-reentrant rule.
3. Replacement staging fails; the existing scene and current path remain valid and the host receives the error at the documented boundary.
4. Clear commits without running another simulation step or leaving old-scene callbacks/references live.

### User Story 2 — Mutate objects/components safely (Priority: P1)

As a game author, I can create, remove, replace, enable, disable, parent, and destroy objects/components from callbacks without skipped, duplicate, or stale callbacks.

**Independent Test**: Record deterministic callback traces and check lifetimes with sanitizers.

**Acceptance Scenarios**:

1. Of three scripts, the first destroys the second during update; the third updates once, and the second receives no subsequent ordinary callbacks.
2. Removing/replacing a collider with pending physics work retires all users before freeing its memory.
3. Parent/child destruction is safe in either allocation order; each behavior receives one `onDestroy` while components still attached at invocation remain readable. Earlier explicit removal/replacement keeps its immediate slot semantics.
4. A behavior created while inactive starts once before its first eligible update.
5. Repeated enable/disable requests reconcile to the final desired registration state.

### User Story 3 — Use stable references and valid scene files (Priority: P1)

As a game author, I can serialize scene-local IDs and keep typed runtime references without stale access or accidental rebinding.

**Independent Test**: Destroy/recreate objects, recycle storage, replace scenes, recreate the runtime, and load invalid IDs/types.

**Acceptance Scenarios**:

1. Retiring a target makes its references empty without reading freed memory; slot reuse, repeated file IDs, and new runtimes do not revive them.
2. Loading a valid scene twice replaces it; one instance exists and name/ID lookup agrees.
3. Duplicate object/component IDs, invalid component data/resources, or unknown behavior types reject staging before live state changes. Behavior references, including incompatible types and required behavior assets, resolve immediately before first start for both loaded and programmatic behaviors. Errors there leave the replacement committed; inactive behaviors defer resolution until activation.
4. A failing load reclaims staged ownership; a failing save leaves a previous valid file intact.

### User Story 4 — Obtain correct serial and parallel physics (Priority: P1)

As a game author, I can move a hierarchy, change collision configuration, and use supported shapes without losing registrations or body state.

**Independent Test**: Compare canonical contacts/events and physical state with one and several workers, and run geometry/sleeping regressions.

**Acceptance Scenarios**:

1. Moving a parent updates the geometry/cells of a child collider without a Rigidbody at the documented synchronization point.
2. One dynamic body held against 128 static supports retains 128 support contacts with one and multiple workers in repeated runs.
3. Disabling a filter ends an existing contact exactly once; re-enabling it rediscovers an unchanged geometric overlap.
4. A 20x20 box at (0,0) and radius-1 circle at (10.95,10.4) do not collide.
5. Removing final support wakes a sleeping body; tall-stack and wide-pile regressions pass with meaningful assertions.

### User Story 5 — Build and run an independent host (Priority: P2)

As a host developer, I can consume the source or installed package, configure assets, and observe startup/load/save errors.

**Independent Test**: Install to a clean prefix, build a public-header consumer with `find_package`, relocate it, and launch from an unrelated working directory.

**Acceptance Scenarios**:

1. Normal CTest does not register unbuilt dependency executables.
2. An explicit asset root resolves mixed-case filenames independently of working directory and without requiring demo folders/default scenes.
3. Invalid SDL or scene input produces an error and nonzero runner exit.
4. Each public header compiles independently of private/generated forwarding headers.

### User Story 6 — Present trustworthy performance results (Priority: P2)

As the diploma author, I can distinguish complete simulation cost from algorithm scopes and reproduce measurements.

**Independent Test**: Run repeated Release sweeps with fixed workload, seed, workers, and timestep; inspect raw samples, manifests, counters, and phase totals.

**Acceptance Scenarios**:

1. Complete-step time includes registration, physics, callbacks, deferred destruction, and final cleanup; rendering is separately labeled.
2. Results record commit, compiler/configuration, machine, scenario, workers, seed, timestep, warmup, measured steps, and repetitions.
3. Historical results with different timing boundaries remain identified as historical; Godot comparisons require a documented equivalent workload and metric.

### Edge Cases

- Self-destruction; removal of the executing ScriptComponent; component replacement in a collision callback; duplicate destroy requests.
- Destruction callbacks attempt additional structural work; the centralized boundary rejects it without changing state, and cleanup/shutdown terminate without recursive frames or resurrection.
- Reparenting dirty subtrees; moving parent and child bodies concurrently; multiple activations before synchronization.
- Invalid references, throwing behavior deserialization, partial scene/resource allocation, or failed temporary-file writes.
- Intentional clear leaves no camera; a physics-only host has no video/audio device.
- Long frame, pause, manual step, invalid/NaN timestep, or window quit during a rendered benchmark.
- No mutations or dirty transforms: safety mechanisms do not scan/copy the scene to discover there is no work.

## Requirements

### Functional Requirements

- **FR-001**: Use one safe structural-mutation policy across frame entry points and callback families.
- **FR-002**: Commit scene changes without recursive simulation; prepare/validate replacement before retiring live state.
- **FR-003**: Define `loadScene` as single-scene replacement and expose explicit requests for scripts; document migration from append behavior.
- **FR-004**: Deliver start once before ordinary callbacks and destruction once while required components remain readable.
- **FR-005**: Retire physics/render/event users before freeing allocations; hierarchy links do not form a second owning tree.
- **FR-006**: Reconcile activation and replacement with final desired registration state.
- **FR-007**: Keep typed references safe after retirement, replacement, storage reuse, and runtime recreation.
- **FR-008**: Separate file-local IDs from runtime lifetime identity; validate uniqueness/types before commit.
- **FR-009**: Surface scene/save failures and reclaim all staged allocations/resources on failure.
- **FR-010**: Update affected descendants' collider geometry and cells after hierarchy position changes.
- **FR-011**: Remove shared-body races from pair processing and ancestor read/write races from movement.
- **FR-012**: Reconcile filters, trigger/body changes, and enter/stay/exit/destruction event semantics.
- **FR-013**: Correct circle/box contacts and preserve support-removal/sleeping coverage.
- **FR-014**: Use fixed steps in interactive running with bounded catch-up, pause, and single-step behavior; keep explicit host stepping deterministic.
- **FR-015**: Configure a runtime-owned asset root, preserve filename case, and keep resources alive through final users.
- **FR-016**: Preserve source/installed consumers, transitive dependency requirements, independent public headers, and standalone example assets.
- **FR-017**: Repair helpers/dependency-test pollution without hiding failures or weakening assertions to match bugs.
- **FR-018**: Measure complete steps and useful subscopes; add repeated-run metadata and correctness checks.
- **FR-019**: Update affected public documentation, examples, README, and stale repository coding guidance.

### Implementation and Performance Requirements

- **PR-001**: Prefer direct ownership, small concrete data structures, existing dense component arrays, and named phases. Abstractions must enforce a concrete invariant or remove duplication.
- **PR-002**: Validate external input/public misuse at boundaries, establish invariants, and use debug assertions internally. Avoid scattered null checks, silent fallbacks, and swallowed exceptions. Optional components/expired handles still have explicit optional semantics.
- **PR-003**: Use indexed membership and coalesced queues. Avoid whole-scene safety sweeps, copies of active lists per callback phase, per-frame ID repair, and linear searches merely to deduplicate queued work.
- **PR-004**: Reuse queue/scratch capacity and reserve known workloads. Avoid allocation in a warmed-up mutation-free Release step unless a specific unavoidable operation is documented.
- **PR-005**: Stop and evaluate a more efficient design whenever an implementation appears inefficient. Implement it if feasible; otherwise leave a local `EFFICIENCY:` comment stating what is slow, why it remains, the efficient alternative, and concrete prerequisites for replacement. Comments do not excuse feasible fixes.
- **PR-006**: Prefer pair-local parallel computation and controlled body reductions to shared hot-path locks/atomics. Establish correctness before accepting speed results.
- **PR-007**: Tests may use scans, allocations, brute-force oracles, and long traces for clarity and discrimination.

### Key Entities

- Runtime/active scene: ownership, registrations, resources, current scene path, step state, transitions.
- GameObject/component/behavior: lifecycle and registration state with explicit ownership.
- Runtime handle/file ID: distinct live identity and serialized identity, bound at controlled boundaries.
- Mutation/dirty subtree: actual changed work, coalesced through flags/indices.
- Collider pair/body support: pair-local geometry and controlled body/event transitions.
- Staged scene/asset root: uncommitted owned state and a stable filesystem base.
- Benchmark sample/run manifest: named timing boundaries, counters, workload/build metadata.

## Success Criteria

- **SC-001**: All project C++ tests pass in fresh normal and ASan/UBSan builds without dependency missing-executable failures or helper sanitizer errors.
- **SC-002**: At least 1,000 scripted replace/clear/reload cycles complete under sanitizers with exactly-once lifecycle traces, no stale references, and no retained old-scene objects.
- **SC-003**: Every callback family has mutation coverage; script growth, destroying another script, and self-removal/replacement produce correct exactly-once traces.
- **SC-004**: Hierarchy, ID/type, rollback, activation/filter, corner, support-removal, and sleeping tests pass without manual repairs bypassing runtime synchronization.
- **SC-005**: One/multiple-worker tests preserve contact/support/event invariants; physical comparisons use documented floating-point tolerances and deterministic processing order.
- **SC-006**: Clean source/installed consumers run outside the repository and after relocation; startup/I/O errors are observable.
- **SC-007**: Complete-step timings include cleanup and preserve raw repeated runs; comparisons do not imply unsupported equivalence.
- **SC-008**: Runtime changes satisfy PR-001 through PR-007; each intentional remaining inefficiency has the required local explanation. Tests need not meet runtime complexity goals.

## Assumptions

- One active scene/runtime meets current diploma scope; loading replaces the old scene. Additive loading is not implicitly retained.
- Existing JSON files remain readable when their identities/types are valid. Zero can remain a valid file ID; uniqueness and type correctness are mandatory.
- Linux is the verified platform; preserve portable design without claiming untested platform support.
- Preserve and document translation-only parenting. Do not silently add inherited rotation/scale/active state.
- Borrowed-pointer convenience APIs retain documented limited lifetimes; persistent references use checked runtime identities.
- The constitution is an unfilled generated template. This feature follows explicit user requirements and the plan's feature-local gates without inventing global governance.
- This workflow produces design artifacts only; tasks/code belong to later workflows.
