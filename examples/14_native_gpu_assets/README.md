# Native GPU assets example

Demonstrates project-relative shader assets and synchronous encoded-image texture decoding. The tiny PNG is embedded as encoded bytes so both encoded-data and staged shader-file APIs are exercised without adding a third-party image asset.

GPU resources are created in `surface_created`, destroyed in dependency order during `context_lost`, and recreated for a replacement surface.
