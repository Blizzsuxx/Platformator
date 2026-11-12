# Implementation Plan: Complete Editor Performance, Focus, Playability & Unity Ergonomics Audit Remediation

**Branch**: `branch-without-bvh` | **Date**: 2026-11-12 | **Spec**: [spec.md](file:///home/user/Projects/Platformator/specs/014-editor-audit-remediation/spec.md)

**Input**: Feature specification from `/specs/014-editor-audit-remediation/spec.md`

## Summary

Remediate all findings from the comprehensive two-pass Platformator Editor audit ([docs/UNITY_STYLE_AND_USABILITY_REVIEW.md](file:///home/user/Projects/Platformator/docs/UNITY_STYLE_AND_USABILITY_REVIEW.md)):
1. **Idle Efficiency & Frame Performance (US1)**: Eliminate the 0 ms `idleWait()` busy-spin caused by unconditional `nextReferenceFallbackCheck` and in-flight `assetWatchSchedule` deadlines; exclude non-project directories (`.venv`, `specs`, `benchmark`, `diplomski`, `paper`, `docs`, and hidden `.*` directories) from `inotify` watching and source-stamp scanning; cache `executable_identity`, `template_author_bindings`/`schemas`, and `SemanticSnapshot` updates across unchanged frames; enable `SDL_SetRenderVSync(renderer, 1)`; and remove leftover per-frame debug logging.
2. **Cross-Panel Focus & Scene Navigation (US2)**: Allow middle-click, right-click, and Hand-tool left-click drags on the Scene canvas to pan immediately and focus the Scene window even when the Hierarchy panel previously held focus; route `F`, `Shift+F`, and `Q`/`W`/`E`/`R` shortcuts when either `Scene` or `Hierarchy` holds focus; and support double-clicking a Hierarchy row to frame that GameObject in the Scene view.
3. **Interactive Embedded Game Play Mode & Centered Playback Controls (US3)**: Forward keyboard and mouse events from a focused `GamePanel` over the editor-session control pipe to the child runtime process (`EditorSessionClient::poll_control` $\rightarrow$ `SDL_PushEvent`), release held keys cleanly on focus loss, suppress single-key editor shortcuts while playing in the focused Game view, raise embedded capture rate from 15 FPS (`66,667 µs`) to 60 FPS (`16,667 µs`), center Play/Pause/Step in the main toolbar with a Play Mode tint, and add aspect ratio / zoom controls to the Game view.
4. **Glitch-Free Inspector Authoring (US4)**: Fix the component header context menu button (`ImGuiTreeNodeFlags_AllowOverlap`, vector-drawn 3-dot icon instead of missing UTF-8 glyph `⋮`, and preserved cursor spacing when collapsed), remove duplicate `ImGui::SeparatorText` component titles, extend `draw_compact_vector2` to Collider `Offset` and Rigidbody `Velocity`/`Force`, place the GameObject identity header at the top of the Inspector above `Object Template`, and add horizontal drag-scrubbing to standard numeric field labels.
5. **Scene Overlay Toolbar, Marquee Selection & Asset Drag-Instantiation (US5)**: Compact the Scene viewport toolbar into a single-row/overlay bar, add empty-space left-drag marquee box-selection in Move/Rotate/Scale modes, and support dragging sprite/audio/script/template assets from the Project panel onto the Scene view and Hierarchy to instantiate GameObjects.
6. **Project Context Menus, Two-Column Browser & Hierarchy Quick-Controls (US6)**: Add right-click context menus in the Project panel (*Create*, *Open*, *Reveal*, *Rename*, *Duplicate*, *Delete*), a two-column folder tree + breadcrumbs + thumbnail size slider, a Hierarchy `+` creation dropdown, and inline row visibility/lock gutter toggles.
7. **Console Severity Badges, Standard Menu Bar & Neutral Charcoal Theme (US7)**: Replace the Console severity combo box with independent Info/Warning/Error count badge toggles, add double-click source file opening, standardize top menus (`File | Edit | Assets | GameObject | Component | Window | Help`), and update `style.cpp` to a neutral dark charcoal palette with 1px input borders and crisp scalable TrueType font rendering.

## Technical Context

**Language/Version**: C++20 (GCC 13+ / Clang 16+ on Linux)  
**Primary Dependencies**: Dear ImGui 1.92.9b, SDL 3.4.18, nlohmann JSON, Eigen3, oneTBB  
**Storage**: Lossless `.scene` and `.pttemplate` JSON files, `.platformator-editor` preferences JSON  
**Testing**: CTest headless editor test suites (`editor/tests/editor_*`) and compact probe (`platformator_editor_gui_probe`)  
**Target Platform**: Linux (x86_64, X11 / Wayland / headless dummy video driver)  
**Project Type**: Native desktop game engine editor (`platformator_editor`) + runtime session client (`libplatformator`)  
**Performance Goals**: Idle wait $\ge 50\text{ ms}$ (< 2% CPU when idle); active authoring frame time $< 16.6\text{ ms}$ (60 FPS) on `examples/mario/level1.scene`; embedded Game view frame capture up to 60 FPS; watched directory count well under the 4,096 ceiling (~100 directories)  
**Constraints**: Must preserve lossless JSON round-tripping, atomic undo/redo command history, single-owner UI thread rules for SDL/ImGui, and compact benchmark artifact policy (`AGENTS.md`)  
**Scale/Scope**: ~25 files across `editor/src/`, `src/editorsession.cpp`, `src/sdlwindow.cpp`, and `editor/tests/`

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Initial Gate | Post-Design Gate |
| :--- | :--- | :--- |
| **I. Performance and Scalability First** | **PASS**: Directly eliminates O(N) per-frame full-scene semantic tree rebuilds, per-frame filesystem `canonical`/`stat` syscalls, 5-second 35,000-file repository hashing, and 0 ms busy-waiting. | **PASS**: Cached generation keys (`documentGeneration`, `selectionRevision`, `projectGeneration`, `file_size`/`last_write_time`) reduce idle/active overhead to O(1) when state is unchanged. |
| **II. Multicore Design with oneTBB** | **PASS**: Evaluates concurrency boundaries: SDL/ImGui rendering and `DocumentSession` mutations remain strictly on the main thread; filesystem scanning and build/query pipelines run asynchronously on background worker futures without blocking UI frames. | **PASS**: No competing thread pools added; background scan tasks now skip non-project trees and coalesce schedules cleanly without waking the main thread prematurely. |
| **III. Research-Backed Collision Architecture** | **PASS**: Core broad-phase/narrow-phase collision algorithms (`src/grid.cpp`, `src/segmentedintervallist.cpp`) are untouched. | **PASS**: Editor scene picking and marquee AABB queries operate purely on editor scene preview bounds. |
| **IV. Simple Code and KISS** | **PASS**: Fixes bugs at their root causes (guarding expired deadlines, unifying panel focus, fixing ImGui header overlap) without introducing redundant abstraction layers. | **PASS**: Reuses existing `EditCommandService`, `SelectionModel`, `draw_compact_vector2`, and editor-session control pipe. |
| **V. Unity-Inspired Developer Experience** | **PASS**: Directly fulfills Principle V across Hierarchy, Inspector, Scene view, Game view, Project browser, Console, Toolbar, and Menu bar. | **PASS**: Standardizes Unity muscle-memory workflows (`Hierarchy click -> F / pan`, `Q/W/E/R`, marquee select, drag-to-scene, centered Play/Pause/Step, component header menus). |
| **VI. Evidence Before Performance Claims** | **PASS**: Baseline established via compact `platformator_editor_gui_probe` (~60–67 ms active frame, ~15–35 µs idle wait, 8,303 unskipped directories). | **PASS**: Verification uses bounded 15-frame `platformator_editor_gui_probe` runs and CTest suites in strict compliance with `AGENTS.md`. |
| **VII. Correctness and Architectural Integrity** | **PASS**: Preserves lossless `.scene` serialization, undo/redo invariants, and safe child-process session lifetimes. | **PASS**: Embedded input forwarding validates message bounds and releases held keys on focus loss or session termination. |

## Project Structure

### Documentation (this feature)

```text
specs/014-editor-audit-remediation/
├── spec.md                                  # Feature specification (US1–US7, FR-001–FR-029)
├── plan.md                                  # This implementation plan
├── research.md                              # Phase 0 technical decisions and rationales
├── data-model.md                            # Phase 1 data structures, states, and invariants
├── quickstart.md                            # Phase 1 verification and compact probe guide
├── checklists/
│   └── requirements.md                      # Spec quality validation checklist
└── contracts/
    ├── performance-and-idle-contract.md     # Idle wait, watcher exclusions, frame caching
    ├── focus-and-viewport-contract.md       # Cross-panel focus, panning, shortcuts, marquee, overlay
    ├── embedded-game-input-contract.md      # Session input forwarding, 60 FPS capture, toolbar
    └── panels-and-workflows-contract.md     # Inspector, Project, Hierarchy, Console, Menus, Style
```

### Source Code (repository root)

```text
src/
├── editorsession.h                          # Add input forwarding control commands & 60 FPS capture interval
├── editorsession.cpp                        # Parse key/mouse control lines & inject via SDL_PushEvent
└── sdlwindow.cpp                            # Enable SDL_SetRenderVSync on created SDL_Renderer

editor/src/
├── editorapp.cpp                            # Fix idleWait deadlines, cache per-frame queries/SemanticSnapshot, standardize menus, Play Mode tint
├── actions.h                                # Expand shortcut routing context (Scene or Hierarchy focus, embedded Game suppression)
├── actions.cpp                              # Allow F/Shift+F/Q/W/E/R from Hierarchy focus; suppress when embedded Game focused
├── viewport.h                               # Add marquee selection state, compact overlay toolbar, asset drop target
├── viewport.cpp                             # Auto-focus on middle/right/Hand pan, remove printf, draw marquee & compact toolbar, accept asset drops
├── playsession.h                            # Enable inputForwarding capability & send_input methods
├── playsession.cpp                          # Write bounded key/mouse commands to session control pipe
├── project.cpp                              # Exclude dot-dirs, .venv, specs, benchmark, docs; cache source file hashes by size+mtime
├── projectsession.cpp                       # Align directory exclusion list with project.cpp and watch_linux.cpp
├── toolbar.h                                # Support centered Play/Pause/Step actions and state
├── toolbar.cpp                              # Render left file/build group, centered Play/Pause/Step group, right status
├── style.h                                  # Neutral charcoal palette, Play Mode tint helpers, TrueType font loader
├── style.cpp                                # Charcoal theme colors, FrameBorderSize=1, vector 3-dot icon, DPI font loading
├── windowhost.cpp                           # Enable SDL_SetRenderVSync on detached viewport renderers
├── platform/
│   └── watch_linux.cpp                      # Exclude dot-dirs, .venv, specs, benchmark, docs from inotify tree watch
└── panels/
    ├── hierarchy.h                          # Header '+' preset menu, row visibility/lock gutter icons, double-click frame callback, asset drop target
    ├── hierarchy.cpp                        # Implement '+' presets, gutter eye/lock toggles, double-click frame, asset drop instantiation
    ├── inspector.h                          # General label drag-scrubbing, fixed component header menu helper
    ├── inspector.cpp                        # Move GameObject header above Template, fix '⋮' overlap/glyph/spacing, remove duplicate SeparatorText, compact Collider/Rigidbody Vector2s
    ├── game.h                               # Aspect ratio preset, zoom scale, input focus capture & forwarding
    ├── game.cpp                             # Capture/forward keyboard & mouse to PlaySession, release on blur, aspect/zoom bar
    ├── project.h                            # Two-column folder tree, breadcrumbs, thumbnail slider, context menu state
    ├── project.cpp                          # Implement context menus (Create/Open/Reveal/Rename/Duplicate/Delete) & two-column view
    ├── console.h                            # Independent Info/Warning/Error toggle flags and counts
    └── console.cpp                          # Render 3 count badge toggles, lower detail pane, double-click external open

editor/tests/
├── idle_and_watch_test.cpp                  # Verify idle wait duration > 0ms and directory skip rules
├── viewport_cases.cpp                       # Verify pan/shortcut focus from Hierarchy & marquee selection
├── inspector_interaction_test.cpp           # Verify header menu, Collider/Rigidbody compact Vector2, label scrubbing
├── play_session_test.cpp                    # Verify embedded input forwarding & 60 FPS capture interval
└── project_and_hierarchy_workflow_test.cpp  # Verify asset drop instantiation & Project context menu operations
```

**Structure Decision**: Modify existing editor and runtime session modules in place under `editor/src/`, `src/`, and `editor/tests/` without introducing new libraries or external dependencies.

## Complexity Tracking

No constitutional violations or unjustified complexity introduced.
