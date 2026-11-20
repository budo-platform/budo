# Budo UI demo and development

The distributable toolkit lives entirely in [ui/](ui/). See the
[library guide](ui/README.md) for its API and [ROADMAP.md](ROADMAP.md) for
the plan.

Run the demo from the repository root:

```sh
./build/budo examples/budo-ui
```

`main.js` demonstrates themes, lists, forms, and effects; `gallery.js` draws
every widget. Tests, snapshots, shaders, and demo assets stay outside `ui/`.
The root `budo-ui.js` and `ui-library.js` retain their existing imports as
compatibility re-exports; new code imports `./ui/budo-ui.js`.

## One canonical library

Edit library modules and their documentation in `examples/budo-ui/ui/`, not
in a vendored copy. Public examples and private examples when present carry
byte-for-byte identical `ui/` directories:

```sh
python3 scripts/sync-budo-ui.py
python3 scripts/sync-budo-ui.py --check
```

`budo init --template ui` embeds this same subtree without rewriting paths.
Project-level `jsconfig.json` or `tsconfig.json` includes the runtime
declarations; library modules contain no location-dependent type references.
CTest checks synchronization and the generated template.

## Tests and snapshots

The behavior tests run against the canonical library. Snapshot tests draw each
gallery section on a recording canvas and compare calls with `snapshots/*.json`.

```sh
node --test examples/budo-ui/budo-ui.test.mjs examples/budo-ui/budo-ui.snapshots.test.mjs
UPDATE_SNAPSHOTS=1 node --test examples/budo-ui/budo-ui.snapshots.test.mjs
```
