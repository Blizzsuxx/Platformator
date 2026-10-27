# Debug overlay verification

The supported overlays still use the buffered debug-object paths, which were not removed. A graphical comparison could not be performed: `DISPLAY=:0.0` is set, but `xrandr --current` reports `Can't open display :0.0`, and no Wayland display is configured. Collider, contact and grid overlay output and interactive controls therefore remain visually unverified; successful debug-on/off compilation is not treated as visual evidence.
