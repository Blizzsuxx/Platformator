# Pause behavior investigation — 2026-11-16

## Implementation follow-up

Game pause now suspends the game mixer's own logical SDL audio device. F5,
editor Pause/Resume and paused startup share the same transition. Paused editor
startup initializes controls before loading scene autoplay, including when
standalone debug tools are disabled. Single-stepping leaves audio suspended.

This freezes existing playback, fades and sounds requested during pause without
altering per-track pause states. Independent editor preview output continues.
Audio cleanup can still release completed tracks; pending replay completion
waits for game resume. The fix does not bulk-pause tracks, so paused transients
are not misclassified as finished and individually paused tracks stay paused.

Regression coverage in `tests/runtime_foundation_test.cpp` now checks playback
position preservation, explicit playback/autoplay during pause, F5 and editor
controls, repeat cycles, single-step, independent preview output, transient
resource retention, paused startup and fade/replay suspension.

`Audio::isPlaying()` and `Audio::isPaused()` continue to report the individual
track state. Game pause suspends output independently, just as skipping Animator
updates does not change an Animator's own playback flag.

Validation after the fix (dummy audio/video, software rendering):

| Build | CTest selection | Result |
| --- | --- | --- |
| `build/`, RelWithDebInfo, debug controls on | runtime foundation, runtime step, general regressions | 3/3 passed |
| `build/`, RelWithDebInfo, debug controls on | editor play session | 1/1 passed |
| `bin/cleanup-benchmark/`, Release, debug controls off | runtime foundation, runtime step | 2/2 passed |

The stock `build/main` runner was rebuilt as well. Release validation reused an
existing build tree; no benchmark campaign was run. These checks verify state
and playback progression through live dummy mixers, without claiming an
audibility test on physical hardware.

## Historical investigation before the fix

The findings and captured verification below describe the former behavior.
`probe.cpp` is the original reproducer and intentionally expects that former
behavior; use the updated regression tests to verify the fix.

Game pause originally meant **skip simulation steps**. It did not suspend the
runtime audio mixer. This is an engine integration gap, independent of the
previous JACK/libgomp leak investigation.

## Why audio continues

- The F5 listener in `src/sdlwindow.cpp` only toggles `paused` and clears the
  manual-step flag. Editor pause reaches `SDLWindow::setEditorSessionPaused()`
  (line 603), which does the same. Construction also stores `startPaused`
  without changing the mixer.
- `GameManager::loop()` (line 277) asks `SimulationClock` for zero simulation
  steps while paused. The rendered single-frame path (line 341) also skips
  `runSimulationStepWithCustomCallback()` while paused.
- Neither pause path calls an SDL audio-device or mixer-track pause function.
  Audio continues through SDL_mixer's device-driven `MixerCallback`
  (`vendored/SDL3_mixer-3.2.4/src/SDL_mixer.c`, line 538), independently of
  simulation calls. Skipping `updateAudio()` would not stop playback either.
- `Audio::pause()` does call `MIX_PauseTrack()` (`src/audio.cpp`, line 182),
  but only when explicitly requested for that component. Game pause never
  invokes it.
- Paused frames deliberately call `SDLWindow::updateAudio()` (line 775).
  Besides releasing completed transient tracks, this finishes pending replay
  requests, so a fade-out can complete and its replacement sound can start
  while simulation is frozen. Autoplay and explicit `play()` also have no
  game-pause guard.

The current contract is encoded in
`tests/runtime_foundation_test.cpp:381`: `audio_replay_paused` expects a fade and
replacement playback to complete without any simulation steps. It passes.
Changing the intended pause behavior requires changing this expectation too.

## What pauses and what keeps running

| System | Current behavior during normal game/editor pause | Evidence |
| --- | --- | --- |
| Script `start`, `fixedUpdate`, `update`, `lateUpdate` | No simulation dispatch | Probe + simulation phase source |
| Pre-step custom callback | Skipped | Probe + existing step test |
| Physics integration, collision detection/response | Skipped with simulation | Probe verifies position; phase source verifies collisions |
| Runtime Animator playback and clip-end dispatch | No automatic advancement | Probe verifies frame; phase source verifies dispatch |
| Gameplay counters/timers advanced by script `dt` | Frozen because callbacks are skipped | Phase source; example behavior review |
| Component audio and transient/fire-and-forget audio | Continues on audio device time | Probe + existing transient test + source |
| Audio fade/replay sequence | Fade continues; replacement can start | Existing paused-replay test |
| New direct audio playback/autoplay | Can start while paused | Probe verifies explicit play; registration source verifies autoplay |
| Rendering, culling, Game view capture | Continues every outer frame | Probe + loop source |
| SDL events, input state, resize/quit and debug controls | Continues | Probe verifies event dispatch; event/input source |
| Owner-thread services/editor IPC and inspection | Continues outside simulation | Probe verifies services; editor-session source |
| Editor asset audio preview and animation preview | Independent of game pause | Source review; separate mixer/preview playback state |
| Explicit `Runtime::simulateFrame*()` | Runs a full step despite pause | Probe + source; explicit-step API behavior |
| Manual F6/editor Step | Intentionally runs one simulation step | Existing step test + editor-session source |

The paused frame therefore remains responsive; it is not an immutable world.
Owner services or SDL listeners can still invoke runtime mutations directly.
Arbitrary game-owned threads and wall-clock timers would also need their own
pause policy; no automatic suspension of user code is provided. No separate
wall-clock gameplay scheduler was found in the reviewed runtime/example code.

`SimulationClock::advance()` clears accumulated time while paused and discards
the first elapsed interval on resume, so paused wall time is not replayed as a
large physics catch-up. The existing clock test verifies this.

F5 and `startPaused` are debug-tool controls. When debug tools are compiled
out, these do not pause a standalone game. Opt-in editor-session pause works
independently of that build flag. This follows the source branches; the fresh
probe used debug tools enabled.

## Fix direction and pitfalls

Use one pause transition for F5, editor commands and paused startup, and connect
it to game audio suspension. Preserve playback positions and fades rather than
only muting the gain. Cover both component tracks and independent transient
tracks, including playback requested while already paused. Keep editor preview
audio separate from game audio.

A naive `MIX_PauseAllTracks()` / `MIX_ResumeAllTracks()` pair is incomplete:

1. `updateAudio()` currently releases a transient track whenever
   `!MIX_TrackPlaying(track)`. SDL_mixer's `MIX_TrackPlaying()` returns false
   for paused tracks too. Bulk track pausing without adjusting this cleanup
   would destroy paused transient sounds on the next frame.
2. Bulk resume also resumes tracks that were individually paused before the
   game pause. A fix must preserve their independent pause state.
3. Pausing existing tracks once does not prevent new `play()`/autoplay/replay
   calls from starting more audio while paused.
4. Single-stepping simulation should have an explicit audio policy; a physics
   step should not accidentally resume continuous playback.

No runtime behavior was changed during this investigation.

## Verification

Rebuilt only `platformator_runtime_foundation_test` and
`platformator_runtime_step_test` and their dependencies in the existing `build/`
tree. Ran these focused checks with a 15-second timeout per process:

```sh
cmake --build build --target platformator_runtime_foundation_test platformator_runtime_step_test --parallel 3
build/platformator_runtime_foundation_test audio_replay_paused
build/platformator_runtime_foundation_test audio_transient_cleanup
build/platformator_runtime_step_test
build/pause-investigation/probe
```

All four processes exited 0. Tests/probe use dummy SDL video/audio and software
rendering, retaining a live audio mixer; this is not headless physics-only mode.
It verifies playback state, not audible output on a physical device.

The [diagnostic source](probe.cpp) exercises both the editor pause setter and
an actual F5 event. In each case scripts, physics position and Animator frame
remain frozen while a sound starts and finishes. An SDL event is delivered,
12 frames are rendered, and the owner service is invoked seven times (the last
call terminates its six-frame loop). A subsequent explicit `simulateFrame()`
advances scripts, physics and animation with pause still set.

Compile it with the same definitions, include directories and linked libraries
as `platformator_runtime_foundation_test`, plus `-Itests`. The captured
[verification log](verification.txt) includes the exact command, source/build
identities and focused test output. No full suite or benchmark was run.
