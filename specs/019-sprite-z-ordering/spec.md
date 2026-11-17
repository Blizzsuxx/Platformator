# Feature Specification: Sprite Z Ordering and Render Culling Cleanup

**Feature Branch**: `branch-without-bvh`

**Created**: 2026-11-17

**Status**: Draft

**Input**: User description: "add z ordering for sprites. Perfomance is very important"

The expanded scope brings render culling policy, ownership and validation into this feature alongside Z ordering.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Control sprite overlap predictably (Priority: P1)

As a game developer, I want to assign a Z Order to each sprite and change it during gameplay, so that backgrounds, characters, and foreground objects overlap as intended without changing their positions or creation order.

**Why this priority**: Explicit, predictable visual stacking is the feature's core behavior. Incorrect transparency or inconsistent ties would make it unsuitable for game scenes.

**Independent Test**: Render overlapping sprites with distinct and equal Z Order values, including partially transparent content. Change their values while the scene runs and compare each frame with an independently specified expected drawing sequence and composited result.

**Acceptance Scenarios**:

1. **Given** overlapping sprites with negative, zero, and positive Z Order values, **When** a frame renders, **Then** lower values draw first and higher values draw later, including when texture or creation order differs from Z Order.
2. **Given** a live sprite, **When** its Z Order changes before rendering preparation, **Then** the next prepared frame uses the new order; multiple changes before that preparation use the final value.
3. **Given** sprites with equal Z Order, **When** repeated frames render or a sprite changes Z Order and later returns to that value, **Then** ties follow their original registration order for that live registration lifetime.
4. **Given** identical scene content rendered through supported culling modes or conservative fallback, **When** sprites overlap, **Then** their visible stacking and transparency agree across those paths. Directly requested individual camera draws continue to follow the caller's submission sequence.

---

### User Story 2 - Author and retain Z Order (Priority: P1)

As a scene author, I want to edit Z Order alongside other sprite properties and retain it in scenes, templates, and copies, so that authored layering works consistently in preview and gameplay.

**Why this priority**: A runtime-only value would leave normal authoring and reusable assets incomplete.

**Independent Test**: Edit a sprite's Z Order in the editor, undo and redo the edit, save and reopen its scene or template, and run or instantiate the result. Also load an existing asset that has no ordering property.

**Acceptance Scenarios**:

1. **Given** a selected sprite, **When** its Z Order is edited, undone, or redone, **Then** the property and the next preview prepared from the committed document agree with the existing editor update workflow; saving and reopening retain the committed value.
2. **Given** a sprite in a template or a template instance, **When** its ordering value is saved, instantiated, overridden, or reverted using the existing workflows, **Then** its Z Order follows the same persistence and override rules as other sprite properties.
3. **Given** an older scene or template with no Z Order, **When** it loads, **Then** every such sprite defaults to zero, initial stacking follows the existing registration sequence, and no manual migration is needed.
4. **Given** a sprite that is cloned or duplicated, **When** the copy is registered, **Then** it retains the source's Z Order and takes its own new place in equal-value registration order.
5. **Given** a sprite ordering field, **When** the minimum and maximum supported integers are authored and reloaded, **Then** they are retained exactly; invalid types and values outside the range are rejected through existing validation, and rejected editor edits leave the last committed value intact.

---

### User Story 3 - Keep layering stable through scene changes (Priority: P1)

As a game developer, I want sprite stacking to remain reliable while objects move, appear, disappear, or are replaced, so that lifecycle operations never introduce flicker, duplicate drawing, or stale objects.

**Why this priority**: Ordering must remain correct under ordinary gameplay and the engine's existing visibility and lifetime behavior.

**Independent Test**: Exercise visibility crossings, activation, parent changes, sprite replacement, removal of unrelated sprites, and scene replacement. Check the expected order and current live identities after each operation, including while paused or after rendering fallback.

**Acceptance Scenarios**:

1. **Given** equal-value surviving sprites, **When** an unrelated sprite is removed or internal storage is compacted, **Then** the survivors retain their relative stacking.
2. **Given** registered sprites that leave the view, become inactive, or lose their texture, **When** they become drawable again, **Then** current Z Order applies and equal-value registration precedence is retained if the registration itself survived.
3. **Given** a sprite removed, destroyed, or replaced before the next prepared frame, **When** rendering resumes, **Then** it is absent, every eligible replacement draws once in its own registration position, and no retired sprite is accessed.
4. **Given** a scene clear, scene replacement, missing camera, temporary fallback, or paused rendering, **When** ordinary drawing next occurs, **Then** only current eligible sprites appear in the current order; rendering does not require a simulation step to observe an already committed Z Order edit.

---

### User Story 4 - Preserve rendering performance at scale (Priority: P1)

As an engine user, I want explicit ordering to avoid repeated unnecessary work, so that large scenes retain the benefits of existing visibility selection and unchanged scenes remain inexpensive to render.

**Why this priority**: The user explicitly identifies performance as very important; correct output alone is insufficient for acceptance.

**Independent Test**: Check affected work and allocation behavior separately from timing. Use a focused comparison of unchanged scenes, sparse visibility during camera motion, and changing Z Order, distinguishing total population, retained drawing candidates, and changed sprites.

**Acceptance Scenarios**:

1. **Given** an already prepared frame with unchanged eligible membership and ordering values, **When** another frame renders, **Then** ordering is reused with zero order reconstruction or sprite-order comparisons, even if sprites move within view or animate without changing membership or order.
2. **Given** indexed culling retains a small drawing population, **When** the camera changes the retained population or a visible sprite's Z Order changes, **Then** restoring drawing order does not require visiting every registered sprite merely to order the retained candidates.
3. **Given** a warmed scene whose storage capacity is sufficient, **When** frames repeat or values change repeatedly before preparation, **Then** ordering introduces zero recurring temporary allocations and intermediate assignments do not cause repeated preparation of the same rendered frame.
4. **Given** comparable builds and workloads, **When** a bounded performance comparison is reported, **Then** it includes total rendering cost, any added update cost, independent timing samples, and unfavorable results; performance claims are limited to the measured cases.

### User Story 5 - Choose culling for the workload (Priority: P1)

An ordinary game launch should reuse its prepared visibility without paying for a spatial grid. A large scrolling game can explicitly select indexed culling using the existing startup control. Both should show the same overlapping sprites in the same order.

**Independent test.** Start fresh windows with an absent selector, an empty selector, each supported explicit selection, and culling disabled. Inspect the effective policy and grid ownership. Render a camera pan and compare visible compositing across modes, including an object whose sprite extends past its collider and a sprite with no collider.

1. With culling enabled and the selector absent or empty, a live window uses cached-linear. A warmed fixed view with no geometry changes reuses its retained candidates.
2. Explicit indexed and current-linear selections remain available at window creation. The same scene, including Z edits, renders correctly under either selection. Sprite count, motion and Z edits do not switch the window policy.
3. A valid explicit selection respects the public culling opt-out. Headless runs allocate zero renderer-owned selection or ordering storage. After a temporary unsupported mapping, the next eligible frame resumes the selected policy with current inputs.

### Edge Cases

- Empty scenes, a single sprite, all sprites sharing one value, all-visible scenes, and all-offscreen scenes.
- Minimum and maximum accepted values, negative values, repeated identical assignments, multiple edits before one frame, and invalid or out-of-range authoring data.
- Equal-value sprites with different textures, animation states, transparent edges, and partial opacity; ordering must not be changed merely to group textures.
- Deleting a sprite from the middle of internal storage; removing the most recently registered sprite; changing a sprite away from and back to a tied value; unregistering and registering it again.
- Offscreen edits followed by camera movement; activation changes; loss and restoration of a texture; source-region edits; movement inherited from a parent.
- Template instances with several equal-value sprites, repeated instantiation, duplicated hierarchies, and scene/template save and reload.
- Rendering with culling disabled or a custom output mapping, returning from fallback, paused editing, and no camera or renderer.
- Large numbers of sprites changing Z Order together; stable-population churn; scene/runtime recreation; retired identities reused for new objects.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Each sprite MUST expose a signed integer Z Order with a default of zero and an accepted range of -2,147,483,648 through 2,147,483,647. The value MUST be editable during authoring and gameplay without changing spatial position, transform hierarchy, or collision behavior.
- **FR-002**: Scene rendering MUST draw lower Z Order values before higher values. For equal values, earlier registered sprites MUST draw before later registered sprites. This precedence MUST remain stable throughout each live registration lifetime, independently of internal storage moves, culling traversal, textures, activation, and temporary invisibility. Actual unregistration followed by registration establishes new precedence.
- **FR-003**: Changes committed before a frame's rendering preparation MUST be reflected in that frame. Multiple edits before preparation MUST use the final value without processing each intermediate ordering separately. Assigning an unchanged value MUST introduce no ordering invalidation or preparation work.
- **FR-004**: The same relative order and alpha-composited appearance MUST apply across all existing scene-rendering modes, including culling opt-out, comparison modes, and conservative fallback. Batching or texture grouping MUST NOT invert required ordering. Existing explicit individual camera drawing remains caller-ordered.
- **FR-005**: The editor MUST display an editable Z Order property for sprites through its existing inspection and editing workflows, including undo/redo, multi-selection where comparable sprite properties support it, and established template override/revert behavior. Preview and gameplay MUST consume the committed value through their established update workflows.
- **FR-006**: Scene and template persistence, runtime template instantiation, sprite cloning, and editor duplication MUST retain the authored Z Order. Assets omitting the value MUST load as zero without manual migration. Invalid types and out-of-range values MUST use established validation/error handling without silent truncation or wraparound.
- **FR-007**: Loading a scene or instantiating a template MUST establish ties in deterministic serialized object/component traversal order. Repeated loads of the same asset MUST reproduce the same tie order within that asset, and repeated instances MUST preserve their internal tie order. New copies and instances receive fresh registration precedence; runtime registration history is not additional authored data.
- **FR-008**: Ordering MUST preserve existing drawing eligibility, conservative visibility, geometry, animation, source-region behavior, clear/present behavior, and debug-overlay placement. Z Order changes MUST NOT refresh spatial bounds, migrate spatial membership, or trigger visibility queries solely because ordering changed. Existing visibility work caused by independent changes remains necessary.
- **FR-009**: Previously prepared ordering MUST be reused when eligible membership, registration precedence, and Z Order values are unchanged. Sprite animation, movement, or camera/output changes that leave those ordering inputs unchanged MUST NOT by themselves cause order reconstruction. Constant-cost frame-level validity checks remain allowed. Drawing still occurs normally and uses current visual content.
- **FR-010**: With indexed culling active, ordering work MUST be based on retained drawing candidates and relevant ordering/membership changes. It MUST NOT add a full-scene scan to discover Z Order edits or restore the order of a sparse candidate result. Existing conservative eligibility rules may retain uncertain candidates; culling-disabled/fallback paths may have the whole population to draw. Cold preparation and scene replacement costs MUST be identified separately.
- **FR-011**: After warmup at sufficient capacity, ordering preparation MUST introduce zero recurring temporary allocations. Additional retained state MUST be bounded by live sprite population and reusable capacity; stable-capacity churn MUST NOT accumulate retired identities or historical edits. Successful scene clear/replacement and runtime teardown MUST retire prior-scene ordering state according to existing ownership boundaries.
- **FR-012**: Removal, replacement, activation, scene changes, and storage relocation MUST keep ordering references current, without duplicate submissions or access to retired objects. Worker completion and existing mutation/render preparation boundaries MUST be respected, and final order MUST be independent of worker completion order.
- **FR-013**: Headless or renderer-free execution MUST add no ordering preparation, renderer-owned ordering storage, or ordering-change tracking. Storing the authored sprite property and existing serialization/lifecycle work remain allowed. A live renderer with culling disabled MUST still honor Z Order.
- **FR-014**: Correctness validation MUST cover all user scenarios and listed boundaries using independently specified expected ordering and focused overlapping-sprite compositing checks. Existing targeted culling and lifecycle checks MUST remain applicable. Instrumented correctness observations MUST be separated from performance timing.
- **FR-015**: Performance validation MUST compare equivalent pre-change and candidate builds for existing all-zero scenes, and compare the candidate against a straightforward correct reference for explicit Z Order workloads. The changed tie behavior after removal MUST be accounted for when determining which outputs are comparable. Measurements MUST include initialization, ordering, total rendering, any added update-side maintenance, allocation/retained memory, and typical/tail latency, without moving work outside measured boundaries.
- **FR-016**: The default performance exercise MUST be focused and bounded under the repository's benchmark artifact policy. Select a small set covering unchanged reuse, sparse visibility/camera motion, and changing order, informed by the existing 1,000- and 10,000-sprite workload scales. Tiny/empty cases, churn, fallback, and large change fractions can use targeted correctness/work observations rather than a full timing matrix. Preserve commands, workload/build identities, raw timing samples, a compact report, and uncertainties. Stop on budget or failure and diagnose the partial result; larger campaigns require an explicit user request.
- **FR-017**: Performance claims and acceptance decisions MUST be supported by equivalent workload measurements and affected-work/resource observations. Unchanged-scene regressions beyond observed run variation MUST be investigated and resolved or explicitly justified with evidence before implementation acceptance. Absolute latency, percentage-speedup targets, and algorithm thresholds MUST NOT be invented without a project or measured basis.
- **FR-018**: Ordinary enabled runs MUST default to cached-linear. Explicit indexed and current-linear startup selections and the existing opt-out MUST retain their meaning. Scene and template files require no migration or strategy setting.
- **FR-019**: The ordinary cached path MUST retain zero grid-specific storage and do zero grid maintenance. All enabled policies MUST use conservative sprite geometry independently of physics colliders, collision masks and simulation steps. The indexed path MUST keep sparse camera-query support.
- **FR-020**: Policy and implementation overhead MUST be distinguished using matched same-build and same-policy comparisons. Dense single-visible-edit cost MUST be assessed at the 10,000-retained scale.

### Key Entities *(include if feature involves data)*

- **Sprite Z Order**: Authored signed integer controlling a sprite's precedence when it overlaps other sprites; defaults to zero and survives supported persistence and copying workflows.
- **Registration Precedence**: A sprite's stable place among equal-value live registrations; unaffected by storage location or visibility and renewed upon actual re-registration.
- **Prepared Drawing Order**: The current eligible sprites in their required submission sequence, valid while its membership and ordering inputs remain unchanged.
- **Ordering Change**: A committed change to Z Order or live registration/membership that can affect a future prepared drawing sequence; repeated assignments before rendering contribute only their final effect.
- **Focused Performance Evidence**: Comparable workloads and builds, independent raw timing samples, resource/work observations, commands, and a compact interpretation of measured costs and limitations.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every ordering fixture produces exactly the expected sequence, with zero Z Order inversions, tie-order changes among surviving registrations, duplicate submissions, or stale-object accesses. Focused transparency fixtures match independently defined expected compositing on the same deterministic renderer.
- **SC-002**: Runtime changes have zero prepared frames of stale order. Editor edits, undo/redo, scene/template reload, overrides/reverts, cloning, and instantiation preserve the expected value in every corresponding fixture; assets without the property load at zero in every legacy fixture.
- **SC-003**: After initial preparation, unchanged ordering inputs produce zero sprite-order comparisons, order reconstructions, and added temporary ordering allocations per frame. Identical assignments and animation/movement that preserve ordering inputs do not invalidate this result.
- **SC-004**: In a controlled indexed-culling case, increasing the offscreen population from the existing 1,000-sprite scale to 10,000 while holding retained drawing candidates and relevant changes fixed adds zero full-population ordering visits. Ordering-only edits cause zero bounds refreshes, spatial migrations, or visibility queries.
- **SC-005**: Headless controls record zero renderer-owned ordering storage or maintenance. Stable-capacity churn records no continuing growth in retained ordering state, and every tested scene/runtime retirement leaves zero references to prior-scene sprites.
- **SC-006**: A focused report contains comparable raw samples for unchanged, sparse-visibility/camera-motion, and ordering-change cases, identifies complete recurring and cold costs, and explains every observed regression beyond run variation. Existing all-zero workload overhead and explicit-order workload costs are reported separately; no unmeasured speedup is claimed.
- **SC-007**: Default measurement stays within 50 MiB of retained output and 600 seconds, using the rendering driver's default 3 repetitions, 5 warmup frames, and 30 measured frames. A budget/failure stop produces a compact partial report with the limitation recorded rather than an automatic retry or expanded campaign.
- **SC-008**: Default, explicit, opt-out, headless and fallback-restoration fixtures resolve the expected policy and produce the expected draw order/compositing. The default has zero grid-specific retained bytes, queries and relinks. Explicit indexed mode exercises a selective grid query during the sparse pan.
- **SC-009**: The compact report separates old indexed vs new indexed from new indexed vs new cached-linear on matched all-zero inputs. For 10,000 retained sprites it distinguishes clean frames from one real Z edit and accounts for the full update and rendering cost of the rebuild and single-entry repair alternatives, subject to the existing partial-result rule.

## Assumptions

- This feature defines one explicit per-sprite Z Order. Named sorting layers, automatic Y-based sorting, inherited parent/group ordering, per-camera ordering, depth-buffer rendering, new render backends, and occlusion systems are outside scope.
- Existing scene rendering registers sprites in a deterministic sequence. For all-zero assets, initial stacking remains compatible. After removal, preserving surviving tie order deliberately supersedes historical stacking changes caused by storage compaction; earlier specifications requiring storage-rank drawing order do not override this feature's explicit order.
- Equal-value registration precedence is a runtime rule. Asset reload establishes it from serialized traversal; saving gameplay-only registration history or reordering the editor hierarchy to author depth is outside scope. Authors needing a particular persistent precedence between overlapping sprites should assign distinct Z Order values.
- Z Order is independent of transform position and parent Z Order. Copies retain the authored value but establish their own registration lifetime. Existing deferred mutation and document-preview workflows remain the synchronization contract.
- Existing editor sprite inspection, scene/template persistence, reference/lifetime handling, and culling facilities are dependencies. Their current source behavior must be rechecked during planning; historical benchmark observations are not newly verified measurements.
- The constitutional requirements to assess alternatives, parallel execution, ownership, scaling, and evidence apply during planning. Data layout, algorithms, change tracking, and sequential/parallel strategy remain planning decisions.
- The repository benchmark artifact policy takes precedence over historical full-campaign retention requirements. Future rendering comparisons use `engine/benchmarks/capture_linear_render.py` with its default guards. Builds stay under `build/` or `bin/`, outside report directories. Retained evidence excludes per-frame identity lists, full-frame images, source archives, binaries, shared libraries, and build trees by default; targeted diagnostics require a specific failure and a small explicit bound.
- No implementation or benchmark has been performed as part of this specification. Quantitative runtime conclusions remain unverified until the focused comparison is executed.
