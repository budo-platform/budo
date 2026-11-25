# Layout demo

Run with `budo run examples/layout_demo`. Use the theme button or press **T**
to switch between Classic and Hologram.

Both themes display the same four stat cards and sparklines, analytics series,
performance metrics, activity, tasks, and regional globe and breakdown.
One dashboard model updates the values and chart samples before either theme
renders them. Hologram places those widgets on a weathered slab, using
translucent 3D meshes for bars and graphs; Classic uses its canvas layout.

Every transition lasts at most 200 ms, and the demo plays no sound. Switching
themes animates a circular wipe from the theme button while the Hologram
camera dives in from the sky (or climbs away when leaving).

In Hologram, click any panel (or the globe) to fly to it: the camera spins,
swoops, and settles in a top-down full view while a shockwave, glitch,
lightning, particles, and a text decrypt effect play. Controls:

- **Click** a panel to focus it; click it again or empty space to go back.
- **Backspace** returns to the overview (**Escape** quits the app).
- **Left** / **Right** fly to the previous or next panel.
- **Space** toggles an automatic tour of every panel.
