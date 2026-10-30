# Feature Specification: Incremental Render Culling

**Feature Branch**: `branch-without-bvh`

**Created**: 2026-10-29

**Status**: Draft

**Input**: User description: "using what you found on the 001-cpp-engine-cleanup branch improve the current linear culling implementation by adding cache and maintaining bounds plus a spatial index (as you recommended)"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Reuse visibility when the scene and view have not changed (Priority: P1)

As a game developer, I want repeated rendering of an unchanged scene to reuse its known visible sprites, so that the engine spends its time drawing rather than repeating the same visibility work.

**Why this priority**: The current linear implementation checks every eligible sprite on every rendered frame. Reusing valid results delivers the simplest observable reduction in that repeated work.

**Independent Test**: Render a prepared scene repeatedly with a stationary camera and stable output. Compare images and ordered submissions with the existing linear implementation, and observe whether bounds and visibility work repeat after the first frame.

**Acceptance Scenarios**:

1. **Given** an already prepared scene with unchanged sprite geometry, membership, order, camera, and output mapping, **When** another frame renders, **Then** the previous ordered visibility result is reused with zero sprite-bounds refreshes, index maintenance, visibility queries, or full-population visibility/order reconstruction; drawing and presentation still occur normally.
2. **Given** the same sprite changes repeatedly before the next rendering preparation, **When** that preparation occurs, **Then** its final state is reflected without repeating bounds/index refresh work for each intermediate edit, and unrelated sprites are not refreshed.
3. **Given** an empty or all-offscreen prepared scene with no changes, **When** frames repeat, **Then** no sprite is submitted and visibility preparation remains independent of the total offscreen population.

---

### User Story 2 - Find visible sprites through maintained spatial bounds (Priority: P1)

As a game developer, I want the engine to maintain sprite bounds and look up sprites by location, so that moving the camera or a small part of a large scene does not require rebuilding and testing every sprite's geometry.

**Why this priority**: Caching unchanged frames alone does not solve repeated full scans during camera motion or ordinary movement. A maintained spatial index is an explicit part of the requested feature.

**Independent Test**: Pan the camera through a large mostly stationary scene, then move only a known subset of sprites. Compare complete images, submission order, bounds updates, and visibility candidate work against linear references.

**Acceptance Scenarios**:

1. **Given** unchanged sprite geometry and a camera whose position changes while its dimensions and output mapping stay fixed, **When** rendering prepares the new view, **Then** it queries the maintained spatial index without refreshing every sprite bound or rebuilding the full index.
2. **Given** a known subset of changed sprites, including descendants affected by parent movement, **When** the next frame renders, **Then** bounds/index maintenance is limited to affected sprites and required structural work; finding changes does not itself scan all scene objects or sprites.
3. **Given** indexed candidates returned in any traversal order, **When** they are rendered, **Then** conservative visibility refinement and ordering reproduce the current renderer's authoritative submission sequence without scanning the entire sprite population merely to restore candidate order.

---

### User Story 3 - Preserve correct frames across every supported change (Priority: P1)

As a game developer, I want cached and indexed rendering to behave like existing rendering through movement, geometry edits, lifecycle changes, and custom output settings, without manual invalidation or collider authoring.

**Why this priority**: Stale results, incorrect ordering, or references to destroyed objects would invalidate any performance gain.

**Independent Test**: Compare corresponding frames with the existing linear and submit-all paths while exercising continuous motion, every relevant state change, and repeated destruction/recreation under lifetime checks.

**Acceptance Scenarios**:

1. **Given** sprites continuously moving across view edges, **When** positions, parent transforms, dimensions, scale, rotation, texture-dependent eligibility, or other rendering-relevant state changes, **Then** the same frame includes every potentially visible sprite and matches reference output.
2. **Given** camera replacement, camera-size changes, output resize, or a change in supported output mapping, **When** a frame renders, **Then** cached results are invalidated or safely refreshed before use; no old projection-dependent bound is used as if it still described the new drawing geometry.
3. **Given** insertion, activation, removal, component replacement, swap-removal ordering, destruction, scene replacement, or runtime recreation, **When** the next frame renders, **Then** only current eligible sprites are submitted, once each and in the existing order, with no stale identity, retained retired reference, or unbounded historical state.
4. **Given** an unclassifiable sprite or unsupported custom renderer mapping, **When** the existing conservative fallback is needed and later normal rendering resumes, **Then** output stays compatible and any state changed during fallback is current before indexed/cached results are reused.

---

### User Story 4 - Establish the value and cost of caching and indexing (Priority: P2)

As an engine maintainer, I want a fair comparison that separates cache savings from spatial-index savings, so that added bookkeeping earns its place and the result can be justified in the paper.

**Why this priority**: Previous results establish useful linear culling, not that any culling structure is universally fastest. This feature must measure its own complete cost and benefit.

**Independent Test**: Compare the verified current linear implementation, a cached-linear comparison, and the cached/indexed candidate on equivalent static, sparse-change, camera-motion, and fully dynamic workloads, with independently checked output and durable raw evidence.

**Acceptance Scenarios**:

1. **Given** equivalent scenes, inputs, output, and timing boundaries, **When** the three approaches are measured, **Then** results account for change tracking, bounds updates, index maintenance, querying/refinement, ordering, submission, allocation, and retained memory, including any work added to simulation.
2. **Given** tiny through large scenes with all, few, or no visible sprites and different changed-sprite fractions, **When** results are reported, **Then** gains, positive overhead, run-to-run uncertainty, tail latency, and workload/backend limits are retained rather than declaring one approach universally best.
3. **Given** a finished capture set, **When** the machine restarts or the bundle is relocated, **Then** its raw results, correctness/resource proofs, source/build identities, and verification instructions remain retrievable without relying on temporary storage.

### Edge Cases

- First eligible render and cold index/cache construction; empty scenes; all-visible and all-offscreen scenes; sprites larger than the view; dense or skewed distributions with many query candidates.
- Exact edge/corner contact, fractional positions, near-quadrant rotations, elongated sprites, nonuniform scale, and unequal camera/output scale. Bounds must describe the actual order of projection and rotation used for drawing.
- Zero, negative, non-finite, overflowing, or otherwise unclassifiable drawing geometry; missing textures; unsupported mappings. Preserve existing conservative submission and error semantics rather than invent new rendering support.
- Camera translation with stationary sprites; camera dimensions or output aspect changing; many simulation steps or repeated edits between rendered frames; paused simulation that continues rendering; repeated identical state assignments.
- Changes that do not pass through position updates, including sprite dimensions, scale/rotation, visibility eligibility, and membership/order. Visual-content changes that leave geometry and eligibility unchanged must still draw current content even when visibility is reused.
- A changed parent with many descendants; overlapping affected subtrees; updates produced by physics workers; retirement before queued visibility work is consumed.
- Object/component identity reuse, last texture-user destruction, scene clear/reload, camera removal/replacement, and fallback frames followed by restored ordinary mapping.
- Switching between empty and populated scenes, prolonged stable-population churn, and repeated scene/runtime replacement without accumulation of stale bounds, index entries, visibility results, or queued changes.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Rendering MUST maintain reusable conservative sprite bounds and a persistent spatial index of current renderable membership. The delivered feature MUST include indexed visibility selection; caching alone does not complete the requested scope. Index family and representation are planning decisions.
- **FR-002**: The renderer MUST reuse the ordered visibility result when every visibility-relevant input is unchanged. Such frames MUST perform no sprite-bound refresh, index maintenance, new visibility query, full-population visibility scan, or full ordering rebuild. This requirement concerns visibility preparation; normal drawing, frame-level eligibility checks, and presentation remain necessary.
- **FR-003**: Bounds/index updates MUST be driven by relevant changes, reusing the engine's established affected-item synchronization and lifecycle mechanisms where appropriate. The renderer MUST NOT add a full-object/full-sprite scan just to discover changes, nor rebuild the index on every frame. Repeated changes to an affected sprite MUST be coalesced before each rendering preparation batch.
- **FR-004**: Change coverage MUST include synchronized world-position changes and affected descendants, sprite dimensions, scale, rotation, rendering eligibility, camera state, output mapping, and membership/order. A position-only change mechanism is insufficient. Developers MUST NOT need to call a new manual invalidation API or add colliders to make rendering correct.
- **FR-005**: Camera-only translation at unchanged camera dimensions/output mapping MUST query the maintained index while reusing sprite bounds and index membership. Initial preparation, scene replacement, or genuine projection changes may require global work; those cases MUST be explicit and measured rather than disguised as ordinary incremental updates.
- **FR-006**: Bounds, query results, cached visibility, and refinement MUST remain conservative for the actual drawing geometry and current view. Preserve partial and boundary-touching visibility, scaled/rotated geometry, existing numerical safety, and invalid-input fallback. Projection-dependent cached bounds MUST be refreshed when their dependencies change.
- **FR-007**: Changes MUST be visible to culling by the same rendered frame in which the existing renderer observes them. Cache validity MUST cover all relevant inputs and must not suppress current animation, texture content, or other drawing data merely because visibility was reused.
- **FR-008**: Cached and indexed submissions MUST preserve the existing active-object rules, current authoritative relative order, transparent compositing, and duplicate-free drawing. Restoring candidate order MUST NOT require an unconditional full-population scan. Traversal order from an index is not a replacement for drawing order.
- **FR-009**: Cache/index entries and pending work MUST have explicit ownership and safe lifetime/identity handling through removal, replacement, retirement, scene transitions, and runtime recreation. Rendering MUST NOT consume stale objects or retain historical scene membership indefinitely. Synchronization MUST respect existing worker completion and owning-thread rendering boundaries.
- **FR-010**: Render indexing MUST have separate membership/state from collider broad phase and gameplay collision pairs. The feature MUST NOT insert the camera as a gameplay collider or change collision eligibility, events, solver behavior, or physics scheduling. Reusing suitable primitives or change-publication points is permitted within this rendering scope.
- **FR-011**: Preserve the existing render-culling opt-out, public scene formats, direct camera drawing semantics, no-camera/no-renderer behavior, clear/present, and debug overlays. Unsupported custom mappings and unclassifiable sprites MUST retain compatible conservative fallback, with valid visibility state restored before later reuse.
- **FR-012**: Headless simulation and explicitly disabled culling MUST incur no added render-cache/index initialization, bounds maintenance, visibility queries, or change-notification bookkeeping. Work already required by the engine's independent transform/lifecycle systems remains unaffected.
- **FR-013**: Added state and bookkeeping MUST be limited to current renderable data plus explicitly bounded reusable capacity. At a stable warmed population/capacity, cache/index visibility preparation MUST have no recurring temporary allocation. Repeated stable-capacity churn and scene transitions MUST NOT cause continuing retained-state growth. Cold construction, growth, and reclamation costs MUST be measured separately.
- **FR-014**: The implementation MUST follow KISS: reuse existing math, change-tracking, synchronization, and lifetime capabilities; give each additional cache/index/change record a concrete owner, invalidation rule, and measured purpose. Avoid unrelated rewrites, generalized backend frameworks, or duplicate scene-wide tracking. Dependencies and algorithm choices require justification during planning, not an assumption that a particular index is fastest.
- **FR-015**: Preserve the current uncached linear implementation as a comparable reference and provide a cached-linear comparison that isolates caching benefit from indexed-selection benefit. These comparison paths may remain test/benchmark facilities rather than expand the public runtime interface. Preserve submit-all rendering for correctness comparison through the existing opt-out.
- **FR-016**: Correctness validation MUST use actual corresponding-frame images, ordered live sprite identities, and an independent visibility oracle. Cover all acceptance scenarios and edge cases, including multi-frame continuous crossings, camera/output changes after an earlier rendered frame, worker-produced movement, lifecycle reuse, fallback/resumption, and unchanged-result reuse. Work observations MUST demonstrate the absence of prohibited full scans/rebuilds and unnecessary updates; instrumentation MUST be separate from timing.
- **FR-017**: Performance validation MUST compare equivalent current-linear, cached-linear, and cached/indexed workloads, including initialization and complete recurring costs. Cover unchanged scenes; small, moderate, and fully changed sprite fractions; camera-only motion; all-visible/offscreen/sparse visibility; uniform/skewed layouts; continuous boundary crossings; churn; output changes; representative engine scenes; and headless/disabled controls. Include tiny populations, the existing 1,000/10,000-sprite scales, at least one larger population selected during planning, and representative one-/multiworker configurations. Report complete rendering and independent simulation-step time, average/tail latency, allocation/retained-memory cost, independent-run variation, and any renderer/platform limitations. Bounds updates, query candidates, ordering, and cache/index maintenance costs MUST remain attributable rather than hidden outside the measured comparison.
- **FR-018**: Acceptance evidence MUST retain matching correctness/resource proofs, raw independent timing samples, commands, source/build/harness/binary identities, and verification checksums in a persistent retrievable location outside tracked bulk source. The evidence MUST remain verifiable after reboot/relocation and MUST NOT rely solely on temporary directories. Preserve earlier baselines and unfavorable results; investigate regressions with appropriate process-level uncertainty before selecting the normal enabled rendering path or declaring the feature complete.

### Key Entities *(include if feature involves data)*

- **Renderable identity**: A current eligible sprite and its place in authoritative drawing order, with a lifetime distinct from a reused object/component address.
- **Maintained sprite bounds**: Conservative enclosing geometry reused until one of its declared sprite/projection dependencies changes; independent of gameplay collider membership.
- **Spatial render index**: Location-based lookup of maintained sprite bounds used to obtain visibility candidates for the current view; updated as eligible membership or bounds change.
- **Cached ordered visibility**: The reusable submission sequence and the scene/view/order conditions under which it is valid. It stores visibility decisions without freezing current drawing content.
- **Relevant change**: A geometry, eligibility, membership/order, or view change that affects bounds/index/cache validity; repeated edits can collapse into the final state before rendering.
- **Rendering view state**: Current camera position/dimensions and output mapping, including eligibility for the established conservative fallback behavior.
- **Comparison evidence bundle**: Matching workloads, current/reference implementations, correctness/resource observations, independent timing runs, and durable source/build/data provenance.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Across correctness fixtures, corresponding frames match reference images on the same deterministic renderer, with zero missing potentially visible sprites, duplicate submissions, or relative-order inversions, including all tested lifecycle and fallback transitions.
- **SC-002**: After initial preparation, a frame with no relevant changes records zero bounds refreshes, index updates, visibility queries, full-population visibility visits, and ordering rebuilds, while still submitting the expected visible sprites normally.
- **SC-003**: A controlled set of affected sprites refreshes only those bounds once per preparation batch; unaffected sprite bounds/index membership receive zero refreshes. Pure camera translation records zero sprite-bounds refreshes and zero full-index rebuilds. Candidate selection/order recovery records no mandatory full-population pass; output-heavy and worst-case query costs remain separately visible.
- **SC-004**: Every tested sprite, view, ordering, or lifecycle change is reflected in the same corresponding reference frame, with zero frames of stale visibility and zero stale-reference/lifetime failures through repeated scene/runtime replacement.
- **SC-005**: Stable warmed visibility preparation adds zero recurring temporary allocations, headless/disabled controls record zero added render-cache/index work, and stable-capacity churn/repeated scene transitions show no continuing growth in culling-owned retained state.
- **SC-006**: Reproducible independent measurements demonstrate a complete rendering-cost improvement beyond observed variation against current uncached linear culling in representative unchanged/mostly-static workloads, and a further indexed-selection benefit against cached-linear culling in at least one representative large sparse-change or camera-panning workload. Include any added simulation-side maintenance when judging that benefit. Report all-visible, fully dynamic, small-scene, tail-latency, and initialization costs alongside gains; resolve or explicitly justify regressions before acceptance, without arbitrary percentage targets or universal speedup claims.
- **SC-007**: Every accepted timing case is linked to matching independently checked output/resource evidence and current source/build identities. A fresh verification of the retained bundle succeeds after relocation without requiring files from the original temporary workspace.

## Focused-first measurement execution

The user's requested next measurement is a bounded, explicitly exploratory pilot selected from mostly-offscreen static, camera motion, a small moving subset, fully moving crossing, and all-visible cases, with selected worker controls where useful. It may omit full phase roots, frozen-reference controls, and per-frame identity traces, but it must retain strict workload/build/backend/resource/sample matching and complete raw timing/resource observations. The pilot is a screening step only; it does not change FR-016/017 correctness or measurement scope, reduce the acceptance repetition/case requirements, or close SC-006/007. The original full matrix remains the acceptance inventory.

## Assumptions

- This is a new feature in `specs/009-incremental-render-culling/`, building on completed feature `008`. The earlier feature's uncached linear behavior and persistent T044 evidence remain the reference; its historical specification is not rewritten to require caches retroactively.
- The findings from `001-cpp-engine-cleanup` provide reusable affected-item/coalescing and lifecycle foundations. That feature's headless measurements provide no direct comparison of rendering strategies. Existing position-change tracking does not cover all rendering changes by itself.
- The current single-main-camera flow, translation-only parenting rules, camera world-view versus output-pixel distinction, and current draw order remain authoritative. New camera models, multiple simultaneous views, occlusion, altered transform inheritance, and rendering-backend replacements are outside scope.
- Both caching and a maintained spatial index are required. Selection among suitable existing primitives, BVH, segmented structures, or another justified index belongs to planning; building several production index backends or adopting an external library is not implicitly required.
- Reuse existing change information at the correct publication boundary, accounting for queues being consumed during ordinary simulation. The new feature does not assume a drained position queue can later reveal every visibility change.
- Work expectations distinguish N eligible sprites, affected sprites (including descendants), and actual candidates/visible output. An index may visit many entries in dense or degenerate cases; no universal sublinear-query guarantee is assumed. Global work on initialization, genuine projection changes, and scene replacement is permitted when explicitly attributed.
- The benchmark comparison is required to establish whether added index complexity improves on caching alone. If that benefit cannot be established, record the limitation and revisit the design; do not silently reduce the requested feature to caching or claim an indexed win from a query-only measurement.
- Bulk evidence must remain persistent and Git-ignored or otherwise durably retrievable, following the lesson from the lost temporary captures. Existing measurement tools and fixtures should be extended only as needed for this feature's specific comparisons.
