# Contract: Opt-in Game Session v1

**Requirements**: FR-015, FR-037–040, FR-044–047, FR-050–052.

## Compatibility and launch

Keep `--editor-catalog` exclusive and side-effect-free before Runtime construction. Catalog schema v1 gains optional validated capabilities, for example:

```json
{
  "schema_version": 1,
  "behaviors": [],
  "editor_capabilities": {
    "session_protocol": "platformator-editor-session-v1",
    "play_control": true,
    "runtime_inspection": true,
    "rgba_frame_stream": {"version": 1, "width": 640, "height": 360, "max_hz": 15},
    "live_asset_reload": false,
    "input_forwarding": false
  }
}
```

Preserve the existing catalog envelope/behavior semantics; validate capability types/versions/limits and bind results to project/source/binary identity. Missing fields mean unsupported, not guessed support. A proposed `PLATFORMATOR_ENABLE_EDITOR_SESSION` build option defaults OFF for runtime/SDK builds; compatible development runners advertise only compiled capabilities. Editor development Build can enable it for stock shared-runner targets. Production/debug-tools settings cannot imply an unavailable capability.

The editor launches `--editor-session-v1` only after validated capability negotiation, with ordinary explicit `--scene`/`--asset-root` plus private inherited channel descriptors. In embedded mode explicitly override hidden-window render size to 640×360, query/assert actual renderer output size before capture, and fail the frame-stream capability if it differs; external mode retains user-configured dimensions. Convert captured pixels to the exact RGBA byte order and stride below before publication. The session entry validates protocol arguments/descriptors before creating exactly one child Runtime. Custom incompatible entries stay editable/buildable and use existing external-window Run where catalog/run compatibility exists. Session-only integration does not add ImGui or native accessibility dependencies to the engine.

## Transport, limits, and message identity

Use separate parent→child command and child→parent event byte channels and a two-slot shared-memory pixel mapping. stdout/stderr remain logs and arbitrary game output cannot corrupt protocol parsing. Each JSON message is prefixed by a little-endian 32-bit byte length; requests ≤64 KiB, events ≤256 KiB. Reject malformed/oversized/truncated frames before allocating their stated size. Keep at most 64 queued requests / 1 MiB and 64 queued events / 1 MiB, reserving 16 KiB of event capacity for reliable control/state acknowledgements. At most one paged inspection response is in flight; excess optional requests are rejected/deferred before acceptance. Stop or explicitly fail a corrupted session; do not block the UI while recovering.

Every message carries protocol version, private session nonce, play generation and request/event sequence; runtime events additionally carry runtime serial. Session launch identity includes TabId, DocumentGeneration, saved history token and saved file identity so a reopened same-path tab cannot receive old state. Validate them against the owned process and current immutable launch-tab identity. Never accept a foreign/reused session through a guessed PID. Parent creates two channel pairs and the mapping. A versioned launch descriptor names `command_read`, `event_write` and `frame_mapping` as explicit child fd/HANDLE values: POSIX spawn actions map them to reserved child descriptors 3/4/5 (first move conflicting parent endpoints above that range); Windows uses a STARTUPINFOEX handle whitelist of only those plus required log handles. Close unused ends on both sides immediately, clear inheritance/CLOEXEC appropriately in the child before any game-created subprocess, and close/unmap every owned endpoint exactly once after cancellation/reaping. Channels are inherited local handles with explicit OS ownership; no network listener or stdout command parser.

Representative commands: `hello`, `pause`, `resume`, `step`, `set_capture`, `release_frame`, `list_objects`, `inspect_object`, `reload_assets` (only if advertised), `stop`. Representative events: `ready`, `capabilities`, `state`, `step_completed`, `object_page`, `object_snapshot`, `frame_ready`, `reload_result`, `error`, `stopped`. Requests have monotonic IDs; each accepted Step produces exactly one completion. Duplicate/replayed IDs do not execute twice. Inspection pages include a snapshot revision; a changed hierarchy invalidates continuation so pages never mix incompatible snapshots.

## Runtime ownership and stepping

All pipe reads/writes are nonblocking or owned by cancellable native I/O dispatchers; UI/Runtime threads enqueue bounded records only and never wait for peer pipe capacity. Full frame-notification queues skip capture and retain/release slot ownership explicitly; reliable controls/step acknowledgements require reserved queue capacity, otherwise reject excess controls before acceptance or fail the session visibly. Do not silently drop an accepted Step completion. Native I/O only validates/buffers messages. Drain commands and produce snapshots at Runtime owner-thread idle/frame boundaries. Pause acknowledges only after simulation has reached a safe boundary; after acknowledgement simulation-step number remains fixed until Step/Resume. Step is accepted while Paused, advances one existing `FIXED_TIME_STEP` (1/120 second currently), runs normal callbacks/physics/mutation handling, renders once if requested, then acknowledges the new step number. Resume resets paused wall-clock debt before using the existing bounded fixed-step clock. Implement remote pause/step state under the session build option independently of debug-only F5/F6 hotkeys, including Release session builds. Do not advertise play_control unless the compiled owner-thread clock controls work in that configuration. Do not replace physics/clock semantics or accumulate catch-up while paused.

Inspection uses runtime serial plus handle slot/generation and expected object kind, not pointers/file IDs alone. Provide paged hierarchy summaries and selected built-in properties on demand. Object pages include display/file ID plus handle slot/generation/kind; inspect requests must carry the latter tuple and runtime serial, never a file ID alone. Add an opt-in reader-provider generated by the existing with-fields registration macro for declared allowlisted scalar/vector/enum/reference members. Invoke it on demand on the owner thread with bounded field output; convert references to target identity/handle summaries without exposing pointers or retaining objects. Declared script fields require these explicitly safe generated/read-only accessors; factory-only/opaque unavailable fields are labeled rather than invoking arbitrary behavior serialization every frame. Limit each snapshot/page to event size; long values are explicitly summarized with a bounded follow-up path. No snapshot writes authoring DOM, history, source files or saved tokens. Destroyed/recreated objects invalidate the previous handle and visible snapshot.

## Game image ownership

Embedded mode creates a hidden child rendering window and uses its normal SDL renderer. Capture before SDL_RenderPresent, on the child owner thread, only when at least one Game view is visible and at most 15 Hz. Payload is 640×360 RGBA8, 2,560-byte stride, 921,600 bytes per slot; two slots total 1,843,200 payload bytes. UI displays aspect-preserving output with resolution/capture status. These limits bound a documented slow readback path; they are not an fps/smoothness guarantee.

Slot ownership follows `Free → ChildWriting → ParentReady → ParentCopying → Free`, coordinated by ready/release messages and platform memory-visibility primitives. The child never modifies a published slot before acknowledgement. If both slots are occupied, skip capture rather than block simulation. Parent copies a complete newest frame to its own prepared pixels/texture, then releases the slot; it may discard an older complete frame with acknowledgement. No SDL_Texture pointer crosses process or renderer boundaries. A lost/invalid slot message fails the session rather than risking torn/reused data. Shared mapping dimensions/lengths are fixed/validated; no unchecked resize.

A hidden Game view requests no capture and causes zero readback. Minimized hosts can keep control/inspection alive without frame capture. Mapping/channels remain valid until the child is stopped/reaped and pending operations cancelled; renderer-local Game textures die before their renderer. Visible capture cost is separately measured because SDL explicitly documents readback as slow/main-thread-only.

Embedded v1 is viewing/control/inspection. It does not claim arbitrary gameplay input injection: direct SDL keyboard-state polling in custom game code is not changed by forwarding an event. External-window Run retains normal native input/resolution. V1 requires input_forwarding=false and has no input injection command. Interactive embedded input requires a separately designed later protocol/capability; do not silently disable or misdirect editor shortcuts.

## Updates, failures, and document isolation

Only a target advertising a safe versioned asset reload capability receives live reload commands, applied through idle-boundary resource transactions. Otherwise expose Restart for assets and Build & Restart for code/source changes with normal save/validation prompts. General arbitrary-code reload is not supported or advertised. A watcher marks source/binary-stale state and cancels stale queued launches; the author explicitly chooses restart.

Play is bound to the launch tab's saved scene/source identity even when another tab becomes active. Closing that tab prompts for Stop or keeping the immutable session detached from authoring selection; project switch/application close stops it. EOF/child crash/timeout/malformed protocol clears live state, preserves authored work and exposes the error. Stop terminates only its owned tree, invalidates session generation, and cancels pending messages/frame consumers.

## Acceptance boundary

Use a tiny compiled compatible runner plus capability-absent/custom/malformed fixtures. Verify separate logs/protocol channels, partial-message framing/size limits, one Runtime per child, safe phase commands, paused step counter and exact 1/120-step behavior, no catch-up on resume, generation/handle rejection, paged snapshots, no authoring mutation, two-slot no-tearing/backpressure, zero hidden readbacks, child crash/Stop cleanup, tab/project switches, external fallback and actual Game image presentation. Pixel transfer tests remain in memory. No virtual full-scene serialization benchmark can substitute for these protocol/lifetime tests.
