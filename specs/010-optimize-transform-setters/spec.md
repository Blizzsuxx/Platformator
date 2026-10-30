# Feature Specification: Efficient Transform Reads and Setters

**Feature Branch**: Current working branch

**Created**: 2026-10-30

**Status**: Draft

**Input**: Audit findings P02 and P05: reduce repeated parent-chain position calculations and unnecessary same-value collider/body updates while defining wake and reset behavior.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Read current positions efficiently (Priority: P1)

Game developers repeatedly read positions in object hierarchies and need current answers immediately after edits, without repeated ancestor calculations.

**Why this priority**: Accurate immediate reads are essential for gameplay and physics.

**Independent Test**: Move, reparent, detach and query nested objects before and after simulation.

**Acceptance Scenarios**:

1. **Given** a nested hierarchy, **When** an ancestor moves, **Then** all descendant position reads immediately reflect that movement.
2. **Given** an object with descendants, **When** it is reparented or detached, **Then** world positions are preserved and subsequent ancestor movement is reflected immediately.
3. **Given** moving parent and child bodies, **When** a frame completes, **Then** each object's world position reflects both movements.

### User Story 2 - Avoid redundant updates (Priority: P2)

Game developers can assign existing physical properties repeatedly without causing redundant geometry or participation updates, while intentional wake and reset operations retain their meaning.

**Why this priority**: Repeated assignments are common in gameplay code and should not create needless simulation work.

**Independent Test**: Repeat assignments on awake and sleeping bodies and colliders, inspect state and subsequent simulation.

**Acceptance Scenarios**:

1. **Given** unchanged shape, offset, material, mass or gravity values, **When** they are assigned again, **Then** no geometry or body participation update is requested and sleeping bodies remain asleep.
2. **Given** a sleeping body, **When** a motion or force value above the wake threshold is assigned, **Then** it wakes even if the stored value matches.
3. **Given** a body, **When** its current body type is assigned, **Then** the existing type initialization and reset behavior remains in effect.

### Edge Cases

- Invalid values are rejected even when the assignment would otherwise be redundant.
- Reparenting and detaching preserve descendant world positions; edits before a frame are visible immediately.
- Parallel parent and child movement must not leave stale reads or change synchronized frame semantics.
- Explicit wake and sleep operations retain their existing behavior.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Position reads MUST return the current world translation after any supported position or hierarchy change without waiting for a frame.
- **FR-002**: Repeated reads of an unchanged hierarchy MUST avoid repeating ancestor position calculations.
- **FR-003**: Hierarchy changes MUST preserve world positions and subsequent ancestor motion MUST reach descendants.
- **FR-004**: Assigning an unchanged shape, offset, mass, friction, restitution or gravity MUST avoid the associated geometry, inertia, wake and participation updates.
- **FR-005**: Unchanged velocity and angular velocity MUST avoid redundant participation updates but retain threshold-based waking; force and torque retain threshold-based waking.
- **FR-006**: Assigning the existing body type MUST preserve its initialization, wake and reset effects; explicit sleep and wake operations retain their existing contracts.
- **FR-007**: Invalid inputs MUST continue to be rejected and changed values MUST retain existing effects.
- **FR-008**: Synchronized rendering and physics snapshots and existing public interfaces MUST remain compatible.

### Key Entities

- **Object hierarchy**: Objects with local translations and parent relationships determining current world positions.
- **Physical body and collider**: Motion, material and shape properties that affect simulation and contacts.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All targeted hierarchy scenarios return the expected immediate position before and after a frame, including parent and child motion.
- **SC-002**: Repeated reads of an unchanged object require no ancestor position calculations per read.
- **SC-003**: Repeated unchanged shape and offset assignments cause zero additional collider geometry updates after a settled frame.
- **SC-004**: All targeted wake, reset, changed-value and invalid-input scenarios retain their defined outcomes.

## Assumptions

- Existing coordinate semantics, threading boundaries and public APIs remain authoritative.
- Body-type assignment is an intentional reinitialization operation, including when the type is unchanged.
- Performance improvements are assessed with focused evidence; no numerical speedup is assumed without comparable measurements.
