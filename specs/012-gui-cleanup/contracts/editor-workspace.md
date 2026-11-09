# Contract: Editor Workspace and Authoring

**Requirements**: FR-001–006, FR-015–017, FR-025–036, FR-043–044, FR-049, FR-052.

## Documents and targeting

A scene tab owns its lossless document, history/saved token, selection, hierarchy expansion and Scene view. An animation tab uses the corresponding asset document. All actions name `{projectGeneration, tabId, documentGeneration}`; the currently focused window is not an implicit substitute for a dialog's original document. Open of an already-open canonical path activates its tab. Save/close prompts name the document; Save All/close-project visits every dirty tab and a Cancel stops remaining closes without losing already completed saves.

Scene selection/order/property edits preserve existing numeric IDs and unknown payloads. Preferences, panel movement, runtime snapshots and editor-only hidden/locked flags do not dirty scene content. Start Run captures and validates the intended active scene; switching tabs afterwards cannot retarget the running process. Closing its source tab requires Stop or an explicit keep-running choice with immutable launch identity; project replacement stops owned play before replacement.

## Scene representation and mapping

- Every object has authoring metadata independent of a Sprite proxy. Cameras show boundaries, colliders show their shapes/offset handles, empty objects show a marker, and missing resources remain visibly unresolved/selectable. Inactive objects can still be edited; editor-only hidden/picking-disabled flags control authoring visibility/picking separately.
- Pick records identify object and optional component; ordering is deterministic and permits overlap cycling. Inspector/Hierarchy receive the same selected target. Frame Selected includes selected objects' relevant bounds and a minimum marker extent for empty objects.
- The editing image fills the Scene panel. Given logical rect `(100,50,360,250)` and pixel ratios `(2,3)`, content uses that rect and the render target is `720×750`; it does not letterbox to `360×180`. The camera's authored width/height describe the separate camera boundary, not a forced editing-panel aspect. World geometry is never stretched to fit.
- Canonical camera `width/height` and legacy `camera.w/h` use shared resolution/validation. Conflicting simultaneously supplied values remain visible errors. Reversible screen/world conversion uses actual image origin, independent framebuffer ratios and current view center/zoom.
- Wheel zoom keeps the pointer's world coordinate fixed. Frame/pan/zoom state is per tab and persists across tab switches; reload/replace resets explicitly. Resize and per-host DPI changes invalidate the target only when physical dimensions change.

## Tools and transactions

Move/Rotate/Scale are labeled, mutually selected modes with W/E/R. Rectangle sizing is an additional explicit mode for sprite/collider bounds. Move can use world/local axes; rotation/scale axes reflect actual local object semantics. Rotation displays degrees with labeled units and converts to existing serialized radians; no format migration. Position/angle/scale snap increments are positive finite preferences. Ctrl-drag snaps world placement for objects/collider centers and converts back to the intended local property; snapping uses the same origin/grid shown to the user.

Selection supports click, additive toggle, and visible-order range. Primary selection anchors common Inspector fields. Group fields are the compatible intersection, with Mixed values and explicit incompatible state. Batch moves act on selected top-level roots to avoid double-transforming descendants. Rotation/scale group pivot is the primary object's world origin by default; display this pivot. Preserve child translation-only inheritance and positive supported local scales; do not invent inherited parent rotation/scale.

Numeric/string editing uses a draft until valid completion or an existing gesture for continuous adjustments. One completed interaction is one transaction; Escape restores original accepted values, including absence. Group transactions prevalidate every member, retain exact before/after state, and publish one change; partial failures restore all members. A separate completed drag/entry cannot merge merely because it targets the same address.

Hierarchy reparent/reorder stores source and destination indexes plus local translation before/after. Reject descendant cycles; preserve world position by default. A multi-root move preserves original sibling relative order. Undo restores exact identities, parents/indexes, references, local values, and selection. F2/Delete/Ctrl+D route through the same validated commands as visible actions.

## Assignment, discovery, and setup

Hierarchy, assets, component/behavior palettes and reference selectors support name/type/path filtering; duplicate names include location/identity context. Search does not mutate documents or reorder serialized content. Selecting a result reveals its expansion/scroll position.

Typed/pasted, browse, drag/drop and double-click/Assign all use the same type/root/reference validator. Double-click assigns only when there is one unambiguous compatible primary selected component; otherwise show a target choice/reason without changing data. Full logical paths can be copied/edited, including unresolved references. Resolve symlinks with [asset containment rules](asset-changes.md). Missing behavior metadata explains whether Build/Refresh or runner integration is needed; source parsing does not authorize a compiled behavior.

Welcome/Open Project is available when restoration/CLI context is invalid. Discover presets through project CMake presets/File API metadata, browse executables, and permit advanced manual overrides. Custom integration/protocol settings remain advanced with explicit capability status. Current title/status shows project + scene/Untitled + dirty marker and updates on actual state changes. Ctrl+, and the action/shortcut reference remain synchronized with implemented commands.

## Diagnostics and Console

Validation is keyed by document/resource revisions and updated within the spec's three seconds of a committed change/settled asset event. It may debounce repeated edits but cannot indefinitely postpone updates. Explicit Validate/F6 and Run complete a current validation boundary; old result success is never a launch permission. Pending validation is labeled. Selecting an issue activates its tab, validates its generation, selects/reveals the object/component and Inspector field, or explains a stale target.

Console retains up to 1 MiB of exact raw output plus bounded derived rows. Selection/copy and Copy All operate on retained bytes; search/severity/collapse do not change those bytes. Clear removes retained display/log data explicitly. Auto-follow is optional and stops when the user scrolls away. The UI explains total retention and display truncation; all retained text is reachable. Parse common compiler `path:line[:column]` and Windows source-location forms; source opening uses an argument vector/editor association, never shell interpolation of log text. Unparsed locations remain copyable text.

External scene changes never reload a dirty tab automatically. Conflict UI offers explicit Reload/keep work/overwrite/Save As as applicable; failed/cancelled saves do not continue Run/close. File durability outcomes follow [desktop.md](desktop.md).

## Window, focus, and semantic behavior

Each host has independent ImGui/renderer state and intra-host docking. Detach/Dock to Main/Move to Window transfer panel presentation by stable panel ID; cross-host native tab dragging is not required. Document state is shared by stable IDs. Host close returns panels to the main host unless the whole application closes. Restore clamps/falls back to a reachable display; Wayland positioning is best-effort. Reset Layout changes only workspace state.

The focused host alone owns text/IME, cursor, gamepad navigation and global shortcut arbitration. Active fields, modal dialogs and gestures suppress unrelated Scene/global commands. Expose real item labels plus visible focus, descriptive tool names and state cues beyond color. UI scale 100/150/200% applies to text, icon geometry, hit targets and spacing; overflow is scrollable/reachable. Native semantic providers expose roles/names/values/actions as defined in [desktop.md](desktop.md), rather than claiming keyboard navigation is screen-reader support.

## Acceptance boundary

Meaningful regressions cover all-object/component picks and framing; camera format agreement; independent pixel ratios and pointer anchoring; group/structural atomicity; duplicate names/range filtering; focus on two hosts; per-tab dirty state/conflicts; keyboard-only primary actions; exact retained Console copying and stale diagnostic targeting. The old hierarchy fixture must validate row kinds/owner/parent/order against a separately constructed expected tree, not just substitute the observed ID vector. Projection tests must reach the later drag/coordinate cases after replacing the old letterbox assumption.
