# Contract: Embedded Game Input Forwarding, 60 FPS Capture & Main Toolbar

**Feature**: `014-editor-audit-remediation`  
**Covers**: `FR-010`, `FR-011`, `FR-012`, `FR-013`, `FR-014`, `SC-005`

---

## 1. Session Handshake & Frame Rate (`src/editorsession.cpp`, `editor/src/playsession.cpp`)

1. `EditorSessionClient` advertises `"input_forwarding": true` in its session ready handshake, and `PlaySession` accepts `capabilities.inputForwarding`.
2. `MinimumCaptureInterval` in `src/editorsession.cpp` is set to `16,667 µs` (60 FPS).

## 2. Input Forwarding & Focus Capture (`editor/src/panels/game.cpp`, `src/editorsession.cpp`)

1. Clicking inside the rendered game viewport in `GamePanel` while an embedded game session is running activates gameplay input capture (`capturingInput = true`).
2. While `capturingInput == true`:
   - Key presses and releases are sent over the session control pipe (`key_down` / `key_up`) and injected into the child process via `SDL_PushEvent`.
   - Mouse motion and button clicks within the rendered game image are mapped to runtime logical coordinates (`0..width`, `0..height`) and sent via `mouse_move` / `mouse_button`.
   - Single-key editor shortcuts (`Q`, `W`, `E`, `R`, `F`, `Delete`) are suppressed so they do not mutate the scene or switch tools while playing.
   - Pressing `Escape` or clicking outside the `Game` panel releases gameplay input capture (`capturingInput = false`) and sends `release_all_inputs` to clear any held keys in the child process.
3. The `GamePanel` status text accurately reflects input state (`"Embedded game running ·Capturing gameplay input (Esc to release)"` or `"Embedded game running · Click Game view to capture input"`).
4. `GamePanel` provides aspect ratio selection (*Free Aspect*, *16:9*, *4:3*, *Native 640×360*) and a zoom scale control (`0.5×`–`4.0×`).

## 3. Centered Main Toolbar & Play Mode Tint (`editor/src/toolbar.cpp`, `editor/src/style.cpp`)

1. `draw_editor_toolbar` places file and build actions (`Open Scene`, `Save`, `Build`) on the left, centers playback controls (`Play / Build and Run`, `Pause / Resume`, `Step`, `Stop`) in the middle of the toolbar, and right-aligns project/scene status.
2. While a game session is active, the toolbar/window border applies a visible Play Mode tint to distinguish live runtime execution from static scene authoring.
