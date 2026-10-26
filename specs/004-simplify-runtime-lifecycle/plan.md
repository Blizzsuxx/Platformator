# Implementation Plan: Simplify Runtime Lifecycle

**Branch**: `001-cpp-engine-cleanup` (actual Git branch) | **Feature**: `004-simplify-runtime-lifecycle` | **Date**: 2026-10-26 | **Spec**: [spec.md](spec.md)

**Input**: `specs/004-simplify-runtime-lifecycle/spec.md`.

The setup script reports `BRANCH=004-simplify-runtime-lifecycle` from the selected feature directory; it does not switch Git branches. Keep the existing checkout and earlier feature artifacts.

## Summary

Simplify the current GameManager, sleep/wake flow, and runtime access without changing gameplay contracts. Remove unused scene-request sequencing and dead private methods, consolidate the duplicated rendered-frame path, and use Runtime as the single source for active-runtime access. Keep phase guards, coalesced work, transform ordering, sleep islands, support batching, and retirement barriers because they serve concrete safety or scaling purposes.

Make the remaining code readable through domain names, braced control flow, normal callback formatting, and blank lines between logical stages. In the sleep path, share the duplicated timer reset after the existing no-op guard, simplify the impact predicate's unused argument, and remove stale commented scaffolding. Do not introduce replacement managers, generic collection wrappers, new runtime state, canonical solver ordering, or a subsystem rewrite.

This command produces design only. Behavioral characterization, source changes, measurements, and maintainer acceptance remain implementation work. The current source inspected is `daa911d40ce00a882d8dffbccfa0d55cfde7d5be`; implementation must record the actual working-tree baseline before editing.

## Technical Context

**Language/Version**: C++20 runtime; Python 3 capture tools; repository CMake minimum 3.16 and Ninja presets.

**Primary Dependencies**: Existing vendored SDL3 3.4.18 with image/mixer/font libraries, Eigen 5.0.1, oneTBB 2022.3.0, and nlohmann JSON. No dependency changes.

**Storage**: In-memory objects, stable identity registry, resource caches, intrusive membership indexes, and reusable work vectors. Existing scene/asset files and benchmark schema 3 remain unchanged.

**Testing**: Standalone CTest executables; focused additions to runtime-step, foundation, bookkeeping, support, and work tests; public-header/package checks; ASan/UBSan and TSan where supported. A small benchmark-only runtime probe fills controlled sleep/activity/hierarchy gaps using the existing recorder.

**Target Platform**: Existing desktop runtime/library platforms. Execution guide targets Linux with dummy SDL drivers and physics-only hosts; record actual worker availability and CPU affinity. Other platforms receive no unexecuted certification.

**Project Type**: Native runtime/library plus desktop consumers; internal refactor with preserved public source contracts and serialized formats.

**Performance Goals**: Less independent state and duplicated maintenance with preserved useful scaling and warmed allocation reuse. Compare complete-step means/tails, affected work, allocation, retained storage, and worker behavior. No invented speedup target or slowdown allowance.

**Constraints**: Performance first among correct alternatives; simpler designs win when comparable. Preserve no-op sleep semantics, group timers, support-transfer atomicity, callback ordering, phase restoration, and object lifetimes. All control-flow bodies in changed functions use braces; statements and logical stages remain visually distinct.

**Scale/Scope**: Six journeys J1–J6. Small/large reference populations 100/1000 and dense controls 50/300 follow existing benchmark practice. Controlled connected groups, active fractions, wake bursts, hierarchy changes, and fixed-peak lifecycle churn supplement the existing scenarios. Population values are workload samples, not capacity promises.

## Constitution Check

*Gate evaluated before research and re-evaluated against the completed design. PASS means a conforming design, not executed acceptance.*

| Principle | Before research | After design |
| --- | --- | --- |
| I. Performance/scalability | PASS: baseline and affected-work comparison required | PASS: preserve incremental queues, active lists, local adjacency refresh, reusable storage, and existing complexity bounds; investigate every regression. No global-state rescans replace bookkeeping. |
| II. Multicore/oneTBB | PASS: explicit parallelism assessment required | PASS: keep independent force/movement and cell/pair workers; main-thread lifecycle, shared-body solver, group aggregation, and support reductions remain sequential for their dependencies. Boundaries and worker tests are specified below. |
| III. Collision architecture | PASS: no foundational broad-phase redesign proposed | PASS: Grid, endpoint/chunk/checkpoint algorithms and cell witnesses remain unchanged. Existing completeness checks remain. Any foundational change reopens the paper-study and design gate rather than entering through this cleanup. |
| IV. KISS | PASS: actual redundancy removal required | PASS: delete dead sequencing/wrappers and duplicate active-manager state; use one existing rendered path. Retained mechanisms have owners/invariants in data-model.md. No new production abstraction layer. |
| V. Developer experience | PASS: preserve current authoring behavior | PASS: public Runtime/Rigidbody interfaces and scene formats stay compatible. Internal object collection becomes const. No new Unity behavior claim or deviation. |
| VI. Evidence | PASS: fresh current baseline required | PASS: matched source/build/workload captures, separate correctness and timing, current schema 3, raw samples/provenance, regression dispositions. Previous feature's unresolved performance findings are context, not a certificate or this feature's baseline. |
| VII. Correctness/integrity | PASS: preserve lifetimes and behavior | PASS: characterize no-op waking, island timers, impact wake, exception cleanup, scene replacement, and access through shutdown. Preserve required event ordering; solver ordering stays unspecified under the prior user amendment. |

No constitutional exception is requested. A discovered incompatible behavior or unjustified tradeoff requires a revised design and corresponding tasks before acceptance.

### Parallelism and costs

- Force integration owns one awake body per task, including its step-local sleep reset. Movement tasks mutate their own local transform and collect changed objects in existing thread-local buffers. Join before main-thread transform/collider publication.
- Keep Grid/cell and distinct-pair worker ownership unchanged. Support/contact reduction and body participation updates occur after the relevant joins. Never mutate traversed membership vectors or reclaim participants during worker batches.
- Sleep-root compression, group aggregation, and solver contacts share mutable bodies/roots. Parallelizing them requires partitioning and synchronization beyond this cleanup; retain the sequential path. Callback/lifecycle work also has observable order and reentrant queues. No new scheduler, locks, atomics, or per-worker sleep copies.
- With `A` awake dynamics, `K` moving kinematics, `S` sleep candidates, `E` merged contact edges, `D` dirty transforms, `V` affected descendants, and `C` changed components/bodies: retain O(A) force work, O(A+K) movement, near-linear union/find sleep work, O(D log D + V) ordered transform synchronization when ordering is needed, and changed-work reconciliation plus affected adjacency. Hierarchy rebuild remains O(N) on hierarchy changes. Reentrant callback cost depends on actual queued changes; do not claim it is bounded by the initial queue length.
- Measure one worker and multiple worker counts (1, 2, and the selected available maximum), with physical-core/topology and affinity recorded. Scheduler overhead, imbalance from resting groups, and serial reductions are assessed through complete-step/subsystem results. No threshold tuning or speedup claim is preselected.

## Project Structure

### Documentation (this feature)

```text
specs/004-simplify-runtime-lifecycle/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── contracts/
│   ├── runtime-lifecycle.md
│   └── validation-evidence.md
├── quickstart.md
└── checklists/requirements.md
```

Future implementation retains concise inventory/walkthroughs and execution evidence here, with bulky raw results in a fresh ignored capture directory. `tasks.md` is produced by `$speckit-tasks`, not this command.

### Source Code (repository root)

```text
src/gamemanager.{h,cpp}           # frame paths, lifecycle, scene request, access cleanup
src/runtime.cpp                 # single active-runtime publication and shutdown ordering
src/runtimeaccess.{h,cpp}        # privileged access through Runtime
src/physicsmanager.{h,cpp}       # sleep decision/impact path; keep inline reset
src/physicsbodyindexes.cpp      # readable membership and island operations
src/rigidbody.cpp               # transition-only shared timer reset
include/platformator/          # preserved public contracts
tests/                         # focused extensions to existing hosts
benchmark/runtime_cleanup_probe.cpp  # future benchmark-only fixture, not an engine subsystem
CMakeLists.txt                 # register probe only for benchmark-enabled test builds
```

**Structure Decision**: Keep current ownership and file organization. Do not split GameManager merely to reduce its line count. Add only the bounded measurement fixture needed to control sleep/activity/hierarchy populations; reuse the existing benchmark recorder and reporting.

## Design and Delivery Boundaries

1. **Characterize first**: Save current source/build identity and baseline. Add only missing observable checks and the common measurement fixture to both baseline and candidate, keeping fixture content identical. Capture J1–J6 owners/paths and a compact remove/simplify/retain inventory before production edits.
2. **GameManager/runtime cleanup**: Remove unused request sequence fields while retaining the explicit request payload and pending-request marker. Delete uncalled private `removeGameObject(string)` and `deleteMarkedGameObjects`. Make the internal object-vector accessor const. Have the no-callback rendered entry point call the existing callback-capable path with an empty callback. Preserve benchmark/debug/event/quit behavior.
3. **Single runtime authority**: Route `RuntimeAccess::gameManager()` through the existing Runtime and its private manager accessor. Remove GameManager's duplicate static pointer/getters/setter and publication calls. Keep the no-runtime check and reject an unavailable manager with the existing inactive-runtime `logic_error` at this access boundary rather than dereferencing an empty owner. Preserve the explicit `Runtime::~Runtime` shutdown before `gameManager.reset()`, and keep `activeRuntime` valid throughout shutdown callbacks/resource release. Manager destruction remains idempotent; no destructor callback is moved into the reset window. Characterize no-runtime rejection, callback access during shutdown, initialization failure, and recreation.
4. **Sleep cleanup**: Share `sleepTimer = 0` once after the transition guard, leaving velocity/force/torque clearing only on sleep entry. Remove the unused impact-lambda receiver argument and stale commented logging/reconciliation code. Keep group scratch, both sleep-decision passes, union rank/path compression, reset on new candidacy, and existing reset before integration. Improve braces, names where needed, and stage spacing without changing thresholds or reconciliation timing.
5. **Validate and review**: Run affected checks, full ordinary/public compatibility checks, relevant sanitizers, work/allocation checks, and matched timing captures. Complete J1–J6 walkthroughs. Report maintainer readability review separately and keep it pending until supplied.

Private `GameObject::hierarchyDepth` and GameManager's accumulated dropped-step statistic have no current readers, but their removal is not needed for the selected reduction. Leave public-class layout and clock accounting unchanged in this pass; record this disposition instead of expanding scope. Public support/timer methods remain even where repository callers are sparse.

See [research.md](research.md) for alternatives, [data-model.md](data-model.md) for retained state, and [contracts](contracts/runtime-lifecycle.md) for invariants. Future tasks must carry each stage's characterization, braces/spacing, compatibility, and cost obligations. There are no unresolved design clarifications.
