# US8 Component reuse evidence

**Status:** In progress. Component settings, version-1 template validation/core instantiation, author declarations, the saved duplication-reference policy, and the save/browse controls have focused Linux headless coverage. Semantic provider registration, interactive desktop walkthroughs, and complete US8 acceptance remain open.

## Component settings continuation T109/T110/T189/T232 (2026-11-06)

The Inspector now offers Copy Settings, Paste Settings, and Reset Settings. Built-in components copy only fields declared by their known defaults; Script components copy only fields declared by the current behavior catalog, and paste requires the same component type and matching Script behavior types/order. Operations apply supported values through one targeted batch history command. Component IDs, kind discriminators, and unrelated unknown target data are preserved. Reset uses built-in defaults and removes declared Script overrides when the catalog supplies no explicit constructor default. It does not instantiate behaviors or invent values. Stale components and unavailable behavior schemas produce an Inspector error without mutating the document.

The registered `platformator_editor_component_operations_test` covers identity exclusion, nested and top-level unknown data preservation, type mismatch rejection, built-in defaults, catalog-declared Script fields, missing Script override removal, one-command Undo/Redo, multi-object group Paste/Reset with one Undo, and injected history-retention failure rollback. The existing Inspector interaction suite also passes after the controls were added.

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_component_operations_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_component_operations_test$'
cmake --build bin/editor-debug --target platformator_editor_inspector_interaction_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^platformator_editor_inspector_interaction_test$'
git diff --check
```

Outcome: editor and focused test builds passed; component operations passed 1/1 and Inspector interactions passed 1/1. This is Linux headless coverage. US8 template, binding, duplicate-policy, and cross-scene identity work has not been completed.

## Template binding, instantiation, and duplication policy T108/T111/T113/T114 (2026-11-06)

The version-1 `platformator.object-template` envelope preserves unknown envelope and subtree fields, rejects duplicate object/component identities and invalid JSON-pointer bindings, and validates the target kind at each declared site. Internal targets must be included in the subtree. External targets carry source-scene identity and matching original kind/ID; external numeric collisions in another scene do not establish identity. The model states: “IDs inside the template are local source identities.” Unbound integers remain opaque.

Reference collection reads current compiled behavior descriptors and supports explicit author declarations for unavailable schemas. It reports unresolved behavior schemas and produces an assurance token tied to the exact subtree and binding list; insertion rejects incomplete or mismatched coverage. Instantiation allocates one complete object/component ID map, remaps only declared internal sites, requires explicit external assignments across scene identities, and inserts the cloned subtree as one retained structural history command. Injected history-retention failure leaves the destination untouched; Undo and Redo preserve the exact allocated IDs and payload.

Hierarchy duplication keeps `PreserveExistingTargets` as the default and persists the selected policy. `RemapInternalTargets` uses one mapping across all selected roots, including references among those roots, while unknown values and external targets remain unchanged. The Hierarchy shows the active policy and explains its effect. Remapping is blocked when typed reference coverage is unavailable.

```text
cmake --build build --target platformator_editor_template_test platformator_editor_hierarchy_move_test platformator_editor -j8
ctest --test-dir build --output-on-failure -R '^(platformator_editor_template_test|platformator_editor_hierarchy_move_test)$'
```

Outcome: editor and focused test builds passed; template/binding/insertion/duplicate tests and hierarchy movement passed 2/2. Cases cover unsupported versions, malformed and duplicate identities, self/descendant/component/cross-root/external/opaque sites, unavailable schemas, source-scene identity, destination ID collision, unique allocation, cross-scene assignment, injected history-retention failure, and exact one-step Undo/Redo IDs. This is Linux headless evidence. Still open: template save/browse/instantiate controls, conflict-safe template file integration, game-scene roundtrip acceptance, and native Windows/macOS verification.

## Opaque behavior reference authoring T112 (2026-11-07)

The Inspector now exposes explicit target-kind declarations for opaque integer fields and JSON-pointer paths inside opaque objects and arrays. Nested paths use escaped JSON Pointer segments and canonical array indices; the control only accepts a path that resolves in the current behavior value to a non-negative integer representable by the scene ID type. Existing nested declarations can be retagged or removed. Completeness attestation remains scoped to the exact project generation, tab, document generation, object, Script component, behavior ordinal, and behavior type. The Hierarchy passes only declarations belonging to the selected subtree into collection. An unavailable schema without an exact completeness attestation leaves collection unassured, and external targets still require explicit assignment when instantiated into a different scene identity.

Added regression coverage for escaped top-level and nested object keys, an array index, external target identity, unavailable metadata, and exact declaration scope. Build and focused run:

```text
cmake --build build --target platformator_editor_template_test platformator_editor_inspector_interaction_test platformator_editor_hierarchy_move_test platformator_editor -j8
ctest --test-dir build -R '^(platformator_editor_template_test|platformator_editor_inspector_interaction_test|platformator_editor_hierarchy_move_test)$' --output-on-failure
```

Outcome: build succeeded and all 3 focused Linux headless tests passed. No native Windows/macOS verification was run.

## Template save, browse, and ordinary scene round-trip T115/T116 (2026-11-07)

The Inspector can save the selected object subtree as a `.pttemplate` under `assets/templates/`. It collects and validates the current compiled/author-declared typed reference sites first, then uses the native atomic writer in create-only mode; a name collision is reported and never overwrites a file. The Project browser already searches visible asset paths and now offers bounded `.pttemplate` loading from files canonically contained under the asset root. Loading validates the version-1 envelope and persisted binding contract. Cross-scene external sites each require a compatible destination object/component before Instantiate is enabled; same-scene targets are retained only when the saved scene identity and target kind still match. Insertion remains one structural history command, with the new root selected afterward.

The template suite now covers create-only atomic save/conflict preservation, persisted binding-coverage revalidation, external identity assignment, one-step Undo/Redo, serialize/reopen through the ordinary scene array format, and loading an instantiated template scene through a headless `platformator::Runtime`. `platformator_editor_template_test` was already registered in `editor/tests/CMakeLists.txt`; focused build/run:

```text
cmake --build build --target platformator_editor_template_test platformator_editor_workspace_test platformator_editor_inspector_interaction_test platformator_editor -j8
ctest --test-dir build -R '^(platformator_editor_template_test|platformator_editor_workspace_test|platformator_editor_inspector_interaction_test)$' --output-on-failure
git diff --check
```

Outcome: targets built, all 3 focused Linux headless tests passed, and `git diff --check` passed. The template suite's headless Runtime fixture loaded both the instantiated root and child using their allocated destination identities. No interactive desktop walkthrough or native Windows/macOS verification was run. T115 remains open for semantic provider registration and interactive evidence; T116's registered headless round-trip, identity, atomic-save and history cases passed. T190/T233 template format, binding, Project/Inspector workflow, history, cross-scene assignment, and ordinary game consumption are implemented; semantic-provider registration remains tracked under T115/T107.

## Duplication policy convergence follow-up T191/T234 (2026-11-07)

The convergence follow-up now has explicit coverage for preserving a selected peer and a target outside the copied selection under the compatibility default; `RemapInternalTargets` maps self, descendant, component, and selected cross-root targets while leaving the external target and opaque integer unchanged. Undo restores the original scene in one command and Redo restores the exact copied identities and references. A legacy version-1 project preference without a policy field migrates to `preserve-existing-targets`; the current selected policy still round-trips through the workspace suite. Hierarchy shows the active policy and explains the target behavior.

```text
cmake --build build --target platformator_editor_template_test platformator_editor_workspace_test -j8
ctest --test-dir build -R '^(platformator_editor_template_test|platformator_editor_workspace_test)$' --output-on-failure
git diff --check
```

Outcome: build succeeded, tests passed 2/2, and the diff check passed. This is Linux headless coverage; no native Windows/macOS verification was run.

## T115 template/component workflow recheck (2026-11-07)

At this checkpoint, template/component controls were implemented but T115 remained open because semantic provider registration was not yet wired for these Inspector/Project controls. The later `T115 semantic template and component actions` section records that registration and closes the implementation task; T107's native-provider and desktop acceptance remains pending.

```text
cmake --build build --target platformator_editor_template_test platformator_editor_component_operations_test platformator_editor_workspace_test platformator_editor_resources_test -j8
ctest --test-dir build -R '^platformator_editor_(template|component_operations|workspace|resources)_test$' --output-on-failure
git diff --check
```

Outcome: build succeeded and the four focused Linux headless suites passed 4/4. No semantic-provider or native desktop accessibility result is claimed.

## T115 semantic template and component actions (2026-11-07)

The Inspector semantic snapshot now exposes stable actions to save the selected GameObject as a template and to copy, paste, and reset each component's supported settings. Dispatch checks the current document/object/component identity and uses the same atomic save or history-backed operation as the visual controls. The Project semantic snapshot exposes Load Template and Instantiate Template actions plus bounded destination-target choices for declared external references. Dispatch validates the project generation, document revisions, selected template, and current destination identities before changing state; instantiation enters the existing one-command history path.

Regressions cover semantic Inspector save/copy/paste/reset, Project template load/instantiate, explicit cross-scene target assignment despite a colliding numeric ID, and rejection of a stale instantiate action after a structural edit. Focused Linux headless results:

```text
cmake --build bin/editor-debug --target platformator_editor platformator_editor_workspace_test platformator_editor_inspector_interaction_test -j2
ctest --test-dir bin/editor-debug --output-on-failure -R '^(platformator_editor_(workspace|inspector_interaction)_test)$'
git diff --check && git diff --cached --check
```

Outcome: build succeeded and both focused suites passed 2/2. The semantic action registration gap is implemented. T115 remains open because its T107 dependency includes native provider and desktop acceptance that has not been completed; no Windows/macOS or assistive-technology walkthrough is claimed.

## Template and component controls current Linux rerun (2026-11-07)

Rebuilt and reran template, component-operation, Workspace/Project, and resource suites; CTest passed 4/4. Template tests cover create-only atomic save/conflict preservation, bounded browsing/loading, semantic save/load/instantiate actions, typed external assignment, stale action rejection, ordinary scene round-trip and exact Undo/Redo. Component tests cover semantic Copy/Paste/Reset through history-backed operations. T115's implementation is complete and checked; T107's native-provider and desktop acceptance remains open, and no assistive-technology or Windows/macOS walkthrough is claimed.

```text
cmake --build build --target platformator_editor_template_test platformator_editor_component_operations_test platformator_editor_workspace_test platformator_editor_resources_test -j2
ctest --test-dir build --output-on-failure -R '^platformator_editor_(template|component_operations|workspace|resources)_test$'
```
