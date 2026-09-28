# Native region passes

This native C example captures asymmetric canvas content in an application-owned RGBA8 render target, then draws it into two partially clipped screen regions.

It demonstrates:

- region-pass capability discovery;
- explicit canvas/target/screen endpoints;
- top-left physical-pixel bounds;
- negative-origin and edge-overflow clipping without source stretching;
- destination-sized `u_resolution` and requested-origin `u_offset`;
- context-loss destruction and surface recreation.

Build and run from the repository root with `budo compile examples/17_native_region_passes --run`. Native projects are trusted, unsandboxed code.
