# Native terrain physics

Native C port of `examples/02_terrain_physics`. It includes the deterministic
fractal desert and rally road, four-wheel terrain suspension, airborne attitude,
four colliding cars with rally-target AI, vegetation and collisions, shadows,
night headlights, force vectors, orbit/follow/FPV cameras, day/night lighting,
radar, telemetry, a runtime physics panel, and responsive desktop/touch controls.
All 25 CC0 Kenney Car Kit models are normalized by the native asset pipeline,
compiled into the application, and selected per live vehicle without runtime
OBJ parsing.

Regenerate the downloaded vehicle assets and native C mesh tables with:

```sh
python3 download_kenney_car_kit.py
```

## Controls

- **Drive:** W/A/S/D or arrow keys
- **Orbit camera:** Q/E
- **Camera elevation:** R/F
- **Camera distance:** T/G
- **Reset car:** Space
- **Cycle camera:** V
- **Select physics parameter:** 1–8, then click the panel edge controls
- **Reset physics:** Backspace
- **Touch:** drag on the left half to drive; drag on the right half to orbit

The terrain is regenerated into a dynamic 65×65 mesh as the car crosses 12-unit
cells, giving the native example an effectively unbounded deterministic world
without retaining old GPU chunks. The HUD uses a tiny built-in GPU bitmap font
because the public native C canvas does not yet expose text shaping. GPU
resources are recreated after context loss and all sources/shaders are declared
by the constrained native manifest.
