# Crystal Citadel — Platformator Example Project

Crystal Citadel is a multi-scene, template-driven 2D action-platformer example built on Platformator.

## What It Demonstrates

- **4 Connected Scenes (`assets/scenes/*.scene`)**:
  - `assets/scenes/hub.scene` — *Sanctuary Hub*: hub area with training crate, crawler pen, jump pad, crystal shrine, shield powerup, and 3 portals into each stage.
  - `assets/scenes/level1_cavern.scene` — *Echoing Caverns*: multi-tiered cavern with crystal platforms, spike hazards, breakable loot crates, patrol crawlers, a wave spawner, and splitting slimes (requires 3 gems for the exit portal).
  - `assets/scenes/level2_foundry.scene` — *Runic Foundry*: chasm platforming gauntlet with moving platforms, composite aiming Arcane Turrets, jump pads, wave spawner, and shield powerup (requires 3 gems).
  - `assets/scenes/level3_sanctum.scene` — *Archon Sanctum*: multi-part boss encounter (`CrystalArchonBoss`) that fires orb volleys, summons minion slimes, and spawns reward gems plus a victory portal back to the hub upon defeat.
- **22 Object Templates (`assets/templates/*.pttemplate`)**:
  - Scene instances retain editor template links (`template`, `templateNodeId`, `templateComponentId`).
  - **Runtime instantiation (`platformator::instantiate`)**: player bolts, enemy bolts, crate loot drops, timed spark/jump effects, wave-spawner enemies, boss minions, and the post-boss victory portal.
  - **Parented template instantiation**: collecting a `ShieldCore` calls `platformator::instantiate(shieldTemplate, getGameObject())` to attach an orbiting `OrbitShield` child object around the player.
  - **Composite templates with internal `ObjectReference` remapping**: `ArcaneTurret.pttemplate` (root + child `TurretBarrel`) and `CrystalArchonBoss.pttemplate` (root + `ArchonCrown`, `ArchonOrbL`, `ArchonOrbR`) bind child objects and components via `reference_bindings` and clone remapping.
  - **Self-referential / recursive templates**: `Slime.pttemplate` references itself via `selfTemplate` and instantiates two smaller generation-2 clones on defeat.

## Build & Run

```sh
cmake -S examples/crystal_citadel -B bin/crystal_citadel -GNinja -DCMAKE_BUILD_TYPE=Release
cmake --build bin/crystal_citadel --parallel 4
bin/crystal_citadel/crystal_citadel
```

`POST_BUILD` copies `assets/` next to the binary (excluding `assets/behaviors/`) and `Release` builds disable `PLATFORMATOR_ENABLE_DEBUG_TOOLS` automatically.

Install a stripped, relocatable standalone bundle (`$ORIGIN/../lib` on Linux, `@loader_path/../lib` on macOS):

```sh
cmake --install bin/crystal_citadel --prefix bin/dist/CrystalCitadel --strip
```

Run the automated headless/software self-test covering all 4 scenes and template instantiation flows:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
  bin/crystal_citadel/crystal_citadel --self-test
```

Regenerate procedural PNG/WAV/animset/template/scene assets if needed:

```sh
python3 examples/crystal_citadel/generate_assets.py
```

## Controls

- `A` / `D` or `Left` / `Right`: Move
- `W` / `Up` / `Space`: Jump & Double-Jump
- `F` / `J` / Left Mouse Button: Fire Arcane Bolt
- `R`: Respawn at level start
- `1` / `2` / `3` / `4`: Quick-warp between Hub, Level 1, Level 2, and Level 3
- `N`: Advance to the next scene
