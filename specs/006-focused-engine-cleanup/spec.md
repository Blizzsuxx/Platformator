# Feature Specification: Focused Engine and Example Cleanup

**Feature Branch**: `001-cpp-engine-cleanup` (existing checkout)

**Created**: 2026-10-27

**Status**: Draft

**Input**: Cleanup audit S01–S05, P01 and P04, Mario findings E01–E02, and only the merger of the two physics membership helpers from S06. All requested assets are required; successfully loaded assets may be reused without rechecking the filesystem.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Predictable, reusable asset loading (Priority: P1)

A game developer loads assets through runtime, component, scene, or behavior paths and receives the requested asset or an actionable error. Repeated requests reuse loaded resources without repeated filesystem checks.

**Why this priority**: It establishes one predictable loading policy and removes repeated work for shared assets.

**Independent Test**: Load shared assets through runtime and scene paths, request missing assets through each supported path, and repeat requests after filesystem changes while the resource remains loaded.

**Acceptance Scenarios**:

1. **Given** a valid nonempty request for a missing texture, audio asset, or animation, **When** it is loaded through a runtime, component, scene, or reference path, **Then** loading fails with an error identifying the request and no fallback is substituted.
2. **Given** an asset successfully loaded and still retained, **When** it is requested again using a previously resolved request after the file or symlink changes, **Then** the already loaded asset is returned without filesystem revalidation.
3. **Given** a loaded resource is released from the resource cache, **When** its path is requested again, **Then** it is resolved and loaded anew or fails according to current filesystem state.
4. **Given** equivalent valid path spellings, **When** each is resolved, **Then** they share the same retained asset and maintain canonical asset identity.

### User Story 2 - Maintain focused engine logic (Priority: P1)

An engine maintainer can follow collision, asset, animation and physics membership behavior without unused parameters, unreachable checks or duplicate helpers hiding their responsibilities.

**Why this priority**: Reduces maintenance work while preserving the physics and lifecycle contracts.

**Independent Test**: Exercise overlap and contact transitions with one and multiple workers; use the collision and animation interfaces; verify body and pair membership transitions and moved-index repair.

**Acceptance Scenarios**:

1. **Given** overlapping objects and changing body/contact participation, **When** objects move, change state or are removed, **Then** collision coverage, membership, event and solver behavior remain consistent with the existing contracts.
2. **Given** an animation is playing or stopped, **When** callers inspect its current clip, **Then** the result remains correct and the supported name and any compatibility alias are documented.
3. **Given** collision contacts, **When** penetration correction is calculated, **Then** it continues to use actual contact separation and does not acquire a second penetration authority.

### User Story 3 - Keep debug rendering and the Mario example clear (Priority: P2)

A developer uses debug overlays and the Mario example with unchanged visible behavior while unnecessary copying, unused example state, and unimplemented declarations are removed.

**Why this priority**: Cleans up developer-facing code without expanding gameplay scope.

**Independent Test**: Build and exercise the example and debug-enabled and debug-disabled configurations; check debug output and scene compatibility against the baseline.

**Acceptance Scenarios**:

1. **Given** debug overlays with growing collections, **When** overlays are rendered, **Then** visual output and controls are preserved and avoidable debug-object copying is reduced.
2. **Given** existing Mario scenes, **When** the example loads and runs, **Then** its camera and enemy behavior remains unchanged and obsolete declarations and unused state no longer suggest nonexistent functionality.

### Edge Cases

- Empty paths used to represent an intentionally unset optional component remain distinct from nonempty requests for missing assets; existing no-asset behavior is preserved where supported.
- Invalid paths and first-time symlink escapes must still be rejected; errors must identify the original request.
- A retained asset continues to be returned for an already resolved request if its file is deleted, replaced, or its symlink changes; after eviction a fresh request revalidates it.
- Different path spellings can refer to the same retained asset; evicting that asset must invalidate all cached request mappings to it. Unresolved aliases still undergo first-time validation.
- Failure to read or decode an existing asset is an error, not an invitation to substitute a fallback.
- Membership removal of the last entry and of a middle entry must leave all retained indexes correct; callback-time script startup cancellation remains unchanged.
- Existing scenes containing the serialized Mario camera lead field continue to load; remove it only if compatibility is deliberately handled and documented.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Every nonempty texture, audio and animation asset request, including nested references, MUST load the requested asset or report failure with the original request; no implicit fallback asset may be selected.
- **FR-002**: Existing supported empty-path/no-asset behavior MUST remain distinct from missing nonempty asset requests.
- **FR-003**: First-time asset requests MUST preserve path validation, configured-root containment and canonical resource identity. A retained, previously resolved request MUST return its loaded asset without rechecking filesystem existence or symlink state.
- **FR-004**: Asset request mappings MUST remain valid only while their associated resource is retained; release or cache teardown MUST remove mappings so subsequent requests cannot return stale resources. Equivalent valid paths MUST share the same asset.
- **FR-005**: The change MUST document the shift from fallback behavior to required loading, affected public consumers, errors and migration actions; intentionally requested assets whose names happen to be fallback names remain ordinary assets.
- **FR-006**: Remove unused axis information from overlap plumbing while preserving the primary-axis distinction and all broad-phase overlap, witness and event semantics. (S01)
- **FR-007**: Remove unreachable required-fallback checks made obsolete by the single required-loading policy. (S02)
- **FR-008**: Remove the ignored collision-construction penetration argument and update supported consumers; retain contact separation as the source of penetration correction and document source compatibility impact. (S03)
- **FR-009**: Establish one preferred animation current-clip name; retain the other as a documented compatibility alias unless current usage and migration evidence justify its removal. Both supported names MUST return the same result. (S04)
- **FR-010**: Remove the unused internal duplicate behavior-lookup helper while preserving the supported behavior query. (S05)
- **FR-011**: Consolidate the two existing body and pair membership helpers into one understandable internal implementation, preserving each list's membership, swap-removal index repair, ownership and visibility invariants. No broader indexed-container refactor or change to script-start cancellation is in scope. (Narrow S06)
- **FR-012**: Remove the obstacle to moving debug objects and preserve overlay output and controls, including debug-enabled and debug-disabled configurations. (P01)
- **FR-013**: Remove verified unused Mario camera/enemy state, constants and undefined unused player declarations without changing existing gameplay or scene loading. Preserve the serialized camera-lead field as an inert compatibility field unless a safe migration is established. (E01–E02)
- **FR-014**: Validate affected behavior, reference lifetimes, scene and asset loading, debug output, public consumers and one/multiple-worker physics. Establish comparable baseline and candidate measurements for repeated asset requests and debug-object copying; report measured results and limitations without assuming a speedup.
- **FR-015**: Preserve existing user changes and preserve existing broad-phase, callback, scene publication and asset ownership invariants; document any intentional source or binary compatibility changes.

### Key Entities

- **Asset request**: A nonempty user-supplied path and asset kind to be validated and associated with a resource.
- **Retained asset**: A successfully loaded resource with a canonical identity and a lifetime governed by existing ownership rules.
- **Membership**: Whether a body or collision pair belongs to a specific active collection and the index used to maintain that collection.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All tested nonempty missing texture, audio and animation requests fail without fallback across runtime and scene/reference loading paths, with the requested path in the diagnostic.
- **SC-002**: In controlled repeated-load checks, 100% of requests for an already resolved, retained asset reuse the same resource without further filesystem resolution; a request after eviction is resolved anew.
- **SC-003**: All relevant baseline collision, membership, scene, asset and example behavior checks pass after the change, including one-worker and multiworker cases.
- **SC-004**: Debug overlays and Mario scene behavior match the baseline in available comparisons; any unavailable visual verification is explicitly reported as unverified.
- **SC-005**: The selected findings S01–S05, P01, P04, E01–E02 and the two-helper merger each have a documented disposition, with consumer migration guidance for intentional compatibility changes.
- **SC-006**: Comparative measurements report the effect on repeated asset loading and debug-object copying under equivalent workloads; no unmeasured runtime speedup is claimed.

## Assumptions

- Existing loaded resources are stable for the duration of their retention; changes to files or symlinks are deliberately ignored on a previously resolved cache hit.
- Resource eviction permits a subsequent load to observe the current filesystem. Failed loads are not promised to remain cached.
- Keeping the existing animation alias and serialized camera-lead field by default avoids unnecessary breaks while still addressing the audit findings; any removal needs evidence and migration handling.
- P02, P03, P05, P06, P07, editor findings E03–E05 and S06 beyond the two membership helpers are outside this feature. Python editor removal is a separate decision.
- Current working-tree edits are user input and must not be overwritten. Existing audit findings are rechecked against the actual source before changes.
