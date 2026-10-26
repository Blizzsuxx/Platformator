# Feature Specification: Simplify Physics Bookkeeping

**Feature Branch**: `001-cpp-engine-cleanup` (existing branch; feature directory is independent)

**Created**: 2026-10-25

**Status**: Draft

**Input**: User description: "Review and simplify the physics bookkeeping introduced by `7906959` as we just now discussed. You do not have to limit yourself to just changes made by the discussed commit—the goal of this spec is simplifying the codebase and making it more readable and easier to audit by a human (me)."


## User-directed scope amendment — 2026-10-26

The maintainer explicitly requested: "completely remove the ordering we just talked about" and "for now we don't really need deterministic physics solving." This supersedes this feature's earlier requirement to preserve canonical solver order and matching solved trajectories across workers. Solver pairs now use append/swap-remove membership order; remove the comparator, sort/reindex pass, ordering helper/call and dirty-order flag without a replacement ordering scheme. Indexed removal, contact completeness, support accounting, finite/stable physical behavior and safe worker/lifetime boundaries remain required. Canonical contact-event batch and endpoint ordering remain required for equivalent transitions; different solved trajectories may naturally produce different later transitions. Exact geometry/oracle and controlled event tests remain applicable, but cross-worker or old/new solved-trajectory equality is no longer an acceptance condition. Earlier canonical-solver design tasks and measurements describe the pre-amendment implementation; they are not a reason to retain the removed machinery or a current performance certificate.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Follow a physics change from cause to effect (Priority: P1)

As the engine maintainer, I want to follow an ordinary physics change through the code and explain which state changes, who owns it, and when it becomes valid, so I can review or modify the engine without reconstructing hidden dependencies.

**Why this priority**: Human understanding is the requested outcome. Existing complexity must earn its place through a clear correctness or performance purpose, rather than survive because an earlier change introduced it.

**Independent Test**: Review the before-and-after code for the eight audit journeys below. Starting from each triggering operation, trace the resulting physical work and identify state owners, update boundaries, and consumers. Have the maintainer perform the same walkthrough against the final code and record any unresolved questions.

**Acceptance Scenarios**:

1. **Given** a stationary body, **When** it begins moving, changes velocity, and stops, **Then** a reviewer can trace how it becomes eligible or ineligible for force and movement work, including changes that occur more than once before a physics update.
2. **Given** a registered body, **When** its type changes among static, kinematic, and dynamic, **Then** a reviewer can trace the consequences for movement, collision response, and sleeping without discovering an undocumented secondary update path.
3. **Given** supported bodies at rest, **When** they sleep and subsequently wake, **Then** a reviewer can explain how individual and connected bodies become eligible for work and when that decision takes effect.
4. **Given** a body with several supports, **When** it loses one support and then its final support, **Then** a reviewer can trace each support contribution and explain why waking is required or unnecessary.
5. **Given** registered bodies and colliders with pending work, **When** a component is removed or replaced, **Then** a reviewer can identify how old participation is cancelled, remaining contacts are updated, and obsolete references stop being used.
6. **Given** a collider pair, **When** contact begins, persists, ends, and begins again, **Then** a reviewer can distinguish spatial candidates, confirmed contacts, physical response, and notifications throughout the lifetime.
7. **Given** existing contacts, **When** filtering or object activation changes, **Then** a reviewer can trace which physical participation changes and when resulting contact transitions become observable.
8. **Given** pending physics work and notifications, **When** a callback retires an object or requests scene replacement, **Then** a reviewer can locate the boundaries that finish or cancel work before memory is reclaimed or the replacement scene starts.

---

### User Story 2 - Maintain a smaller, coherent state model (Priority: P1)

As the engine maintainer, I want unnecessary bookkeeping and duplicated update responsibilities removed, so a correct change requires fewer independently coordinated facts and the code itself communicates the remaining rules.

**Why this priority**: A review report or a new file arrangement does not by itself make future changes easier. The feature must simplify actual state maintenance while preserving the behavior that state serves.

**Independent Test**: Compare the bookkeeping inventory and the eight audit journeys before and after the change. Demonstrate a concrete reduction in independently maintained state or update dependencies, and verify that each remaining mechanism has an identifiable owner, rule, and consumer.

**Acceptance Scenarios**:

1. **Given** a retained piece of bookkeeping, **When** a reviewer asks why it exists, **Then** its owner, meaning, update triggers, validity boundary, consumers, and correctness or scaling benefit are identifiable in the code and supporting review material.
2. **Given** multiple representations or maintenance paths for the same physical fact, **When** a simpler equivalent is feasible under the project constraints, **Then** the redundant state or coordination is removed and the new source of truth is clear.
3. **Given** a proposed simplification, **When** its before-and-after walkthrough is reviewed, **Then** the code requires fewer independently maintained facts or update dependencies on at least one audit journey, without concealing equivalent complexity behind new names or indirection.
4. **Given** an existing mechanism whose benefit justifies retention, **When** simpler alternatives are considered, **Then** the retained mechanism has a specific rationale and the comparison does not assume that all existing optimizations must remain or that all bookkeeping must disappear.

---

### User Story 3 - Trust the engine after simplification (Priority: P1)

As a game author and engine maintainer, I want the simplification to preserve gameplay behavior, safe lifetimes, and useful scaling, so improved readability does not introduce physical regressions or turn quiet scenes into repeated global work.

**Why this priority**: Correctness and performance remain constitutional constraints on the readability goal. The current transition-driven contact contract is the baseline for this feature.

**Independent Test**: Exercise the audit journeys against a recorded pre-change baseline. Compare physical results and ordered event histories with one and multiple workers, then compare work performed, allocations, memory, and average and tail step times on equivalent representative workloads.

**Acceptance Scenarios**:

1. **Given** equivalent scenes and inputs before and after simplification, **When** the eight audit journeys execute, **Then** contact completeness, response, support, sleeping, wake-up, and lifecycle behavior agree under recorded tolerances and exact ordering rules.
2. **Given** a contact sequence with zero, one, or two eligible script recipients, **When** contact begins, persists, and ends, **Then** generated Enter/Exit histories remain independent of scripts, unchanged contacts produce no recurring notifications, and Stay remains absent.
3. **Given** equivalent runs with one and multiple workers, **When** worker completion orders vary, **Then** observable notification order and physical results remain consistent with the established contract and no unsafe concurrent mutation or premature reclamation is introduced.
4. **Given** quiet scenes and scenes with movement, dense contacts, churn, or uneven spatial occupancy, **When** before-and-after measurements are compared, **Then** the maintainer can assess saved complexity against actual runtime, allocation, and memory costs, with every regression and unresolved limitation explicitly reported.

### Edge Cases

- Repeated or cancelling changes before work is consumed must not leave a body or contact participating twice, absent when needed, or carrying stale participation after removal.
- A body can change type, gravity usage, velocity, activation, or sleeping state while contacts remain geometrically unchanged; physical eligibility must still be updated at the correct boundary.
- One support disappearing must not wake a body solely because intermediate bookkeeping temporarily reaches zero when another support is added during the same established reduction boundary. Losing final support must retain the existing wake-up behavior.
- Several spatial observations of the same pair must not duplicate contact participation or end contact while valid coverage remains elsewhere.
- Components can be replaced while previous instances still have pending support changes, notifications, or body work. New instances must not inherit stale participation or references.
- Callback exceptions, self-removal, removal of the other endpoint, and both endpoints retiring must retain existing completion, delivery eligibility, and cleanup guarantees.
- Empty scenes, isolated bodies, sleeping populations, large connected groups, and repeated add/remove cycles must remain valid, with retained storage and work growth accounted for.
- Contact or shape recomputation may be necessary without a new contact lifetime; internal refresh must not manufacture Enter/Exit or reintroduce script-driven physical maintenance.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The review MUST cover the physics bookkeeping introduced by `79069591488a68223f8e71ce5f24ddf2d37fb609` and the current related code needed to understand or simplify it. Commit provenance and the four initially discussed files MUST NOT be treated as hard boundaries. Expansion MUST be justified by a dependency on an audit journey; this is not an unrestricted repository rewrite. Acceptance: all eight journeys in User Story 1 and the scope rationale.
- **FR-002**: The feature MUST establish a before-and-after inventory of body participation, pending changes, contact participation, response eligibility, support, sleeping relationships, and ordering state. For every retained, removed, consolidated, or added mechanism, the inventory MUST identify its meaning, owner, update triggers, validity boundary, consumers, and disposition rationale. Acceptance: User Story 2, scenarios 1, 2, and 4.
- **FR-003**: The final code and concise supporting explanation MUST allow a reviewer to follow all eight audit journeys from trigger through consumption and cleanup, including deferred updates and worker boundaries. Necessary explanations of non-obvious invariants MUST be available beside the code they govern; documentation alone MUST NOT substitute for simplifying confusing control flow. Acceptance: User Story 1 and the maintainer walkthrough.
- **FR-004**: The implementation MUST remove or consolidate avoidable state maintenance or update dependencies and demonstrate the reduction on at least one audit journey. Renaming, reformatting, moving functions between files, or adding explanatory material alone MUST NOT satisfy this requirement. Acceptance: User Story 2, scenarios 2 and 3, with a before-and-after comparison.
- **FR-005**: Each retained or new bookkeeping mechanism MUST have a clear purpose and consistent maintenance rules. Duplicated representations, repeated eligibility decisions, cross-system invalidation, and generalized machinery MUST be challenged against simpler alternatives; retention MUST be justified by correctness, measured performance, or an explicit scaling need. Acceptance: User Story 2, scenarios 1 and 4.
- **FR-006**: The simplification MUST preserve existing physical behavior for body registration and type changes, movement, contact detection and response, support, sleeping, and waking. It MUST preserve candidate/contact distinctions and pair deduplication. Existing algorithms and state layouts MAY change only where the equivalence and benefit are demonstrated within this feature's scope. Acceptance: User Story 3, scenario 1, and the physical edge cases.
- **FR-007**: The current contact contract MUST remain intact: Enter/Exit follow confirmed transitions independently of scripts; persistent contacts generate no recurring notifications; late script changes create no replay or synthetic transitions; recipient eligibility is checked at delivery; Stay remains removed. Acceptance: User Story 3, scenario 2, and the existing contact specification.
- **FR-008**: Component replacement, retirement, callback mutation and exceptions, deferred destruction, and scene replacement MUST preserve their existing behavior and valid lifetimes. Removed objects MUST cease participating before their storage is reclaimed. Acceptance: User Story 1, scenarios 5 and 8; User Story 3, scenarios 1 and 3; lifecycle edge cases.
- **FR-009**: Mutation ownership, ordering, and visibility boundaries MUST remain explicit and correct with one and multiple workers. Changes MUST preserve deterministic event histories and physical results within declared numerical tolerances, and MUST NOT introduce races, deadlocks, or overlapping access to reclaimed state. Acceptance: User Story 3, scenario 3.
- **FR-010**: Simplification MUST be assessed against a recorded current baseline for small and large scenes, quiet and moving populations, contact churn and cell migration, uniform and uneven spatial distributions, and one and multiple workers. Evidence MUST include complete-step means and tails, run variation, affected work, allocations, and retained memory. Changed measurement boundaries MUST be labeled rather than presented as equivalent. Acceptance: User Story 3, scenario 4.
- **FR-011**: The feature MUST preserve efficient handling of unchanged or unaffected state unless a measured and explicitly justified tradeoff supports an alternative. Every timing, allocation, memory, or scaling regression MUST be investigated before acceptance; correct designs with meaningful measured performance or scaling advantages take precedence, while comparable designs favor simplicity. An unexplained regression or missing measurement MUST NOT be reported as a successful optimization. Acceptance: User Story 2, scenario 4; User Story 3, scenario 4; project constitution.
- **FR-012**: The completion review MUST include code-backed walkthroughs, the state inventory comparison, behavioral and cost evidence, and unresolved findings. The maintainer MUST be able to assess readability from the final code; automated checks or documentation quantity MUST NOT be reported as proof of human readability. Existing public authoring behavior, scene formats, and maintained reporting MUST remain compatible. Acceptance: all three stories, with a separate maintainer-readability finding.

### Key Entities *(include if feature involves data)*

- **Physics body**: An object with physical properties whose current state determines whether it receives forces, moves, responds to contacts, sleeps, or wakes.
- **Collider pair and confirmed contact**: A potential interaction and its physically confirmed touching relationship, distinct from spatial coverage and notification recipients.
- **Support relationship**: A contact contribution that affects whether a body is supported and how loss of support changes sleeping or waking.
- **Bookkeeping mechanism**: Maintained information or pending work used to avoid recomputation, preserve ordering, or coordinate physical state. Each has an owner and a defined point at which consumers may rely on it.
- **Audit journey**: One of the eight representative changes in User Story 1, followed from its cause through physical consequences and cleanup.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All eight audit journeys have a before-and-after walkthrough identifying owners, update timing, consumers, and cleanup. At final maintainer review, each journey can be explained from the code and concise accompanying material, with zero unresolved questions about who updates a fact or when it becomes valid. Pending maintainer review is reported as pending, not inferred to have passed.
- **SC-002**: Every bookkeeping mechanism in the reviewed scope has a recorded disposition and rationale. The final implementation demonstrates fewer independently maintained facts or update dependencies on at least one audit journey, with no equivalent hidden coordination introduced elsewhere. Every added mechanism has an explicit benefit; cosmetic changes alone do not count as simplification.
- **SC-003**: All eight journeys and their applicable edge cases preserve established physical and lifecycle behavior against the recorded baseline. Equivalent one-worker and multiworker runs agree within declared physical tolerances and produce identical ordered transition histories.
- **SC-004**: Across 100 unchanged updates, persistent contacts with zero, one, and two eligible script recipients produce zero additional contact events or event-production visits to unchanged contacts. Subsequent separation produces exactly one pair-level Exit, with delivery governed by existing eligibility rules.
- **SC-005**: The representative workload comparison covers every dimension in FR-010 and reports means, tails, variation, work, allocation, memory, and worker scaling. All regressions have an investigation and disposition; missing or inconclusive evidence remains explicitly unverified. No arbitrary speedup target or universal performance claim is required.
- **SC-006**: Repeated fixed-peak add/remove and replacement cycles restore the expected physical participation without stale references, duplicate events, or continuing unexplained growth in retained storage. The final review records executed safety checks and their actual limitations without treating unrelated or unresolved findings as passes.

## Assumptions

- The primary user is the human engine maintainer who requested this work. Readability is judged by their ability to audit the code, alongside objective before-and-after state and dependency evidence; it is not inferred from line count, file size, class count, or documentation volume.
- Commit `79069591488a68223f8e71ce5f24ddf2d37fb609` is historical context for the confusion, not a rollback target or the required behavioral baseline. The constitution applies to this new work even though it was written after that commit.
- The baseline is the current engine after [Simplify Contact Events](../002-simplify-contact-events/spec.md), including its transition-driven event contract. Existing correctness, lifetime, benchmark, and affected-work coverage are dependencies to preserve or strengthen.
- The initially discussed areas are physics body indexing, contacts, geometry, and orchestration. Related body, collider, spatial, or runtime lifecycle code is in scope when needed to simplify their state maintenance. Discovery outside the old commit is expected; unrelated editor, packaging, audio, and asset-system rewrites are excluded.
- This specification does not prescribe new classes, a particular file layout, a generic collection abstraction, deletion of all indexes, or wholesale replacement of incremental work with global scans. Planning must compare alternatives against the current constitution and the audit journeys.
- New physical features, a deliberate solver or broad-phase algorithm redesign, and solver parallelization are separate objectives. Local changes needed for simpler bookkeeping remain possible under FR-006 and FR-009; a material algorithm redesign requires a separately scoped proposal.
- Human readability and performance are both acceptance concerns. There is no blanket permission for an arbitrary slowdown, and no requirement to retain an optimization solely because it already exists. Tradeoffs follow the constitution rather than invented percentage thresholds.
- Previously recorded desktop leak and lock-order findings remain visible validation context. This feature must distinguish pre-existing limitations from introduced defects; fixing unrelated dependencies is not its purpose.
