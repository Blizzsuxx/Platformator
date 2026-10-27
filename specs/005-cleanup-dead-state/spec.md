# Feature Specification: Clean Up Dead State and Internal Code

**Feature Branch**: `001-cpp-engine-cleanup` (existing branch; no branch was created for this specification)

**Created**: 2026-10-27

**Status**: Draft

**Input**: User description: "cleanup dead state and internal code as per the audit findings"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Remove obsolete collision bookkeeping (Priority: P1)

As a game developer, I want collision detection and response to retain only state that contributes to their current behavior, so the engine is easier to maintain and does not spend memory and work maintaining unused collision records.

**Why this priority**: Collision bookkeeping is performance-sensitive, and several audited fields appear to have been retained for mechanisms that are not active. Their removal must preserve contact behavior and parallel safety.

**Independent Test**: Exercise creation, persistence, movement and removal of overlapping pairs, including contacts shared across cells, with one and multiple workers. Compare contact events, support and solver outcomes with the pre-cleanup behavior while reviewing the disposition of each collision finding.

**Acceptance Scenarios**:

1. **Given** overlapping objects that span one or more cells, **When** they move or are removed, **Then** contact creation, persistence and exit occur as before, including when one cell witness disappears while another remains.
2. **Given** persistent dynamic contacts, **When** the simulation processes penetration correction and repeated contact updates, **Then** normal and friction response, warm starting and support behavior remain equivalent to the baseline within the project's existing tolerances.
3. **Given** enough independent pending pairs to exercise parallel processing, **When** equivalent scenes run with one and multiple workers, **Then** observable contact events and physical outcomes remain consistent with the established ordering and tolerance contracts.
4. **Given** an audited collision item, **When** the cleanup is reviewed, **Then** the obsolete item has been removed or documented as retained with evidence of a current consumer or invariant.

---

### User Story 2 - Simplify object and component lifecycle state (Priority: P1)

As an engine maintainer, I want unused object, component and runtime bookkeeping removed, so ownership and lifecycle code express the state the engine actually needs.

**Why this priority**: These objects are used throughout scene creation, modification and teardown, where redundant state can obscure active lifecycle guarantees.

**Independent Test**: Create and load scenes; add, replace, deactivate and remove components; destroy objects and clear scenes. Verify the same observable lifecycle and callback behavior before and after the cleanup, including the audited unused runtime and audio state.

**Acceptance Scenarios**:

1. **Given** a scene with a hierarchy of objects and components, **When** objects are created, reparented, moved and destroyed, **Then** their visible positions, component behavior and lifecycle callbacks remain consistent with the baseline.
2. **Given** a component that is replaced, deactivated or removed, **When** pending lifecycle work is reconciled, **Then** its existing system registration and retirement behavior remains safe and observable events remain unchanged.
3. **Given** active audio and fire-and-forget playback, **When** an audio component or its owner is removed, **Then** playback ownership, stop and release behavior continue to follow the existing lifecycle contract.
4. **Given** a simulation that falls behind its fixed-step schedule, **When** the existing catch-up limit is reached, **Then** its stepping and dropped-work policy remains unchanged without requiring a new telemetry feature.

---

### User Story 3 - Remove unused support paths and document compatibility (Priority: P2)

As a library consumer and maintainer, I want unused asset-path and debug-drawing support code cleaned up with a clear account of affected public interfaces, so the engine remains predictable to use and straightforward to update.

**Why this priority**: These findings reduce maintenance cost while touching asset diagnostics, debug tooling and a small number of installed declarations.

**Independent Test**: Load valid, invalid, missing and fallback assets; render the supported debug overlays; build an independent consumer using the retained public interfaces; and check the documented migration for the audited removals.

**Acceptance Scenarios**:

1. **Given** valid, invalid, missing or fallback asset requests, **When** those assets are resolved, **Then** the same asset selection, containment checks and error messages remain available for each case.
2. **Given** debug overlays are enabled, **When** supported collider, contact and grid information is rendered, **Then** the same visible overlays and controls remain available.
3. **Given** a consumer uses a public declaration identified as unused in the audit, **When** the consumer updates to the cleaned-up library, **Then** release or migration documentation identifies the removed declaration and the appropriate action.
4. **Given** a consumer uses a public interface outside the identified removals, **When** it is rebuilt against the cleaned-up library, **Then** the supported interface remains usable.

### Edge Cases

- A pair spans several cells, loses one witness, and later loses its final witness; neither duplicate transitions nor premature removal may result.
- A pair changes contact orientation or has one or two contact points; retained warm-start and penetration-correction behavior must continue to use the existing physical state.
- A parent and child change position in the same step, or a child is reparented while transform work is queued; hierarchy ordering and immediate position reads must retain their current semantics.
- A component is replaced or its owner is retired while callbacks, collision work or deferred reconciliation are pending; eligible events and destruction lifetimes must follow the existing contract.
- Audio playback is already stopped, is deactivated and reactivated, or outlives its initiating component through fire-and-forget playback; the existing resource lifetime rules apply.
- The requested asset is missing, malformed or outside the configured root; useful original-request diagnostics and fallback/error behavior must remain intact.
- Debug tools are disabled or there is no active camera; existing build and rendering behavior must remain valid.
- A fresh audit finds a proposed item is used by current code or required by a supported contract; it must be retained with specific evidence rather than removed on the strength of the older audit alone.
- An external consumer uses one of the identified public declarations; the removal is an intentional compatibility change and must be documented rather than silently presented as compatible.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The cleanup MUST examine every confirmed-unused finding C01–C14 in [the audit](../../docs/CODE_CLEANUP_AUDIT.md) against the implementation current at the start of work, and record whether it was removed or retained with a concrete current consumer or correctness invariant.
- **FR-002**: For findings confirmed unused, the cleanup MUST remove the obsolete state, declarations, definitions and maintenance-only call chains identified by the audit; it MUST NOT introduce a new feature merely to provide a use for them.
- **FR-003**: Collision-pair cleanup MUST preserve pair identity, witness counting, candidate coverage and deduplication, parallel work boundaries, contact and solver membership, and all active indexes used by those functions.
- **FR-004**: Contact-state cleanup MUST preserve penetration and restitution correction, the accumulated normal and friction impulses that affect the solver, and both existing normal-solver paths. A separate bias or split-impulse solver is outside this feature.
- **FR-005**: Object and lifecycle cleanup MUST preserve hierarchy ordering, immediate and synchronized transform behavior, component registration, replacement and retirement, callbacks, scene transitions, object identity and the established catch-up/drop policy.
- **FR-006**: Audio cleanup MUST preserve component playback, autoplay, deactivation, removal and resource release, including the ownership and completion of fire-and-forget playback.
- **FR-007**: Asset-path cleanup MUST preserve original-request diagnostics, path validation and containment, canonical identity, required-asset errors and optional fallbacks.
- **FR-008**: Debug-code cleanup MUST preserve all currently supported overlay output and controls and the behavior of builds with debug tools enabled and disabled.
- **FR-009**: The cleanup MUST preserve all supported public interfaces and serialized scene and asset formats, except for the explicitly scoped unused public declarations and layout identified by C03, C04 and C12, and the private GameObject layout change in C02. C03, C04 and C12 include intentional source and potentially binary compatibility changes; C02 changes binary layout and requires consumers to rebuild; affected consumers MUST receive a migration note and rebuild guidance.
- **FR-010**: Documentation that claims removed collision-pair slots have active enumeration or result consumers MUST be corrected to reflect the resulting implementation, without rewriting historical evidence as if it were newly measured.
- **FR-011**: Existing meaningful behavior coverage MUST remain in place. Assertions that solely preserve removed state MUST be updated while preserving coverage of observable behavior, including contact-feature matching and normal/tangent warm starting.
- **FR-012**: Before making a performance claim, the work MUST establish comparable baseline and candidate evidence for workloads affected by the cleanup, including pair insertion/removal and retained memory; record worker configuration and distinguish timing from correctness instrumentation. Any unmeasured effect MUST be reported as unverified.
- **FR-013**: The cleanup MUST preserve independent pair processing and the existing synchronization and ownership boundaries; its verification MUST cover both single-worker and multi-worker operation where affected.
- **FR-014**: The feature MUST remain limited to C01–C14 and necessary compatibility, validation and documentation updates. The audit's separate simplification, performance, editor and example findings S01–S06, P01–P07 and E01–E05 remain outside scope unless needed to complete a C01–C14 removal.

### Key Entities *(include if feature involves data)*

- **Audit finding**: One of the fourteen confirmed-unused candidates, identified by C01–C14, with an evidence-backed final disposition.
- **Collision pair and contact**: The relationship between eligible colliders and its observable contact, support and response behavior; it remains valid for as long as the existing physics contract requires.
- **Object and component lifecycle**: The owner, component and runtime state governing registration, callbacks, retirement and resource release.
- **Public consumer**: A project or external user of installed library declarations who must be able to identify and migrate any explicitly removed interfaces.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All 14 findings C01–C14 have a documented, evidence-backed disposition, and every item still confirmed unused has been removed with its maintenance-only dependencies.
- **SC-002**: All applicable existing regression and integration checks for the affected collision, hierarchy, lifecycle, asset, audio and debug behavior pass; any pre-existing failure is identified against an equivalent baseline rather than attributed to the cleanup without evidence.
- **SC-003**: Controlled single-worker and multi-worker scenarios preserve the project's established contact-event ordering and physical tolerances for pair creation, persistence, movement and removal. Unconstrained solved trajectories are not required to match across workers, consistent with the existing solver-order waiver.
- **SC-004**: An independent consumer can build and use the retained public interfaces, and 100% of the explicitly removed public declarations and layout impacts have migration and rebuild guidance.
- **SC-005**: A reviewer can trace any performance claim to comparable baseline and candidate measurements of relevant pair churn and memory, or see that the claim remains unverified; no speedup target is assumed in advance.

## Assumptions

- The audit at baseline `61949f4` is the source of candidate findings; later code and supported usage must be checked before implementation. The audit document was committed subsequently in `66707f6`.
- Removing unused public contact, audio and projection declarations in C03, C04 and C12 and the private GameObject field in C02 is within the requested cleanup. C02 changes the layout of an installed class, so consumers must rebuild even though no public declaration is removed. Consumers must follow documented migration guidance; all other public behavior and serialized formats are preserved.
- The unobserved dropped-step accumulator in C05 will be removed when confirmed unused. Adding or exposing new dropped-step telemetry is outside scope.
- No engine implementation is modified by creating this specification. Planning will choose the relevant existing checks, independent consumer validation, workload matrix and any necessary focused coverage.
- The project constitution and existing benchmark methodology govern performance-sensitive planning and validation; the cleanup does not claim an unmeasured speedup.
