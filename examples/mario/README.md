# Mario Example

This example adds a small Mario-like level on top of Platformator and now uses native collider callbacks for pickups, goal triggers, and enemy contacts instead of per-frame overlap scans.

The Mario example's clip definitions now live directly in `examples/mario/level1.scene`, so you can retarget the player, enemy, coin, and flag animations by editing scene data rather than recompiling C++.

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

Scene-defined animation clips:

- Player: `idle`, `run`, `jump`, `fall`, `win`
- Goomba enemies: `walk`, `squash`
- Coins: `spin`
- Goal flag: `wave`

The current scene uses the single-frame art above for those clips, so the example runs with the assets that already exist in the repository.

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

If you add richer animation frames, update `examples/mario/level1.scene` to point each clip at those files.

Optional audio asset paths:

- `examples/mario/assets/audio/music.ogg`
- `examples/mario/assets/audio/jump.wav`
- `examples/mario/assets/audio/coin.wav`
- `examples/mario/assets/audio/stomp.wav`
- `examples/mario/assets/audio/hurt.wav`
- `examples/mario/assets/audio/win.wav`

If the base art assets are not present yet, the example will still load the scene and physics objects. The richer animation frames and audio files are optional; the scene already references the audio emitters, and the sounds will start working as soon as you add matching files under `examples/mario/assets/audio/`. Use the engine debug draw if you need to inspect layout before art is in place.