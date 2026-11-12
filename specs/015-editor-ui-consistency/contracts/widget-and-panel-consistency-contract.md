# Contract: Widget, Menu, and Panel Consistency

**Feature**: `015-editor-ui-consistency`  
**Covers**: `FR-005` through `FR-018`, `SC-002`, `SC-003`, `SC-004`, `SC-005`

---

## 1. Deduplication Contract (`FR-005`–`FR-008`)

1. **Hierarchy Creation Button (`FR-005`)**:
   - `HierarchyPanel` header renders a single `+` button (`##hierarchy-create-button`) to open `##hierarchy-create-popup`. The adjacent `+ Object` button is removed.
2. **Scene Viewport Snap & Zoom Controls (`FR-006`)**:
   - `SceneViewport` top bar renders a single Snap settings popup trigger (`Snap: <step>`) obok/with the snap toggle, eliminating two side-by-side buttons that open `##scene-snap-settings`.
   - `SceneViewport` renders zoom percentage only in the top toolbar (`100%` reset button); the redundant bottom-left `"Zoom 100%"` canvas badge is removed.
3. **Single Inspector Feedback Output (`FR-007`)**:
   - `InspectorPanel` renders its `feedback_` string at most once at the bottom of `InspectorPanel::draw`; `editorapp.cpp` MUST NOT render `inspectorPanel.feedback()` a second time.
4. **Menu Bar Deduplication (`FR-008`)**:
   - `Preferences... (Ctrl+,)` appears only under `Edit`, not `Help`.
   - `Window` menu contains only panel toggles and layout management items (`Layout`, panel list, `Reset Layout`); duplicate `Build`, `Build and Run`, `Stop`, `Zoom In/Out`, and `Frame Selected/All` items are removed from `Window`.

---

## 2. Clean Information Density & Inline Workflows (`FR-009`–`FR-012`)

1. **Human-Readable Display Names (`FR-009`)**:
   - Visible text drawn in `HierarchyPanel` rows and `InspectorPanel` reference selector buttons strips the internal ` (#<id>)` suffix so users see clean names (`Player`, `Main Camera`).
   - Programmatic data structures (`HierarchyRow::label`, `SceneReferenceInfo::label`, and search filter strings) retain `(#<id>)` for disambiguation in dropdown lists, tooltips, and existing unit tests.
2. **Inspector Top Order & Tooltips (`FR-010`, `FR-012`)**:
   - Order at the top of single-object `InspectorPanel`:
     1. Search bar (with `×` clear button)
     2. GameObject identity header (`Active` checkbox, `Name` input, `Tag` input, `Layer`)
     3. `Transform` component (`Position`, `Scale`, `Rotation`)
     4. Compact `Object Template` row/section
     5. Built-in and Script components
   - The `Transform` header MUST NOT display `"Drop a behavior header anywhere in the Inspector to add it to this GameObject."`
   - Folder nodes in `ProjectPanel::draw_folder_tree` MUST NOT show a tooltip that merely repeats the visible folder label; asset tiles show relative project paths (`assets/...`) rather than raw OS absolute paths.
   - `RunSettingsPanel`'s `"Embed Game view in editor"` tooltip accurately states that gameplay keyboard and mouse input is captured when clicking the Game view.
3. **Inline Asset Rename & Confirmed Delete (`FR-011`)**:
   - Pressing `F2` or selecting `Rename` on an item in `ProjectPanel` activates an inline `ImGui::InputText` on that tile or list row (`Enter` or focus-loss commits; `Escape` cancels), removing `"Rename Asset##modal"`.
   - Interactive deletion in `ProjectPanel` (`Delete` key or context menu `Delete`) opens a confirmation popup before calling `execute_context_delete`.

---

## 3. Uniform Widgets, Multi-Object Headers, and UI Scaling (`FR-013`–`FR-018`)

1. **Numeric Formatting & Label Scrubbing (`FR-013`)**:
   - Float/double display across `InspectorPanel` (single and multi-object) and `BehaviorFieldsPanel` formats numbers cleanly without raw `%.9g`/`%.17g` precision noise, and supports horizontal drag scrubbing on field labels.
2. **Multi-Object Component Headers (`FR-014`)**:
   - `InspectorPanel::draw_multi_object_fields` renders each shared component inside an `ImGui::CollapsingHeader` with a right-aligned `⋮` menu button containing `Reset Settings for Selection`, `Copy Settings`, and `Paste Settings to Selection`.
3. **Compact Asset Reference Slot (`FR-015`)**:
   - `draw_asset_reference_field` renders a single-line slot (`Label` + `[Asset button / drop target]` + `[Clear]`) with right-click context menu (`Copy Path`, `Clear`), removing the separate `"Edit asset path"` tree node under each field.
4. **Proportional UI Scaling (`FR-016`)**:
   - All fixed pixel widths in `InspectorPanel`, `BehaviorFieldsPanel`, `referencefields.cpp`, and `SceneViewport` multiply by `uiScale`.
5. **Uniform Search Bars (`FR-017`)**:
   - Search inputs in `HierarchyPanel`, `ProjectPanel`, `InspectorPanel`, and `ConsolePanel` all provide an inline/adjacent clear (`×`) button when non-empty.
6. **Toolbar, Game View, and Console Polish (`FR-018`)**:
   - `MainToolbar` displays transform/scene actions on the left and unified playback controls (`Build and Run` / `Play`, `Pause`, `Step`, `Stop`) in the center without multi-row wrapping.
   - `GamePanel` places playback/aspect/scale controls in a single top bar and formats runtime object inspection cleanly without raw `component.dump()` JSON dumps.
   - `ConsolePanel` draws vector severity icons (`EditorIcon::Info`, `EditorIcon::Warning`, `EditorIcon::Error`) on filter badges and log rows.
