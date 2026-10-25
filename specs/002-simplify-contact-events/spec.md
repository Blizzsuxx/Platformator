# Feature Specification: Simplify Contact Events

**Feature Branch**: `001-cpp-engine-cleanup` (existing branch; feature directory is independent)

**Created**: 2026-10-25

**Status**: Draft

**Input**: User description: "Simplify the events and contacts system. Remove the Stay events. Enter/exit events come from contact transitions, independently of script availability. Stay events and callbacks are removed."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Produce events from contact transitions (Priority: P1)

As a game author, I want a contact beginning or ending to produce the same event regardless of whether either object has a script, so contact behavior remains predictable when scripts change.

**Why this priority**: Contact state must describe the physical interaction rather than whether a script can currently receive a notification.

**Independent Test**: Run a separate-to-touching-to-separated sequence with neither object, one object, and both objects having eligible scripts. Compare generated events in every run and callbacks for each eligible recipient.

**Acceptance Scenarios**:

1. **Given** two eligible colliders without scripts and no confirmed contact, **When** a physics update confirms their contact, **Then** exactly one Enter event is generated for the pair.
2. **Given** a confirmed contact without scripts, **When** a physics update confirms its end, **Then** exactly one Exit event is generated for the pair.
3. **Given** the same contact transition in runs with zero, one, or two eligible script recipients, **When** events are produced, **Then** the generated event sequence is identical and each eligible recipient receives its corresponding callback once.
4. **Given** overlapping bounding regions whose shapes do not touch, **When** the interaction is evaluated, **Then** no Enter or Exit event is generated.
5. **Given** a pair spanning multiple spatial regions, **When** it begins or ends one confirmed contact, **Then** duplicate spatial observations do not create duplicate events or a premature Exit.

---

### User Story 2 - Remove Stay from the contact lifecycle (Priority: P1)

As a game author, I want only Enter and Exit contact notifications, so an unchanged contact does not repeatedly notify scripts or require Stay-specific bookkeeping.

**Why this priority**: Removing Stay is the requested simplification and removes recurring event work for persistent contacts.

**Independent Test**: Keep a pair in contact for 100 physics updates, then separate it. Observe one Enter, no intervening contact events or callbacks, and one Exit; review the supported contact callback surface and current usage guidance.

**Acceptance Scenarios**:

1. **Given** a newly confirmed contact, **When** it remains confirmed for 100 subsequent physics updates, **Then** its event history contains only the initial Enter, with no Stay events or repeated Enter callbacks.
2. **Given** an unchanged contact between stationary or sleeping objects, **When** further physics updates occur, **Then** no recurring contact event or callback work is performed for that pair.
3. **Given** the supported contact callback contract, **When** an author reviews available callbacks and current examples, **Then** only Enter and Exit contact callbacks are supported and Stay callbacks are absent.
4. **Given** current event reporting and benchmark output, **When** a run reports contact events, **Then** Enter and Exit counts reflect generated pair events, including pairs without scripts, and no Stay event metric is reported.

---

### User Story 3 - Change scripts without changing contacts (Priority: P2)

As a game author, I want to add, remove, or enable scripts during a contact without creating artificial transitions or disrupting physical behavior.

**Why this priority**: Separating event production from delivery must remain correct during ordinary gameplay and callback-driven changes.

**Independent Test**: Change script availability while colliders remain touching, then separate them. Repeat with object removal during a callback and compare physical behavior and event histories with equivalent control runs.

**Acceptance Scenarios**:

1. **Given** a contact that began without a script, **When** an eligible script is added during that contact, **Then** no Enter is replayed; the script receives Exit if it remains eligible when the real contact ends.
2. **Given** an existing contact, **When** scripts are removed, replaced, enabled, or disabled while collider eligibility and geometry remain unchanged, **Then** no contact transition is created and physical contact handling is unaffected.
3. **Given** a generated transition, **When** a recipient has no eligible script at delivery, **Then** that recipient receives no callback and the generated event remains valid for the other eligible recipient.
4. **Given** an Enter or Exit callback that removes a script, collider, or object, or requests a scene change, **When** remaining notifications and deferred lifecycle work execute, **Then** existing mutation and lifetime guarantees are preserved without duplicate callbacks or access to retired recipients.
5. **Given** a resting supported body, **When** its contact persists without Stay callbacks and later loses its final support, **Then** support, sleeping, and wake-up behavior match the existing physical contract.

### Edge Cases

- First contact, persistent contact, separation, and re-contact produce Enter, silence, Exit, and a new Enter respectively.
- A contact ending because filtering changes, an object becomes inactive, or a collider is removed produces one Exit; any callback is subject to recipient eligibility and safe lifetime handling.
- A filter change that permits an unchanged geometric overlap again produces a new Enter when the contact is confirmed.
- Script availability changing between event generation and delivery affects delivery only; past events are not retained for later script attachment or activation.
- A script attached after contact begins can receive Exit without having received Enter. This follows contact transitions rather than per-script notification history.
- Multiple spatial witnesses, repeated synchronization, and different worker completion orders must not duplicate or reorder the observable transition sequence for an equivalent run.
- Stationary, sleeping, and moving contacts remain valid for physical handling even when they produce no new events.
- Scene replacement clears old-scene pending work under the existing lifecycle contract; old events must not reach the replacement scene.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST generate exactly one Enter event per collider pair when confirmed contact changes from absent to present, independently of script presence, behavior count, or callback eligibility. Acceptance: User Story 1, scenarios 1 and 3.
- **FR-002**: The system MUST generate exactly one Exit event per collider pair when confirmed contact changes from present to absent, including separation, filtering exclusion, deactivation, and collider removal. Event generation MUST NOT depend on script availability. Acceptance: User Story 1, scenarios 2 and 3, and lifecycle edge cases.
- **FR-003**: The system MUST use confirmed shape contacts as the source of transitions. Candidate overlaps and duplicate spatial observations MUST NOT manufacture contacts, duplicate transitions, or end a contact that still has valid spatial coverage. Acceptance: User Story 1, scenarios 4 and 5.
- **FR-004**: The system MUST produce no contact event when the confirmed contact state is unchanged. Re-contact after an actual end MUST produce a new Enter. Acceptance: User Story 2, scenarios 1 and 2, and the re-contact edge case.
- **FR-005**: The system MUST deliver Enter and Exit callbacks once to each recipient that is eligible under the existing callback and lifetime rules at delivery. An unavailable recipient MUST NOT suppress the other recipient's callback. Acceptance: User Story 1, scenario 3; User Story 3, scenarios 3 and 4.
- **FR-006**: Adding, removing, replacing, enabling, or disabling scripts alone MUST NOT change contact state, generate Enter or Exit, or replay past events. An eligible recipient MAY receive Exit for a contact whose Enter occurred before it became eligible. Acceptance: User Story 3, scenarios 1 and 2, and the delivery-timing edge case.
- **FR-007**: Stay events and callbacks MUST be removed from the supported contact lifecycle, including their declarations, dispatch paths, current examples, tests, and current contract documentation. They MUST NOT remain as deprecated callbacks or inactive compatibility placeholders. Acceptance: User Story 2, scenarios 1 and 3.
- **FR-008**: Bookkeeping, subscription maintenance, recurring contact traversal, and reporting that exist solely to support Stay events MUST be removed. Persistent contacts MUST incur no recurring work solely to construct or deliver contact events; event production MUST follow actual transitions rather than enumerate unchanged contacts. Acceptance: User Story 2, scenario 2, with affected-work evidence.
- **FR-009**: Contact persistence, collision response, support tracking, sleeping, and wake-up behavior MUST remain independent of script availability and MUST preserve their existing physical guarantees after Stay removal. Acceptance: User Story 3, scenarios 2 and 5.
- **FR-010**: Enter and Exit processing MUST preserve deterministic ordering for equivalent inputs across supported worker counts and existing callback mutation, retirement, deferred destruction, and scene replacement guarantees. Acceptance: User Story 3, scenario 4, and ordering and scene-replacement edge cases.
- **FR-011**: Current event metrics MUST report generated Enter and Exit pair events independently of delivered callback counts, and MUST remove Stay-specific metrics. Updated benchmark output and its current consumers MUST agree on the changed reporting contract. Acceptance: User Story 2, scenario 4.

### Key Entities *(include if feature involves data)*

- **Collider pair**: Two colliders considered together, regardless of how many spatial regions observe their overlap.
- **Confirmed contact**: A physical touching relationship established by shape testing, with a lifetime independent of scripts and with physical information needed for response and support.
- **Contact transition event**: One pair-level Enter or Exit describing a change in confirmed contact state. Its existence is distinct from the number of callbacks delivered.
- **Callback recipient**: A script-bearing object whose current eligibility and lifetime determine whether it receives a generated transition notification.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every verified contact lifetime produces exactly one Enter and one Exit, with identical pair-event histories for zero, one, and two eligible script recipients.
- **SC-002**: Across 100 unchanged physics updates after contact begins, each persistent pair produces zero additional contact events or callbacks; the later separation produces exactly one Exit.
- **SC-003**: All script-availability scenarios produce zero synthetic transitions or replayed notifications, and every eligible recipient receives each real transition once.
- **SC-004**: Supported callback surfaces, current examples, and current event reports contain zero Stay contact callbacks or metrics, and all maintained consumers use the revised contract.
- **SC-005**: Equivalent runs with one and multiple workers produce identical ordered transition histories, while support, sleeping, collision response, and callback mutation scenarios preserve existing correctness guarantees.
- **SC-006**: Affected-work evidence shows zero per-update event-production visits to unchanged contacts, both with and without scripts. Representative small and large scenes, persistent-contact populations, contact churn, and worker counts are compared against a recorded baseline; any timing or allocation regression is investigated before acceptance. No unmeasured speedup or arbitrary latency target is required.

## Assumptions

- The feature applies to the existing collider contact event family. It does not introduce a separate trigger-event family or change which shape interactions qualify as contacts.
- Script availability means the presence and delivery eligibility of scripts and behaviors. Collider eligibility, object activation, filtering, and physical geometry remain inputs to actual contact state.
- The request authorizes an intentional public compatibility break for Stay callbacks and a reporting compatibility break for Stay metrics. Affected callers, tests, current documentation, and benchmark consumers must be updated; archived specifications and historical benchmark captures remain historical records.
- Enter and Exit names, payload meaning, callback timing, and lifecycle eligibility rules remain as currently defined except for transition-driven generation and the explicitly documented absence of notification replay.
- Continued per-update gameplay logic can use existing update callbacks; this feature adds no replacement recurring contact notification or new contact-query capability.
- Existing contact detection, physical response, support handling, lifecycle boundaries, and benchmark methodology are dependencies. Broad-phase redesign, solver redesign, unrelated event families, and unrelated editor work are outside this feature.
- Performance validation follows the project constitution and benchmark methodology, using representative workloads and measured evidence rather than invented numerical performance targets.
