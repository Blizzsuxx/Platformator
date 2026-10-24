<!--
Sync Impact Report — temporary amendment review material; remove before committing.
Version change: 1.0.0 -> 1.1.0 (new parallelism principle and materially expanded guidance).
Modified principles:
  I. Performance and Scalability First -> I. Performance and Scalability First (expanded).
  II. Simple Code with Clear Responsibilities -> IV. Simple Code and KISS.
  III. Research-Backed Architectural Integrity -> III. Research-Backed Collision Architecture.
  IV. Unity-Inspired Developer Experience -> V. Unity-Inspired Developer Experience.
  V. Evidence Before Performance Claims -> VI. Evidence Before Performance Claims.
  VI. Correctness and Sustainable Evolution -> VII. Correctness and Architectural Integrity.
Added principle: II. Multicore Design with oneTBB.
Added sections: none; existing foundations, workflow, and governance sections revised.
Removed sections: none; scalability and sustainable evolution consolidated into I and IV.
Sync review: plan Constitution Check and dependent commands consume this file at runtime;
  template and command sources require no edits and remain unchanged.
Deferred placeholders and follow-up TODOs: none.
-->
# Platformator Constitution

Platformator is a C++20 2D game engine whose defining goals are exceptional runtime performance,
scalability across modern multicore CPUs, understandable code, and Unity-inspired usability.
Correctness is non-negotiable. Among correct designs, meaningful performance and scalability gains
take priority over convenience; comparable solutions MUST favor simplicity.

## Core Principles

### I. Performance and Scalability First

Performance MUST inform design, implementation, refactoring, and review. Assess algorithmic
complexity, execution latency, allocation and reclamation, retained memory, cache locality,
data-oriented layout and access patterns, and abstraction or indirection costs. Prioritize efficient
algorithms and architecture before micro-optimizations. Avoiding premature optimization MUST NOT
excuse an inefficient foundation.

Performance-sensitive work MUST evaluate feasible faster or more scalable alternatives, especially
in collision detection, physics, object updates, and scheduling. Assess growth with object count,
spatial density and cell occupancy, active-object fraction, insertion/removal and migration rates,
affected overlaps, and core count. Exploit temporal and spatial coherence through incremental work,
local processing, and reusable storage where beneficial; avoid unnecessary scans and recomputation
of unchanged state. Large workloads and skewed distributions MUST inform foundational decisions.

If a known inefficient approach must remain within the change, a local `EFFICIENCY:` comment MUST
state its cost and frequency, why it remains, the better design, and concrete replacement work.
This does not substitute for a feasible fix. Speculative micro-optimizations MUST NOT justify
complexity in insignificant paths.

### II. Multicore Design with oneTBB

Designs and reviews of performance-sensitive systems MUST explicitly evaluate parallel execution
and explain the chosen parallel or sequential strategy. Favor independent work, clear data
ownership, and partitioning that scales. oneAPI Threading Building Blocks (oneTBB) is the preferred
CPU parallelization framework: reuse its scheduling facilities and parallel algorithms. Competing
worker pools or threading infrastructure require a documented unmet need, composition and
oversubscription analysis, and supporting evidence.

Parallel designs MUST account for task granularity, scheduling overhead, load balance, memory
bandwidth, synchronization, lock contention, false sharing, and shared mutable state. Preserve
cell-level concurrency and use task-local or thread-local results where appropriate. Minimize
unnecessary serial bottlenecks without assuming that shared-body solving or callbacks are independent.
Sequential execution is valid when dependencies or parallel overhead justify it; thresholds and
scaling claims MUST be evaluated on representative workloads and worker counts.

Tasks MUST have explicit mutation and visibility boundaries. Reductions, callbacks, cell erasure,
and resource reclamation MUST respect worker completion and object lifetimes. Concurrent containers
do not make arbitrary compound operations or concurrent erasure safe. Required ordering and
simulation results within declared tolerances MUST remain independent of worker completion order.
Evaluating parallelism is mandatory; parallelizing every operation is not.

### III. Research-Backed Collision Architecture

Before modifying foundational broad-phase systems, developers MUST study the relevant code and
*Efficient Large-Scale Sweep and Prune Methods with AABB Insertion and Removal* (Tracy, Buss, and
Woods, 2009), especially §§3.1–3.3 ([paper PDF](../../paper/Efficient_Large-Scale_Sweep_and_Prune_Methods_with.pdf),
[searchable text](../../paper/paper.txt)). Understand how subdivision limits unrelated endpoint
swaps, segmented interval lists localize insertion/removal, and overlap-change output exploits
temporal coherence. The segmented structure combines linked chunks of small arrays, a searchable
chunk index, and checkpoint sets covering intervals across chunk boundaries.

Changes MUST preserve or explicitly replace endpoint ordering and tie semantics, endpoint-to-chunk
bindings and indexes, checkpoint coverage and fast-motion cancellation, split/merge repair, and
insertion/removal and movement invariants. Avoid unnecessary memory movement, allocation, and
full-list traversal. Broad-phase coverage MUST remain conservative for eligible colliders,
including large AABBs spanning many cells. Consolidation MUST count cell witnesses consistently so
that losing one witness cannot remove a pair still overlapping elsewhere, and duplicate cells
cannot manufacture duplicate pair transitions. AABB candidates MUST remain distinct from confirmed
narrow-phase contacts and gameplay events.

Material broad-phase designs MUST evaluate local versus global sorting, batch insertion/removal,
and adaptive strategies against movement, churn, and occupancy. Use overlap deltas where consumers
can avoid revisiting unchanged pairs; preserve continuous processing where solver or callback
contracts require it. The paper is a technical foundation, not an immutable specification or a
claim that every technique is implemented. Its tuning constants, conditional complexity bounds,
and single-thread benchmark results MUST NOT be treated as modern hardware guarantees or measured
multicore scaling. Alternatives are welcome when correctness and comparative evidence demonstrate
meaningful benefits.

### IV. Simple Code and KISS

Implementations MUST use the simplest design that satisfies correctness, performance, and
scalability requirements. Responsibilities and ownership MUST be clear. Avoid unnecessary layers,
indirection, speculative generalization, and duplicated functionality. Added abstractions, flags,
queues, caches, and membership indexes MUST have an explicit owner, invariant, and correctness or
scaling purpose. Minimize derived state and cross-system invalidation; bookkeeping alone is not an
optimization. Complexity is acceptable when a meaningful correctness, performance, or architectural
benefit is explained and supported. Design for realistic scaling needs without speculative frameworks.

Validate external input and public misuse at boundaries; assert established internal invariants
where useful. Blanket null checks, repeated global validation, and silent recovery MUST NOT replace
explicit ownership or a correct algorithm.

### V. Unity-Inspired Developer Experience

Public API and behavior decisions MUST consider: "How would this work in Unity, and what would a
Unity developer expect?" Favor familiar naming, GameObject/component interactions, lifecycle
concepts, and predictable scene operations where practical, while respecting project conventions.
A convenient public API MUST be backed by efficient internals and explicit native C++ lifetimes.

Unity similarity MUST NOT require copying its internals, adding performance overhead solely for
compatibility, or forcing conventions that conflict with Platformator's architecture. Significant
deviations MUST document their usability, performance, or architectural benefit. Public contract
changes MUST identify affected callers and update relevant documentation and tests. Claims about
specific Unity behavior MUST be checked against official documentation or direct evidence.

### VI. Evidence Before Performance Claims

Significant performance-sensitive changes MUST establish a baseline, state the expected benefit
and costs, verify correctness, compare representative workloads, and investigate regressions or
unexpected results. Complexity analysis, affected-work and allocation probes, and profiling MAY
support algorithmic claims; runtime and multicore speedup claims MUST have comparable measurements.
If measurement is unavailable, state the limitation and leave the expected improvement unverified.

Benchmarks MUST follow [repository methodology](../../benchmark/README.md): equivalent workloads
and timing boundaries, recorded source/build/hardware and worker configuration, retained raw
results, and independent repetitions. Separate expensive correctness instrumentation from timing
captures. Use complete-step measurements to assess engine impact alongside relevant subsystem
scopes; do not sum nested scopes or equate incompatible timing boundaries.

Validation MUST cover the dimensions relevant to the change: small and large populations; mostly
static and highly dynamic scenes; insertion/removal churn and cell migration; uniform and skewed
spatial distributions; one worker and multiple worker counts; memory and allocation behavior; and
average and tail or worst-case latency where relevant. Representative engine workloads take
priority over isolated microbenchmarks. Preserve benchmark infrastructure and applicable regression
coverage. Historical or incorrect baselines MUST NOT be silently rewritten or presented as equivalent
comparisons. Performance targets and tuning thresholds MUST follow measured evidence or an explicit
project requirement, rather than arbitrary constants.

### VII. Correctness and Architectural Integrity

Optimizations MUST preserve defined observable behavior, collision completeness, pair deduplication,
data structure invariants, and valid references and resource lifetimes. Object destruction,
component replacement, and scene transitions MUST remain safe across callbacks and queued physics
work. Ownership, mutation, and synchronization boundaries MUST be explicit; races, deadlocks,
missed contacts, invalid lifetimes, and silent simulation changes are unacceptable performance costs.
Preserve determinism where required, with explicit event ordering and numerical tolerances.

Foundational changes MUST document their rationale, alternatives, invariants, and tradeoffs, and
establish correctness before accepting performance results. Existing code is evidence to inspect,
not a permanent requirement or an exemption from these principles. Algorithms and data structures
may evolve under the same correctness, performance, and maintainability obligations.

## Technical Foundations and Scope

The current architecture provides the following starting point, to be rechecked against source
when designing changes:

- `src/grid.cpp` and `src/gridcell.cpp` buffer collider operations, distribute placement/migration
  work and independent cell updates with `tbb::parallel_for`, then consolidate thread-local pair
  deltas by stable handles and cell-witness counts. Concurrent cell maps and operation buffers
  support these stages; empty-cell erasure follows worker completion.
- `src/aabb.cpp`, `src/segmentedintervallist.cpp`, and `src/localsortarray.cpp` implement per-cell
  two-axis sweep and prune, local endpoint repair, chunk lookup, splitting/merging, and primary-axis
  checkpoints. Buffered per-cell operations are not the paper's batch merge algorithm; adaptive
  local/global sorting is not currently implemented.
- `src/physicsmanager.cpp` parallelizes force/movement work over active body lists and sufficiently
  large pending narrow-phase pair sets, retaining a sequential path for small pair sets. Movement
  uses thread-local results; support/contact reduction and canonical solver/event ordering follow
  pair processing. The collision solver and behavior callbacks currently execute sequentially.
- `src/gamemanager.cpp` owns ordered runtime phases, main-thread structural mutation, transform
  synchronization, callback dispatch, and deferred lifecycle reconciliation. Dirty work, active-body
  and contact lists, sleeping, and callback subscriptions are existing performance-sensitive systems.

The primary product is the C++ runtime/library with public headers in `include/platformator`.
GameObjects, components, and Behavior scripts provide hierarchy, scene serialization, and
runtime-owned objects/assets. SDL3 supplies platform services, Eigen supplies mathematics, and
oneTBB supplies CPU task execution. The runner and example game are consumers; the Python editor
is optional tooling. Internal caches, callback sets, and tuning values remain feature-level choices.

## Engineering Workflow and Review

- Specifications MUST define observable behavior, public compatibility, deliberate Unity deviations,
  and relevant workload/scale expectations. Performance requirements MUST have an explicit project
  basis; no invented targets are required to fill a template.
- Plans MUST identify hot paths, invariants, ownership, complexity and scaling costs, feasible
  alternatives, and validation. For performance-sensitive changes, the Constitution Check MUST
  record the parallelization assessment, task/data boundaries, sequential bottlenecks or rationale,
  and how runtime and worker scaling will be checked. Recheck compliance after design; tasks MUST
  carry the resulting correctness, measurement, and documentation obligations into bounded work.
- Verification MUST match the change: focused regressions, integration/public-consumer checks,
  independent overlap oracles, sanitizers, serial/parallel comparisons, or benchmarks as relevant.
  Segmented sweep and prune checks MUST cover affected cases such as endpoint ties, large intervals,
  fast movement across chunks, checkpoint cancellation, split/merge, insertion/removal, cell migration,
  and duplicate witnesses. Unchanged tests alone do not establish a new algorithm's correctness.
- Reviews MUST assess correctness, performance, parallelism, scalability, complexity, and compatibility;
  explain additional complexity; and report evidence, regressions, uncertainty, and checks actually
  performed. Scope MUST remain focused; unrelated rewrites require their own justification.

## Governance

This constitution governs project specifications, plans, tasks, code, and reviews. Conflicting
project guidance MUST be reconciled with it; existing code is not presumed compliant. Applicable
principles MUST be checked during planning, after design, and in review. Tradeoffs and proposed
exceptions MUST be explicit; they cannot silently waive correctness or evidence requirements.
Dependent Spec Kit templates and commands consume this constitution at runtime.

Amendments MUST record rationale and impact, obtain project-owner acceptance, update version and
amendment date, and identify affected guidance or contracts and any required migration or
synchronization. MAJOR versions cover incompatible principle removals or redefinitions; MINOR
versions add principles or materially expand guidance; PATCH versions clarify wording without
changing obligations. Ratification records initial adoption and remains unchanged by amendments.

**Version**: 1.1.0 | **Ratified**: 2026-10-24 | **Last Amended**: 2026-10-25
