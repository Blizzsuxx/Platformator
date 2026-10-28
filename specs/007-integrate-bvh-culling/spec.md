# Feature Specification: Selectable BVH and Segmented Spatial Culling

**Feature Branch**: `001-cpp-engine-cleanup` (existing checkout; feature directory is independent)

**Created**: 2026-10-28

**Status**: Draft

**Input**: User description: "integrate BVH as we agreed - using BVH for rendering and broad phase (as a benchmark). Add ability to use the existing broad phase implementation for render culling as well (a separate AABB but for sprites and camera)"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Compare collision broad phases in the same engine (Priority: P1)

An engine developer selects either the current grid plus segmented sweep-and-prune broad phase or a dynamic bounding-volume hierarchy (BVH) for collision candidate generation. They can exercise the same scenes and evaluate each method without substituting a different physics simulation.

**Why this priority**: Provides the requested collider comparison while preserving the established simulation and a usable reference implementation.

**Independent Test**: Run the same collision fixtures and recorded bounds changes through both choices while retaining the current rendering path. Compare candidate coverage with an independent overlap reference and check confirmed contacts, lifecycle events and complete simulation measurements.

**Acceptance Scenarios**:

1. **Given** an existing scene and no new selection, **When** the engine starts, **Then** it uses the current collision broad phase and preserves existing loading, stepping and contact behavior.
2. **Given** either collision choice, **When** eligible colliders move, rotate, resize, appear, disappear or change filters, **Then** every eligible bounding-box overlap is covered, duplicate discoveries produce one logical candidate pair, and confirmed contacts and events follow the existing contracts.
3. **Given** one collision choice and a rendered scene, **When** the developer changes only the rendering choice for another run, **Then** collider participation and simulation rules remain unchanged, including for off-screen objects.
4. **Given** identical recorded collider inputs, **When** the developer runs each method with one worker and supported multiple-worker configurations, **Then** candidate coverage and logical contact transitions remain correct independently of worker completion order.
5. **Given** an unknown or unsupported selection, **When** a run is requested, **Then** it reports a clear configuration failure and cannot emit results labeled as that selection while silently using another method.

---

### User Story 2 - Compare sprite visibility methods independently of physics (Priority: P1)

A game or engine developer selects current submit-all rendering, a direct linear bounds check, dynamic-BVH culling, or culling through a separate sprite-and-camera instance of the existing segmented broad-phase method. The picture and draw ordering remain the same while unnecessary sprite submissions can be avoided.

**Why this priority**: Delivers both requested rendering candidates and preserves simple reference paths so that the benefit of visibility rejection can be distinguished from the benefit of a spatial index.

**Independent Test**: Keep the current collision broad phase and render controlled scenes using all four visibility choices. Include moving sprite-only objects, transparent overlaps, cameras crossing boundaries, and sprites whose artwork differs from their collider bounds. Compare output against submit-all rendering and check submission counts.

**Acceptance Scenarios**:

1. **Given** a scene with sprite-only objects and differently sized sprite/collider geometry, **When** each culling method renders it, **Then** visibility follows the rendered sprite geometry without requiring a physics collider or modifying physics registration.
2. **Given** the segmented visibility choice, **When** camera and sprite bounds begin or end an overlap, **Then** a separate visibility index updates the camera/sprite relationship without creating gameplay contacts, events or solver work. A sprite discovered through multiple regions is submitted once.
3. **Given** moving, rotating, scaling or animated sprites and a moving/resizing camera, **When** a frame renders, **Then** all visibility methods use the current published rendering state and retain every sprite that can contribute visible pixels.
4. **Given** overlapping transparent sprites, **When** any visibility choice is used, **Then** retained sprites preserve the existing submission order and the controlled frame matches the submit-all reference.
5. **Given** paused or zero-simulation-step frames, a missing camera, or headless operation, **When** the engine follows the existing frame path, **Then** it preserves that path's behavior, does not use stale visibility when rendering resumes, and does not manufacture rendering measurements for work that did not occur.

---

### User Story 3 - Produce defensible comparison evidence (Priority: P2)

A researcher runs repeatable comparisons for the paper and inspects the complete costs and correctness of each option. They can distinguish static-query results, dynamic index maintenance, collision pair processing, actual rendering and full-engine effects, including cases where the new choices are slower.

**Why this priority**: Prevents a fast query over a prebuilt static scene from being mistaken for evidence about a dynamic engine workload, and makes negative or mixed results useful.

**Independent Test**: Produce a comparison bundle for one recorded-input workload and one full-engine workload. Confirm matching declared inputs/configuration, separate correctness validation, retained raw repetitions, accounted maintenance costs, and explicit limitations. No particular backend needs to win for the evidence to be accepted.

**Acceptance Scenarios**:

1. **Given** recorded spatial operations and a fixed seed, **When** each applicable method replays them, **Then** each receives the same ordered changes and queries, including movement, resizing, filter changes, insertion and removal, and measured work includes the required index and result maintenance.
2. **Given** static and dynamic populations, **When** results are reported, **Then** static construction/query timings and dynamic update/query timings are identified separately; no update or rebuild cost required by the measured policy is omitted.
3. **Given** a full-engine comparison, **When** records are produced, **Then** complete simulation-step and relevant subsystem timings are reported alongside rendering/visibility measurements where applicable, without adding nested timing scopes together.
4. **Given** a failed correctness check, incomplete run, mismatched input/configuration, or unavailable renderer, **When** a report is generated, **Then** it identifies the failure or unavailable measurement and excludes invalid samples from performance aggregates.
5. **Given** eligible repeated runs, **When** a reader examines the comparison, **Then** they can identify the exact methods, source/dependency revisions, bounds/maintenance policies, workload, worker counts, machine, build and rendering configuration and reproduce the selected cases without manual source edits.

### Edge Cases

- Empty scenes; no colliders or sprites; all inactive objects; no active camera; all sprites off-screen or all on-screen.
- Negative coordinates, touching edges, equal endpoint positions, zero-area bounds where currently supported, and rotation at viewport corners.
- Camera/output aspect ratios that scale the two axes differently before sprite rotation; source rectangles, flips, animation dimension changes and large sprite/collider size differences.
- Large bounds crossing many cells/chunks, dense or skewed occupancy, distant sparse objects, rapid camera movement and teleportation across the scene.
- Repeated overlap discoveries, entering and leaving several regions during one update, and multiple updates coalesced before publication.
- Parent/child movement, reparenting, late-frame changes, component replacement, callback-time destruction and scene replacement while spatial work is pending.
- Filter changes without movement, trigger/body-type changes, sleeping/waking bodies, removal of the last support, and objects simulated outside the camera view.
- Paused and zero-step rendered frames, different simulation/render rates, window resizing, headless physics, and headless visibility-only replay.
- Invalid/nonfinite bounds or valid engine geometry beyond a selected experimental index's supported range: report unsupported input or retain conservative coverage, never silently drop objects or disguise another backend as the requested choice.
- Index maintenance, reductions, callbacks and object reclamation meeting at worker-completion boundaries; results must not depend on workers finishing in a particular order.
- Rebuild or bounds-padding settings changing candidate counts or memory; solver-order differences producing diverging trajectories in closed-loop simulations.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Developers MUST be able to select independently between two collision broad phases: the existing grid plus segmented sweep-and-prune method and a dynamic BVH. Selection MUST be available to embedded applications, the stock runner and benchmark tooling before a run starts.
- **FR-002**: Developers MUST be able to select independently among four rendering choices: current submit-all rendering, a linear conservative bounds check, dynamic-BVH culling, and segmented broad-phase culling using sprite/camera bounds. All eight collision/rendering combinations MUST be supported.
- **FR-003**: Unspecified settings MUST preserve current collision and rendering choices. Existing scenes, examples and source consumers MUST continue to work under those defaults. Unknown or unsupported settings MUST fail clearly rather than silently fall back.
- **FR-004**: The BVH choice MUST support incremental insertion, removal and bounds updates for dynamic objects; it MUST NOT depend on treating the scene as permanently static. Any full rebuilding or alternative maintenance policy used by a run MUST be identified and fully charged to that run.
- **FR-005**: Physics and rendering MUST have separate spatial membership and overlap state even when they use the same underlying method. Rendering MUST support sprites without colliders and MUST NOT register camera/sprite visibility proxies with gameplay physics.
- **FR-006**: The segmented rendering choice MUST reuse the existing segmented sweep-and-prune broad-phase capability in a separate sprite/camera visibility context. Its required results are camera/sprite overlaps; sprite/sprite overlaps MUST NOT acquire gameplay meaning. The comparison MUST identify the actual partitioning configuration rather than label a global interval list as the existing partitioned method.
- **FR-007**: Collision choices MUST feed the same existing narrow-phase, contact, support, solver, sleeping and callback contracts. Backend selection MUST NOT change collision geometry, timestep or integration rules, or use camera visibility to suppress physics.
- **FR-008**: Every eligible exact collider-bounds overlap MUST be covered by the selected broad phase. Duplicate discoveries MUST yield one logical candidate pair. Conservative extra candidates are permitted, but MUST NOT manufacture confirmed contacts or gameplay events and their downstream cost MUST be measured.
- **FR-009**: Collider movement, shape changes, filters, activation, body/trigger response changes, sleeping/waking, retirement and scene changes MUST be reconciled correctly without missing newly eligible pairs or retaining references to removed objects. Multi-region overlap changes MUST preserve net pair membership.
- **FR-010**: All visibility choices MUST conservatively cover the actual current rendered sprite geometry, including object scale, rotation, animation dimensions, source rectangles, flips, parent movement and camera/output transformations. Collider geometry MUST NOT substitute for sprite geometry.
- **FR-011**: Culling MUST retain every sprite that can affect the current viewport and preserve the existing order of retained submissions. Duplicate visibility discoveries MUST NOT duplicate a draw. Conservative false-positive visibility is allowed and MUST be distinguishable from missed visible output.
- **FR-012**: Spatial changes MUST become visible at the existing safe mutation/transform publication boundaries before their consuming physics step or rendered frame. This MUST cover paused and zero-step frames and differing simulation/render rates. Queries, publication and reclamation MUST respect object lifetimes and worker completion.
- **FR-013**: Normal runs and performance captures MUST advance only the selected spatial methods. Unselected indexes, shadow comparisons and expensive correctness oracles MUST NOT add hidden work to measured runs. Headless/no-camera paths MUST preserve existing behavior and identify work not performed.
- **FR-014**: Configuration documentation MUST explain independent selections, preserved defaults, experimental limitations and startup errors. Installed and source-embedded consumers MUST retain the existing dependency/lifetime contracts; this feature MUST NOT require scene-file migration or a replacement physics library.
- **FR-015**: Correctness validation MUST use independent exact-overlap references on small controlled/randomized inputs and matched recorded inputs for candidate/contact transitions. It MUST cover dynamic updates, filter changes, insertion/removal, ties, migration, large bounds, duplicate suppression and lifecycle changes.
- **FR-016**: Rendering validation MUST compare every culling choice with submit-all output on controlled fixtures using the same renderer and output configuration, including transparent ordering, rotated/scaled edge cases, animation changes, and sprite-only objects. Readback/comparison work MUST be excluded from performance captures.
- **FR-017**: Existing applicable physics, lifecycle, scene, public-consumer and package checks MUST continue to pass. One-worker and supported multiple-worker validation MUST preserve required coverage and event behavior; floating-point tolerances for full simulations MUST be declared before interpreting differences.
- **FR-018**: Benchmarking MUST support deterministic replay of identical recorded spatial changes and queries for each applicable method, separately from full-engine execution. Recorded-input comparisons MUST isolate spatial behavior from backend-dependent solver trajectories and disclose trace preparation/loading excluded from timing.
- **FR-019**: Benchmark presets MUST cover small and large populations; stationary and changing populations; insertion/removal and cell migration; uniform, clustered and skewed distributions; similar and mixed object sizes; sparse and dense overlap; camera pan/jumps and output changes; and different visible fractions. Representative coverage is required rather than an exhaustive Cartesian product.
- **FR-020**: The sparse comparison presets MUST support at least 100, 1,000 and 10,000 spatial objects. The recorded-input case set MUST represent 0%, 1%, 10% and 100% changing bounds. Rendered case coverage MUST include 0%, 1%, 10% and 100% visible populations. Dense and closed-loop workload sizes MUST be bounded and explicitly recorded rather than assumed equivalent to sparse cases.
- **FR-021**: Comparisons MUST measure complete required bounds preparation, insertion/removal, updates, rebuilding, queries, deduplication, ordering and pair/result maintenance within declared boundaries. Full-engine runs MUST also expose complete simulation-step, relevant physics/subsystem and actual rendered-frame costs. Shared/nested scopes MUST not be double-counted, and visibility-only replay MUST not be labeled actual rendering.
- **FR-022**: Reports MUST include average and tail latency, initialization cost, changing-object counts, candidate/visible/submitted counts, and memory/allocation measurements with declared coverage. Bounds padding and maintenance-policy settings MUST be recorded so their extra candidates, memory and downstream work are visible. Expensive instrumentation MUST be separated from timing captures.
- **FR-023**: Comparable cases MUST use the same workload inputs, seed, warmup/measurement boundaries, object geometry, worker allowance, build/instrumentation settings and renderer/presentation configuration where applicable. Reports MUST identify effective settings, source and dependency revisions, raw samples and at least five independent-process repetitions for each reported performance comparison case.
- **FR-024**: The benchmark workflow MUST compare one-worker and supported multiple-worker cases, assess the chosen sequential/parallel strategy and report its limitations. It MUST NOT assume that tree mutation, shared-body solving, renderer calls or callbacks can run concurrently. The project’s existing worker configuration remains the basis for comparisons.
- **FR-025**: Invalid, incomplete, correctness-failing or non-equivalent runs MUST be identified and excluded from eligible performance aggregates. An unavailable hardware/renderer measurement MUST remain explicitly unverified; timing a static prebuilt query MUST NOT stand in for a dynamic result.
- **FR-026**: Implementation changes required to share spatial capabilities MUST preserve the current segmented collision backend as a usable control. A same-method before/after comparison MUST assess any refactoring overhead before differences are attributed to the competing method. Unexpected regressions MUST be investigated and documented.
- **FR-027**: The delivered comparison MUST state where each method helps, regresses or remains unmeasured. No automatic adoption decision, mandatory speedup, automatic backend selection, or removal of the current broad phase is required for this prototype to succeed.

### Key Entities

- **Spatial method selection**: The independent collision and rendering choices and effective startup configuration for a run.
- **Spatial proxy**: A live object's identity, current conservative bounds, purpose, eligibility information and membership in a physics or visibility context.
- **Collision candidate**: A deduplicated eligible collider pair that may require exact collision evaluation; distinct from a confirmed contact or gameplay event.
- **Visibility relationship**: A camera/sprite overlap that can make a sprite eligible for ordered rendering; independent of gameplay collision state.
- **Recorded workload**: Reproducible ordered changes, queries and identities, with geometry, seed, population, movement/churn and camera parameters.
- **Comparison bundle**: Raw repetitions, effective configuration and provenance, correctness status, timing boundaries, counts, resource measurements, aggregates and limitations for a declared set of comparable runs.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Developers can run all eight supported collision/rendering combinations through documented startup configuration without editing engine source; omitted settings reproduce the existing default choices.
- **SC-002**: Across the delivered controlled and randomized matched-input validation cases, 100% of eligible exact bounding-box overlaps are covered, candidate identities are deduplicated, and expected logical contact/event transitions pass the existing contracts for one and supported multiple workers.
- **SC-003**: Every delivered controlled rendering fixture produces the same visible output and retained submission order as the submit-all reference under the same renderer/output settings, with no missing visible sprites or duplicate draws.
- **SC-004**: In a sparse fixture of 1,000 sprites with exactly 10 intersecting the viewport and all other bounds clearly separated, each culling choice submits those 10 sprites once and preserves the reference image; submit-all remains available as the comparison control.
- **SC-005**: A reviewer can reproduce the declared recorded-input and full-engine comparison cases from the documented commands and bundle metadata. Each reported performance case has at least five independent process repetitions, retained raw samples and explicit correctness/eligibility status.
- **SC-006**: Delivered evidence covers the three sparse population scales, four changing-bound fractions and four visible fractions in FR-020 across a declared representative case set, along with one-worker/multiworker, churn, mixed-size, dense/skewed and camera-movement cases. Omitted cross-combinations and unavailable measurements are identified.
- **SC-007**: Every eligible comparison accounts for required ongoing maintenance and result processing and reports its initialization, steady-state/tail latency, output/work counts and resource costs. No dynamic-performance conclusion relies solely on timings of a prebuilt static query.
- **SC-008**: Applicable existing regression, lifecycle, scene and installed/source-consumer checks pass under preserved defaults, and the new choices pass the relevant contract checks. The same-method control quantifies any change to reference performance caused by integration work.
- **SC-009**: The final evidence gives the developer a documented, reproducible basis to retain or change each spatial choice, including measured regressions and limitations. A slower competing method does not constitute feature failure when its implementation is correct and its measured costs are complete.

## Assumptions

- This is an experimental comparison feature. Backend selection occurs at startup; changing indexes while a scene is running, automatic adaptation, and choosing a new production default are outside scope.
- The default collision choice remains the existing grid plus segmented sweep-and-prune method. The default rendering choice remains submit-all during the prototype. The linear rendering baseline is included to separate culling gains from indexing gains.
- The already extracted [Box2D dynamic-tree subset](../../vendored/box2d-3.2.0/README.platformator.md) is the agreed BVH dependency. This feature integrates that available capability rather than adding an unrelated implementation or replacing the engine's collision geometry and solver.
- The separate sprite/camera AABB context means separate visibility membership/state using the existing segmented capability, not a camera or sprite collider added to gameplay physics. Reuse does not require identical sprite and collider bounds or a physically shared index.
- Eligibility, lifetime, callback and scene contracts come from the current engine. Numerical equality of chaotic closed-loop trajectories is not assumed; declared-tolerance full-engine checks supplement identical-input replay rather than replacing it.
- Five independent timing repetitions follow the repository's benchmark practice. The population/fraction requirements describe coverage of representative presets, not every possible cross-combination or an invented speedup target.
- Physics can be measured headlessly. Visibility-only replay may also run without a renderer, but actual rendering claims require measured rendered frames. A controlled software renderer is acceptable when clearly identified; it does not establish unmeasured hardware-renderer performance.
- The earlier [P03 investigation](../../docs/P03_SPRITE_RENDERING_INVESTIGATION.md) is exploratory evidence about static queries and software rendering, not proof of the best dynamic method. Its raw records remain historical and are not overwritten as new evidence.
- The [project constitution](../../.specify/memory/constitution.md), [benchmark methodology](../../benchmark/README.md), and [segmented sweep-and-prune research](../../paper/paper.txt) govern correctness, multicore assessment, comparison boundaries and interpretation. Detailed architecture and maintenance-policy choices belong to planning.
- Editor controls, new camera/rendering effects, scene-format changes, a new solver, general collision-algorithm redesign and migration of unrelated cleanup findings are outside scope. Existing supported scene, runner, embedding and package workflows remain in scope for compatibility validation.
