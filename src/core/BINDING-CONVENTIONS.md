# Budo — Binding Error-Reporting Convention

This document is the single source of truth for how `js_*_bindings.c`,
`lua_*_bindings.c`, and `wasm_*_bindings.c` should report failures to
guest code. It keeps application-level error handling predictable across
runtimes.

Read this once before adding a new binding or changing an existing one.

## The rule

Two failure categories, two responses:

| Category               | When to use                                                           | JS / Lua response                                              | WASM response                                  |
|------------------------|-----------------------------------------------------------------------|----------------------------------------------------------------|------------------------------------------------|
| **Programmer error**   | The guest violated the function contract — wrong arity, wrong type, missing required argument, calling before init, nonsense parameter range, the binding's preconditions cannot possibly be satisfied without a code change. | **Throw** (`JS_Throw…Error` / `lua_error`). Application code should treat this like an uncaught exception — there is nothing useful to recover from at runtime. | Return a sentinel value (`-1` for handle/count getters; `0` for boolean/`success` getters). Document the contract violation in the WAT comment in `budo-llm.md`. WASM has no exception channel, but a contract violation should still be observable as the documented sentinel and accompanied by a host-side `fprintf(stderr, …)` so the developer sees it. |
| **Transient / runtime failure** | The contract was respected but the operation could not complete in the current environment — device disconnected, file missing, network refused, slot table full, peer hung up, model not loaded, query returned no rows. | **Return a falsy value** (`JS_NULL`, `false`, `0`, `-1` for handle returns, `""` for string getters). Pair it with a stateful `getError()` accessor when the message is non-trivial. Do NOT throw. | Same falsy/sentinel convention as for programmer error, again paired with a `*_get_error` accessor where one already exists. |

Mnemonic: *"throw on impossible, return on improbable."*

## What counts as a programmer error

- Wrong number of arguments to a non-variadic binding.
- Argument of the wrong JS / Lua type when the contract requires a
  specific type (e.g. passing a string where a `Float32Array` is required).
- Calling a binding before its subsystem was initialised by the host.
  (This is a host bug masquerading as a guest bug — still throw,
  because there is nothing the guest can do.)
- Out-of-range enum/index values that have no defensible runtime
  meaning (e.g. `paint.setStrokeCap(99)`).
- Path arguments that fail the static safety check (absolute paths,
  `..` segments) — the guest cannot satisfy the contract by retrying.

## What counts as a transient failure

- File / directory missing, permission denied.
- Database busy, schema mismatch, query syntax error.
- HTTP request rejected by the network policy.
- MIDI / UDP / RTP-MIDI device not present, send buffer full,
  network unreachable.
- Neural model failed to load (corrupt file, ORT not installed),
  inference failed at runtime.
- Slot table exhausted (too many concurrent in-flight ops).
- Audio backend declined to start.

## How `getError()` interacts with the rule

A binding subsystem MAY expose a `getError()` accessor (JS / Lua) or
`*_get_error_*` import pair (WASM). The contract is:

1. Successful operations clear the error.
2. Transient failures set a human-readable message before returning
   the falsy sentinel.
3. Programmer errors do NOT set the error string — they throw, and
   the thrown value carries the message. The error string is reserved
   for things the guest could legitimately have not known.

## Why JS exceptions are not used for transient failures

Two reasons:

1. **Symmetry with WASM.** WebAssembly guests cannot catch host
   exceptions through Wasmtime; they get a trap that aborts the
   instance. Forcing JS / Lua callers to write `try / catch` around
   every device read makes the application code shape diverge sharply
   from WASM. A falsy sentinel + `getError()` works identically
   everywhere.

2. **Cost and ergonomics.** Frame loops poll inputs and sensors
   dozens of times per frame. `try { sys.sensors.getCompass() } catch
   (e) {}` is both expensive and noisy. `if (data) { … }` is the
   ergonomic shape we want.

## Current Binding Behavior

The current bindings already line up with this convention in the vast
majority of cases:

| Subsystem           | JS / Lua throws on                                              | Returns falsy on                                                  |
|---------------------|-----------------------------------------------------------------|-------------------------------------------------------------------|
| `sys.neural`        | `loadModel` (corrupt file is treated as programmer error so the developer notices immediately), `getModelInfo` for invalid id, `run` arity / type mismatch and inference failure (also treated as programmer error — a model that has stopped working warrants surfacing). | `isAvailable()` returns `false` when ORT is not present. |
| `sys.db`            | `open` invalid name, `execute` SQL syntax, `run` SQL syntax, `query` SQL syntax. (All four are programmer-authored statements.) | `open` returns the integer handle or `-1` from the underlying wrapper for resource-exhaustion situations. |
| `sys.files`, `sys.assets` | Wrong arity/type; `sys.files` paths without an `assets/` or `files/` mount; writes outside `files/`; absolute, traversal, empty file, or otherwise invalid paths. `sys.assets` accepts safe bundle-relative paths and exposes no writes. | Missing/denied files and directories, unavailable roots, I/O/resource/limit failures return `null`/`false` in JS or `nil`/`false` in Lua and set the shared `getError()`. Successful operations clear it. |
| `sys.fetch` (JS)    | Promise rejection on policy violation (programmer error — the policy lives in `app.json`). | Promise resolves to a non-2xx response object on transient HTTP failures. |
| `sys.midi`, `sys.net.udp`, `sys.sensors`, `sys.device` | Wrong-type arguments.                                            | `null` / `false` when the device or session is absent.            |

If you encounter a binding that violates the rule (e.g. silently
returns `null` on an obvious programmer error, or throws on a
transient absence), file it as a follow-up in `TODO.md` and include
the binding file path.

## How to apply the rule when writing a new binding

1. **At entry, validate arity / types / handle ranges.** If anything
   looks wrong by contract, *throw* (JS / Lua) or return the documented
   sentinel + `fprintf(stderr, …)` (WASM).
2. **Call the wrapper.** Trust the wrapper's success / failure return.
3. **On wrapper failure, decide:** is this something a healthy
   application could trigger by accident in production (network,
   device, file)? → falsy. Or is it something only a buggy app would
   trigger (bad SQL, bad enum, bad path)? → throw.
4. **Mirror the decision in `*_bindings.h` / `budo-llm.md`.** Every
   binding's documented signature should make clear which failure mode
   applies.

## Cross-references

- `src/core/capabilities.h` — feature probes that callers should consult
  *before* attempting an operation, to avoid avoidable transient
  failures.
- `budo-llm.md` — per-API documentation includes the throw-vs-return
  decision in each section.
- `CODE-STRUCTURE.md` §8 "Key conventions" — references this file.
