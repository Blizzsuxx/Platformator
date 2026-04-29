# Mario Example

This example adds a small Mario-like level on top of Platformator and uses native collider callbacks for pickups, goal triggers, and enemy contacts instead of per-frame overlap scans.

The Mario example keeps reusable sound and animation assets in external files, but the scene still lists every runtime component explicitly. Objects that animate have an explicit `animator` block in `examples/mario/level1.scene`, while the script blocks own the `.animset` references that are loaded into those animator components at startup.

Run it with:

```bash
cmake --build --preset debug --target mario_example
./bin/mario_example
```

Controls:

- Left / A: move left
- Right / D: move right
- Up / W / Space: jump
- R: respawn at the start
- F7: save the current scene back to `examples/mario/level1.scene`
- F1-F6: existing engine debug controls still work

Expected asset paths:

- `examples/mario/assets/player/mario_idle.png`
- `examples/mario/assets/enemies/goomba.png`
- `examples/mario/assets/tiles/ground.png`
- `examples/mario/assets/tiles/platform.png`
- `examples/mario/assets/tiles/brick.png`
- `examples/mario/assets/tiles/pipe.png`
- `examples/mario/assets/items/coin.png`
- `examples/mario/assets/items/flag.png`

Animation set assets:

- `examples/mario/assets/animations/player.animset`
- `examples/mario/assets/animations/player_run.animset`
- `examples/mario/assets/animations/player_jump.animset`
- `examples/mario/assets/animations/player_fall.animset`
- `examples/mario/assets/animations/player_win.animset`
- `examples/mario/assets/animations/goomba.animset`
- `examples/mario/assets/animations/goomba_squash.animset`
- `examples/mario/assets/animations/coin.animset`
- `examples/mario/assets/animations/flag.animset`

Each `.animset` file now contains exactly one clip in TOML form. The script block lists whichever clip assets a behavior needs, and gameplay code switches clips by calling `Animator::play(animationClip)` on the explicit animator component.

Optional richer animation asset paths:

- `examples/mario/assets/player/mario_idle_0.png`
- `examples/mario/assets/player/mario_idle_1.png`
- `examples/mario/assets/player/mario_run_0.png`
- `examples/mario/assets/player/mario_run_1.png`
- `examples/mario/assets/player/mario_run_2.png`
- `examples/mario/assets/player/mario_jump.png`
- `examples/mario/assets/player/mario_fall.png`
- `examples/mario/assets/player/mario_win_0.png`
- `examples/mario/assets/player/mario_win_1.png`
- `examples/mario/assets/enemies/goomba_walk_0.png`
- `examples/mario/assets/enemies/goomba_walk_1.png`
- `examples/mario/assets/enemies/goomba_squash.png`
- `examples/mario/assets/items/coin_spin_0.png`
- `examples/mario/assets/items/coin_spin_1.png`
- `examples/mario/assets/items/coin_spin_2.png`
- `examples/mario/assets/items/coin_spin_3.png`
- `examples/mario/assets/items/flag_wave_0.png`
- `examples/mario/assets/items/flag_wave_1.png`

If you add richer animation frames, update the `.animset` files instead of editing the scene.

Optional audio asset paths:

- `examples/mario/assets/audio/music.ogg`
- `examples/mario/assets/audio/jump.wav`
- `examples/mario/assets/audio/coin.wav`
- `examples/mario/assets/audio/stomp.wav`
- `examples/mario/assets/audio/hurt.wav`
- `examples/mario/assets/audio/win.wav`

If the base art assets are not present yet, the example will still load the scene and physics objects. The richer animation frames and audio files are optional; the explicit scene components will start working as soon as you add matching files under `examples/mario/assets/`. Use the engine debug draw if you need to inspect layout before art is in place.