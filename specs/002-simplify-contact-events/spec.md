# Feature Specification: Simplify Contact Events

**Feature Branch**: `001-cpp-engine-cleanup` (existing branch; feature directory is independent)

**Created**: 2026-10-25

**Status**: Draft

**Input**: User description: "Simplify the events and contacts system. Remove the Stay events. Enter/exit events come from contact transitions, independently of script availability. Stay events and callbacks are removed."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Report actual contact transitions (Priority: P1)

As a game author, I can rely on enter and exit events to describe when confirmed contacts begin and end, regardless of whether either object has scripts.

**Why this priority**: Contact history must describe physical interaction consistently, including interactions with no callback recipients.

**Independent Test**: Establish, maintain, and end the same contact with no scripts, scripts on one object, and scripts on both. Compare generated transitions separately from delivered callbacks.

**Acceptance Scenarios**:

1. **Given** two eligible colliders without scripts and no confirmed contact, **When** a contact is confirmed, **Then** exactly one enter event is generated for the pair and no callbacks run.
2. **Given** a confirmed contact, **When** it persists through subsequent simulation steps, **Then** no further contact events are generated until its state changes.
3. **Given** a confirmed contact, **When** the colliders separate, **Then** exactly one exit event is generated; continued separation generates no additional exits, and a later confirmed contact generates a new enter.
4. **Given** the same contact sequence with eligible scripts on one or both objects, **When** transitions occur, **Then** the generated event sequence is identical to the sequence without scripts, and each eligible behavior receives one corresponding callback per transition.
5. **Given** colliders whose bounding regions overlap without a confirmed contact, **When** collision detection runs, **Then** no enter or exit event is generated for that candidate pair.

---

### User Story 2 - Use an enter/exit-only contract (Priority: P1)

As a game author, I have a contact notification contract containing enter and exit only, with clear migration guidance for behavior that previously relied on Stay callbacks.

**Why this priority**: Removing recurring notifications simplifies the contract and eliminates notifications for unchanged contacts.

**Independent Test**: Exercise moving, stationary, sleeping, and trigger contacts across multiple steps; inspect the supported callback contract and maintained consumer guidance.

**Acceptance Scenarios**:

1. **Given** a confirmed moving, stationary, sleeping, or trigger contact, **When** it persists for 100 additional steps, **Then** no Stay event or callback occurs and the contact continues to have its existing physical effect.
2. **Given** a game author reviewing the supported contact notifications, **When** they consult the public behavior contract and current documentation, **Then** only enter and exit are offered; Stay callbacks are removed rather than retained as compatibility no-ops.
3. **Given** a maintained consumer that previously used Stay callbacks, **When** it is migrated, **Then** it uses the enter/exit-only contract and migration guidance explains how to manage ongoing interaction through ordinary updates where needed.
4. **Given** newly recorded event diagnostics, **When** contact activity is reported, **Then** enter and exit counts describe generated pair transitions independently of callback recipient counts, and Stay is not reported as a supported event category.

---

### User Story 3 - Keep contact history stable during lifecycle changes (Priority: P2)

As a game author, I can change scripts or retire contacting objects without inventing contact transitions, duplicating callbacks, or accessing retired state.

**Why this priority**: Script independence must hold during lifecycle changes as well as ordinary movement.

**Independent Test**: Change script availability during a persistent contact, then exercise filter changes, object retirement, and scene replacement. Repeat equivalent contact scenarios with serial and parallel processing.

**Acceptance Scenarios**:

1. **Given** a contact that began without a script on one object, **When** an eligible script is added while the contact persists, **Then** no enter is synthesized or replayed; if that script remains eligible when the contact later ends, it receives the exit callback even though it never received an enter.
2. **Given** a persistent contact, **When** its script component is removed, replaced, or becomes ineligible without changing collider eligibility, **Then** no contact event is generated solely by that script change and the physical contact continues.
3. **Given** a confirmed contact, **When** filtering, collider deactivation, removal, or object destruction ends it, **Then** one exit transition is recorded, an eligible survivor receives the exit with the other collider still readable during that callback, and retired objects or replaced scripts receive no later callbacks.
4. **Given** equivalent initial scenes and changes, **When** contact processing runs serially or in parallel, **Then** enter/exit order and counts agree and support, sleeping, and physical results remain within existing correctness tolerances.

### Edge Cases

- An empty script component, a script awaiting initialization, and an object without a script all leave event generation unchanged; delivery follows existing initialization and eligibility rules.
- Adding or removing a script on one side cannot suppress transitions or callbacks on the other side.
- Disabling and re-enabling a filter or collider before the existing reconciliation boundary produces no exit/enter if the final confirmed contact is unchanged. Ending contact at one boundary and restoring it at a later boundary produces one exit followed by one enter.
- Multiple spatial cells witnessing the same collider pair produce one transition for that pair. Multiple behaviors may receive callbacks without multiplying the generated event count.
- A trigger or body-response change that leaves a confirmed contact intact creates no contact transition; its physical response and support state still update under existing rules.
- A callback that destroys an object, removes its script, or requests scene replacement follows the existing deferred mutation boundaries. Stale callbacks do not reach retired objects or a replacement scene.
- When both objects retire, no callback is delivered to either retired object. Retirement does not duplicate an already recorded exit.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST generate exactly one enter event per collider pair when its confirmed contact changes from absent to present, and exactly one exit event when it changes from present to absent.
- **FR-002**: Event generation and confirmed contact membership MUST be independent of script presence, behavior count, initialization state, and callback eligibility on either object.
- **FR-003**: A contact that remains present MUST generate no further contact events, including when colliders are stationary or bodies are sleeping. A candidate overlap without a confirmed contact MUST generate no contact event.
- **FR-004**: Callback delivery MUST apply existing initialization, lifecycle, and eligibility rules separately to each recipient. A generated pair event MUST invoke the corresponding callback once per eligible behavior, without requiring an eligible recipient on the other object.
- **FR-005**: Script availability changes alone MUST NOT start or end contacts, generate synthetic transitions, or replay earlier transitions. An eligible script attached during an existing contact MUST receive a subsequent exit even if it did not receive the earlier enter.
- **FR-006**: Stay events and callbacks MUST be removed from the supported contract and all event production and delivery. No deprecated Stay callback, compatibility no-op, or replacement recurring contact notification is included.
- **FR-007**: Confirmed-contact rules MUST apply to ordinary collisions and trigger overlaps. Physical response, support accounting, and sleeping behavior MUST remain correct without recurring contact notifications.
- **FR-008**: Contact termination from separation, filtering, collider deactivation or removal, and object retirement MUST produce a single exit transition. Net-unchanged changes coalesced at an existing reconciliation boundary MUST NOT manufacture transitions.
- **FR-009**: Enter/exit delivery MUST preserve existing callback mutation and scene-change boundaries, suppress callbacks to retired or replaced recipients, and keep the other collider readable during an eligible survivor's retirement exit callback.
- **FR-010**: Generated transition order and counts MUST remain deterministic and equivalent between serial and parallel processing for equivalent inputs. Multiple spatial witnesses MUST NOT multiply pair events.
- **FR-011**: Current event diagnostics MUST count generated enters and exits independently of scripts or the number of delivered callbacks, and remove Stay as a supported event category.
- **FR-012**: Affected maintained consumers, tests, and current documentation MUST adopt the enter/exit-only contract. Migration guidance MUST identify callback removal as a breaking change and explain ongoing interaction using the remaining lifecycle facilities.

### Key Entities *(include if feature involves data)*

- **Confirmed contact**: A validated interaction between a collider pair, including a trigger overlap, distinct from a candidate bounding-region overlap. Its lifetime is independent of scripts.
- **Contact transition event**: A single pair-level enter or exit describing a change in confirmed contact state. Its existence does not depend on successful callback delivery.
- **Callback recipient**: A behavior eligible to receive the corresponding transition callback under existing initialization and lifecycle rules. One event may have zero, one, or several recipients.
- **Contact diagnostics**: Reports of generated enter/exit transitions, whose counts do not change when only the number or availability of scripts changes.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For identical contact sequences with zero scripts, scripts on one object, and scripts on both objects, 100% of generated enter/exit transitions have matching types, order, and pair identities. Each uninterrupted contact interval has one enter and one exit when it ends.
- **SC-002**: A contact held for 100 additional steps after entry produces zero additional contact events or callbacks until exit, across moving, stationary, sleeping, and trigger scenarios.
- **SC-003**: All script-only additions, removals, and replacements in the acceptance scenarios produce zero contact transitions. Eligible recipients receive each subsequent transition once, including an exit after joining during contact.
- **SC-004**: All defined filtering, retirement, duplicate-witness, and serial/parallel scenarios pass without duplicate transitions, stale callbacks, or regressions in existing support and sleeping expectations.
- **SC-005**: All maintained consumers use the enter/exit-only contract, and a game author can find a consistent explanation of Stay removal and ongoing-interaction migration in current documentation, with no supported Stay event or callback remaining.

## Assumptions

- The target users are game authors and hosts of the existing Platformator runtime. This feature covers contact events and affected consumers; unrelated event families, the editor, and foundational collision algorithms are outside scope.
- Existing contact detection, filtering, reconciliation timing, behavior initialization, callback arguments, and scene-change boundaries remain the basis of the contract. A new contact-query facility, event subscription system, or callback-count reporting facility is not required.
- Script availability means whether an eligible behavior can receive a callback. Collider eligibility can independently affect contact state; changing an object's active state may therefore cause a real contact transition.
- The Unity-inspired behavior model deliberately adopts an enter/exit-only contact contract. Removing Stay is an intentional compatibility break; no automatic conversion of external user scripts is included.
- Game authors needing ongoing interaction can maintain their own state through enter/exit and ordinary updates. Late script attachment does not guarantee a matching earlier enter callback.
- Current guidance and newly produced diagnostics must describe the new contract. Historical specifications, recorded benchmark results, and other historical evidence retain their original meaning and are not rewritten.
