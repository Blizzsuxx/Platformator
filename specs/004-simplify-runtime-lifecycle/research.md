# Research: Simplify Runtime Lifecycle

Research is based on current repository code, the feature specification, constitution 1.1.0, existing tests, and the prior solver-order amendment. Two read-only research agents inspected runtime/lifecycle and sleep/wake paths; findings were checked against source. No external package/API choice or claim about Unity semantics requires web research. Source anchors describe the planning baseline and may move during implementation.

## 1. Remove dead request sequencing and private methods

**Decision**: Remove `SceneRequest::sequence`, `sceneRequestSequence`, private `removeGameObject(string)`, and `deleteMarkedGameObjects`. Keep the request struct's path and outer optional so no request, clear, and load remain visibly distinct.

**Evidence**: `src/gamemanager.h:143` declares the request counter; `src/gamemanager.cpp:1194` is its only update, with no reader. `commitMutations` consumes the overwritten pending request at lines 1065–1072. Repository-wide call searches find no caller for the private name-removal overload (line 187) or deletion wrapper (line 643).

**Rationale**: Last-request-wins comes from replacing the pending request, not a sequence comparison. Dead methods and counters add concepts without behavior. Existing scene transition contract/stress checks cover supersession and preparation failure.

**Alternatives considered**: Keep counters as speculative future ordering; replace the request with nested optionals; introduce a request queue. All retain or add concepts that current behavior does not need. Revalidate call sites before deletion.

## 2. Consolidate rendering without an execution framework

**Decision**: Delegate `simulateAndRenderFrame(dt)` to `simulateAndRenderFrameWithCustomCallback({}, dt)`. Keep the existing simulation-step implementation and RAII `PhaseGuard`; render and simulation retain separate scopes.

**Evidence**: The two frame bodies at `src/gamemanager.cpp:342` and `:371` repeat event/quit checks, pause/debug handling, step selection, and rendering. The step path already accepts an empty callback (`:228–293`). `tests/runtime_step_test.cpp` covers hook order, invalid dt, clock behavior, and quit; `tests/benchmark_contract_test.cpp` checks independent render sampling and cleanup scopes.

**Rationale**: One maintained path removes duplicated behavior without runtime polymorphism, a stage registry, or extra storage. Formatting the existing templated stage lambda is sufficient; do not convert it to allocating/type-erased callbacks.

**Alternatives considered**: New loop classes, generic frame pipelines, or separately maintained overloads. No demonstrated benefit offsets their maintenance cost. Add targeted paused/callback/exception checks where existing tests do not distinguish both entry paths.

## 3. Keep one active-runtime authority

**Decision**: Keep `Runtime::activeRuntime`, remove GameManager's duplicate current-instance storage/accessors, and resolve privileged manager access through Runtime. Preserve explicit shutdown before manager reset and all resource-release ordering. At that access boundary preserve the no-runtime check and reject an absent manager with the existing inactive-runtime `logic_error`; do not dereference an empty owner or add validation throughout consumers.

**Evidence**: `src/runtime.cpp:14–35` is the only manager construction/publication path. GameManager's static pointer is read only by the fallback in `RuntimeAccess::gameManager` (`src/runtimeaccess.cpp:18–27`). SDLWindow/PhysicsManager construction does not call RuntimeAccess. After reset begins, current manager destruction only repeats already-completed shutdown and destroys physics/window; those destructors do not call RuntimeAccess. Before reset, shutdown callbacks/resource release can and must still resolve the active Runtime/manager.

**Rationale**: One source of truth removes a synchronization obligation and fallback routing that ordinary callers never require. Runtime is the public lifetime owner. No public API is removed; GameManager is an internal header. Preserve the existing no-active-runtime error type and initialization-failure behavior.

**Alternatives considered**: Keep both pointers, remove RuntimeAccess, or remove Runtime's explicit shutdown. The first retains redundant publication; the second breaks an intentional private-access boundary; the third moves callbacks into manager destruction and weakens the lifecycle argument. If characterization reveals a live reset-window consumer missed by research, retain the two-pointer mechanism and revise this decision rather than dereferencing an absent manager.

## 4. Preserve deferred lifecycle and transform machinery

**Decision**: Retain phase restoration, reconciliation work/drain vectors, pending starts, retirement batches, stable identity invalidation, dirty transforms, hierarchy order, and visit stamps. Return the internal all-object collection as a const reference. Do not delete hierarchyDepth or clock statistics in this pass.

**Evidence**: `src/gamemanager.cpp:823–1156` implements mutation checks, work queues, transform publication, support/event flush, callback-before-free ordering, transactional scene preparation, and shutdown. `Runtime::getAllGameObjects` (`src/runtime.cpp:219`) is the sole consumer of the private mutable accessor and already exposes a const collection.

**Rationale**: These mechanisms serve different consumers and validity boundaries. The pending/drain split handles reentrant work; retired allocations survive callbacks/support reconciliation; transform ordering and stamps avoid duplicate subtree publication. Collapsing them into immediate mutation or repeated global scans changes correctness or cost.

**Alternatives considered**: Unconditional object scans, immediate free, generic lifecycle coordinator, or wholesale GameManager splitting. None is justified. Write-only hierarchyDepth removal would change a complete public type's layout; its limited value does not justify broadening this cleanup.

## 5. Simplify sleep transitions without changing rules

**Decision**: In `Rigidbody::setIsSleeping`, retain the unchanged-state return, clear motion/force only when entering sleep, and reset the timer once for either real transition. Remove obsolete commented logging. Make the impact predicate accept only its used argument, and delete the stale commented reconciliation call in `applyPhysics`.

**Evidence**: `src/rigidbody.cpp:373–397` resets `sleepTimer` identically in both branches. `wakeUp` delegates to `setIsSleeping(false)` at line 415. `src/physicsmanager.cpp:324` defines an unused receiver argument in the impact predicate. Existing body participation checks cover explicit sleep, velocity waking, gravity-free sleep, and gravity-change waking.

**Rationale**: Repeated wake requests on an already-awake body currently preserve accrued quiet time. Moving the timer reset above the early return would alter that behavior. Impact motion thresholds must remain distinct from candidate eligibility and explicit setter wake thresholds.

**Alternatives considered**: Unconditional wake timer reset, unifying all motion predicates, deleting public support/timer methods, or rebuilding the mechanic. These change semantics or source contracts. A narrow regression must prove no-op waking retains the timer; formatting changes need no mirror tests.

## 6. Retain two-pass group decisions and incremental participation

**Decision**: Keep intrusive union/find scratch, inline reset, union-by-rank/path compression, two sleep passes, coalesced body participation, and net support batching. Explain their purposes in nearby code where needed.

**Evidence**: `applyPhysics` resets awake nodes before integration (`src/physicsmanager.cpp:27`); solver preparation wakes/merges bodies (`:312–339`); new candidacy also resets scratch (`src/physicsbodyindexes.cpp:62–70`). Group quietness and minimum timer are reduced before decisions (`src/physicsmanager.cpp:573–594`). Support transfer commits a net body delta before waking (`src/physicscontacts.cpp:52–97`). Prior feature evidence records an inline-reset correction after measuring an out-of-line cost.

**Rationale**: Newly awakened candidates can enter after force integration, so their reset is not proven duplicate. One moving member keeps its group awake; the minimum member timer controls accumulated quiet time. Decisions queue list changes and drain only after traversal. The old support recipient remains allocated through the net-delta flush even after component replacement.

**Alternatives considered**: Per-body sleeping loses connected-group semantics. Persistent adjacency traversal or new per-step island containers adds ownership, storage, traversal, and allocation costs without an established win. Parallel root reduction would require partitioning/shared-root synchronization; sequential ownership is appropriate here. Keep existing independent oneTBB workers and their joins.

## 7. Close specific validation gaps

**Decision**: Extend current hosts for unequal quiet timers in a connected group, already-awake no-op wake calls, controlled impact waking and subsequent participation, and active-runtime access during shutdown. Reuse existing lifecycle/support/hierarchy/clock/event tests. Establish expected outcomes on the baseline before edits; do not silently fix unrelated behavior.

**Evidence**: `tests/regression_tests.cpp:450–580` covers supported sleeping and stable piles. `physics_bookkeeping_test.cpp:131–147` covers basic sleep/wake; `physics_support_test.cpp:133–190` covers replacement and support transfer; runtime-step/foundation/recreation and scene-transition hosts cover orchestration. Narrow unequal-timer and impact-resumption assertions are absent in the inspected coverage.

**Rationale**: These cases specifically challenge the simplification's guards and boundaries. A small shared baseline/candidate fixture is more informative than a new broad testing framework. No private runtime state becomes public solely for testing; public timer operations and existing internal test access are sufficient.

**Alternatives considered**: Only rerun unchanged tests, assert exact arbitrary solver trajectories across workers, or write style-mirroring unit tests. These miss the changed contracts or reinstate a removed requirement.

## 8. Measure controlled workloads using existing reporting

**Decision**: Reuse schema-3 runner controls for spatial/churn/contact guards. Add a benchmark-only `platformator_runtime_cleanup_probe` from `benchmark/runtime_cleanup_probe.cpp` for six modes: quiet, mixed, wake burst, connected group, hierarchy, and lifecycle. Link the existing engine/recorder; do not modify recorder schema, production timing boundaries, or add a scheduler. Apply identical probe/test additions to the isolated baseline before measurement.

**Rationale**: Existing `rigid_body_container` does not guarantee a specified sleeping fraction or connected group. Persistent trigger contacts are not a sleeping-support workload. A small purpose-specific host can verify actual populations and exercise caller-hook work inside the existing complete-step boundary.

**Alternatives considered**: Infer sleep from warmup, label work-probe durations as release timings, reuse old captures, or build a general benchmark framework. These do not establish comparable evidence. Record fixture parameters, source hashes, initial/final state and worker configuration; reject invalid fixtures. No timing claim follows from this design.

All research questions have a selected decision. Empirical characterization and measurement remain planned verification, not unresolved product requirements.
