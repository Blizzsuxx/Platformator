# Quickstart Verification: Final Editor Cleanup (`016-final-editor-cleanup`)

**Feature**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Build & Automated Test Suite

Build engine, editor, and all test targets under `bin/editor-debug`:

```bash
cmake -S . -B bin/editor-debug -DPLATFORMATOR_BUILD_EDITOR=ON -DPLATFORMATOR_BUILD_TESTS=ON
cmake --build bin/editor-debug --parallel 12
ctest --test-dir bin/editor-debug --output-on-failure
```

Launch the editor directly against `examples/mario`:

```bash
./bin/editor-debug/platformator_editor examples/mario
```

---

## End-to-End Acceptance Walkthroughs

### Scenario 1 — Create, Switch, and Generate CMake Projects (US1)
1. Launch `platformator_editor`. Verify **Recent Projects** shows valid paths and Welcome Screen exposes **New Project...** and **Open Project...**.
2. Click **File → New Project...**, enter `SmokeGame` under `/tmp`, confirm.
   - Verify `/tmp/SmokeGame/CMakeLists.txt`, `main.cpp`, `.platformator/layers.json`, and `assets/scenes/main.scene` exist.
   - Inspect generated `CMakeLists.txt`: confirm `find_package(Platformator CONFIG QUIET)` followed by `add_subdirectory` fallback pointing to the Platformator source checkout.
   - Press Play: confirm `SmokeGame` configures, compiles cleanly, and opens `main.scene` in Embedded Game View.
3. Modify `main.scene` without saving, then choose **File → Open Project Folder... → `examples/mario`**.
   - Verify the dirty-state modal prompts `Save` / `Don't Save` / `Cancel`; pick `Don't Save`.
   - Verify `examples/mario` opens with zero leftover tabs, undo entries, or behavior catalog entries from `SmokeGame`.
4. Open an asset/scene directory without `CMakeLists.txt`: verify the editor opens the folder cleanly and offers a one-click **Generate CMakeLists.txt** action.

### Scenario 2 — Static C++ Behavior & Field Scanning Without `--editor-catalog` (US2)
1. In `SmokeGame`, create `assets/behaviors/patrol_box.h` + `.cpp` declaring `PatrolBox : public ScriptComponent` with `float speed = 3.5f;`, `bool loop = true;`, `Eigen::Vector2f offset{1.0f, 0.0f};`, and `SERIALIZABLE_SCRIPT(PatrolBox, speed, loop, offset)`.
2. Without compiling `SmokeGame`, select a GameObject in Hierarchy → **Add Component**.
   - Verify `PatrolBox` appears immediately and its `speed`, `loop`, and `offset` fields render editable in Inspector with their C++ defaults (`"Build to load field"` never appears).
3. Verify running the built executable with `--editor-catalog` no longer exists as a catalog mechanism (`Process` stage never spawns `--editor-catalog`).

### Scenario 3 — Project Settings & Clean UI / Dock (US3)
1. Inspect default layout: verify the bottom dock contains Console / Validation cleanly with **no** permanent `Build & Run` tab.
2. Open **Edit → Project Settings...**:
   - Verify left sidebar switches cleanly between **Build & Run** and **Layers & Physics**.
   - Verify **Build & Run** omits `Catalog protocol`, `Recheck Sources`, `Rediscover Targets`, and duplicate transport buttons.
3. Inspect the Project panel and GameObject Inspector:
   - Verify no standalone `Refresh Assets`, `Load Template`, or redundant `Assign` button bars; verify right-clicking inside the Project browser reveals `Refresh` alongside creation/file actions.
   - Verify the GameObject header shows Active and Name on its first row, then Tag. Verify `Layer` is available in each Collider component and ordinary GameObjects have no empty `Object Template` block; only linked template instances show template controls.

### Scenario 4 — Physics Layers & Collision Matrix (US4)
1. Open **Edit → Project Settings... → Layers & Physics**.
   - Verify slot `0` is `"Default"` (read-only). Name slot `1` `"Player"` and slot `2` `"GhostWall"`.
   - Uncheck the intersection cell `(Player, GhostWall)` (confirm symmetry updates automatically across the diagonal).
2. Assign `"Player"` layer to a dynamic falling box and `"GhostWall"` layer to a static floor platform underneath another box on `"Default"` layer.
3. Press Play:
   - Verify the `"Player"` box passes cleanly through `"GhostWall"`, while the `"Default"` box collides normally with `"GhostWall"`.
   - Verify layer assignment causes zero change to sprite draw order.

### Scenario 5 — Template Creation, Editing Mode & Instance Sync (US5)
1. Create a parent GameObject `"CoinBundle"` with a `Sprite`, `CircleCollider`, and a child GameObject, right-click → **Save as Template...** (`assets/templates/CoinBundle.pttemplate`).
   - Verify nested template metadata (if any child had been a template instance) is flattened inside the saved `.pttemplate`.
2. Drag `CoinBundle.pttemplate` into the scene twice to create two linked instances; move Instance B's `position` and change Instance B's `tag` (marking them overridden in bold).
3. Double-click `CoinBundle.pttemplate` in Project panel (or click **Open** on Instance A):
   - Verify **Template Editing Mode** opens cleanly in its isolated tab.
   - Rename `CoinBundle`, change child position, and add a new component; click **Save** and exit Template Editing Mode.
   - Return to the scene: verify **both** instances receive the new component, updated name, and updated child position, while Instance B preserves its overridden `position` and `tag`.
   - Click **Unpack** on Instance A and verify further `.pttemplate` edits no longer affect Instance A.

### Scenario 6 — Audio Double-Click Preview (US6)
1. Locate `assets/audio/coin.wav` and `assets/audio/jump.wav` in Project panel.
2. Double-click `coin.wav`: verify immediate audio playback with subtle active indicator.
3. Double-click `coin.wav` again while playing: verify playback stops/pauses cleanly.
4. Double-click `jump.wav` while `coin.wav` is playing: verify `coin.wav` stops immediately and `jump.wav` plays without overlapping audio.

### Scenario 7 — Window Layouts & Automatic Validation (US7)
1. Switch between **Window → Layouts** presets (e.g., Default, Tall, Compact) and toggle panel visibility cleanly.
2. Delete a texture referenced by a Sprite in the active scene on disk: without pressing any manual check button, verify the Validation badge/list automatically reports the missing asset within ~1 second and clears automatically when restored.
