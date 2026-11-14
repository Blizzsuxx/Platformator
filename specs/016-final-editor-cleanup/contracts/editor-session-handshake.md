# Contract: Editor Session Handshake (Removing `--editor-catalog`)

**Feature**: `016-final-editor-cleanup`
**Covers**: `FR-005`, `FR-008`

## 1. Command-Line Surface Removal

- **Removed CLI flag**: `--editor-catalog` is completely removed from the Platformator runtime CLI (`Runtime` / `src/scriptregistration.cpp`) and from the editor (`editor/src/behaviorcatalog.cpp` subprocess catalog query & `editor/src/process.cpp` `PipelineStage::CatalogCheck`).
- **Removed Preferences & UI**: `RunPreferences::catalogProtocol` and all `Catalog protocol` / `Rediscover Targets` / pre-launch `--editor-catalog` error displays are removed.

## 2. Embedded Game View Launch Handshake (`PlaySession`)

When Play Mode starts in **Embedded Game View** mode:

1. `GamePipeline` proceeds directly from `Configuring` (if needed) -> `Building` (if needed) -> `Launching` (never invoking `--editor-catalog`).
2. `PlaySession::start_process(...)` spawns the target executable with embedded editor session arguments/pipe FDs (`--editor-session ...`) and enters `PlaySessionState::Starting` (no pre-populated `SessionCapabilities` required beforehand).
3. Inside `src/editorsession.cpp`, on session initialization, the runtime child builds its local `editor_capabilities` JSON (`session_protocol`, `play_control`, `runtime_inspection`, `live_asset_reload`, `input_forwarding`, `rgba_frame_stream`) directly in-process and immediately emits:
   - Event `"capabilities"` containing the `editor_capabilities` payload.
   - Event `"ready"` once initial scene load & frame capture slot initialization complete.
4. `PlaySession` validates incoming `"capabilities"` against `SessionCapabilities`:
   - If `supported == true && frameStream == true`: stores `capabilities_`, validates frame ring geometry, and proceeds to `"ready"` -> `PlaySessionState::Running` (or `Paused` if `startPaused`).
   - If `!supported || !frameStream`, or handshake timeout expires, or child exits before `"capabilities"`/`"ready"`: kills child cleanly, transitions to `PlaySessionState::Failed`, and surfaces an actionable error directing the user to inspect logs or switch Launch Mode to **External Game Window**.

## 3. External Game Window Mode

- External Game Window mode spawns the built target executable directly with standard window/scene flags (no `--editor-catalog` pre-check and no embedded session pipe).
