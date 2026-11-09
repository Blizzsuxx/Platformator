# Data Model: GUI Cleanup

This model extends the current lossless document/history system. Fields below are design contracts, not a requirement to introduce a generic entity framework. Native UI/Runtime mutation remains on the application owner thread; immutable job results cross task/I/O boundaries.

## Identity and lifetime

| Identity | Meaning | Invalidated by |
| --- | --- | --- |
| `ProjectGeneration` | Monotonic application-session project/root incarnation | Root replacement/project switch/shutdown |
| `TabId` | Never-reused open-tab instance ID | Tab close |
| `DocumentGeneration` | Document replacement incarnation within a tab | Reopen/replace content; not ordinary edits |
| `StructureRevision`, `ValueRevision` | Authoring ordering/shape and committed value changes | Corresponding accepted mutations |
| `PathVersion` | Monotonic logical resource invalidation version, including notified same-metadata writes | Relevant change, alias retarget, recovery invalidation |
| `HostLayoutId` | Persistent panel/host arrangement identity stored across restarts | Explicit layout removal/reset |
| `WindowHostGeneration`, `RendererGeneration` | One native host/context and renderer incarnation | Window/context/device recreation/destruction |
| `PlayGeneration`, `RuntimeSerial`, handle slot/generation | One child session and live object incarnation | Stop/restart, runtime recreation, object retirement |

Every async result carries the narrowest complete identity tuple. `TabId + DocumentGeneration` is essential because existing workspaces independently begin at generation 1. No stored UI selection/diagnostic/play value contains a raw GameObject, JSON-node, SDL-window, or texture pointer after a frame boundary.

## Workspace and documents

**EditorApplication** owns main SDL/Runtime lifetime, event scheduler, `ProjectSession`, `WindowHost` map, focus arbiter and semantic publication. It has at most one authoring Runtime and one `PlaySession` child. **ProjectSession** owns roots, generation, change/resource/source/catalog services, `DocumentManager`, and per-project preferences.

**DocumentTab** is heap-stable: `tabId`, kind (`Scene`, `Animation`), canonical optional source path/identity, document generation/revisions, workspace or lossless asset root, saved token, history, ordered selection, active view/expanded hierarchy state, diagnostics, and external-change status. It owns its history before binding borrowed workspace references. Closing a tab cancels its jobs and prompts for its own dirty content. Only the active scene materializes Runtime proxies; other tabs retain metadata/view state. Opening the same canonical scene activates its tab instead of creating conflicting writable copies.

**SelectionSet** stores ordered unique selected object IDs, primary object ID, optional primary component/behavior, and focused host/panel. Repair removes missing members, validates component ownership, and picks the next surviving primary. Component-specific mode cannot imply that an unrelated component on every selected object is selected. A group operation uses selected top-level roots to avoid applying ancestor motion twice.

**WorkspacePreferences v2** stores project key, open-tab paths/active identity, panel→HostLayoutId assignments, per-host geometry and a distinct ImGui docking/settings blob per HostLayoutId, user scale, panel visibility, per-tab view and hierarchy expansion, scene-keyed editor-only hidden/locked IDs, run settings, snap increments/coordinate mode, duplication policy and resource limits. Migrate v1 recentScene/main-window settings without losing existing run choices. Hidden/locked IDs are document-scoped; prune/repair on deletion and changed scene identity. Preferences never enter scene files or scene history. Session history is not serialized.

## Commands and addresses

**PropertyAddress** becomes `{projectGeneration, tabId, documentGeneration, objectId?, componentId?, behaviorOrdinal?, fieldPath}`; asset addresses use a typed document path without pretending to be scene objects. IDs/type discriminators are not ordinary editable fields.

**CommandTransaction** retains a label, before/after selection, and one ordered list of property/insert/delete/move operations. Property values distinguish absent, null, zero and empty. Prevalidate all destinations and allocate retained payload before publishing mutation. A transaction either commits all operations once or restores the exact prior state. It publishes one deduplicated affected-object/path set with dirty categories. Undo/Redo consumes the same transaction in reverse/forward order; saved token follows the existing history model and budget pruning rule.

**MoveOperation** retains source/destination parent IDs and sibling indexes plus old/new local translation. Validate no cycle or duplicate placement; normalize same-parent indexes after removal. New local position = old world translation − destination parent world translation. Store the original positions for exact undo, rather than recomputing from later state. Rotation/scale remain local under current engine inheritance.

**EditDraft/Gesture** holds one tab/address set, original values, validated drafts, interaction token and state (`Editing → Committed | Cancelled`). Draft-only invalid text cannot authorize save/run; completing a valid gesture emits one transaction, and unchanged/cancelled gestures emit none. Structural changes that invalidate addresses cancel or finish the gesture explicitly before proceeding.

## Authoring and render state

**AuthoringRecord** stores object ID, owning/parent IDs, order, local pose/active, accumulated translation, optional camera/collider/sprite descriptors and component IDs, marker/bounds, and field/resource revisions. It covers all objects; it never stores a recursive child JSON value. Descriptors retain only their own inputs. An unknown/missing resource yields a selectable marker, not a substitute serialized asset.

**DocumentDelta** stores identity/revisions, structural flag, changed object IDs/asset paths, and categories (`Label`, `Pose`, `Active`, `Sprite`, `Collider`, `Camera`, `BehaviorReference`, `Structure`). Descendant propagation is required only for inherited effects; reverse references may invalidate non-descendant diagnostics.

**AuthoringResourceBudget** admits at most 256 MiB of retained proxy texture versions and a 64 MiB main Scene target before allocation; pending/limited resources remain selectable markers, and source dimensions/rectangle mapping stay authoritative.

**SceneImageKey** stores active tab/generation, pose/render/resource revisions, view center/zoom/rect, target pixel dimensions and renderer generation. Equal keys reuse the cached Scene image. Selection/hover may invalidate overlays only. Redraw of a changed Scene image preserves original sprite order. View bounds reject irrelevant sprites conservatively without changing that order.

**UiTextureResource** is a stable logical ID + version + prepared CPU pixels/source descriptor + renderer-local replicas + frame pins + LRU accounting. Renderer replacement drops only its replicas. SDL_Texture pointers are never reused across renderers. A frame pin outlives draw submission/present. Scene readback replicas are invalidated on source-image change, not every host repaint.

## Change service and resources

**ChangeRecord** stores project generation, watched root ID, normalized relative path, kind, optional old path/rename cookie, backend sequence, and coverage-loss flags. **ChangeBatch** coalesces paths but retains uncertainty/root-dirty state and a subtree uncertainty epoch. **DirectorySnapshot** carries directory invalidation revision and request ID; late mismatched results cannot overwrite current listings. Filenames are re-resolved on application; records are hints, not trusted current filesystem facts.

**CoverageState** transitions `Starting → Reconciling → Current`; loss/error moves to `Degraded/RootUnavailable → Reconciling`, or `Stopped` at teardown. A failed/truncated snapshot cannot replace a complete snapshot or silently become Current. Coverage is tracked per root/subtree so visible folders remain refreshable even if global discovery is incomplete. The only completion transition to Current requires initial snapshot plus replay/reconciliation of intervening events.

**ResourceEntry** stores logical asset path, canonical contained target, type, file identity, path/content version, alias links, last-good data, error/retry deadline, and reverse dependents. Consumers include directory snapshots, thumbnails, animation descriptions/frame images, active proxies, selected references, diagnostics and source/catalog identity. Alias target resolution validates containment and cycles at discovery and use. Broken links remain entries with unresolved state.

**ReloadState** transitions `Ready → Dirty → Preparing → AwaitingOwnerCommit → Ready`, with `Preparing → RetryPending → Preparing` for temporary writes and `Failed` after bounded exhaustion. Newer content/project generations supersede old jobs. Failure keeps last-good data distinct from current validity. A commit atomically rebinds all affected retained resource references at an idle boundary; obsolete wrappers are freed only when owners release them.

**SourceIdentity** combines configured source/CMake inputs and metadata identity with event/uncertainty revisions and a requires-forced-rebuild flag. This flag handles timestamp-driven build reuse after a delivered/uncertain same-metadata change and clears only after a successful forced explicit build. A notified same-metadata source write changes the effective identity and stales the catalog/pipeline. Before Build/Run, verify the latest source generation and snapshot; changes during build prevent stale catalog launch.

Bounds: 4,096 watches; 8,192 coalesced paths/2 MiB; original full-scan 50,000 files/100,000 entries; targeted listing 10,000 files/2,000 directories/20,000 entries; at most 512 fallback reference path-stat units per two-second interval; one recovery scan per root/two pending targeted scans; 256 path commits or 2 ms per frame; one decode/64 pending paths/8 MiB completed thumbnails; admitted image ≤64 MiB encoded, ≤8,192 per dimension and ≤16,777,216 pixels with an inclusive 128 MiB decoder working set (≤64 MiB decoded + ≤64 MiB scratch/input staging); 256px/64 MiB/256-entry thumbnail cache; 1 MiB/512-frame animation previews. Every exhaustion state is explicit. Full authoring documents remain lossless even when a preview exceeds its admission budget.

## Diagnostics and semantic controls

**Diagnostic** stores stable row ID/key, category/severity, message, repeat count, time, optional typed document address, optional source path/line/column, and blocksRun. Validation inputs are immutable affected-local descriptors/reference dependencies, prepared without copying the recursive whole scene per edit. Explicit Validate/Run establishes a quiescent latest-revision barrier with bounded staged work while servicing UI/cancellation. Validation result carries input document/resource revisions; a stale result may display as pending but cannot authorize Run. The bounded Console keeps exact raw retained bytes separately from filtered/collapsed rows. Copy All exports retained bytes, and truncation has an explicit marker/status.

**SemanticNode** stores stable host/document/control ID, role, accessible name/help, value/range, enabled/selected/expanded/focused/error states, bounds, parent/visible children, and supported actions. Native providers acquire shared/ref-counted immutable per-host snapshots, tagged with HostLayoutId and WindowHostGeneration, so a callback keeps its data alive across publication/recreation. Unregister/drain callbacks before host destruction; reject actions targeting retired generations. Actions are queued to the owner thread with current identities and normal validation/input rules. Publish deltas for visible controls plus the focused/reveal path, not a fresh full-scene accessibility tree per frame. Window/context changes retire old nodes safely.

## Templates, asset editing, and play

**TemplateAsset v1** contains a lossless subtree, metadata version/name, and typed reference bindings (relative object/component/field path, expected target kind, internal/external classification, with source-scene identity/original target kind/ID for externals). IDs inside the template are local source identities. Instantiation allocates a complete old→new identity map before insertion; remaps declared internal references and validates explicit externals against their source-scene identity or an explicit destination assignment, never integer/type coincidence. Opaque integer fields are preserved and never guessed; self-contained remapping requires current metadata or explicit bindings for unavailable reference schemas.

**AnimationDocument** retains source identity, unknown JSON, ordered frames, clip FPS/loop/size/name, selected frame, preview time, history/saved token and pending image dimensions. A frame uses the existing texture reference, optional source rectangle and duration. Positive duration overrides FPS; zero means the existing FPS fallback. Scrubbing/playback changes view state only. Sprite-sheet region drafts commit into selected Sprite fields or an animation frame; no new runtime sprite format is introduced.

**PlaySession** stores immutable launch-tab/project/source/binary identity, launch TabId/DocumentGeneration/saved token and file identity, process tree owner, session nonce/generation, protocol/capabilities, command/event queues, shared frame mapping/slots, runtime serial, latest step number and selected inspection handle. States: `Idle → Configuring → Building → CatalogCheck → Starting → Running ↔ Paused → Stopping → Idle`; any stage may enter `Failed` without losing authoring state. Unsupported features remain explicitly unavailable with external-window Run offered. A source change marks the session Stale and offers controlled restart.

**FrameSlot** stores sequence/slot ownership, width/height/stride/format, runtime serial and payload. Two 640×360 RGBA8 slots have 1,843,200 payload bytes total. Producer writes only a free slot, publishes a ready event after completing bytes, and cannot reuse it until parent acknowledgement. Parent releases only after copying newest complete pixels; stale events do not acquire a slot. Stop child before unmapping. Snapshot/inspection references combine runtime serial with slot/generation; file IDs alone cannot identify a live recreated object.
