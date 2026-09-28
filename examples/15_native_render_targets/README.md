# Native render targets

This native C example draws asymmetric canvas content, downsamples it through two application-owned RGBA8 render targets, and presents the final target with explicit fullscreen passes.

It demonstrates:

- render-target capability discovery;
- create, resize, query, and explicit destruction ownership;
- canvas-to-target, target-to-target, and target-to-screen passes;
- `u_canvas` on reserved texture unit zero and destination-sized `u_resolution`;
- context-loss destruction and surface recreation.

Build and run from the repository root with `budo compile examples/15_native_render_targets --run`. Native projects are trusted, unsandboxed code.
