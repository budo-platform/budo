# Managed API contracts

The JSON files in `api/contracts/` are the source-backed inventory of Budo's
managed API. Each contract declares wrapper operations, guest-visible exports,
runtime and platform support, error behavior, capability relationships, and the
binding source that registers each export.

Validate every contract and generate the support matrix with:

```sh
python3 scripts/validate-api-contracts.py \
  --repo . \
  --output build/generated/api-support-matrix.json
```

The validator uses only the Python standard library. It verifies contract
structure, referenced source files, wrapper ownership, registration symbols,
guest export tokens, constants, capability references, and explicit unsupported
runtime evidence. It is intentionally a structural validator, not a C parser or
a substitute for runtime conformance tests.

Generate checked-in contract artifacts with
`make generate-contract-artifacts`. This writes the pilot TypeScript fragment,
`api/support-matrix.json`, `api/SUPPORT.md`, registration and conformance
metadata, and the operation-level runtime availability section in
`documentation/pages/api-reference.md`. `make generate-types` consumes that
fragment for `SysCapabilities`, `SysDevice`, and `SysMath`, while the remaining
declarations continue to use source extraction and hand-authored annotations
during migration. It synchronizes `types/budo.d.ts`, the root CLI snapshot, and
the MIDI preset saver example snapshot. CTest checks artifact, documentation,
and final declaration drift without rewriting files.

## Runtime conformance

`basic_runtime_conformance` executes equivalent capability, device, and math
operations through the QuickJS, Lua, and desktop Wasmtime adapters. Each runner
emits normalized JSON records with `runtime`, `operation`, `result`, `errorKind`,
and `errorCode` fields. The test compares the records before printing them, so a
runtime-specific registration, conversion, or result difference fails CTest at
the first mismatched operation.

Add deterministic pilot scenarios to
`tests/basic_runtime_conformance_test.c`. Keep platform-dependent values behind
the same compiled capability probe, use typed JSON result literals, and preserve
intentional ABI differences instead of asserting unsupported parity. Stateful,
asynchronous, or resource-owning subsystems should use a separate focused
conformance test once their lifecycle fixtures are available.

`api/conformance-matrix.json` assigns every contract to an executable
conformance owner. `normalized_trace` compares structured result/error records
across the common JS/Lua/Wasmtime subset. `backend_sequence` is reserved for
surfaces where cross-runtime parity is intentionally absent or pixel/GL
sequencing is the actual contract. `conformance_matrix` fails when a subsystem
is missing, references an unknown CTest, or claims normalized coverage without
multiple runtimes and explicit operations.

## Adding or changing an API

1. Update the owning contract with the wrapper operation and every supported
   runtime export.
2. Keep runtime-specific omissions explicit; do not claim parity where ABIs or
   features differ.
3. Run `api_contract_consistency` and `api_contract_validator`.
4. Add behavioral tests for value conversion, errors, ownership, and teardown.
5. Regenerate TypeScript declarations and documentation when the JavaScript
   surface changes.

Stateful bindings must keep their wrapper, VM references, callback queues,
linear memory, and pending operations in an instance-owned binding state.
QuickJS uses function data or context opaque state, Lua uses closure upvalues or
private registry entries, and Wasmtime uses callback environments owned by the
canvas runtime. Process-wide mutable binding contexts are not allowed.

Canvas ownership is enforced by `canvas_binding_ownership`: QuickJS callbacks
must resolve `JSCanvasContext` through context/runtime opaque data and clear both
before teardown; Lua callbacks must resolve `LuaCanvasContext` through the
owning state's private registry entry and clear it before closing the state;
Wasmtime host functions must receive `WasmCanvasContext` through their linker
environment. The source audit rejects mutable file-scope declarations in all
three canvas adapters, while the JS/Lua/WASM canvas state tests exercise two
coexisting instances and peer teardown.

The file picker bridge is the exception: platforms cannot safely retain native
pointers, so it uses a bounded process-wide registry of opaque generation
tokens. Tokens are invalidated before runtime teardown, and platform callbacks
only enqueue copied results for delivery by `js_file_poll()` on the runtime
thread.

## Lifecycle ownership

`SubsystemRegistry` is a resource-owner registry, not an API inventory. Every
entry has a real instance context and cleanup callback. Stateless registrations
must document their no-lifecycle semantics instead of adding placeholder
contexts or no-op cleanup functions.

Stateful managed resources are declared per host/runtime in
`api/managed-lifecycle.json`. `managed_lifecycle_manifest` compares that matrix
with desktop, web, and private Android registration calls, rejects direct
cleanup/poll bypasses, and verifies that desktop/Android `ApplicationDriver`
callbacks forward pause, resume, and context loss to the registry. Registration
order defines dependencies: poll and resume run in declaration order; pause,
context loss, rollback, and shutdown run in reverse order. Lifecycle callbacks
are optional and must only be supplied when a subsystem has a real contract for
that event.

Capability and JavaScript/Lua math bindings retain no state, require no polling,
and are discarded with their VM. Wasmtime math memory/error state is owned by
`WasmCanvasContext`. Device bindings are different: each managed runtime owns a
`DeviceContext`, captured by the runtime adapter and cleaned before VM/store
teardown. Cleanup always releases an active `keepScreenOn` request, including on
desktop watch reload, web managed shutdown, Android driver shutdown, and
Wasmtime context destruction. The device service reference-counts active runtime
owners, so destroying one coexisting runtime does not release another runtime's
request; the platform request is disabled when the final owner is cleaned up.

## GPU boundary

Desktop OpenGL, WebGL 2, and Android GLES share one explicit Skia/raw-GL
transition contract. Mid-sequence Skia flushes run through
`GlStateSnapshot`, which captures and restores framebuffer, viewport, program,
VAO, array/index buffers, renderbuffer, Budo texture units 0 through 7 for both
2D and cube bindings,
active texture, blend/depth/cull/scissor state and parameters, and color/depth
write masks. Terminal presentation may discard prior state only through the
named terminal transition helper before establishing the shared presentation
baseline.

Buffer, texture, and render-target slot layouts plus 1-based ID validation are
owned by `gpu_resource_state`. `gpu_boundary_invariants` rejects backend-local
copies and direct Skia flushes outside the two transition policies. The mock GL
guard test mutates every protected field and verifies exact restoration.