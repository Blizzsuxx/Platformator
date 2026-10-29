# Feature Specification: Linear Rendering Culling

**Feature Branch**: `branch-without-bvh`

**Created**: 2026-10-28

**Status**: Draft

**Input**: User description: "add linear culling implementation for rendering as we agreed. Try to stick to KISS where possible, try to keep the changes self contained and optimize for performance."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Skip sprites outside the view without changing the image (Priority: P1)

As a game developer, I want rendering to skip sprites that cannot contribute to the camera image, so that large scenes spend less time submitting invisible sprites while looking exactly as before.

**Why this priority**: Avoiding unnecessary drawing is the feature's purpose; missing visible sprites or changing transparency order would make the optimization unusable.

**Independent Test**: Render the same scene with culling enabled and disabled. Compare the resulting images and the ordered submitted sprites, including partially visible and overlapping transparent sprites.

**Acceptance Scenarios**:

1. **Given** active sprites inside, outside, and partially intersecting the camera view, **When** a frame is rendered with culling enabled, **Then** every potentially visible sprite is submitted, sprites with conservative bounds wholly outside the view are skipped, and the output matches culling disabled.
2. **Given** overlapping transparent sprites in the renderer's existing order, **When** offscreen sprites are skipped, **Then** the retained sprites keep their relative order and the composite image is unchanged.
3. **Given** rotated, scaled, or flipped sprites and an output aspect ratio different from the camera's proportions, **When** rendering occurs, **Then** the culling decision conservatively encloses the geometry actually drawn and never removes a visible portion.

---

### User Story 2 - Keep moving and changing scenes visually correct (Priority: P1)

As a game developer, I want sprites and the camera to move or change normally without stale visibility, special registration, or coupling to colliders.

**Why this priority**: Dynamic sprites are ordinary engine usage and were a central concern in the earlier culling investigation.

**Independent Test**: Run a deterministic scene with many sprites continuously moving across all view edges, then repeat with culling disabled and compare matching frames. Exercise camera and sprite changes in the same fixture.

**Acceptance Scenarios**:

1. **Given** many sprites continuously moving into and out of view, **When** they cross a camera edge, **Then** their visible portions appear on the same frame as with culling disabled, without popping or stale exclusions.
2. **Given** a stationary scene, **When** the camera moves, changes dimensions, is replaced, or the render output is resized, **Then** the next rendered frame uses the current view and matches the unculled reference.
3. **Given** sprites affected by parent movement, size, scale, rotation, texture or source-rectangle changes, **When** a change becomes visible to the existing renderer, **Then** culling reflects it in that same frame.
4. **Given** sprite activation, insertion, removal, component replacement, destruction, or scene transition, **When** the next frame renders, **Then** current membership and ordering are respected without stale references or duplicate submissions.

---

### User Story 3 - Verify that the focused optimization pays for itself (Priority: P2)

As an engine maintainer, I want a direct comparison with the existing submit-all rendering behavior, so that I can judge the complete rendering benefit and added cost without restoring the BVH experiment.

**Why this priority**: The earlier experiment showed that bookkeeping and measurement overhead can outweigh a faster visibility query. This feature must establish its own benefit on the cleaned branch.

**Independent Test**: Compare culling enabled, culling disabled, and the pre-feature baseline under equivalent rendered and headless workloads, retaining independent repetitions and workload details.

**Acceptance Scenarios**:

1. **Given** identical scene and camera trajectories, **When** culling is enabled or disabled, **Then** only sprite submission eligibility changes; simulation, sprite ordering, and rendering semantics remain equivalent.
2. **Given** small and large scenes ranging from mostly offscreen to entirely visible, **When** performance is measured, **Then** results include complete rendering time, simulation-step time separately, average and tail latency, submitted sprite counts, and memory/allocation cost.
3. **Given** a run without rendering, **When** simulation advances, **Then** no sprite visibility preparation or culling bookkeeping is performed and physics behavior remains unchanged.

### Edge Cases

- Empty sprite collection; all sprites visible; all sprites outside the view; missing camera; missing renderer; zero-sized render output. Existing clear, presentation, and debug-overlay behavior is preserved where applicable.
- A sprite touching any view edge or corner; a sprite larger than the view; a sprite whose center is outside but whose rotated corners are visible.
- Rotation after unequal horizontal and vertical camera scaling: bounds must describe the actual rendered geometry rather than assume a different transform order.
- Fractional coordinates and numerical rounding near viewport edges. Uncertain exclusions retain the sprite conservatively.
- Zero or negative sprite dimensions, negative scale, non-finite geometry, or missing textures. Culling must not create a new crash or change existing rendering semantics; geometry that cannot be safely classified follows the existing submission behavior.
- Many moving sprites repeatedly crossing view boundaries, independently of camera movement; large camera moves; repeated resize and scene changes.
- Unchanged scenes rendered repeatedly, and scenes where every sprite changes every frame.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Rendering MUST apply linear visibility filtering to the existing sprite sequence: inspect each eligible sprite at most once per visibility pass and preserve the relative order of retained sprites. No visibility-based sorting is required.
- **FR-002**: Culling MUST skip a sprite only when conservative bounds for its actual rendered geometry are wholly outside the current renderable view. Partially visible and boundary-touching sprites MUST be retained. Conservative false positives are acceptable; missed visible sprites are not.
- **FR-003**: Culling MUST use the sprite's rendering geometry, independently of whether it has a collider or where that collider lies. Bounds MUST account for the current position, rendered dimensions, scale, rotation, camera mapping, and output dimensions as used by the existing drawing path.
- **FR-004**: Visibility MUST reflect all relevant scene and view changes by the same rendered frame in which the existing renderer observes those changes, including hierarchical movement, membership, and sprite geometry changes. Any reused derived state MUST satisfy this rule.
- **FR-005**: The existing active-object rules, sprite ordering, transparency, texture/source selection, debug rendering, camera behavior, and no-camera/no-renderer behavior MUST remain compatible. This feature MUST NOT introduce new transform or drawing semantics.
- **FR-006**: Sprites whose bounds cannot be safely classified MUST follow the existing submission behavior rather than be silently culled. Existing supported inputs MUST require no new component, collider, scene-data field, or manual visibility updates.
- **FR-007**: Linear culling MUST be enabled by default. A single explicit opt-out MUST retain submit-all behavior for comparison and troubleshooting; switching modes during a running session is not required.
- **FR-008**: The change MUST remain confined to rendering and directly necessary integration or validation. It MUST NOT alter collider broad phase, collision state, physics scheduling, or gameplay events, and MUST NOT perform culling work during headless simulation.
- **FR-009**: The feature MUST use existing project capabilities without adding a third-party dependency or spatial-index framework. New caches, queues, cross-system notifications, or generalized backend abstractions MUST be omitted unless a concrete correctness or measured performance need justifies their cost and ownership.
- **FR-010**: A visibility pass MUST have O(N) worst-case work for N eligible sprites, with any retained culling state bounded by O(N). The implementation MUST avoid per-sprite temporary allocation and recurring allocation after warmup at a stable population. Reusing unchanged results is optional and must earn any added bookkeeping through evidence.
- **FR-011**: Correctness validation MUST compare enabled and disabled rendering using actual output images and retained submission order, covering all acceptance scenarios and the boundary, transform, and lifecycle edge cases above. Expensive correctness checks MUST remain outside performance measurements.
- **FR-012**: Performance validation MUST establish a baseline on this branch and compare both feature modes under equivalent conditions. Coverage MUST include small and large populations, mostly offscreen and entirely visible scenes, unchanged scenes, continuous sprite motion across view edges, camera motion, insertion/removal, and headless controls. Include one-worker and representative multiworker engine configurations, complete rendering and simulation-step boundaries, independent repetitions, average and tail latency, and allocation/retained-memory observations. Reuse the existing measurement methodology and record source, build, hardware, renderer, and workload provenance with raw results.

### Key Entities *(include if feature involves data)*

- **Renderable sprite**: An existing eligible sprite, its current drawing geometry, and its position in the renderer's established submission sequence.
- **Renderable view**: The portion of the current camera mapping that contributes to the current output; changes with the camera or render output.
- **Conservative sprite bounds**: An enclosing rectangle used to reject clearly invisible sprites, without depending on collider geometry.
- **Reference rendering mode**: Existing submit-all behavior used to establish image equivalence and measure culling's complete cost and benefit.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Across the correctness fixtures, there are zero missing visible sprites, zero duplicate submissions, and zero relative-order differences among retained sprites. Enabled and disabled output images match exactly on the same deterministic rendering backend.
- **SC-002**: Every eligible sprite whose valid conservative bounds are wholly outside the view is omitted from submission. In an all-offscreen fixture with classifiable bounds, zero sprites are submitted; in an all-visible fixture, every eligible sprite is submitted in its existing order.
- **SC-003**: Continuous boundary crossings and every tested camera, geometry, and lifecycle change produce zero frames of stale visibility compared with the disabled reference.
- **SC-004**: Repeated frames at a stable warmed population require zero recurring allocations attributable to culling. Headless runs perform zero visibility work and retain equivalent simulation results.
- **SC-005**: A reproducible comparison covers every workload category in FR-012 and demonstrates a complete-rendering-time reduction beyond observed run-to-run variation for at least one representative mostly-offscreen workload. All-visible, small-scene, headless, and tail-latency costs are reported alongside gains; regressions beyond observed variation are investigated before acceptance. No universal speedup or arbitrary percentage target is assumed.

## Assumptions

- This is a new, focused feature on `branch-without-bvh`. Earlier BVH findings motivate scope and validation but are not performance evidence for this branch.
- The current single main-camera rendering flow and existing sprite sequence remain authoritative. Multiple cameras, occlusion culling, new sprite sorting rules, and rendering-backend changes are outside scope.
- Linear filtering is the chosen algorithm. BVH, segmented broad-phase reuse, collider-backed visibility, and physics refactoring are outside scope.
- KISS takes precedence between comparably performing correct designs. The earlier discussion of dirty bounds and cached visibility describes possible optimizations, not a requirement to recreate the experimental coordinator or cross-system invalidation machinery.
- The existing rendering and math facilities are sufficient; no additional library is needed. Public scene formats and gameplay-facing sprite/camera behavior remain compatible.
- Workload sizes and available rendering backends will be recorded during planning using existing engine scenarios and the machine available for measurement. Renderer-specific findings must be labeled; software-only measurements cannot establish hardware-renderer performance.
