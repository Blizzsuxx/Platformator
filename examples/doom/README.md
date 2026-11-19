# DOOM 2.5D — Platformator Raycasting Showcase

A playable DOOM-inspired 2.5D first-person shooter built directly on **Platformator** without modifying the engine.

## What It Demonstrates

- **320-Column Raycast Renderer (`DoomRaycaster` + `Runtime::raycast`)**:
  - Casts 320 rays per fixed step against `LAYER_WALL | LAYER_DOOR` colliders, including axis-aligned `BoxCollider`s, **45°-rotated `BoxCollider` walls**, and **cylindrical `CircleCollider` pillars**.
  - Projects perspective wall strips with `Sprite::setSourceRect()`, local rotated-box UV mapping, normal + distance fog lighting (`Sprite::setColor()`), and `Sprite::setZOrder()` painter's sorting.
  - Renders depth-sliced `Animator`-driven **2.5D billboards** (enemies, explosive barrels, pickups, fireballs, plasma bolts, and impact sparks) clipped cleanly against the 1D wall depth buffer so sprites peek authentically around corners.
- **Live 2D Physics & Collision (`M` / `Tab`)**:
  - The 2D arena runs continuously using Platformator `CircleCollider` + `Rigidbody` sliding collisions, trigger pickups, motorized sliding doors, and bitmask-filtered hitscan/projectile raycasts (`HITSCAN_RAY_MASK`, `LOS_RAY_MASK`).
  - Pressing `M` or `Tab` zooms the camera directly onto the 2D physics world!
- **2 Connected Scenes & Template Drops**:
  - `assets/scenes/e1m1_hangar.scene` — *E1M1: Phobos Hangar*
  - `assets/scenes/e1m2_reactor.scene` — *E1M2: Reactor Core* (octagonal core + Reactor Baron boss dropping `RedKeycard.pttemplate`)

## Build & Run

```sh
cmake -S examples/doom -B build/doom -GNinja -DCMAKE_BUILD_TYPE=Release
cmake --build build/doom --parallel 4
build/doom/doom
```

Run the automated headless/software self-test:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
  build/doom/doom --self-test
```

Regenerate procedural textures, audio, animsets, templates, and scenes:

```sh
python3 examples/doom/generate_assets.py
```

## Controls

- `W` / `S` / `Up` / `Down`: Move forward / backward
- `A` / `D`: Strafe left / right
- `Left` / `Right` or `Q`: Turn left / right
- `Shift`: Sprint
- Mouse Move: Look/turn (mouse lock is enabled by default in interactive windows)
- `Esc`: Unlock mouse cursor
- Left Click (when unlocked) or `L`: Re-lock / toggle mouse look
- `Space` / `F` / `Ctrl` / Left Mouse: Fire current weapon
- `1` / `2` / `3`: Switch weapon (Pistol / Shotgun / Plasma Rifle)
- `E` / `Enter`: Open / interact with doors
- `M` / `Tab`: Toggle 3D raycast view / 2D top-down physics map
- `R`: Respawn when defeated
- `F7` / `F8` / `N`: Jump to E1M1 / E1M2 / next scene
