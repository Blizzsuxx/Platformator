<!--
Sync Impact Report — temporary amendment review material; remove before committing.
Version change: unratified scaffold -> 1.0.0 (first project-specific adoption).
Modified principles: five unnamed template slots replaced by six project principles:
  I. Performance and Scalability First
  II. Simple Code with Clear Responsibilities
  III. Research-Backed Architectural Integrity
  IV. Unity-Inspired Developer Experience
  V. Evidence Before Performance Claims
  VI. Correctness and Sustainable Evolution
Added sections: Technical Foundations and Scope; Engineering Workflow and Review.
Governance: replaced placeholders with amendment, versioning, and compliance rules.
Removed sections: none; removed scaffold examples and placeholder guidance.
Dependent templates and commands: unchanged; they consume the constitution at runtime.
Deferred placeholders and follow-up TODOs: none.
-->
# Platformator Constitution

Platformator is a performance-oriented C++ 2D game engine and diploma project, inspired by
research on large-scale sweep and prune. Its goals are exceptional performance and scalability,
understandable implementation, and a familiar Unity-like developer experience. Correctness is
mandatory; demonstrated performance takes priority over convenience, while comparable solutions
MUST favor simplicity.

## Core Principles

### I. Performance and Scalability First

Designs and reviews MUST consider runtime cost, asymptotic behavior, allocation and reclamation,
retained memory, cache locality, and data access patterns from the beginning. Evaluate work against
moving objects, cell occupancy, insertions/removals, and affected overlaps, not object count alone.
Frequently executed paths MUST avoid unnecessary processing of unchanged state; use incremental
updates, local work, and reusable storage where they provide a meaningful benefit.

Within the proposed change, developers MUST evaluate feasible faster or more scalable designs
before retaining a known inefficient approach. If such an approach must remain, a local
`EFFICIENCY:` comment MUST identify its cost and frequency, why it remains, the better design,
and the concrete work needed to replace it. This comment is not a substitute for a feasible fix.
Speculative micro-optimizations MUST NOT justify unnecessary complexity.

### II. Simple Code with Clear Responsibilities

Implementations MUST use the simplest design that satisfies correctness and the relevant
performance requirements. Added abstractions, flags, queues, caches, and membership indexes MUST
have a clear owner, invariant, and correctness or scaling purpose. Derived state and cross-system
invalidation MUST be minimized; extra bookkeeping is not inherently an optimization.

Validate external input and public misuse at boundaries, and assert established internal
invariants where useful. Blanket null checks, repeated global validation, and silent recovery
MUST NOT replace explicit ownership or a correct algorithm. More complex designs are acceptable
when their meaningful correctness, runtime, or scalability benefit is explained and supported.

### III. Research-Backed Architectural Integrity

Foundational broad-phase changes MUST be informed by the paper in `paper/`, especially subdivision,
segmented interval lists, and overlap-change output in sections 3.1–3.3. The segmented structure is
an unrolled linked list of small arrays with a searchable chunk index and checkpoint sets; its
purpose is efficient local updates and insertion/removal under temporal coherence.

Before changing these structures, developers MUST identify the affected invariants: endpoint
ordering and bindings, checkpoint coverage, split/merge repair, and duplicate-pair accounting
across cells. Candidate generation MUST remain conservative for eligible colliders. Broad-phase
AABB candidates MUST remain distinct from confirmed narrow-phase contacts and gameplay events.

Changes MUST preserve these properties or explain their replacement and demonstrate correctness
and relevant scaling behavior. The paper permits improvement: its workload assumptions and tuning
results MUST NOT be treated as universal guarantees or fixed engine constants.

### IV. Unity-Inspired Developer Experience

Public API and behavior decisions MUST consider what a Unity developer would expect, using
Platformator's GameObject, Component, and Behavior model as the starting point. Favor familiar
concepts, naming, component access, lifecycle interactions, and predictable scene operations where
practical, while respecting established project conventions.

Unity similarity MUST NOT require copying its internals, imposing expensive abstractions, or
weakening native C++ ownership and lifetime rules. Significant deliberate deviations MUST be
documented with their usability, performance, or architectural reason. Public contract changes
MUST identify affected callers and update relevant documentation and tests. Claims about specific
Unity behavior MUST be checked against appropriate official documentation or direct evidence.

### V. Evidence Before Performance Claims

Significant performance-sensitive changes MUST establish the current behavior, expected benefit,
costs, and a suitable validation method. Use complexity analysis, affected-work/allocation probes,
profiling, or representative benchmarks as appropriate. Material runtime claims MUST be supported
by comparable before/after measurements when practical; unavailable measurements and uncertainty
MUST be stated rather than replaced with an improvement claim.

Benchmarks MUST follow [the repository methodology](../../benchmark/README.md): compare equivalent
workloads and timing boundaries, retain source/configuration provenance and raw results, and
separate expensive correctness instrumentation from performance captures. Include relevant
large-scale, sparse-motion, dense, or churn workloads; microbenchmarks alone do not establish
engine-wide improvement. Historical or incorrect baselines MUST NOT be silently rewritten or
presented as equivalent comparisons.

### VI. Correctness and Sustainable Evolution

Optimizations MUST preserve defined observable behavior, valid references, and resource lifetimes.
Object destruction, component replacement, and scene changes MUST remain safe across callbacks
and queued physics work. Ownership and mutation boundaries MUST be explicit; parallel execution
MUST preserve shared-state invariants and defined results within stated numerical tolerances.

Designs MUST leave room for demonstrated future scaling needs without building frameworks for
hypothetical requirements. Foundational improvements and replacements are welcome under these
principles. Existing implementation details are evidence to examine, not permanent requirements
or exemptions from review. No speedup compensates for missed contacts, invalid lifetimes, data
races, or silently changed simulation behavior.

## Technical Foundations and Scope

The foundational reference is Tracy, Buss, and Woods, *Efficient Large-Scale Sweep and Prune
Methods with AABB Insertion and Removal* (2009): [paper PDF](../../paper/Efficient_Large-Scale_Sweep_and_Prune_Methods_with.pdf)
and [searchable text](../../paper/paper.txt).

The current C++20 implementation combines a spatial grid, per-cell AABB sweep and prune,
`SegmentedIntervalList`, and linked `LocalSortArray` chunks. Cells buffer work for parallel
processing and their overlap deltas are consolidated across cell witnesses. SDL3 supplies platform
services, Eigen supplies mathematics, and oneTBB supports parallel execution.

The primary product is the C++ runtime/library with public headers in `include/platformator`.
GameObjects and components support Behavior scripts, hierarchy, scene serialization, and runtime
ownership of objects and assets. The runner and example game are consumers; the Python editor is
optional tooling. Callback sets, tuning values, and internal caches remain feature-level choices
governed by the principles above.

## Engineering Workflow and Review

- Specifications MUST define observable behavior and public compatibility impact, including any
  deliberate Unity deviation. Plans MUST identify relevant hot paths, invariants, complexity,
  ownership, alternatives, and validation. Tasks MUST carry those obligations into bounded work.
- Before changing foundational algorithms, inspect the relevant implementation and paper sections.
  Correctness checks MUST cover affected invariants; for segmented sweep and prune, relevant cases
  include endpoint ties, fast movement across chunks, split/merge, insertion/removal, cell migration,
  and duplicate witnesses. Independent overlap oracles are appropriate even if slow in tests.
- Verification MUST match the change: focused regressions, integration/public-consumer checks,
  sanitizers, serial/parallel comparisons, or benchmarks as relevant. Correctness MUST be established
  before accepting performance results; unchanged tests alone do not prove a new algorithm correct.
- Reviews MUST explain the benefit of additional complexity, assess regression risk, and report
  what was actually verified. Performance thresholds MUST come from declared workloads and
  evidence, rather than arbitrary numbers. Scope MUST remain focused; unrelated rewrites require
  their own justification.

## Governance

This constitution governs project specifications, plans, tasks, code, and reviews. Conflicting
project guidance MUST be reconciled with it; existing code is not presumed compliant. Reviews
MUST address applicable principles and explicitly justify tradeoffs or proposed exceptions.

Amendments MUST record their rationale and impact, obtain project-owner acceptance, update the
version and amendment date, and identify affected guidance or contracts. MAJOR versions cover
incompatible principle removals or redefinitions; MINOR versions add principles or materially
expand guidance; PATCH versions clarify wording without changing obligations. Ratification records
the initial adoption date and remains unchanged by later amendments.

**Version**: 1.0.0 | **Ratified**: 2026-10-24 | **Last Amended**: 2026-10-24
