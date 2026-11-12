# Data Model: Complete Editor Performance, Focus, Playability & Unity Ergonomics Audit Remediation

**Feature**: `014-editor-audit-remediation`  
**Date**: 2026-11-12  
**Status**: Completed  

---

## 1. Entities & State Models

### 1.1 Idle Wake Schedule & Per-Frame Cache State (`IdleWakeSchedule`, `EditorFrameCache`)

Governs when the main event loop blocks in `SDL_WaitEventTimeout` and when expensive derived editor projections are recomputed.

```cpp
struct IdleWakeSchedule
{
    bool hasImmediateWork = false;                     // True during active drag, pending UI redraw, or non-empty event queue
    bool backgroundTaskInFlight = false;               // True while build, catalog query, or assetWatchScan is running
    bool referenceFallbackNeeded = false;              // True only when coverage != ProjectCoverageState::Current && !assetWatchScan
    bool assetWatchDue = false;                        // True only when !assetWatchScan && assetWatchSchedule.pending()
    std::chrono::steady_clock::time_point nextDeadline;
};

struct TemplateAuthorCacheKey
{
    std::uint64_t projectGeneration = 0;
    std::uint64_t tabId = 0;
    std::uint64_t documentGeneration = 0;
    std::uint64_t catalogRevision = 0;
    bool operator==(const TemplateAuthorCacheKey &) const noexcept = default;
};

struct SemanticSnapshotCacheKey
{
    std::uint64_t projectGeneration = 0;
    std::uint64_t documentGeneration = 0;
    std::uint64_t selectionRevision = 0;
    std::uint64_t consoleRevision = 0;
    FocusedPanel focusedPanel = FocusedPanel::None;
    bool operationActive = false;
    bool operator==(const SemanticSnapshotCacheKey &) const noexcept = default;
};
```

#### Invariants
- `nextReferenceFallbackCheck` is included in `nextDeadline` **if and only if** `referenceFallbackNeeded == true`.
- `assetWatchSchedule.deadline()` is included in `nextDeadline` **if and only if** `assetWatchDue == true`.
- When idle (`!hasImmediateWork && !backgroundTaskInFlight`), `editor_wait_duration` returns a positive timeout up to `idleHeartbeat` (250 ms).

---

### 1.2 Panel Focus Routing & Marquee Selection State (`ShortcutCaptureState`, `MarqueeSelectionState`)

Synchronizes keyboard shortcut routing and viewport mouse gestures across `Hierarchy`, `Scene`, and `Game` panels.

```cpp
struct ShortcutCaptureState
{
    bool wantTextInput = false;
    bool anyPopupOpen = false;
    bool sceneFocused = false;
    bool hierarchyFocused = false;
    bool embeddedGameFocused = false; // True when running embedded Game panel captures gameplay input
};

struct MarqueeSelectionState
{
    bool active = false;
    bool additive = false;            // True when Shift or Ctrl held
    ImVec2 startScreen{0.0f, 0.0f};
    ImVec2 currentScreen{0.0f, 0.0f};
};
```

#### Focus & Gesture Transitions
- **Scene Pan Initiation**: Pressing Middle Mouse, Right Mouse, or Left Mouse (when `tool_ == SceneTool::Hand`) over the hovered Scene canvas immediately sets `panning_ = true`, calls `ImGui::SetWindowFocus()`, and sets `selection.focusedPanel = FocusedPanel::Scene`.
- **Scene Tool & Framing Shortcuts**: `Q`, `W`, `E`, `R`, `F`, and `Shift+F` are enabled when `(sceneFocused || hierarchyFocused) && !wantTextInput && !anyPopupOpen && !embeddedGameFocused`.
- **Marquee Selection**: Left-click drag on empty Scene canvas when `tool_ != SceneTool::Hand` activates `MarqueeSelectionState`; releasing the mouse button computes the world-space AABB from `startScreen` and `currentScreen` and selects all intersecting visible, unlocked GameObjects.

---

### 1.3 Embedded Game Input Forwarding Protocol (`EmbeddedInputEvent`, `GameViewConfig`)

Represents live keyboard and mouse events forwarded from `GamePanel` to the running child process over the editor-session control pipe.

```cpp
enum class EmbeddedInputKind
{
    KeyDown,
    KeyUp,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    ReleaseAll
};

struct EmbeddedInputEvent
{
    EmbeddedInputKind kind = EmbeddedInputKind::ReleaseAll;
    int scancode = 0;                 // SDL_Scancode for KeyDown / KeyUp
    int keycode = 0;                  // SDL_Keycode for KeyDown / KeyUp
    std::uint16_t modifiers = 0;      // SDL_Keymod bitmask
    int mouseButton = 0;              // SDL mouse button index (1=Left, 2=Middle, 3=Right)
    float logicalX = 0.0f;            // X in runtime logical coordinates [0, width)
    float logicalY = 0.0f;            // Y in runtime logical coordinates [0, height)
};

enum class GameAspectPreset
{
    FreeAspect,
    Aspect16x9,
    Aspect4x3,
    Native640x360
};

struct GameViewConfig
{
    GameAspectPreset aspectPreset = GameAspectPreset::Aspect16x9;
    float zoomScale = 1.0f;           // [0.5f, 4.0f]
    bool capturingInput = false;      // True when Game view is focused and forwarding input
};
```

#### Wire Format (Editor $\rightarrow$ Runtime Control Pipe)
- `key_down <scancode> <keycode> <mod>\n`
- `key_up <scancode> <keycode> <mod>\n`
- `mouse_move <x> <y>\n`
- `mouse_button <button> <0|1> <x> <y>\n`
- `release_all_inputs\n`

#### Invariants
- Whenever `capturingInput` transitions from `true` to `false` (e.g., user clicks another panel, presses `Escape`, or pauses/stops the session), `release_all_inputs` is transmitted so no keys remain stuck in the runtime.
- `MinimumCaptureInterval` in `EditorSessionClient` is `16,667 µs` (60 FPS).

---

### 1.4 Asset Drag-and-Drop Instantiation Payload (`AssetDragPayload`)

Carried by Dear ImGui drag-and-drop (`"PLATFORMATOR_ASSET_PATH"`) from `ProjectPanel` to `SceneViewport` and `HierarchyPanel`.

```cpp
struct AssetDragPayload
{
    std::filesystem::path relativeAssetPath; // Relative to document.assetRoot
    EditorAssetKind kind = EditorAssetKind::Unknown;
};
```

#### Instantiation Mapping
- **`EditorAssetKind::Image` dropped on Scene / Hierarchy**: Creates a new GameObject named after the file stem at the target world position (or `(0, 0)` in Hierarchy) with a default `Sprite` component (`type = 6`) whose `textureFilePath` is set to `relativeAssetPath`.
- **`EditorAssetKind::Template` (`.pttemplate`) dropped on Scene / Hierarchy**: Instantiates the template hierarchy via `instantiate_template()` at the drop world coordinates (or under the target parent in Hierarchy).
- **`EditorAssetKind::Audio` dropped on Scene / Hierarchy**: Creates a new GameObject with an `Audio` component (`type = 1`) whose `filePath` is set to `relativeAssetPath`.
- **`EditorAssetKind::Script` (Behavior header) dropped on a GameObject**: Attaches the behavior type to the target GameObject's `Script` component (`type = 5`).

---

### 1.5 Project Browser & Console Filter State (`ProjectBrowserViewState`, `ConsoleFilterState`)

```cpp
struct ProjectBrowserViewState
{
    float folderTreeWidth = 190.0f;
    float thumbnailScale = 0.0f;      // 0.0f = compact list view; (0.0f, 1.0f] = grid tiles (48px..144px)
    std::filesystem::path contextTargetPath;
    std::string renameBuffer;
    bool renamePopupOpen = false;
};

struct ConsoleFilterState
{
    bool showInfo = true;
    bool showWarnings = true;
    bool showErrors = true;
    std::size_t infoCount = 0;
    std::size_t warningCount = 0;
    std::size_t errorCount = 0;
};
```
