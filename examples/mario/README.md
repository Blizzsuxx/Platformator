# Mario Example

This example adds a small Mario-like level on top of Platformator without changing the engine architecture beyond a minimal per-frame callback hook.

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

If the assets are not present yet, the example will still load the scene and physics objects. Use the engine debug draw if you need to inspect layout before art is in place.