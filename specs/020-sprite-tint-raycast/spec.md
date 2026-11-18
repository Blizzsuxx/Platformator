# Feature Specification: Sprite Color Tinting, Clear Color Override, and 2D Raycasting

**Feature Branch**: `branch-without-bvh`

**Created**: 2026-11-18

**Status**: Draft

**Input**: User description: "add the small engine capabilities, as we just now discussed (and as you suggested)"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Tint and fade individual sprites independently (Priority: P1)

As a game developer, I want to assign an RGBA color tint and opacity to each individual sprite and change it during gameplay or authoring, so that sprites sharing the same texture asset can display independent distance shading, lighting levels, damage flashes, or partial transparency without affecting other sprites or requiring duplicate texture assets.

**Why this priority**: Per-sprite shading and opacity is essential for 2.5D depth cueing, lighting, hit feedback, and translucent effects while preserving shared texture caching.

**Independent Test**: Render multiple sprites referencing the same texture with different RGB tints and alpha values in the same frame, plus legacy assets omitting color data, and verify that each sprite displays its own modulated color and opacity without bleeding state onto subsequent draws.

**Acceptance Scenarios**:

1. **Given** multiple visible sprites sharing one texture asset with different RGB tint values (including full white default, darkened shade, and single-channel tint), **When** a frame renders, **Then** each sprite appears tinted by its own RGB values without altering any other sprite sharing that texture.
2. **Given** a sprite with reduced opacity (alpha between 0 and 255), **When** it renders over a background or another sprite, **Then** it blends according to its alpha value while respecting established Z Order stacking.
3. **Given** a scene or template saved with custom sprite color and opacity values, or an older scene/template that omits color fields, **When** loaded or instantiated, **Then** authored values are preserved exactly and omitted values default to opaque white `(255, 255, 255, 255)`.
4. **Given** a sprite whose color or opacity is edited in the scene editor (including undo/redo, copy/paste, template override/revert, and duplication), **When** previewed or saved, **Then** the updated color and opacity are reflected consistently.

---

### User Story 2 - Query 2D line-of-sight and hitscan rays against colliders (Priority: P1)

As a game developer, I want to cast a 2D ray from an origin along a direction up to a maximum distance with collision layer mask and trigger filtering, so that hitscan weapons, enemy line-of-sight checks, and raycast rendering can find the closest or all intersecting colliders, their intersection points, distances, and surface normals.

**Why this priority**: Direct spatial ray queries eliminate manual full-scene object scans for hitscan combat, AI visibility, and raycast projection.

**Independent Test**: Cast rays against scenes containing rotated and axis-aligned box colliders, circle colliders, inactive objects, trigger colliders, and masked collision layers, verifying closest-hit and multi-hit results against analytical geometry.

**Acceptance Scenarios**:

1. **Given** active box (axis-aligned and rotated) and circle colliders along a ray's path, **When** a closest-hit raycast is performed within the maximum distance, **Then** it returns the nearest valid collider along with the world-space hit point, outward surface normal, distance, and normalized hit fraction.
2. **Given** colliders on different collision groups and trigger states, **When** a raycast specifies a collision mask and whether triggers are included, **Then** non-matching groups, inactive objects, and excluded triggers are ignored.
3. **Given** a ray that originates inside a collider or misses all eligible colliders within the maximum distance, **When** queried, **Then** it handles the boundary deterministically according to the documented contract without false hits beyond the maximum distance.
4. **Given** multiple eligible colliders along a ray, **When** an all-hits raycast is performed, **Then** all intersecting eligible colliders within the maximum distance are returned ordered by increasing distance from the origin.

---

### User Story 3 - Override the frame background clear color (Priority: P2)

As a game developer, I want to configure the initial window background clear color and change it dynamically at runtime, so that scenes have an appropriate backdrop color (such as a dark ceiling/void or sky tone) and can trigger full-screen background color transitions without requiring oversized background sprites.

**Why this priority**: Eliminates the hardcoded white background limitation while keeping existing default behavior unchanged.

**Independent Test**: Start a session with default settings (verifying opaque white clear color), start with a configured custom clear color, and change the clear color at runtime between frames, verifying the rendered backdrop pixels on each frame.

**Acceptance Scenarios**:

1. **Given** default window settings with no custom clear color specified, **When** a frame renders, **Then** the background clears to opaque white `(255, 255, 255, 255)`, preserving backward compatibility with existing scenes and visual tests.
2. **Given** startup window settings or a runtime call specifying a custom clear color, **When** the next frame renders (with or without a main camera), **Then** the render target clears to the specified RGBA color before any sprites are drawn.

---

### Edge Cases

- Sprites with `alpha = 0` (fully transparent) vs `alpha = 255` (fully opaque) and `color = (0, 0, 0, 255)` (solid black silhouette).
- Multiple sprites sharing one texture where the first sprite is tinted or translucent and the second sprite uses default `(255, 255, 255, 255)` — the second sprite must not inherit stale modulation from the first.
- Invalid or out-of-range color channel values in persisted scene or template data (`< 0`, `> 255`, non-integer, or malformed color entry) must be rejected cleanly without corrupting the sprite, while omitted color fields load as `(255, 255, 255, 255)`.
- Changing sprite color or clear color in headless execution mode: stores the property cleanly without requiring a video renderer or allocating graphics resources.
- Changing sprite color or clear color on a stationary camera view with render culling enabled: must not invalidate spatial visibility bounds or force unnecessary culling bounds refreshes.
- Raycast with zero-length or non-finite direction vector, negative or non-finite maximum distance, non-finite origin, or empty scene.
- Raycast against rotated box colliders, scaled game objects, colliders with non-zero local offsets, and colliders modified earlier in the same frame before a physics step.
- Raycast starting exactly on a collider boundary or inside a collider volume.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Each sprite MUST support an 8-bit per-channel RGBA color modulation `(r, g, b, a)` with channel range `0..255`, defaulting to opaque white `(255, 255, 255, 255)`, readable and mutable at runtime and copied when a sprite is cloned.
- **FR-002**: During rendering across all active paths (including cached visibility culling, culling opt-out, conservative fallback, and direct single-camera draws), each sprite MUST be drawn with its own RGBA color and opacity modulation without leaking tint or alpha state onto any other sprite sharing the same texture.
- **FR-003**: Scene and template persistence MUST save and load each sprite's RGBA color and opacity; legacy assets omitting color data MUST load as `(255, 255, 255, 255)`, invalid or out-of-range values MUST be rejected through established validation without silent clamping or corruption, and the scene editor MUST support inspecting, editing, undoing/redoing, copying/pasting/resetting, duplicating, and overriding/reverting sprite color and opacity.
- **FR-004**: Color-only or opacity-only edits on a live sprite MUST NOT invalidate spatial culling bounds, force visibility re-scans, or rebuild sprite Z drawing order.
- **FR-005**: Startup window configuration MUST allow specifying an initial RGBA clear color (defaulting to opaque white `(255, 255, 255, 255)`), and the active runtime session MUST provide read and write access to update the clear color dynamically between frames.
- **FR-006**: Every rendered frame MUST clear the backdrop using the configured clear color before drawing scene sprites or when no main camera is present, while headless sessions MUST store the configured clear color safely as a graphics no-op.
- **FR-007**: The runtime MUST expose 2D raycast query operations supporting both closest-hit and all-hits queries given a 2D origin, 2D direction, maximum distance, optional collision layer mask (defaulting to all layers), and optional trigger-inclusion flag.
- **FR-008**: Raycast intersection tests MUST accurately handle both circle colliders and box colliders — accounting for world position, local collider offset, object scale, and object rotation — using current synchronized or immediate world transforms.
- **FR-009**: Each raycast hit result MUST report the hit collider, owning object, world-space intersection point, outward unit surface normal, distance along the ray, and normalized hit fraction in `[0, 1]`; all-hits results MUST be ordered by increasing distance with deterministic tie-breaking when two colliders intersect at identical distances.
- **FR-010**: Raycast queries MUST validate inputs cleanly so zero-length or non-finite directions, non-finite origins, and negative or non-finite maximum distances produce deterministic errors or empty results without undefined behavior.
- **FR-011**: Raycast execution MUST leverage spatial partitioning or bounded traversal to avoid scanning unrelated distant colliders where applicable, skip inactive objects and non-matching masks/triggers, and perform zero recurring heap allocations during closest-hit queries.
- **FR-012**: All existing scenes, templates, editor workflows, headless contracts, and regression suites MUST remain 100% backward compatible with no manual asset migration required.

### Key Entities *(include if feature involves data)*

- **Sprite Color Modulation**: Per-sprite RGBA tint and opacity quadruple `(r, g, b, a)` in `0..255` (default `(255, 255, 255, 255)`) applied independently per draw and persisted across scenes, templates, and copies.
- **Frame Clear Color**: Window-level RGBA backdrop color (default `(255, 255, 255, 255)`) configured at startup or updated dynamically at runtime and used to clear each rendered frame.
- **Raycast Query**: A 2D directional segment query defined by origin, direction, maximum distance, collision mask filter, and trigger inclusion flag.
- **Raycast Hit Result**: A structured intersection record containing the hit collider, owning object, world-space hit point, outward surface normal, hit distance, and normalized fraction `[0, 1]`.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Overlapping and adjacent sprites sharing a single texture render with 100% independent per-sprite RGB tinting and alpha blending, with zero cross-sprite modulation leakage across consecutive draws and all rendering paths.
- **SC-002**: 100% of existing scenes and templates omitting sprite color data load cleanly at opaque white `(255, 255, 255, 255)` with white `(255, 255, 255, 255)` default frame clearing, requiring zero manual migration.
- **SC-003**: Editing only sprite color/opacity or frame clear color on a warmed scene produces 0 extra culling bounds refreshes, 0 visibility re-scans, and 0 draw-order re-sorts.
- **SC-004**: Raycast hit distances, world intersection points, and outward unit surface normals agree with analytical 2D geometry within `1e-4` tolerance across axis-aligned, rotated, scaled, and offset box and circle colliders, while filtering 100% of inactive objects, excluded triggers, and mismatched collision masks.
- **SC-005**: Closest-hit raycast queries allocate 0 recurring heap memory per query and complete deterministically across all boundary cases (misses, zero-distance origins inside colliders, multi-hit sorting, and invalid inputs).
- **SC-006**: 100% of existing engine, editor, and example regression suites pass alongside new automated tests covering tinting, clear color overrides, editor workflows, and 2D raycasting.

## Assumptions

- Color channels use 8-bit unsigned integers (`0..255`) for RGB tint and alpha opacity, multiplying source texel colors per component with standard alpha blending for translucent sprites.
- Both sprite color modulation and frame clear color default to opaque white `(255, 255, 255, 255)` to guarantee identical visuals for all existing projects and visual verification baselines.
- When a ray originates strictly inside an eligible collider volume, the hit distance and fraction are `0.0` at the ray origin, with a deterministic normal pointing opposite to the normalized ray direction (documented clearly in the API contract).
- Closest-hit and all-hits raycast queries operate on active, registered colliders in the active runtime using immediate world positions/rotations/scales so gameplay queries reflect transforms updated earlier in the same frame.
- Custom GPU shader pipelines and per-pixel software floor/ceiling buffers remain out of scope; per-sprite RGBA modulation satisfies shading, lighting, damage tint, and translucency needs natively.
