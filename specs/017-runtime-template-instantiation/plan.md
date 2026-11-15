# Implementation Plan: Runtime Template Instantiation

**Branch**: `017-runtime-template-instantiation` | **Date**: 2026-11-15 | **Spec**: [spec.md](./spec.md)  
**Input**: Feature specification from `/specs/017-runtime-template-instantiation/spec.md`

## Summary

Add first-class runtime object templates (`Template : public Asset` referenced via `AssetReference<Template>` / `TemplateAssetRef`) that load `.pttemplate` files into an inert, unregistered `Staged` `GameObject` prototype hierarchy owned by `std::vector<std::unique_ptr<GameObject, ObjectDeleter>>` (`objects[0]` = root) alongside `Asset::Reference<T>` pre-retained media dependencies. Provide `instantiate(...)` overloads to deep-clone the prototype hierarchy, attach and register clones via `GameManager::registerGameObjectSubtree`, remap internal `ObjectReference<T>` fields cleanly before `start()` runs while clearing cross-scene external references, support cycle-safe nested templates without refcount loops, and expose `.pttemplate` asset-reference fields across the editor's source scanner, Inspector, validation, and template editing workflows.

Design emphasis: keep the implementation **KISS** — reuse existing `Asset`, `AssetReference<T>`, `ReferenceResolver`, `SERIALIZABLE_SCRIPT`, `SceneBuildContext` deserialization helpers, and `registerGameObjectSubtree`; validate inputs only at public/external boundaries and assert established internal invariants without blanket null checks.

## Technical Context

**Language/Version**: C++20 (runtime library + editor)  
**Primary Dependencies**: SDL3, SDL3_mixer, SDL3_image, SDL3_ttf, Eigen 5, nlohmann/json, oneTBB, ImGui (editor)  
**Storage**: `.pttemplate` v1 JSON files and `.scene` JSON arrays on local filesystem  
**Testing**: CMake + CTest (`debug` preset for runtime tests, `editor-debug` preset for editor tests)  
**Target Platform**: Linux desktop  
**Project Type**: Native C++20 2D game engine library (`platformator`), runner executable, and C++/ImGui editor (`platformator_editor`)  
**Performance Goals**: Zero disk reads or media decoding on repeated spawns after template load; `O(nodes + components + behaviors)` cloning per spawn (`SC-003`)  
**Constraints**: Preserve existing runtime single-writer mutation boundaries, deferred callback/reconciliation phases, cycle-free asset refcounting (`SC-005`), and KISS boundaries with no unnecessary null checks  
**Scale/Scope**: Tested up to 100+ sequential/mid-step spawns with nested child hierarchies, all 8 component types, typed component/object internal references, and cyclic template-to-template references

## Constitution Check

*GATE: Evaluated before Phase 0 research and re-checked after Phase 1 design.*

| Principle | Assessment | Status |
|-----------|------------|--------|
| **I. Performance & Scalability First** | Prototype JSON parsing and media decoding execute once per `.pttemplate` per scene load; repeated `instantiate()` clones directly in memory (`O(subtree)`) without disk I/O or per-frame global scans. | **PASS** |
| **II. Multicore Design with oneTBB** | Structural registration (`registerGameObjectSubtree`, `ObjectRegistry::add`, script attachment) is inherently serialized on the main simulation thread inside `assertCanMutate()`. Spawning executes sequentially under that boundary with zero locks or shared cross-thread mutation; future parallel ticks still process spawned colliders/bodies normally through `Grid` / `PhysicsManager`. | **PASS** |
| **III. Research-Backed Collision Architecture** | Does not modify broad-phase sweep-and-prune data structures; cloned colliders enter/leave `Grid` through standard `addComponentsToGameManager()` and deferred reconciliation. | **PASS** |
| **IV. Simple Code & KISS** | Reuses `Asset`, `AssetReference<T>`, `SERIALIZABLE_SCRIPT` (`clone()` + existing `resolveReferences()`), `SceneBuildContext` JSON deserializers, and `registerGameObjectSubtree`. `Template` owns `std::vector<std::unique_ptr<GameObject, ObjectDeleter>>` (`~Template() = default;`) with boundary-only validation and zero speculative frameworks or blanket null checks. | **PASS** |
| **V. Unity-Inspired Developer Experience** | Mirrors Unity `Instantiate(prefab, ...)` ergonomics: default/parent-only preserves local transform, explicit position places root in world space, cross-scene externals clear cleanly, and `.pttemplate` fields appear in the Inspector without building first. | **PASS** |
| **VI. Evidence Before Performance Claims** | No micro-benchmark claims invented; Quickstart & verification rely on behavioral, allocation/leak-cleanup, and 100-spawn stress tests via CTest. | **PASS** |
| **VII. Correctness & Architectural Integrity** | Enforces atomic clone rollback on failure, live-handle/ID allocation before `start()`, exact target-kind checks on internal bindings, cycle-safe lazy resolution without refcount leaks, and scene-level retention for spawned assets. | **PASS** |

*Post-Phase-1 Re-check*: All Phase 1 artifacts (`data-model.md`, `contracts/runtime-template-api.md`, `quickstart.md`) preserve strict single ownership, cycle-free teardown (`SC-005`), and zero Constitution violations.

## Project Structure

### Documentation (this feature)

```text
specs/017-runtime-template-instantiation/
├── plan.md                                # This file
├── research.md                            # Phase 0 design decisions & rationale
├── data-model.md                          # Entities, ownership, remapping, lifecycle states
├── quickstart.md                          # Runnable validation scenarios
├── contracts/
│   └── runtime-template-api.md            # Public C++ & JSON/editor contracts
└── tasks.md                               # Phase 2 output (/speckit-tasks)
```

### Source Code (repository root)

```text
include/platformator/
├── template.h              # NEW: Template : public Asset + platformator::instantiate(...) helpers
├── assetreference.h        # Add Template asset loading & TemplateAssetRef alias
├── baseobject.h / gameobject.h / behavior.h / scriptcomponent.h
│                           # Friend access for Template + Component/Behavior clone hooks
├── referenceresolver.h     # Add loadTemplate(...) to ReferenceResolver
├── objectreference.h       # Update resolve() to sync objectId with handle on remap
├── runtime.h               # Add Runtime::loadTemplate(...)
└── scriptmetadata.h / scriptregistration.h
                            # AssetTargetName<Template>, SERIALIZABLE_SCRIPT clone() generation

src/
├── template.cpp            # NEW: .pttemplate loading, prototype ownership, clone + instantiate()
├── assetreference.cpp      # loadTemplateAssetReference bridge (matching texture/audio/clip)
├── gamemanager.h / .cpp    # templateCache/Aliases, loadTemplate (cycle-safe), freeTemplate, scene retention
├── scenebuilder.h / .cpp   # Shared JSON subtree builder helper reused by Scene & Template
├── scenecomponentio.h/.cpp # Any small shared deserialization adjustments
├── gameobject.cpp / runtime.cpp / animator.cpp / audio.cpp / camera.cpp / ...
│                           # Concrete Component::clone() overrides (minimal property copies)
└── CMakeLists.txt          # Register template.cpp + new runtime tests

editor/src/
├── sourcescanner.cpp       # Recognize TemplateAssetRef / AssetReference<Template> -> "Template"
├── assetformats.h          # asset_reference_extension_matches("Template", path)
├── resourceindex.h / .cpp  # ResourceKind::Template for .pttemplate asset resolution
├── filedialogs.cpp         # Template file filter in picker dialogs
└── panels/referencefields.cpp
                            # Map "Template" -> ResourceKind::Template

tests/ & editor/tests/
├── runtime_template_test.cpp         # NEW: prototype isolation, cloning, internal remap, external clear,
│                                     # cyclic templates, mid-step spawning, world/local placement, unload cleanup
└── templates_test.cpp / source scanner & reference tests
                                      # Editor source-scan, Inspector validation, save/duplicate/revert round-trip
```

**Structure Decision**: Extend Platformator's existing modular split directly (`include/platformator/` public headers, `src/` runtime implementation, `editor/src/` optional editor tooling, `tests/` + `editor/tests/` CTest targets) with zero new libraries or extra architectural layers.

## Complexity Tracking

*No Constitution violations — design strictly satisfies KISS (`§IV`).*
