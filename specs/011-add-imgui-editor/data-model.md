# Data Model: Dear ImGui Editor

All types below are proposed editor concepts, not a replacement serialized scene schema. Runtime pointers/handles never enter saved documents or command addresses.

## AuthoringDocument

| Field | Meaning |
| --- | --- |
| `root` | Authoritative nlohmann JSON array of game objects, including unknown keys |
| `sourcePath`, `projectRoot`, `assetRoot` | Explicit document/project locations; source may be absent for a new scene |
| `documentGeneration` | New identity on document/project replacement; rejects delayed work from prior contexts |
| `structureRevision`, `valueRevision` | Incremented on committed topology/value changes; provisional gestures also notify affected preview values |
| `objectsById`, `componentsById` | Derived ID-to-location/owner/kind indexes, rebuilt after structural edits |
| `parentById`, `orderedObjectIds`, `orderedSpriteIds` | Derived hierarchy and order; checked against engine fixture order, not unordered-map iteration |
| `validationIssues` | ID/address/severity/message; includes missing resources and unavailable behaviors |

Parse into a temporary document, validate structure/IDs, then swap only after open succeeds and unsaved-work resolution permits it. Reject non-array roots, malformed object/component structure, duplicate/negative/out-of-range IDs, duplicate incompatible component slots, and invalid hierarchy representation. File ID zero is valid: the runtime loader and legacy model both accept it, and existing scene fixtures use it. Preserve valid unfamiliar components/fields without instantiating them. JSON tree structure cannot form parent cycles; any derived indexes must also enforce acyclic ownership.

Use engine numeric constraints for committed known fields: finite transforms/vectors/scalars, positive scales and collider dimensions, finite offsets, valid mass/body-kind rules, nonnegative friction, restitution in [0,1], valid enums/masks, and current camera/sprite/animator/audio constraints. Catalogs do not invent game-specific validation. Imported recoverable property/resource errors remain editable and block Run where the runtime would reject them; structural errors reject document adoption. No silent clamping or reference repair on open. Hierarchy/index visitors should use explicit traversal storage for deep input rather than unbounded native call-stack recursion.

Legacy camera aliases are preserved on no-edit save. An explicit edit may materialize the current canonical width/height form with a documented normalization action; unknown neighboring fields remain untouched. Report conflicting legacy/canonical values instead of guessing. Known asset separators may normalize according to existing tests; no broad rewriting of arbitrary strings.

## PropertyAddress and Selection

`PropertyAddress` contains document generation, object ID, optional component ID, optional behavior ordinal, and a relative JSON field path. Resolve against current indexes at every operation; do not retain DOM pointers across commands. Nested/list array positions are local to a transaction and invalidated by structural edits. Behavior lists do not gain serialized editor IDs.

`Selection` contains object ID and optional owned component ID, plus focused panel. Missing object clears selection; missing component falls back to its surviving owner. Selection-only changes do not dirty the scene. Undo/redo commands may restore their recorded selection if still valid.

An object reference is an integer file ID or null, with expected target kind from metadata. An asset reference is a case-preserving `assets/...` string or null/unset as supported by that field. Unknown integers/strings are not inferred to be references. Deleting a target leaves dangling serialized values visibly unresolved rather than rebinding or silently clearing them.

## EditTransaction and History

| Kind | Retained data | Work |
| --- | --- | --- |
| Property/field assignment | Address, before/after JSON value or missing-key sentinel, before/after selection | Changed payload plus path lookup |
| Add/delete component or behavior | Owner address, insertion index, affected payload, selection | Affected payload; rebuild structural indexes |
| Create/delete/duplicate object subtree | Parent/sibling insertion position, affected subtree, allocated IDs, selection | Affected subtree plus topology/order rebuild |
| Continuous gesture | Initial value and evolving final value at one address or small fixed address set | No document snapshot per update |

History has ordered commands, a cursor, monotonic state tokens, optional saved token, retained-payload bytes, and optional active gesture. Dirty means active changed gesture or current token differs from the reachable saved token. This avoids comparing/serializing the full document on each input.

Transitions: `Idle -> Begin -> Updating -> Commit -> Idle` or `Cancel -> Idle`. Commit equal to the initial value creates no command. Cancel restores exact initial values/missing keys and emits preview updates. Saving/opening/history navigation first resolves the current gesture. Separate gestures never merge by object/control identity alone.

Undo/redo validates addresses and command preconditions before changing any payload; a stale/corrupt command reports an error without partial mutation. New edits after Undo discard redo, invalidating a saved token on that discarded branch. Prune oldest completed history beyond a 64 MiB payload budget while retaining the newest single oversized command; display the available history limit. An unreachable saved state stays dirty until the next successful save. View/layout/navigation operations are not commands.

Duplication reassigns copied object/component IDs without collisions, inserts immediately after the source in the same parent, gives the copied root a unique name and (+32,+32) local offset, and preserves all copied numeric reference values. Record this legacy behavior in UI/help and test it explicitly.

## ProjectContext and CatalogSnapshot

Project context owns canonical project/asset roots, selected configure/build presets, target, executable location, explicit run options, source-input identity, and a generation. No single presumed `bin/main` path is hardcoded; changing targets changes catalog identity and next launch.

`catalogProtocol` is `common-runner-v1` or `unavailable`; bundled shared-runner targets default to the former, other entries to the latter until explicit project declaration. Unavailable protocol disables query/automatic Run but not editing or Build-only. Changing this declaration invalidates catalog identity; declaration alone never certifies behavior registration or authorizes rewriting game source.

Catalog snapshot contains schema version, binary/build/source-input identity, descriptor list, observation time, and status (`Unavailable`, `Stale`, `Current`). Each behavior distinguishes registered availability from metadata completeness. Scene-only types and cached field samples remain unverified. Descriptor names/types are immutable snapshots; receiving a catalog never edits scene payloads.

Refresh source-input identity through explicit Refresh/Build and asynchronous change scans of configured source inputs/CMake files, not a full source walk on every frame. A detected change marks Current stale. For a declared compatible target, Run always configures/builds and queries the resulting binary again before availability checks and launch.

## PreviewScene and ViewportState

Preview owns a document-ID/component-ID to proxy mapping, explicit authoring draw order, retained texture/animation references, unresolved resource records, and document/root generation. Proxies contain only flattened root GameObjects with Sprite. Their IDs are runtime-owned and never substitute for document file IDs.

Viewport contains editor-only world origin, zoom, Scene image rectangle in logical coordinates, framebuffer pixel extent, selected handles, and active drag. Parent translation is accumulated, while scale/rotation follow the existing engine's local-only inheritance. Active-object treatment follows the engine rather than assuming Unity's hierarchy activation. Static animator preview reads the selected frame and dimensions/source rectangle without advancing playback.

One target texture belongs to the Runtime renderer. Release texture/proxies/references before that renderer/runtime; remove borrowed pointers from ordered submission before destroying proxies. On root change, release the old preview/Runtime and rebuild at a frame boundary; authoring JSON/history remain intact. Zero dimensions suspend preview allocation/drawing; growing/resizing replaces the target only after safe release of prior frame references.

## BuildRunOperation

States: `Idle -> Configuring -> Building -> QueryingCatalog -> Launching -> Running -> Idle`. Build-only ends after Building. Errors transition through `Failed` to operable Idle with outcome retained. `Stopping` clears all queued stages, terminates the owned process group, drains/reaps, then returns to Idle. No overlapping operation or stale completion may advance a newer operation.

Fields: operation generation, immutable scene/project/settings snapshot, argv/cwd per stage, PID/process-group ownership, nonblocking pipe, exit status, cancellation deadline, pending bytes, and Console ring. Catalog payloads have a separate 4 MiB limit and timeout (10 seconds); Console retains 1 MiB with an explicit truncation marker. Processing per frame is bounded independently of retained bytes. Child failures never launch an old executable.

## Workspace and AsyncRequest

Workspace preference schema stores project key, version, recent successful scene, panel visibility/layout, run options, display/user scale, and navigation preferences outside scene JSON. Writes use atomic replacement. Bad preference data resets affected preferences, not the document. Stable panel IDs decouple layout identity from visible labels.

File-dialog requests own filter/default-location storage and a callback context containing shared completion state plus request/session/document generations. Callbacks copy paths/errors and release their request context; they do not access SDL objects, panels, or documents. Main-thread consumption ignores superseded/shutdown requests. Pending callback state must remain valid beyond EditorApp teardown if the native dialog completes late.

Asset/source scans use oneTBB task-group work with immutable inputs/results, cancellation, bounded pending publications, and a generation. Main thread is the only consumer/mutator. Task completion precedes owner destruction; no worker retains a DOM or Runtime pointer.
