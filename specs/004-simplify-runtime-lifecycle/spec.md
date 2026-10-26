# Feature Specification: Simplify Runtime Lifecycle

**Feature Branch**: `001-cpp-engine-cleanup` (existing branch; feature directory is independent)

**Created**: 2026-10-26

**Status**: Draft

**Input**: User description: Simplify GameManager, related runtime classes, and object sleeping/waking. Remove redundancy and make the code cleaner, leaner, easier to maintain, and easier for the human maintainer to audit. Preserve the constitution's performance-first approach. Review complexity associated with commit `79069591488a68223f8e71ce5f24ddf2d37fb609` and other development. Always use `{}` for control-flow bodies and sensible blank lines between logical steps.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Follow GameManager's execution flow (Priority: P1)

As the engine maintainer, I want to follow a frame and its deferred changes without reconstructing redundant state or unnecessary indirection, so I can confidently audit and modify runtime behavior.

**Why this priority**: Human understanding is the primary outcome. Moving complexity into other files or renaming it does not satisfy that goal.

**Independent Test**: Walk through J1–J3 in the before-and-after code. Identify execution order, state owners, visibility boundaries, and safe release points. Compare the facts and coordination that a maintainer must track.

**Acceptance Scenarios**:

1. **J1 — Frame execution**: **Given** a running scene, **When** a simulation step executes with or without a caller callback, **Then** a reviewer can follow startup, the caller callback, fixed updates, physics, collision delivery, updates, animation, late updates, and cleanup in their established order. Rendered and interactive entry points preserve their existing timing and pause behavior.
2. **J2 — Deferred lifecycle changes**: **Given** live objects and components, **When** callbacks create, deactivate, replace, or destroy them, **Then** a reviewer can explain participation changes, pending work, destruction delivery, and the boundary before storage is reclaimed.
3. **J3 — Transform synchronization**: **Given** pending parent and child changes, **When** transforms become visible to consumers, **Then** propagation, affected collider updates, and waking consequences remain correct and traceable without undocumented duplicate maintenance paths.
4. **Given** the changed functions, **When** the maintainer reads them, **Then** branches and loops use braces, statements remain visually distinct, and blank lines separate logical steps.

---

### User Story 2 - Explain sleeping and waking (Priority: P1)

As the engine maintainer, I want clear sleep/wake state, ownership, and transitions, so I can explain why a body sleeps or wakes and safely maintain the mechanic.

**Why this priority**: Sleeping affects correctness and quiet-scene cost. Its cross-class rules must remain understandable without losing their work savings.

**Independent Test**: Follow J4–J5 through their callers and consumers. Exercise isolated bodies and supported groups; compare sleep/wake decisions, timing, and participation with the current baseline.

**Acceptance Scenarios**:

1. **J4 — Entering sleep**: **Given** a quiet supported body, connected supported bodies, or a gravity-free body, **When** existing quiet-time conditions are met, **Then** eligibility, group decisions, and timing remain equivalent, and a reviewer can identify who makes each decision and how unnecessary work stops.
2. **J5 — Waking and participation**: **Given** a sleeping body, **When** an existing wake trigger occurs, **Then** it resumes appropriate work at the established boundary and the trigger-to-participation path is readable. Coverage includes explicit waking, relevant motion/force changes, transforms, contact interactions, and final support loss.
3. **Given** several supports, **When** one disappears or support transfers during the same committed change, **Then** existing support and waking semantics remain intact; actual final support loss still wakes the body.
4. **Given** repeated type, gravity, activation, or sleep changes before an update, **When** participation is reconciled, **Then** it reflects the final valid state without duplicate work or stale references.

---

### User Story 3 - Trust the surrounding runtime (Priority: P2)

As a game author and engine maintainer, I want setup, entry points, scene changes, and shutdown to remain predictable after cleanup, so existing games work and future changes require less coordination.

**Why this priority**: GameManager and sleeping simplification must preserve surrounding public contracts, lifetimes, and scaling.

**Independent Test**: Exercise J6 through public runtime operations, including failures and recreation. Run affected behavior checks and compare representative workloads against a recorded current baseline.

**Acceptance Scenarios**:

1. **J6 — Runtime and scene lifetime**: **Given** an active runtime, **When** a scene loads, clears, fails to prepare, is replaced, or the runtime shuts down and is recreated, **Then** request selection, failure behavior, object identity validity, destruction delivery, and resource ownership remain compatible and traceable.
2. **Given** a callback throws with changes pending, **When** applicable cleanup finishes, **Then** existing error propagation and cleanup obligations remain intact without consumers referencing released objects.
3. **Given** existing game code, scenes, and assets, **When** they use the cleaned runtime, **Then** supported public calls, callback contracts, and serialized content remain compatible.
4. **Given** equivalent small and large scenes, **When** quiet, active, and frequently changing populations run with one and multiple workers, **Then** behavioral and cost evidence supports acceptance under the constitution, with regressions and measurement limitations reported.

### Edge Cases

- Empty scenes, objects without scripts or physics, and repeated shutdown.
- Creation/removal before first start; replacement or retirement inside callbacks; callbacks scheduling additional permitted changes.
- Parent/child changes queued together, repeated transform writes, hierarchy changes, and retirement before synchronization.
- Bodies near existing motion/quiet-time thresholds; repeated sleep/wake requests; gravity-free unsupported bodies and unsupported bodies with gravity enabled.
- Connected bodies with different timers or motion; moving bodies contacting sleepers; support removal, replacement, and net-zero transfer.
- Repeated body-state changes before update; removal with pending participation or contact work.
- Multiple scene requests before commit, failed scene preparation, and exceptions during steps or destruction.
- Interactive pause, single-step, catch-up limits, quitting during event handling, and runtime recreation.
- Mostly sleeping populations with a small active subset, large resting groups, and bursts of waking or lifecycle changes.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001 — Scope**: Cleanup MUST center on GameManager, sleeping/waking, and runtime orchestration. Related object, component, transform, support/contact, scene, and resource-lifetime code MAY change where directly needed to simplify these responsibilities. Each change outside the core areas MUST identify its connection to J1–J6. Unrelated rewrites are excluded. Acceptance: journey walkthroughs and final scope review.
- **FR-002 — Actual simplification**: The change MUST remove identified redundant work, duplicated responsibilities, unnecessary maintained state, or unnecessary indirection. Reviewed mechanisms MUST receive a disposition: remove, simplify, or retain with a correctness or meaningful performance/scaling reason. Renaming, reformatting, file splitting, and documentation alone are insufficient. Acceptance: before-and-after comparison and SC-002.
- **FR-003 — Clear responsibilities**: Retained state MUST have an identifiable meaning, owner, producers, consumers, and validity boundary. Names MUST describe domain actions or facts. Non-obvious lifetime, synchronization, or efficiency constraints MUST have concise explanations where needed. New abstractions or bookkeeping MUST have a demonstrated purpose; ordinary control flow MUST remain straightforward to trace. Acceptance: J1–J6 walkthroughs.
- **FR-004 — Formatting**: All new or modified control-flow statements MUST use `{}` bodies, including single-statement branches and loops. Existing unbraced bodies within a changed function MUST also be braced. Changed functions MUST place statements on distinct lines and use sensible blank lines between logical stages. Compressed branches or callbacks MUST NOT obscure execution order. Acceptance: review of all changed functions, including helpers and lambdas.
- **FR-005 — Frame behavior**: Preserve simulation phase order, caller callback timing, start/update eligibility, mutation visibility, transform synchronization, interactive clock behavior, and rendering boundaries. Structural mutation restrictions and exception cleanup MUST remain correct. Acceptance: J1–J3, User Story 3 scenario 2, and applicable edge cases.
- **FR-006 — Sleep/wake behavior**: Preserve sleep eligibility, thresholds, quiet-time accumulation/reset, connected-group decisions, explicit sleep/wake effects, wake triggers, and resulting force, movement, and collision participation. Repeated changes MUST resolve safely to the final valid state at the established boundary. Acceptance: J4–J5 and threshold/body-state edge cases.
- **FR-007 — Support and contacts**: Sleeping-related changes MUST preserve support ownership, committed support-transfer semantics, final-support-loss waking, contact completeness, and existing Enter/Exit delivery contracts. Acceptance: User Story 2 scenarios 2–4 and affected contact/lifecycle checks.
- **FR-008 — Compatibility and lifetime**: Preserve supported public runtime/authoring interfaces, scene/asset formats, scene selection and failure behavior, valid object identity, and destruction delivery. Deferred consumers MUST stop using retired objects before storage is reclaimed, including across callback failures, replacement, scene changes, and recreation. Acceptance: J2, J6, and User Story 3 scenarios 2–3.
- **FR-009 — Performance first**: Evaluate correct alternatives under the constitution's performance/scalability priorities; comparable alternatives MUST favor simplicity. Preserve efficient handling of unchanged or unaffected state unless comparative evidence justifies an alternative. Fewer lines MUST NOT justify repeated global work or avoidable allocation. Retained complexity MUST have a specific benefit, and known inefficient paths MUST receive the constitution's required treatment. Acceptance: mechanism dispositions and workload comparisons.
- **FR-010 — Concurrency**: Performance-sensitive changes MUST assess parallel/sequential alternatives, explain the selected strategy, and preserve worker completion, ownership, mutation, and visibility boundaries under the constitution. Introduced races, deadlocks, invalid lifetimes, or dependencies on incidental worker completion order are unacceptable. This feature MUST NOT introduce a deterministic solver-order requirement. Acceptance: ownership review and relevant one-worker/multiworker checks.
- **FR-011 — Evidence**: Record the current behavior and cost baseline before implementation. Validate affected journeys and compare equivalent small/large populations, quiet/active fractions, resting groups, wake/lifecycle churn, hierarchy changes, and one/multiple workers. Include spatial skew and migration where affected physics work depends on them. Report complete-step and relevant subsystem average/tail times, variation, affected work, allocations, and retained memory using repository methodology. Investigate regressions; unsupported performance claims MUST remain unverified. Acceptance: SC-003, SC-005, and SC-006.
- **FR-012 — Human audit**: Completion material MUST include concise before-and-after journey explanations, concrete simplifications, reasons for retained complexity, actual validation results, and limitations. Maintainer readability acceptance MUST be recorded separately from automated validation; pending review MUST remain pending. Acceptance: SC-001 and SC-002.

### Key Entities *(include if feature involves data)*

- **Runtime phase**: Execution stage governing permitted work, callback timing, and visibility of pending changes.
- **Pending lifecycle change**: Object/component change awaiting participation updates or safe release.
- **Transform change**: Local/hierarchy change that must reach consumers and may wake a body.
- **Body sleep state**: Sleeping status, quiet history, eligibility, and connected-group participation.
- **Support relationship**: Physical contact contribution affecting sleep eligibility and waking.
- **Runtime lifetime**: Ownership interval for a runtime's objects/resources, distinct from later recreation.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All six journeys have before-and-after walkthroughs. At final maintainer review, each journey's order, ownership, visibility, and cleanup can be explained from the code with zero unresolved ownership/timing questions. Outstanding maintainer review is reported as pending.
- **SC-002**: Every reviewed redundancy candidate has a disposition. At least one journey demonstrates fewer independently maintained facts, duplicated maintenance paths, or unnecessary forwarding/coordination steps without equivalent hidden complexity elsewhere. The maintainer confirms improved auditability; line count and document volume are not substitutes.
- **SC-003**: All six journeys and applicable edge cases pass recorded behavioral comparisons with zero introduced compatibility failures, missed/duplicate required callbacks, or stale-reference failures. Controlled scenarios preserve exact contracts and declared numerical tolerances; unconstrained solved-trajectory equality is not required.
- **SC-004**: Changed functions contain zero unbraced control-flow bodies or compressed statement groups obscuring logical stages. The maintainer can distinguish those stages without reformatting the code.
- **SC-005**: Every applicable FR-011 workload dimension has comparable before-and-after results. Every observed timing, work, allocation, memory, or worker-scaling regression has an investigation and explicit disposition under the constitution. Missing/inconclusive evidence remains unverified; no arbitrary slowdown allowance or speedup quota is introduced.
- **SC-006**: Repeated sleep/wake, activation, replacement, scene-transition, and recreation cycles restore expected populations and participation without stale work or continuing unexplained memory growth at a fixed peak population. Safety checks report actual coverage and limitations.

## Assumptions

- This invocation creates a specification; implementation and design selection follow in later phases. The primary user is the human engine maintainer requesting easier audits.
- [Constitution 1.1.0](../../.specify/memory/constitution.md) governs. Readability improvements must satisfy correctness and performance/scalability obligations; existing complexity earns retention through purpose and evidence.
- Commit `79069591488a68223f8e71ce5f24ddf2d37fb609` and earlier development are inspection context, not rollback targets. The baseline is current working code, including subsequent cleanups, recorded before implementation.
- The preceding [physics bookkeeping feature](../003-simplify-physics-bookkeeping/spec.md) removed canonical solver ordering. Preserve that decision and the current transition-driven contact contract. Controlled event ordering remains required where applicable; exact unconstrained trajectories across workers do not.
- Initial inspection areas are `src/gamemanager.*`, `src/runtime.cpp`, `src/runtimeaccess.*`, and sleep/wake portions of Rigidbody, PhysicsManager, and their participation/support collaborators. Related lifecycle, transform, and scene code is included only as needed for J1–J6. These boundaries prescribe no replacement classes, file layout, containers, or algorithms.
- New gameplay mechanics, public API redesign, deliberate sleep-rule changes, foundational broad-phase/solver redesign, editor work, and unrelated serialization, asset, audio, packaging, or benchmark rewrites are excluded. Existing resource ownership and measurement support remain relevant.
- Existing runtime-step/work, hierarchy, support, physics-bookkeeping, scene-transition, and recreation checks and [benchmark methodology](../../benchmark/README.md) are validation dependencies. Add focused coverage where changed behavior/invariants lack it; formatting-only changes do not require duplicate tests.
- Distinguish pre-existing defects and measurement limits from introduced regressions. Record unrelated findings separately rather than silently changing behavior or expanding scope.
